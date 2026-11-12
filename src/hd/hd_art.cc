#include "hd/hd_art.h"
#include "hd/art_config.h"

#ifdef FALLOUT_HD

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "art.h"
#include "config.h"
#include "debug.h"
#include "game_config.h"
#include "hd/bc7.h"
#include "hd/gl_private.h"
#include "hd/json_reader.h"
#include "hd/map_renderer.h"

namespace fallout::hd {

namespace {

    constexpr int kPageSize = 2048;
    constexpr long long kPageBytes = static_cast<long long>(kPageSize / 4) * (kPageSize / 4) * 16; // BC7: 16 bytes per 4x4 block
    constexpr int kMaxPages = 2048; // D3D11 array limit
    constexpr long long kUploadLimitPerBuild = 24ll << 20;
    constexpr long long kRamLimit = 1024ll << 20;
    constexpr unsigned int kDxgiBc7Unorm = 98;
    constexpr unsigned int kDxgiBc7Srgb = 99;

    struct Cell {
        int x; // 1x pixels in the pack layout
        int y;
        int width;
        int height;
        unsigned int hash;
    };

    struct Resident {
        int page = -1;
        unsigned int generation = 0;
        int x = 0;
        int y = 0;
    };

    enum class PackState {
        Loading,
        Loaded,
        Missing,
        Failed,
        Dropped, // data freed for RAM; reloaded on next use
    };

    // What the loader thread produces.
    struct PackData {
        bool hardAlpha = false;
        int layoutWidth = 0;
        int layoutHeight = 0;
        int rotationFirst[6] = { -1, -1, -1, -1, -1, -1 }; // first cell of each rotation (same_as resolved)
        int rotationFrames[6] = {};
        std::vector<Cell> cells;
        int tier = 0;
        std::vector<unsigned char> blocks; // mip 0 of albedo_<tier>x.dds
        int blockPitch = 0; // bytes per row of blocks
        int blockRows = 0;
    };

    struct Pack {
        std::string dir;
        PackState state = PackState::Loading;
        bool forcedMissing = false;
        bool loggedMismatch = false;
        PackData data;
        std::vector<Resident> resident; // per cell
        std::unordered_map<int, std::vector<Resident>> tiles; // per cell larger than a page: its tiles, row by row
        unsigned int lastUse = 0;
        unsigned int mapUse = 0; // stamp of the last map build that asked for it
    };

    struct Page {
        unsigned int generation = 1;
        unsigned int lastUse = 0;
        unsigned int mapUse = 0; // stamp of the last map build that used it
        bool used = false;
        int nextY = 0;
        struct Shelf {
            int y;
            int height;
            int x;
        };
        std::vector<Shelf> shelves;

        void clear()
        {
            generation++;
            used = false;
            nextY = 0;
            shelves.clear();
        }

        // Block-aligned shelf packing (w, h multiples of 4).
        bool alloc(int w, int h, int* x, int* y)
        {
            if (w > kPageSize || h > kPageSize) {
                return false;
            }
            for (Shelf& shelf : shelves) {
                if (h <= shelf.height && h * 2 > shelf.height && shelf.x + w <= kPageSize) {
                    *x = shelf.x;
                    *y = shelf.y;
                    shelf.x += w;
                    used = true;
                    return true;
                }
            }
            if (nextY + h > kPageSize) {
                return false;
            }
            shelves.push_back({ nextY, h, w });
            *x = 0;
            *y = nextY;
            nextY += h;
            used = true;
            return true;
        }
    };

    struct Request {
        Pack* pack;
        int tier;
    };

    struct Result {
        Pack* pack;
        int tier;
        PackState state;
        PackData data;
    };

    struct State {
        bool configured = false;
        bool enabled = true; // Ctrl+F8
        bool forceOriginal = false;
        int fallbackPercent = 0;
        int debugTileSpan = 0; // [hd_debug] hd_tile_span, 0 = off
        std::string root;
        int budgetMb = 1024;

        GLuint pool = 0;
        bool compressed = false;
        std::vector<Page> pages;

        std::unordered_map<std::string, std::unique_ptr<Pack>> packs; // by dir
        std::unordered_map<int, Pack*> byFid;
        std::unordered_map<std::string, Pack*> byPath; // named art

        std::thread loader;
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<Request> requests;
        std::vector<Result> results;
        bool stop = false;
        int inFlight = 0; // main thread's count of requests not yet taken back

