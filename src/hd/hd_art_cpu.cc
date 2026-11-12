#include "hd_art_cpu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "bc7.h"

namespace fallout::hd {

// BC7 decoding is shared with the GL map store (bc7.cc).
void bc7DecodeBlock(const uint8_t* block, uint8_t out[16][4])
{
    bc7Decode(block, &out[0][0]);
}

namespace {
    uint32_t rd32(const uint8_t* p)
    {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
} // namespace

bool ddsParseBc7(const std::vector<uint8_t>& file, DdsBc7& out)
{
    if (file.size() < 148 || std::memcmp(file.data(), "DDS ", 4) != 0 || rd32(&file[4]) != 124) {
        return false;
    }
    if (std::memcmp(&file[84], "DX10", 4) != 0) {
        return false;
    }
    uint32_t dxgi = rd32(&file[128]);
    if (dxgi != 98 && dxgi != 99) {
        return false;
    }
    int h = static_cast<int>(rd32(&file[12]));
    int w = static_cast<int>(rd32(&file[16]));
    if (w <= 0 || h <= 0 || w > 65536 || h > 65536) {
        return false;
    }
    size_t need = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * 16;
    if (file.size() - 148 < need) {
        return false;
    }
    out.width = w;
    out.height = h;
    out.blocks = file.data() + 148;
    out.blockBytes = need;
    return true;
}

bool ddsDecodeRect(const DdsBc7& dds, int x, int y, int w, int h, HdRgba& out)
{
    if (dds.blocks == nullptr || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > dds.width || y + h > dds.height) {
        return false;
    }
    int blocksPerRow = (dds.width + 3) / 4;
    out.width = w;
    out.height = h;
    out.pixels.assign(static_cast<size_t>(w) * h * 4, 0);
    for (int by = y / 4; by <= (y + h - 1) / 4; by++) {
        for (int bx = x / 4; bx <= (x + w - 1) / 4; bx++) {
            uint8_t px[16][4];
            bc7DecodeBlock(dds.blocks + (static_cast<size_t>(by) * blocksPerRow + bx) * 16, px);
            for (int i = 0; i < 16; i++) {
                int gx = bx * 4 + (i & 3);
                int gy = by * 4 + (i >> 2);
                if (gx < x || gx >= x + w || gy < y || gy >= y + h) {
                    continue;
                }
                std::memcpy(&out.pixels[(static_cast<size_t>(gy - y) * w + (gx - x)) * 4], px[i], 4);
            }
        }
    }
    return true;
}

uint32_t hdFnv1a(const uint8_t* data, size_t size)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ data[i]) * 16777619u;
    }
    return h;
}

bool hdPackKey(const char* path, std::string& key)
{
    if (path == nullptr || *path == '\0') {
        return false;
    }
    std::string s(path);
    for (char& c : s) {
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    size_t art = s.find("art/");
    if (art == std::string::npos) {
        return false;
    }
    s = s.substr(art);
    size_t dot = s.rfind('.');
    if (dot == std::string::npos || s.find('/', dot) != std::string::npos) {
        return false;
    }
    std::string ext = s.substr(dot + 1);
    s.resize(dot);
    if (ext != "frm") {
        s += "_" + ext;
    }
    key = s;
    return true;
}

// ------------------------------------------------------------------ JSON
// Just enough JSON for pack.json: only arrays of numbers inside
// "rotations" matter, everything else is skipped.

namespace {

    struct JsonCursor {
        const char* p;
        const char* end;
        void ws()
        {
            while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
                p++;
            }
        }
        bool eat(char c)
        {
            ws();
            if (p < end && *p == c) {
                p++;
                return true;
            }
            return false;
        }
        bool str(std::string& s)
        {
            ws();
            if (p >= end || *p != '"') {
                return false;
            }
            p++;
            s.clear();
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) {
                    p++;
                }
                s += *p++;
            }
            if (p >= end) {
                return false;
            }
            p++;
            return true;
        }
        bool number(double& v)
        {
            ws();
            char* e = nullptr;
            v = std::strtod(p, &e);
            if (e == p) {
                return false;
            }
            p = e;
            return true;
        }
        bool skip()
        {
            ws();
            if (p >= end) {
                return false;
            }
            if (*p == '"') {
                std::string s;
                return str(s);
            }
            if (*p == '{' || *p == '[') {
                char close = *p == '{' ? '}' : ']';
                p++;
                if (eat(close)) {
                    return true;
                }
                do {
                    if (close == '}') {
                        std::string k;
                        if (!str(k) || !eat(':')) {
                            return false;
                        }
                    }
                    if (!skip()) {
                        return false;
                    }
                } while (eat(','));
                return eat(close);
            }
            while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n') {
                p++;
            }
            return true;
        }
    };

    bool readFile(const std::string& path, std::vector<uint8_t>& out)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f == nullptr) {
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size <= 0) {
            std::fclose(f);
            return false;
        }
        out.resize(static_cast<size_t>(size));
        bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
        std::fclose(f);
        return ok;
    }

} // namespace