        int tier = 0;
        unsigned int stamp = 0; // counts builds of both clients
        unsigned int mapStamp = 0; // the map's last build
        HdArtClient building = HdArtClient::Map;
        bool buildMissedLoading = false; // last build wanted a pack still loading
        bool arrived = false; // a pack the map's last build wanted finished loading
        bool deferred = false; // the map's last build hit the upload limit
        bool uiArrived = false; // any pack finished loading since the UI's last poll
        bool uiDeferred = false; // the UI's last build hit the upload limit
        long long buildUploadBytes = 0;
        HdArtStats stats;
        HdArtStats mapCounts; // the map's build* counters, kept during a UI build
    };

    State* gState = nullptr;

    unsigned int pathHash(const std::string& s)
    {
        unsigned int h = 2166136261u;
        for (char c : s) {
            h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
        }
        return h;
    }

    bool readFile(const std::string& path, std::vector<unsigned char>& out)
    {
        FILE* stream = fopen(path.c_str(), "rb");
        if (stream == nullptr) {
            return false;
        }
        fseek(stream, 0, SEEK_END);
        long size = ftell(stream);
        fseek(stream, 0, SEEK_SET);
        bool ok = size >= 0 && size <= (1024L << 20);
        if (ok) {
            out.resize(static_cast<size_t>(size));
            ok = size == 0 || fread(out.data(), 1, out.size(), stream) == out.size();
        }
        fclose(stream);
        return ok;
    }

    unsigned int readU32(const unsigned char* p)
    {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<unsigned int>(p[3]) << 24);
    }

    // pack.json -> PackData (everything but the blocks).
    bool parsePack(const std::vector<unsigned char>& text, PackData& data, std::string& error)
    {
        JsonValue root;
        if (!jsonParse(reinterpret_cast<const char*>(text.data()), text.size(), root, &error)) {
            return false;
        }
        if (root["version"].asInt() != 1) {
            error = "unsupported pack version";
            return false;
        }
        int lw = root["layout"][size_t(0)].asInt();
        int lh = root["layout"][1].asInt();
        if (lw <= 0 || lh <= 0 || lw > 32768 || lh > 32768 || lw % 4 || lh % 4) {
            error = "bad layout";
            return false;
        }
        data.hardAlpha = root["hard_alpha"].boolean;
        data.layoutWidth = root["layout"][static_cast<size_t>(0)].asInt();
        data.layoutHeight = root["layout"][1].asInt();
        const JsonValue& rotations = root["rotations"];
        if (rotations.size() != 6) {
            error = "expected 6 rotations";
            return false;
        }
        for (int r = 0; r < 6; r++) {
            const JsonValue& rotation = rotations[r];
            if (rotation.has("same_as")) {
                continue;
            }
            const JsonValue& frames = rotation["frames"];
            data.rotationFirst[r] = static_cast<int>(data.cells.size());
            data.rotationFrames[r] = static_cast<int>(frames.size());
            for (size_t f = 0; f < frames.size(); f++) {
                const JsonValue& cell = frames[f];
                data.cells.push_back({ cell[static_cast<size_t>(0)].asInt(), cell[1].asInt(), cell[2].asInt(), cell[3].asInt(), cell[4].asUint() });
            }
        }
        for (int r = 0; r < 6; r++) {
            const JsonValue& rotation = rotations[r];
            if (rotation.has("same_as")) {
                int other = rotation["same_as"].asInt();
                if (other < 0 || other >= 6 || data.rotationFirst[other] < 0) {
                    error = "bad same_as";
                    return false;
                }
                data.rotationFirst[r] = data.rotationFirst[other];
                data.rotationFrames[r] = data.rotationFrames[other];
            }
        }
        for (const Cell& cell : data.cells) {
            if (cell.x < 0 || cell.y < 0 || cell.x % 4 || cell.y % 4 || cell.width <= 0 || cell.height <= 0
                || cell.width > data.layoutWidth || cell.height > data.layoutHeight
                || cell.x > data.layoutWidth - ((cell.width + 3) & ~3)
                || cell.y > data.layoutHeight - ((cell.height + 3) & ~3)) {
                error = "cell outside atlas";
                return false;
            }
        }
        return true;
    }

    // DDS (DX10 header, BC7) -> mip 0 blocks.
    bool parseDds(const std::vector<unsigned char>& file, int expectWidth, int expectHeight, PackData& data, std::string& error)
    {
        if (file.size() < 148 || readU32(file.data() + 4) != 124 || readU32(file.data() + 76) != 32 || readU32(file.data() + 132) != 3 || readU32(file.data() + 140) != 1 || memcmp(file.data(), "DDS ", 4) != 0 || memcmp(file.data() + 84, "DX10", 4) != 0) {
            error = "not a DX10 DDS";
            return false;
        }
        int height = static_cast<int>(readU32(file.data() + 12));
        int width = static_cast<int>(readU32(file.data() + 16));
        unsigned int format = readU32(file.data() + 128);
        if (format != kDxgiBc7Unorm && format != kDxgiBc7Srgb) {
            error = "not BC7";
            return false;
        }
        if (width != expectWidth || height != expectHeight || width % 4 != 0 || height % 4 != 0) {
            error = "atlas size doesn't match the layout";
            return false;
        }
        data.blockPitch = (width / 4) * 16;
        data.blockRows = height / 4;
        size_t bytes = static_cast<size_t>(data.blockPitch) * data.blockRows;
        if (file.size() < 148 + bytes) {
            error = "DDS truncated";
            return false;
        }
        data.blocks.assign(file.begin() + 148, file.begin() + 148 + bytes);
        return true;
    }

    void loaderMain()
    {
        while (true) {
            Request request;
            {
                std::unique_lock<std::mutex> lock(gState->mutex);
                gState->wake.wait(lock, [] { return gState->stop || !gState->requests.empty(); });
                if (gState->stop) {
                    return;
                }
                request = gState->requests.front();
                gState->requests.pop_front();
            }

            Result result = { request.pack, request.tier, PackState::Failed, {} };
            std::vector<unsigned char> json;
            std::string error;
            const std::string& dir = request.pack->dir;
            if (!readFile(dir + "/pack.json", json)) {
                result.state = PackState::Missing;
            } else if (!parsePack(json, result.data, error)) {
                debugPrint("HD: art pack %s: %s\n", dir.c_str(), error.c_str());
            } else {
                std::vector<unsigned char> dds;
                char name[32];
                snprintf(name, sizeof(name), "/albedo_%dx.dds", request.tier);
                if (!readFile(dir + name, dds)) {
                    debugPrint("HD: art pack %s: no %s\n", dir.c_str(), name + 1);
                } else if (!parseDds(dds, result.data.layoutWidth * request.tier, result.data.layoutHeight * request.tier, result.data, error)) {
                    debugPrint("HD: art pack %s%s: %s\n", dir.c_str(), name, error.c_str());
                } else {
                    result.data.tier = request.tier;
                    result.state = PackState::Loaded;
                }
            }

            std::lock_guard<std::mutex> lock(gState->mutex);
            gState->results.push_back(std::move(result));
        }
    }

    void request(Pack* pack)
    {
        pack->state = PackState::Loading;
        {
            std::lock_guard<std::mutex> lock(gState->mutex);
            gState->requests.push_back({ pack, gState->tier });
        }
        gState->inFlight++;
        gState->wake.notify_one();
    }