bool HdArtCpu::open(const std::string& root, int tier)
{
    close();
    struct stat st;
    if (tier < 2 || tier > 4 || ::stat(root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        return false;
    }
    _root = root;
    while (_root.size() > 1 && (_root.back() == '/' || _root.back() == '\\')) {
        _root.pop_back();
    }
    _tier = tier;
    _open = true;
    return true;
}

void HdArtCpu::close()
{
    _open = false;
    _packs.clear();
    _lru.clear();
}

HdArtCpu::Pack* HdArtCpu::get(const std::string& key)
{
    constexpr size_t kMaxResident = 12; // decoded-on-demand DDS files kept in memory
    auto it = _packs.find(key);
    if (it != _packs.end()) {
        if (it->second.valid) {
            _lru.remove(key);
            _lru.push_front(key);
            return &it->second;
        }
        return nullptr;
    }

    Pack& pack = _packs[key]; // stays invalid on any failure (negative cache)
    std::string dir = _root + "/" + key;
    std::vector<uint8_t> json;
    if (!readFile(dir + "/pack.json", json)) {
        return nullptr;
    }

    // Walk: object -> "rotations": [ {"same_as": n} | {"frames": [[x,y,w,h,hash],...]} ]
    JsonCursor c { reinterpret_cast<const char*>(json.data()), reinterpret_cast<const char*>(json.data()) + json.size() };
    std::vector<int> sameAs;
    std::vector<std::vector<Frame>> rots;
    bool haveRot = false;
    if (!c.eat('{')) {
        return nullptr;
    }
    do {
        std::string k;
        if (!c.str(k) || !c.eat(':')) {
            return nullptr;
        }
        if (k != "rotations") {
            if (!c.skip()) {
                return nullptr;
            }
            continue;
        }
        haveRot = true;
        if (!c.eat('[')) {
            return nullptr;
        }
        if (!c.eat(']')) {
            do {
                if (!c.eat('{')) {
                    return nullptr;
                }
                int same = -1;
                std::vector<Frame> frames;
                if (!c.eat('}')) {
                    do {
                        std::string rk;
                        if (!c.str(rk) || !c.eat(':')) {
                            return nullptr;
                        }
                        if (rk == "same_as") {
                            double v;
                            if (!c.number(v)) {
                                return nullptr;
                            }
                            same = static_cast<int>(v);
                        } else if (rk == "frames") {
                            if (!c.eat('[')) {
                                return nullptr;
                            }
                            if (!c.eat(']')) {
                                do {
                                    if (!c.eat('[')) {
                                        return nullptr;
                                    }
                                    double v[5];
                                    for (int i = 0; i < 5; i++) {
                                        if ((i > 0 && !c.eat(',')) || !c.number(v[i])) {
                                            return nullptr;
                                        }
                                    }
                                    if (!c.eat(']')) {
                                        return nullptr;
                                    }
                                    Frame f;
                                    f.x = static_cast<int>(v[0]);
                                    f.y = static_cast<int>(v[1]);
                                    f.w = static_cast<int>(v[2]);
                                    f.h = static_cast<int>(v[3]);
                                    f.hash = static_cast<uint32_t>(v[4]);
                                    frames.push_back(f);
                                } while (c.eat(','));
                                if (!c.eat(']')) {
                                    return nullptr;
                                }
                            }
                        } else if (!c.skip()) {
                            return nullptr;
                        }
                    } while (c.eat(','));
                    if (!c.eat('}')) {
                        return nullptr;
                    }
                }
                sameAs.push_back(same);
                rots.push_back(std::move(frames));
            } while (c.eat(','));
            if (!c.eat(']')) {
                return nullptr;
            }
        }
    } while (c.eat(','));
    if (!haveRot || rots.empty()) {
        return nullptr;
    }
    for (size_t r = 0; r < rots.size(); r++) {
        int hops = 0;
        size_t cur = r;
        while (sameAs[cur] >= 0 && hops++ < 8 && static_cast<size_t>(sameAs[cur]) < rots.size()) {
            cur = static_cast<size_t>(sameAs[cur]);
        }
        if (cur != r) {
            rots[r] = rots[cur];
        }
    }

    char name[32];
    std::snprintf(name, sizeof(name), "/albedo_%dx.dds", _tier);
    if (!readFile(dir + name, pack.file) || !ddsParseBc7(pack.file, pack.dds)) {
        pack.file.clear();
        return nullptr;
    }
    pack.frames = std::move(rots);
    pack.valid = true;
    _lru.push_front(key);
    while (_lru.size() > kMaxResident) {
        Pack& old = _packs[_lru.back()];
        old = Pack(); // drops the file, forgets nothing else: it reloads on next use
        _packs.erase(_lru.back());
        _lru.pop_back();
    }
    return &pack;
}

bool HdArtCpu::load(const char* frmPath, int rotation, int frame, const uint8_t* indexed, int width, int height, HdRgba& out)
{
    std::string key;
    if (!_open || indexed == nullptr || width <= 0 || height <= 0 || !hdPackKey(frmPath, key)) {
        return false;
    }
    Pack* pack = get(key);
    if (pack == nullptr || rotation < 0 || static_cast<size_t>(rotation) >= pack->frames.size()) {
        return false;
    }
    const std::vector<Frame>& frames = pack->frames[rotation];
    if (frame < 0 || static_cast<size_t>(frame) >= frames.size()) {
        return false;
    }
    const Frame& f = frames[frame];
    if (f.w != width || f.h != height) {
        return false;
    }
    if (hdFnv1a(indexed, static_cast<size_t>(width) * height) != f.hash) {
        return false;
    }
    HdRgba tmp;
    if (!ddsDecodeRect(pack->dds, f.x * _tier, f.y * _tier, f.w * _tier, f.h * _tier, tmp)) {
        return false;
    }
    out = std::move(tmp);
    return true;
}

} // namespace fallout::hd