    // Lower-case "art\<type>\<name>" for an FRM's path, the pipeline's
    // folder name (fo2hd.hdassets.asset_dir: .frN files keep the suffix).
    bool packKey(const char* path, std::string& key)
    {
        if (path == nullptr || *path == '\0') {
            return false;
        }
        std::string lower(path);
        for (char& c : lower) {
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
        std::replace(lower.begin(), lower.end(), char(92), '/');
        size_t art = lower.find("art/");
        if (art == std::string::npos) {
            return false;
        }
        lower = lower.substr(art);
        size_t dot = lower.rfind('.');
        if (dot == std::string::npos) {
            return false;
        }
        std::string ext = lower.substr(dot + 1);
        lower.resize(dot);
        if (ext != "frm") {
            lower += "_" + ext;
        }
        key = lower;
        return true;
    }

    Pack* packForPath(const char* path)
    {
        std::string key;
        Pack* pack = nullptr;
        if (packKey(path, key)) {
            std::string dir = gState->root + "/" + key;
            auto found = gState->packs.find(dir);
            if (found == gState->packs.end()) {
                auto fresh = std::make_unique<Pack>();
                fresh->dir = dir;
                fresh->forcedMissing = gState->forceOriginal
                    || (gState->fallbackPercent > 0 && static_cast<int>(pathHash(key) % 100) < gState->fallbackPercent);
                pack = fresh.get();
                gState->packs.emplace(dir, std::move(fresh));
                if (pack->forcedMissing) {
                    pack->state = PackState::Missing;
                } else {
                    request(pack);
                }
            } else {
                pack = found->second.get();
            }
        }
        return pack;
    }

    Pack* packFor(int fid)
    {
        auto it = gState->byFid.find(fid);
        if (it != gState->byFid.end()) {
            return it->second;
        }
        Pack* pack = packForPath(FrmId(fid).filePath());
        gState->byFid.emplace(fid, pack);
        return pack;
    }

    Pack* packForNamed(const char* path)
    {
        auto it = gState->byPath.find(path);
        if (it != gState->byPath.end()) {
            return it->second;
        }
        Pack* pack = packForPath(path);
        gState->byPath.emplace(path, pack);
        return pack;
    }

    // Names of the FRMs drawn this session that have no pack (art not built
    // yet, or not meant to be: P7). Too long for debug.log (debugPrint cuts
    // long lines), so the names go to hd_art_missing.txt in the game folder
    // and the log gets a count per art folder.
    void writeMissingList()
    {
        std::vector<std::string> names;
        for (const auto& entry : gState->packs) {
            const Pack& pack = *entry.second;
            if (pack.state == PackState::Missing && !pack.forcedMissing) {
                names.push_back(pack.dir.substr(std::min(pack.dir.size(), gState->root.size() + 1)));
            }
        }
        if (names.empty()) {
            return;
        }
        std::sort(names.begin(), names.end());
        FILE* stream = fopen("hd_art_missing.txt", "wt");
        if (stream != nullptr) {
            for (const std::string& name : names) {
                fprintf(stream, "%s\n", name.c_str());
            }
            fclose(stream);
        }
        std::string perFolder;
        for (size_t i = 0; i < names.size();) {
            size_t slash = names[i].rfind('/');
            std::string folder = slash == std::string::npos ? std::string() : names[i].substr(0, slash);
            size_t j = i;
            while (j < names.size() && names[j].compare(0, folder.size() + 1, folder + "/") == 0) {
                j++;
            }
            j = std::max(j, i + 1);
            char part[96];
            snprintf(part, sizeof(part), "%s%s %d", perFolder.empty() ? "" : ", ", folder.c_str(), static_cast<int>(j - i));
            perFolder += part;
            i = j;
        }
        debugPrint("HD: art without packs: %d (%s); names in hd_art_missing.txt\n", static_cast<int>(names.size()), perFolder.c_str());
    }

    void countPacks()
    {
        HdArtStats& s = gState->stats;
        s.packsKnown = static_cast<int>(gState->packs.size());
        s.packsLoaded = s.packsMissing = s.packsFailed = s.packsLoading = 0;
        s.ramBytes = 0;
        for (const auto& entry : gState->packs) {
            const Pack& pack = *entry.second;
            switch (pack.state) {
            case PackState::Loaded:
                s.packsLoaded++;
                s.ramBytes += static_cast<long long>(pack.data.blocks.size());
                break;
            case PackState::Missing:
                s.packsMissing++;
                break;
            case PackState::Failed:
                s.packsFailed++;
                break;
            case PackState::Loading:
                s.packsLoading++;
                break;
            case PackState::Dropped:
                break;
            }
        }
        s.pagesUsed = 0;
        for (const Page& page : gState->pages) {
            if (page.used) {
                s.pagesUsed++;
            }
        }
    }

    // Frees pack data, least recently used first, while over the RAM limit.
    // Frames already in the pool stay usable.
    void trimRam()
    {
        long long total = 0;
        std::vector<Pack*> loaded;
        for (auto& entry : gState->packs) {
            Pack* pack = entry.second.get();
            if (pack->state == PackState::Loaded) {
                total += static_cast<long long>(pack->data.blocks.size());
                loaded.push_back(pack);
            }
        }
        if (total <= kRamLimit) {
            return;
        }
        std::sort(loaded.begin(), loaded.end(), [](const Pack* a, const Pack* b) { return a->lastUse < b->lastUse; });
        for (Pack* pack : loaded) {
            if (total <= kRamLimit * 3 / 4) {
                break;
            }
            if (pack->lastUse == gState->stamp || (gState->mapStamp != 0 && pack->mapUse == gState->mapStamp)) {
                continue;
            }
            total -= static_cast<long long>(pack->data.blocks.size());
            pack->data.blocks.clear();
            pack->data.blocks.shrink_to_fit();
            pack->state = PackState::Dropped;
        }
    }

    bool allocate(int w, int h, int* page, int* x, int* y)
    {
        int count = static_cast<int>(gState->pages.size());
        for (int index = 0; index < count; index++) {
            if (gState->pages[index].used && gState->pages[index].alloc(w, h, x, y)) {
                *page = index;
                return true;
            }
        }
        for (int index = 0; index < count; index++) {
            if (!gState->pages[index].used && gState->pages[index].alloc(w, h, x, y)) {
                *page = index;
                return true;
            }
        }
        // Evict the least recently used page neither this build nor the
        // map's last build touches.
        int victim = -1;
        for (int index = 0; index < count; index++) {
            const Page& candidate = gState->pages[index];
            bool mapNeeds = gState->mapStamp != 0 && candidate.mapUse == gState->mapStamp;
            if (candidate.lastUse != gState->stamp && !mapNeeds && (victim < 0 || candidate.lastUse < gState->pages[victim].lastUse)) {
                victim = index;
            }
        }
        if (victim < 0) {
            return false;
        }
        gState->pages[victim].clear();
        gState->stats.evictionsTotal++;
        if (!gState->pages[victim].alloc(w, h, x, y)) {
            return false;
        }
        *page = victim;
        return true;
    }

    bool createPool()
    {
        if (!hdGlLoad()) return false;
        gState->compressed = hdGlHasBc7();
        int force = 0;
        configGetInt(&gGameConfig, "hd_debug", "bc7_cpu", &force, 0);
        if (force) gState->compressed = false;
        long long pageBytes = gState->compressed ? kPageBytes : kPageSize * kPageSize * 4ll;
        GLint maxLayers = 0;
        glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
        int slices = static_cast<int>(std::min<long long>(maxLayers, std::max<long long>(1, (static_cast<long long>(std::max(gState->budgetMb, 1)) << 20) / pageBytes)));
        glGenTextures(1, &gState->pool);
        glBindTexture(GL_TEXTURE_2D_ARRAY, gState->pool);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, gState->compressed ? 0x8E8C : GL_RGBA8, kPageSize, kPageSize, slices);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (glGetError() != GL_NO_ERROR) {
            glDeleteTextures(1, &gState->pool);
            return false;
        }
        gState->pages.assign(slices, Page());
        gState->stats.pages = slices;
        gState->stats.poolBytes = slices * pageBytes;
        return true;
    }

    // The pack's cell for (rotation, frame), if the pack is loaded at this
    // tier and the cell matches the art CE draws.
    const Cell* matchingCell(Pack* pack, int rotation, int frame, unsigned int hash, int width, int height, int* index)
    {
        HdArtStats& s = gState->stats;
        if (pack == nullptr) {
            return nullptr;
        }
        pack->lastUse = gState->stamp;
        if (gState->building == HdArtClient::Map) {
            pack->mapUse = gState->stamp;
        }
        if (pack->state == PackState::Dropped && !pack->forcedMissing) {
            request(pack);
        }
        if (pack->state == PackState::Loading) {
            gState->buildMissedLoading = true;
            return nullptr;
        }
        if (pack->state != PackState::Loaded || pack->data.tier != gState->tier) {
            return nullptr;
        }

        const PackData& data = pack->data;
        if (frame < 0 || frame >= data.rotationFrames[rotation]) {
            return nullptr;
        }
        *index = data.rotationFirst[rotation] + frame;
        const Cell& cell = data.cells[*index];
        if (cell.width != width || cell.height != height || cell.hash != hash) {
            s.mismatches++;
            if (!pack->loggedMismatch) {
                pack->loggedMismatch = true;
                debugPrint("HD: art pack %s r%d f%d doesn't match the game's art (%dx%d %08x vs %dx%d %08x), original art used\n",
                    pack->dir.c_str(), rotation, frame, cell.width, cell.height, cell.hash, width, height, hash);
            }
            return nullptr;
        }
        return &cell;
    }

    // Puts the part (tx, ty, tw, th) of a cell into the pool unless it is
    // there (1x pixels; tx and ty multiples of 4, so the part starts on a
    // BC7 block at every tier).
    bool residentPart(const PackData& data, const Cell& cell, Resident& slot, int tx, int ty, int tw, int th)
    {
        HdArtStats& s = gState->stats;
        int tier = gState->tier;
        if (slot.page < 0 || gState->pages[slot.page].generation != slot.generation) {
            int w = ((tw + 3) & ~3) * tier;
            int h = ((th + 3) & ~3) * tier;
            long long bytes = gState->compressed ? static_cast<long long>(w / 4) * (h / 4) * 16 : static_cast<long long>(w) * h * 4;
            if (gState->buildUploadBytes + bytes > kUploadLimitPerBuild) {
                s.buildDeferred++;
                return false;
            }
            int page;
            int x;
            int y;
            if (!allocate(w, h, &page, &x, &y)) {
                s.buildOverBudget++;
                s.overBudgetTotal++;
                return false;
            }
            const unsigned char* src = data.blocks.data() + static_cast<size_t>((cell.y + ty) * tier / 4) * data.blockPitch + static_cast<size_t>((cell.x + tx) * tier / 4) * 16;
            glBindTexture(GL_TEXTURE_2D_ARRAY, gState->pool);
            // GL compressed subimages have no row-pitch argument. Gather
            // the BC7 rectangle, or decode its blocks into tightly packed RGBA.
            if (gState->compressed) {
                std::vector<unsigned char> blocks(static_cast<size_t>(w / 4) * (h / 4) * 16);
                for (int row = 0; row < h / 4; row++)
                    memcpy(blocks.data() + row * (w / 4) * 16, src + row * data.blockPitch, (w / 4) * 16);
                glCompressedTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, x, y, page, w, h, 1, 0x8E8C, blocks.size(), blocks.data());
            } else {
                std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
                for (int by = 0; by < h / 4; by++)
                    for (int bx = 0; bx < w / 4; bx++) {
                        unsigned char block[64];
                        bc7Decode(src + by * data.blockPitch + bx * 16, block);
                        for (int row = 0; row < 4; row++)
                            memcpy(rgba.data() + ((by * 4 + row) * w + bx * 4) * 4, block + row * 16, 16);
                    }
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, x, y, page, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            }
            if (glGetError() != GL_NO_ERROR) return false;
            slot.page = page;
            slot.generation = gState->pages[page].generation;
            slot.x = x;
            slot.y = y;
            gState->buildUploadBytes += bytes;
            s.buildUploads++;
            s.buildUploadBytes += bytes;
            s.uploadsTotal++;
        }
        gState->pages[slot.page].lastUse = gState->stamp;
        if (gState->building == HdArtClient::Map) {
            gState->pages[slot.page].mapUse = gState->stamp;
        }
        return true;
    }

    // hdArtFrame / hdArtFrameNamed once the pack is known.
    bool frameIn(Pack* pack, int rotation, int frame, unsigned int hash, int width, int height, HdFrame* out)
    {
        int index = 0;
        const Cell* cell = matchingCell(pack, rotation, frame, hash, width, height, &index);
        if (cell == nullptr) {
            return false;
        }
        if (pack->resident.size() != pack->data.cells.size()) {
            pack->resident.assign(pack->data.cells.size(), Resident());
        }
        Resident& slot = pack->resident[index];
        if (!residentPart(pack->data, *cell, slot, 0, 0, cell->width, cell->height)) {
            return false;
        }
        out->x = slot.x;
        out->y = slot.y;
        out->slice = slot.page;
        out->hardAlpha = pack->data.hardAlpha;
        gState->stats.buildHd++;
        return true;
    }

    // 1x pixels a tile spans: a multiple of 4 whose HD size fits a page
    // (1024 at 2x, 680 at 3x, 512 at 4x), or less with [hd_debug]
    // hd_tile_span, which puts ordinary art through the tiled paths.
    int tileSpan()
    {
        int span = (kPageSize / gState->tier) & ~3;
        return gState->debugTileSpan > 0 ? std::min(span, gState->debugTileSpan) : span;
    }

    // hdArtFrameTiles once the pack is known. A cell larger than a page at
    // this tier is kept in square tiles of tileSpan() 1x pixels.
    bool frameTilesIn(Pack* pack, int rotation, int frame, unsigned int hash, int width, int height, int x0, int y0, int w0, int h0, std::vector<HdFrameTile>* out)
    {
        out->clear();
        int index = 0;
        const Cell* cell = matchingCell(pack, rotation, frame, hash, width, height, &index);
        if (cell == nullptr) {
            return false;
        }
        int span = tileSpan();
        if (((cell->width + 3) & ~3) <= span && ((cell->height + 3) & ~3) <= span) {
            HdFrame hd;
            if (!frameIn(pack, rotation, frame, hash, width, height, &hd)) {
                return false;
            }
            out->push_back({ 0, 0, cell->width, cell->height, hd });
            return true;
        }
        int columns = (cell->width + span - 1) / span;
        int rows = (cell->height + span - 1) / span;
        std::vector<Resident>& tiles = pack->tiles[index];
        if (tiles.size() != static_cast<size_t>(columns * rows)) {
            tiles.assign(static_cast<size_t>(columns * rows), Resident());
        }
        for (int row = 0; row < rows; row++) {
            for (int column = 0; column < columns; column++) {
                int tx = column * span;
                int ty = row * span;
                int tw = std::min(span, cell->width - tx);
                int th = std::min(span, cell->height - ty);
                if (tx >= x0 + w0 || ty >= y0 + h0 || tx + tw <= x0 || ty + th <= y0) {
                    continue; // outside the part asked for
                }
                Resident& slot = tiles[static_cast<size_t>(row * columns + column)];
                if (!residentPart(pack->data, *cell, slot, tx, ty, tw, th)) {
                    out->clear();
                    return false;
                }
                out->push_back({ tx, ty, tw, th, { slot.x, slot.y, slot.page, pack->data.hardAlpha } });
            }
        }
        gState->stats.buildHd++;
        return true;
    }

    // Finished loads from the loader thread into their packs. The map hears
    // about packs its last build asked for, the UI layer about any.
    void takeResults()
    {
        std::vector<Result> results;
        {
            std::lock_guard<std::mutex> lock(gState->mutex);
            results.swap(gState->results);
        }
        for (Result& result : results) {
            gState->inFlight--;
            Pack* pack = result.pack;
            if (result.tier != gState->tier) {
                // Loaded for a tier no longer in use.
                pack->state = PackState::Dropped;
                continue;
            }
            pack->state = result.state;
            pack->data = std::move(result.data);
            pack->resident.clear();
            gState->uiArrived = true;
            if (pack->state == PackState::Loaded && gState->mapStamp != 0 && pack->mapUse == gState->mapStamp) {
                gState->arrived = true;
            }
        }
    }

} // namespace

bool hdArtInit()
{
    GlState saved;
    hdArtShutdown();
    int enabled = 1;
    configGetInt(&gGameConfig, "hd", "hd_art", &enabled, 1);
    if (enabled == 0) {
        return false;
    }

    gState = new State();
    gState->root = hdArtConfiguredDir();
    configGetInt(&gGameConfig, "hd", "vram_budget_mb", &gState->budgetMb, 1024);
    int force = 0;
    configGetInt(&gGameConfig, "hd_debug", "hd_force_original", &force, 0);
    gState->forceOriginal = force != 0;
    gState->stats.forceOriginal = gState->forceOriginal;
    configGetInt(&gGameConfig, "hd_debug", "hd_fallback_percent", &gState->fallbackPercent, 0);
    int tileSpanSetting = 0;
    configGetInt(&gGameConfig, "hd_debug", "hd_tile_span", &tileSpanSetting, 0);
    gState->debugTileSpan = tileSpanSetting > 0 ? std::max(tileSpanSetting & ~3, 16) : 0;

    if (!createPool()) {
        delete gState;
        gState = nullptr;
        return false;
    }
    gState->loader = std::thread(loaderMain);
    gState->configured = true;
    debugPrint("HD: art store up: %s, pool %d pages (%lld MB)%s%s\n", gState->root.c_str(), gState->stats.pages, gState->stats.poolBytes >> 20,
        gState->forceOriginal ? ", original art forced" : "", gState->fallbackPercent > 0 ? ", some art forced to fall back" : "");
    if (gState->debugTileSpan > 0) {
        debugPrint("HD: art store: frames over %d px are drawn in tiles ([hd_debug] hd_tile_span)\n", gState->debugTileSpan);
    }
    return true;
}

void hdArtShutdown()
{
    if (gState == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(gState->mutex);
        gState->stop = true;
    }
    gState->wake.notify_all();
    if (gState->loader.joinable()) {
        gState->loader.join();
    }
    const HdArtStats& s = gState->stats;
    debugPrint("HD: art store stats: %d packs (%d loaded, %d missing, %d failed), %lld uploads, %lld evictions, %lld over budget, %d mismatches\n",
        s.packsKnown, s.packsLoaded, s.packsMissing, s.packsFailed, s.uploadsTotal, s.evictionsTotal, s.overBudgetTotal, s.mismatches);
    writeMissingList();
    debugPrint("HD: VRAM: process peak %llu MB (OS budget %llu MB); pool %lld MB with %d of %d pages used, map targets+atlas %lld MB\n",
        s.vramPeak >> 20, s.vramOsBudget >> 20, s.poolBytes >> 20, s.pagesUsed, s.pages, mapRendererGpuBytes() >> 20);
    glDeleteTextures(1, &gState->pool);
    delete gState;
    gState = nullptr;
}

bool hdArtActive()
{
    return gState != nullptr && gState->configured && gState->enabled;
}

bool hdArtToggle()
{
    if (gState == nullptr) {
        return false;
    }
    gState->enabled = !gState->enabled;
    return gState->enabled;
}

void hdArtBeginBuild(int tier, HdArtClient client)
{
    if (gState == nullptr) {
        return;
    }
    if (tier != gState->tier) {
        // Another tier's texels: start the pool and the packs over.
        for (Page& page : gState->pages) {
            page.clear();
            page.lastUse = 0;
            page.mapUse = 0;
        }
        for (auto& entry : gState->packs) {
            Pack& pack = *entry.second;
            if (pack.state == PackState::Loaded || pack.state == PackState::Dropped) {
                pack.data = PackData();
                pack.resident.clear();
                pack.state = PackState::Dropped;
            }
        }
        gState->tier = tier;
        gState->stats.tier = tier;
    }
    takeResults();
    gState->stamp++;
    gState->building = client;
    gState->buildMissedLoading = false;
    gState->buildUploadBytes = 0;
    HdArtStats& s = gState->stats;
    if (client == HdArtClient::Map) {
        gState->mapStamp = gState->stamp;
        gState->arrived = false;
    } else {
        gState->mapCounts = s; // hdArtFrame counts into build*; the map's are put back at the end
    }
    s.buildFrames = s.buildHd = s.buildUploads = s.buildDeferred = s.buildOverBudget = 0;
    s.buildUploadBytes = 0;
}

void hdArtEndBuild()
{
    if (gState == nullptr) {
        return;
    }
    HdArtStats& s = gState->stats;
    if (gState->building == HdArtClient::Map) {
        gState->deferred = s.buildDeferred > 0;
    } else {
        gState->uiDeferred = s.buildDeferred > 0;
        s.uiFrames = s.buildFrames;
        s.uiHd = s.buildHd;
        s.uiUploads = s.buildUploads;
        s.uiUploadBytes = s.buildUploadBytes;
        s.uiDeferred = s.buildDeferred;
        s.uiOverBudget = s.buildOverBudget;
        const HdArtStats& map = gState->mapCounts;
        s.buildFrames = map.buildFrames;
        s.buildHd = map.buildHd;
        s.buildUploads = map.buildUploads;
        s.buildUploadBytes = map.buildUploadBytes;
        s.buildDeferred = map.buildDeferred;
        s.buildOverBudget = map.buildOverBudget;
        gState->building = HdArtClient::Map;
    }
    trimRam();
    countPacks();
    // Settled counts, once per change: the shutdown stats never print where
    // the process just ends (Android), so this is the on-device evidence.
    static int loggedKnown = -1, loggedLoaded = -1, loggedHd = -1;
    if (gState->building == HdArtClient::Map && s.packsLoading == 0 && gState->inFlight == 0
        && (s.packsKnown != loggedKnown || s.packsLoaded != loggedLoaded || s.buildHd != loggedHd)) {
        loggedKnown = s.packsKnown;
        loggedLoaded = s.packsLoaded;
        loggedHd = s.buildHd;
        debugPrint("HD: art packs: %d known, %d loaded, %d missing, %d failed; map frames %d, HD %d; pages %d/%d\n",
            s.packsKnown, s.packsLoaded, s.packsMissing, s.packsFailed, s.buildFrames, s.buildHd, s.pagesUsed, s.pages);
    }
}

bool hdArtFrame(int fid, int rotation, int frame, unsigned int hash, int width, int height, HdFrame* out)
{
    if (!hdArtActive() || gState->tier < 2 || rotation < 0 || rotation >= 6) {
        return false;
    }
    gState->stats.buildFrames++;
    return frameIn(packFor(fid), rotation, frame, hash, width, height, out);
}

bool hdArtFrameNamed(const char* path, int rotation, int frame, unsigned int hash, int width, int height, HdFrame* out)
{
    if (!hdArtActive() || gState->tier < 2 || rotation < 0 || rotation >= 6 || path == nullptr) {
        return false;
    }
    gState->stats.buildFrames++;
    return frameIn(packForNamed(path), rotation, frame, hash, width, height, out);
}

bool hdArtFrameTiles(int fid, const char* path, int rotation, int frame, unsigned int hash, int width, int height, int x, int y, int w, int h, std::vector<HdFrameTile>* out)
{
    out->clear();
    if (!hdArtActive() || gState->tier < 2 || rotation < 0 || rotation >= 6 || (fid == -1 && path == nullptr)) {
        return false;
    }
    gState->stats.buildFrames++;
    return frameTilesIn(fid != -1 ? packFor(fid) : packForNamed(path), rotation, frame, hash, width, height, x, y, w, h, out);
}

bool hdArtFitsPage(int width, int height)
{
    if (gState == nullptr || gState->tier < 2) {
        return true;
    }
    int span = tileSpan();
    return ((width + 3) & ~3) <= span && ((height + 3) & ~3) <= span;
}

bool hdArtPoll(HdArtClient client)
{
    if (gState == nullptr) {
        return false;
    }
    takeResults();
    bool redraw;
    if (client == HdArtClient::Map) {
        redraw = gState->arrived || gState->deferred;
        gState->arrived = false;
        gState->deferred = false;
    } else {
        redraw = gState->uiArrived || gState->uiDeferred;
        gState->uiArrived = false;
        gState->uiDeferred = false;
    }
    return redraw;
}

bool hdArtLoading()
{
    return gState != nullptr && gState->inFlight > 0;
}

GLuint hdArtPoolView()
{
    return gState != nullptr ? gState->pool : 0;
}

const HdArtStats& hdArtStats()
{
    static HdArtStats empty;
    return gState != nullptr ? gState->stats : empty;
}

void hdArtDrawOverlay() { }

} // namespace fallout::hd

#endif // FALLOUT_HD
