#include "hd/map_renderer.h"

#ifdef FALLOUT_HD

#include <math.h>
#include <string.h>

#include <algorithm>
#include <unordered_map>
#include <utility>

#include "art.h"
#include "color.h"
#include "config.h"
#include "debug.h"
#include "game_config.h"
#include "hd/draw_list.h"
#include "hd/gl_private.h"
#include "hd/hd_art.h"
#include "hd/sprite_shape.h"
#include "map.h"
#include "obj_types.h"
#include "object.h"
#include "tile.h"

namespace fallout::hd {

namespace {

    // Op codes shared with the shaders (and MapRenderStats::opCounts).
    enum : unsigned int {
        kOpFill = 0,
        kOpLit = 1, // _dark_trans_buf_to_buf
        kOpFloor = 2, // tileRenderFloor's interpolated light
        kOpCopy = 3, // blitBufferToBufferTrans from the atlas
        kOpText = 4, // blitBufferToBufferTrans from the text texture
        kOpTrans = 5, // _dark_translucent_trans_buf_to_buf (compute)
        kOpEgg = 6, // _intensity_mask_buf_to_buf (compute)
    };

    // Rows of the tables texture (256 wide, R8_UINT).
    constexpr int kTableIntensity = 0; // [color][level]
    constexpr int kTableMixAdd = 256; // [a][b]
    constexpr int kTableBlend = 512; // 5 tables x 16 gray rows, [dest]
    constexpr int kTableGray = 592; // common, glass
    constexpr int kTableRows = 594;

    // Order of the translucency tables in kTableBlend.
    enum BlendTable {
        kBlendWall,
        kBlendGlass,
        kBlendSteam,
        kBlendEnergy,
        kBlendRed,
    };
    // The color each table blends toward (object.cc's _obj_render_object).
    // The COLOR_ names are palette lookups, valid only once color.pal is in.
    Color blendTableColor(int table)
    {
        const Color colors[5] = { COLOR_PALE_BLUE, COLOR_CYAN, COLOR_WHITE, COLOR_YELLOW_GREEN, COLOR_RED };
        return colors[table];
    }

    // Sprite::hd[3] flags (HD_ART, HD_HARD_ALPHA in the shader).
    constexpr int kHdArt = 1;
    constexpr int kHdHardAlpha = 2;
    constexpr int kHdEdgeFill = 4;
    constexpr int kHdEdgeClamp = 8;

    // Tiling art's flags. [hd] edge_fill=0 keeps the plain 1x mask, saw teeth and all.
    int hardAlphaFlags()
    {
        static const int flags = [] {
            int on = 1;
            configGetInt(&gGameConfig, "hd", "edge_fill", &on, 1);
            return kHdHardAlpha | (on != 0 ? kHdEdgeFill : 0);
        }();
        return flags;
    }

    // [hd] edge_clamp: HD colors in a tiling piece's edge cells (and the stair fill past
    // them) are kept within the original colors of the cell's 3x3 neighborhood. The packs
    // were upscaled with smeared colors around each piece, which leave light or dark
    // specks just inside the 1x mask; where pieces join they line up as a dashed line.
    // 0 off, 1 floors (default: 0.9.1, the dashed lines along floor seams), 2 floors and
    // roofs, 3 walls too.
    bool edgeClampFor(MapLightCategory category)
    {
        static const int mode = [] {
            int value = 1;
            configGetInt(&gGameConfig, "hd", "edge_clamp", &value, 1);
            return value;
        }();
        return (mode >= 1 && category == kLightFloor) || (mode >= 2 && category == kLightRoof) || (mode >= 3 && category == kLightWall);
    }

    constexpr int kAtlasSize = 4096;
    constexpr int kTextWidth = 2048;
    constexpr int kTextHeight = 1024;

    // tile.cc's floor intensity interpolation (verticies, triangle and row
    // tables), used to precompute which triangle writes each cell.
    constexpr int kVertexOffsets[10] = { 16, 48, 960, 992, 1024, 1936, 1968, 2000, 2912, 2944 };
    constexpr int kRightsideUpTriangles[5][3] = { { 2, 3, 0 }, { 3, 4, 1 }, { 5, 6, 3 }, { 6, 7, 4 }, { 8, 9, 6 } };
    constexpr int kUpsideDownTriangles[5][3] = { { 0, 3, 1 }, { 2, 5, 3 }, { 3, 6, 4 }, { 5, 8, 6 }, { 6, 9, 7 } };
    constexpr int kRightsideUpTable[13][2] = { { -1, 2 }, { 78, 2 }, { 76, 6 }, { 73, 8 }, { 71, 10 }, { 68, 14 }, { 65, 16 }, { 63, 18 }, { 61, 20 }, { 58, 24 }, { 55, 26 }, { 53, 28 }, { 50, 32 } };
    constexpr int kUpsideDownTable[13][2] = { { 0, 32 }, { 48, 32 }, { 49, 30 }, { 52, 26 }, { 55, 24 }, { 57, 22 }, { 60, 18 }, { 63, 16 }, { 65, 14 }, { 67, 12 }, { 70, 8 }, { 73, 6 }, { 75, 4 } };
    constexpr int kIntensityMapWidth = 80;
    constexpr int kIntensityMapSize = 3280;

    struct Sprite {
        int rect[4];
        int origin[2];
        int src[2];
        int size[2];
        unsigned int op;
        unsigned int param;
        int egg[4];
        int eggSrc[2];
        unsigned int table;
        unsigned int tableColor;
        int hd[4];
        int light[4];
    };
    static_assert(sizeof(Sprite) == 112, "Sprite must match the HLSL struct");

    enum class CommandKind {
        Raster,
        PixelOp,
        Outline,
    };

    struct Command {
        CommandKind kind;
        int first;
        int count;
    };

    struct PassConstants {
        int targetSize[2];
        int scale;
        int spriteBase;
    };

    // cbuffer Gbuffer (psGbuffer, csOccluders).
    struct GbufferConstants {
        float category[kLightCategoryCount][4];
        float detailScale;
        int occluderOrigin[2];
        int pad;
        int occluderSize[2];
        int pad2[2];
    };
    static_assert(sizeof(GbufferConstants) == 176, "GbufferConstants must match the HLSL cbuffer");

    struct Target {
        GLuint texture = 0;
        int width = 0, height = 0;
    };
    using HdTarget = Target;
    // Shelf packer for the atlas and the text texture.
    struct Packer {
        struct Shelf {
            int y;
            int height;
            int x;
        };

        int width = 0;
        int height = 0;
        int nextY = 0;
        std::vector<Shelf> shelves;

        void reset(int w, int h)
        {
            width = w;
            height = h;
            nextY = 0;
            shelves.clear();
        }

        bool alloc(int w, int h, int* x, int* y)
        {
            if (w > width) {
                return false;
            }
            for (Shelf& shelf : shelves) {
                if (h <= shelf.height && h * 2 > shelf.height && shelf.x + w <= width) {
                    *x = shelf.x;
                    *y = shelf.y;
                    shelf.x += w;
                    return true;
                }
            }
            if (nextY + h > height) {
                return false;
            }
            shelves.push_back({ nextY, h, w });
            *x = 0;
            *y = nextY;
            nextY += h;
            return true;
        }
    };

    struct AtlasEntry {
        int x;
        int y;
        int width;
        int height;
        unsigned int hash; // FNV-1a of the indices (HD packs carry the same)
        bool hasCycle; // uses palette-cycle colors (229..254)
        bool hasShape; // its shape is in the shape atlas (new light)
        // Interface art only: the pixels last uploaded (see atlasFrame).
        std::vector<unsigned char> pixels;
    };

    struct State {
        GLuint atlas = 0, text = 0, tables = 0, floorCells = 0, shapeAtlas = 0;
        GLuint sprites = 0, vertexLight = 0, basePalette = 0, program = 0, gbuffer = 0, copyFbo = 0, occluderBuffer = 0, readFbo = 0;
        Packer atlasPacker, textPacker;
        std::unordered_map<unsigned long long, AtlasEntry> atlasEntries;
        std::vector<unsigned char> tablesCpu, shapeScratch;
        bool tablesValid = false, shapesWanted = false, built = false, hasCycle = false;
        std::vector<std::pair<int, int>> glowPoints;
        std::vector<Sprite> spriteList;
        std::vector<int> vertexLightList;
        std::vector<Command> commands;
        int mapWidth = 0, mapHeight = 0, hdTier = 0;
        Target targets[5], hdTargets[5], coverage;
        MapRenderStats stats;
    };
    State* gState = nullptr;
    constexpr char kShaderSource[] =
#include "hd/map_shader.inc"
        ;
    unsigned int fnv1a(const unsigned char* data, size_t size)
    {
        unsigned int h = 2166136261u;
        for (size_t i = 0; i < size; i++) {
            h = (h ^ data[i]) * 16777619u;
        }
        return h;
    }

    // Palette-cycle colors at phase 0: cycle.cc's tables before the first
    // rotation (8-bit, the game keeps them >> 2), the alarm at its peak.
    // The HD art was upscaled with these (fo2hd.frm.load_palette).
    constexpr unsigned char kCyclePhase0[26][3] = {
        { 0, 108, 0 }, { 11, 115, 7 }, { 27, 123, 15 }, { 43, 131, 27 }, // slime 229
        { 107, 107, 111 }, { 99, 103, 127 }, { 87, 107, 143 }, { 0, 147, 163 }, { 107, 187, 255 }, // monitors 233
        { 255, 0, 0 }, { 215, 0, 0 }, { 147, 43, 11 }, { 255, 119, 0 }, { 255, 59, 0 }, // slow fire 238
        { 71, 0, 0 }, { 123, 0, 0 }, { 179, 0, 0 }, { 123, 0, 0 }, { 71, 0, 0 }, // fast fire 243
        { 83, 63, 43 }, { 75, 59, 43 }, { 67, 55, 39 }, { 63, 51, 39 }, { 55, 47, 35 }, { 51, 43, 35 }, // shoreline 248
        { 252, 0, 0 }, // alarm 254
    };

    // The base palette for the HD passes: the caller's 256 colors, then at
    // 256 + index the phase-0 cycle colors (the shaders' cycleAt).
    bool uploadBasePalette(const float* palette)
    {
        float colors[512 * 4] = {};
        memcpy(colors, palette, 256 * 4 * sizeof(float));
        for (int i = 0; i < 26; i++) {
            for (int c = 0; c < 3; c++) {
                colors[(256 + 229 + i) * 4 + c] = (kCyclePhase0[i][c] >> 2) * 4 / 255.0f;
            }
            colors[(256 + 229 + i) * 4 + 3] = 1.0f;
        }
        glBuffer(gState->basePalette, colors, sizeof(colors));
        return true;
    }

    // Which triangle (and which row/column of it) last writes each cell of
    // tile.cc's _intensity_map, replaying its fill order.
    std::vector<unsigned short> buildFloorCells()
    {
        std::vector<unsigned short> cells(kIntensityMapSize, 0);
        for (int tri = 0; tri < 5; tri++) {
            int p = kVertexOffsets[kRightsideUpTriangles[tri][2]];
            for (int i = 0; i < 13; i++) {
                p += kRightsideUpTable[i][0];
                for (int j = 0; j < kRightsideUpTable[i][1]; j++) {
                    cells[p++] = static_cast<unsigned short>(0x8000 | (tri << 11) | (i << 6) | j);
                }
            }
        }
        for (int tri = 0; tri < 5; tri++) {
            int p = kVertexOffsets[kUpsideDownTriangles[tri][0]];
            for (int i = 0; i < 13; i++) {
                p += kUpsideDownTable[i][0];
                for (int j = 0; j < kUpsideDownTable[i][1]; j++) {
                    cells[p++] = static_cast<unsigned short>(0x8000 | 0x4000 | (tri << 11) | (i << 6) | j);
                }
            }
        }
        return cells;
    }

    // CE's lookup tables, laid out as the shaders read them. Re-uploaded
    // when any of them changes (palette loads rebuild intensityColorTable).
    void syncTables()
    {
        std::vector<unsigned char> rows(256 * kTableRows, 0);
        for (int color = 0; color < 256; color++) {
            memcpy(&rows[(kTableIntensity + color) * 256], intensityColorTable[color], 256);
            memcpy(&rows[(kTableMixAdd + color) * 256], colorMixAddTable[color], 256);
        }

        for (int table = 0; table < 5; table++) {
            const Color* blend = _blendTable[blendTableColor(table)];
            if (blend != nullptr) {
                memcpy(&rows[(kTableBlend + table * 16) * 256], blend, 16 * 256);
            }
        }

        memcpy(&rows[kTableGray * 256], _commonGrayTable, 256);
        // object.cc keeps its glass gray table private; same formula.
        for (int index = 0; index < 256; index++) {
            int rgb = Color2RGB(static_cast<Color>(index));
            int r = (rgb & 0x7C00) >> 10;
            int g = (rgb & 0x3E0) >> 5;
            int b = rgb & 0x1F;
            rows[(kTableGray + 1) * 256 + index] = static_cast<unsigned char>(((r + 5 * g + 4 * b) / 10) >> 2);
        }
        rows[(kTableGray + 1) * 256] = 0;

        if (gState->tablesValid && rows == gState->tablesCpu) {
            return;
        }
        gState->tablesCpu.swap(rows);
        glUpload(gState->tables, 0, 0, 256, kTableRows, GL_RED_INTEGER, GL_UNSIGNED_BYTE, gState->tablesCpu.data());
        gState->tablesValid = true;
    }

    unsigned long long atlasKey(int fid, int frame, int rotation)
    {
        return (static_cast<unsigned long long>(static_cast<unsigned int>(fid)) << 32) | (static_cast<unsigned int>(rotation) << 16) | static_cast<unsigned int>(frame & 0xFFFF);
    }

    void resetAtlas()
    {
        gState->atlasEntries.clear();
        gState->atlasPacker.reset(kAtlasSize, kAtlasSize);
    }

    // The frame's place in the atlas, uploading it on first use. Returns
    // null if the art can't be loaded or the atlas is full (`full` set).
    //
    // Art is immutable except interface art: game_mouse.cc draws action
    // points, accuracy and the "no path" X into the hex and bouncing cursor
    // frames in place. Those frames are compared on every use and uploaded
    // again when they change.
    // Round scenery (the author, 2026-11-09: trees, cacti, bushes, rocks and
    // barrels should look more 3D) gets a rounded body (sprite_shape.h) of
    // [hd_light] round_body strength and its own light category (round=).
    // Known by art name; cliffs, fences and buildings stay as they were.
    // Default 0.5: the author's blind vote of 2026-11-10 (0.5 won 5 of 6 against
    // 0, 1 and 1.5; "the difference was very hard to tell").
    float roundBodyStrength()
    {
        static const float strength = [] {
            double value = 0.5;
            configGetDouble(&gGameConfig, "hd_light", "round_body", &value);
            return static_cast<float>(std::max(value, 0.0));
        }();
        return strength;
    }

    bool roundScenery(int fid)
    {
        if (FrmId(fid).objectType() != OBJ_TYPE_SCENERY || roundBodyStrength() <= 0.0f) {
            return false;
        }
        static std::unordered_map<int, bool> known;
        int key = fid & 0x0F000FFF;
        auto it = known.find(key);
        if (it != known.end()) {
            return it->second;
        }
        char name[16] = {};
        bool round = false;
        if (artCopyFileName(FrmId(fid), name) == 0) {
            for (char& c : name) {
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            }
            static const char* const kPrefixes[] = { "tree", "tre1", "bush", "cac", "rock", "drock", "modrock", "mbrocks", "barrel", "brl", "weed", "cvweed" };
            for (const char* prefix : kPrefixes) {
                if (strncmp(name, prefix, strlen(prefix)) == 0) {
                    round = true;
                    break;
                }
            }
        }
        known[key] = round;
        return round;
    }

    // The frame's shape (sprite_shape.h) into the shape atlas, at the
    // entry's place.
    void uploadShape(int fid, AtlasEntry& entry, const unsigned char* data)
    {
        gState->shapeScratch.resize(static_cast<size_t>(entry.width) * entry.height * 4);
        spriteShape(data, entry.width, entry.height, FrmId(fid).objectType(), gState->shapeScratch.data(), roundScenery(fid) ? roundBodyStrength() : 0.0f);
        glUpload(gState->shapeAtlas, entry.x, entry.y, entry.width, entry.height, GL_RGBA, GL_UNSIGNED_BYTE, gState->shapeScratch.data());
        entry.hasShape = true;
    }

    // Shapes are computed only while the new light is on; frames that went
    // into the atlas before get theirs on next use.
    void ensureShape(int fid, int frame, int rotation, AtlasEntry& entry)
    {
        if (!gState->shapesWanted || entry.hasShape) {
            return;
        }
        CacheEntry* handle;
        Art* art = artLock(FrmId(fid), &handle);
        if (art == nullptr) {
            return;
        }
        unsigned char* data = artGetFrameData(art, frame, static_cast<Rotation>(rotation));
        if (data != nullptr) {
            uploadShape(fid, entry, data);
        }
        artUnlock(handle);
    }

    const AtlasEntry* atlasFrame(int fid, int frame, int rotation, bool* full)
    {
        unsigned long long key = atlasKey(fid, frame, rotation);
        auto it = gState->atlasEntries.find(key);
        bool mutableArt = FrmId(fid).objectType() == OBJ_TYPE_INTERFACE;
        if (it != gState->atlasEntries.end() && !mutableArt) {
            ensureShape(fid, frame, rotation, it->second);
            return &it->second;
        }

        CacheEntry* handle;
        Art* art = artLock(FrmId(fid), &handle);
        if (art == nullptr) {
            return nullptr;
        }
        int width = 0;
        int height = 0;
        artGetSize(art, frame, static_cast<Rotation>(rotation), &width, &height);
        unsigned char* data = artGetFrameData(art, frame, static_cast<Rotation>(rotation));
        size_t size = static_cast<size_t>(width) * height;
        bool ok = data != nullptr && width > 0 && height > 0;

        AtlasEntry* entry = nullptr;
        if (ok && it != gState->atlasEntries.end() && it->second.width == width && it->second.height == height) {
            entry = &it->second;
            if (memcmp(entry->pixels.data(), data, size) == 0) {
                artUnlock(handle);
                return entry;
            }
        } else if (ok) {
            AtlasEntry fresh = { 0, 0, width, height, 0, false, false, {} };
            if (!gState->atlasPacker.alloc(width, height, &fresh.x, &fresh.y)) {
                *full = true;
                ok = false;
            } else {
                gState->atlasEntries.erase(key);
                entry = &gState->atlasEntries.emplace(key, std::move(fresh)).first->second;
            }
        }
        if (ok) {
            glUpload(gState->atlas, entry->x, entry->y, width, height, GL_RED_INTEGER, GL_UNSIGNED_BYTE, data);
            gState->stats.atlasUploads++;
            entry->hash = fnv1a(data, size);
            entry->hasCycle = false;
            for (size_t i = 0; i < size; i++) {
                if (data[i] >= 0xE5 && data[i] != 0xFF) {
                    entry->hasCycle = true;
                    break;
                }
            }
            if (mutableArt) {
                entry->pixels.assign(data, data + size);
            }
            entry->hasShape = false;
            if (gState->shapesWanted && !mutableArt) {
                uploadShape(fid, *entry, data);
            }
        }
        artUnlock(handle);
        return ok ? entry : nullptr;
    }

    bool intersect(const Rect& a, const Rect& b, Rect* out)
    {
        out->left = std::max(a.left, b.left);
        out->top = std::max(a.top, b.top);
        out->right = std::min(a.right, b.right);
        out->bottom = std::min(a.bottom, b.bottom);
        return out->left <= out->right && out->top <= out->bottom;
    }

    Sprite makeSprite(unsigned int op, const Rect& rect, int originX, int originY, const AtlasEntry* source)
    {
        if (source != nullptr && source->hasCycle) {
            gState->hasCycle = true;
        }
        Sprite s = {};
        s.op = op;
        s.rect[0] = rect.left;
        s.rect[1] = rect.top;
        s.rect[2] = rect.right;
        s.rect[3] = rect.bottom;
        s.origin[0] = originX;
        s.origin[1] = originY;
        if (source != nullptr) {
            s.src[0] = source->x;
            s.src[1] = source->y;
            s.size[0] = source->width;
            s.size[1] = source->height;
        }
        s.egg[0] = 0;
        s.egg[1] = 0;
        s.egg[2] = -1; // no egg
        s.egg[3] = -1;
        return s;
    }

    void addOneSprite(CommandKind kind, const Sprite& sprite)
    {
        int index = static_cast<int>(gState->spriteList.size());
        gState->spriteList.push_back(sprite);
        if (kind == CommandKind::Raster && !gState->commands.empty() && gState->commands.back().kind == CommandKind::Raster) {
            gState->commands.back().count++;
        } else {
            gState->commands.push_back({ kind, index, 1 });
        }
        if (kind == CommandKind::PixelOp) {
            gState->stats.computeOps++;
        } else if (kind == CommandKind::Outline) {
            gState->stats.outlines++;
        }
        gState->stats.opCounts[kind == CommandKind::Outline ? kMapOpOutline : sprite.op]++;
    }

    // `tiles` (from setHd): the frame is larger than a pool page, so its HD
    // art is in tiles, each in its own place in the pool. One sprite per tile
    // then, over the tile's part of the rect: every pass reads the art at
    // the pixel it draws (hdLuma's neighbors stay inside the tile, light.zw),
    // so the pieces draw what one sprite would. The occluder pass walks the
    // whole frame per piece; its max makes that the same.
    void addSprite(CommandKind kind, const Sprite& sprite, const std::vector<HdFrameTile>* tiles = nullptr)
    {
        if (tiles == nullptr || tiles->empty() || (sprite.hd[3] & kHdArt) == 0) {
            addOneSprite(kind, sprite);
            return;
        }
        int tier = gState->hdTier;
        Rect rect = { sprite.rect[0], sprite.rect[1], sprite.rect[2], sprite.rect[3] };
        for (const HdFrameTile& tile : *tiles) {
            Rect part = { sprite.origin[0] + tile.x, sprite.origin[1] + tile.y, sprite.origin[0] + tile.x + tile.width - 1, sprite.origin[1] + tile.y + tile.height - 1 };
            Rect cut;
            if (!intersect(rect, part, &cut)) {
                continue;
            }
            Sprite piece = sprite;
            piece.rect[0] = cut.left;
            piece.rect[1] = cut.top;
            piece.rect[2] = cut.right;
            piece.rect[3] = cut.bottom;
            piece.hd[0] = tile.frame.x - tile.x * tier;
            piece.hd[1] = tile.frame.y - tile.y * tier;
            piece.hd[2] = tile.frame.slice;
            piece.light[2] = (tile.x * tier) | ((tile.y * tier) << 16);
            piece.light[3] = ((tile.x + tile.width) * tier) | (((tile.y + tile.height) * tier) << 16);
            addOneSprite(kind, piece);
        }
    }

    MapLightCategory lightCategory(const DrawRecord& record)
    {
        if (record.op == DrawOp::Floor) {
            return kLightFloor;
        }
        if (record.op == DrawOp::Roof) {
            return kLightRoof;
        }
        switch (FrmId(record.fid).objectType()) {
        case OBJ_TYPE_WALL:
            return kLightWall;
        case OBJ_TYPE_SCENERY:
            return roundScenery(record.fid) ? kLightRound : kLightScenery;
        case OBJ_TYPE_CRITTER:
            return kLightCritter;
        case OBJ_TYPE_ITEM:
            return kLightItem;
        case OBJ_TYPE_MISC:
            return kLightMisc;
        default:
            return kLightNone;
        }
    }

    // Hexes of the lights that make nearby flames and neon glow (kLightGlow):
    // objects with a light on the current elevation, critters excepted. Kept
    // as screen points: tileDistanceBetween walks the hex path, too slow for
    // every sprite against every light.
    void gatherGlowPoints()
    {
        gState->glowPoints.clear();
        for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
            for (Object* object = objectFindFirstAtLocation(gElevation, tile); object != nullptr; object = objectFindNextAtLocation()) {
                int x;
                int y;
                if (object->elevation == gElevation && object->lightDistance > 0 && object->lightIntensity > 0
                    && FrmId(object->fid).objectType() != OBJ_TYPE_CRITTER && tileToScreenXY(tile, &x, &y) == 0) {
                    gState->glowPoints.emplace_back(x, y);
                    break;
                }
            }
        }
    }

    // Within about 2 hexes of a glow light (ground units: v = 2 * screen y,
    // a hex is 32 wide).
    bool nearGlowPoint(int tile)
    {
        int x;
        int y;
        if (gState->glowPoints.empty() || tileToScreenXY(tile, &x, &y) != 0) {
            return false;
        }
        for (const auto& light : gState->glowPoints) {
            int dx = x - light.first;
            int dy = 2 * (y - light.second);
            if (dx * dx + dy * dy <= 80 * 80) {
                return true;
            }
        }
        return false;
    }

    // Where the HD pass finds this frame's HD art (hd/hd_art.h), or zeros:
    // the original art. Also the sprite's light category and flags for the
    // G-buffer. A frame larger than a pool page comes in `tiles` (the part
    // of it the sprite's rect shows), for addSprite to split the sprite by;
    // without `tiles` such a frame keeps the original art.
    void setHd(Sprite& s, const DrawRecord& record, int frame, int rotation, const AtlasEntry* art, std::vector<HdFrameTile>* tiles = nullptr)
    {
        s.light[0] = lightCategory(record);
        s.light[1] = record.op == DrawOp::Object && (record.flags & OBJECT_FLAT) != 0 ? kLightFlat : 0;
        if (gState->shapesWanted && record.op == DrawOp::Object && record.tile >= 0 && nearGlowPoint(record.tile)) {
            s.light[1] |= kLightGlow;
        }
        if (gState->hdTier == 0) {
            return;
        }
        if (tiles != nullptr && !hdArtFitsPage(art->width, art->height)) {
            int x = s.rect[0] - s.origin[0];
            int y = s.rect[1] - s.origin[1];
            if (hdArtFrameTiles(record.fid, nullptr, rotation, frame, art->hash, art->width, art->height, x, y, s.rect[2] - s.rect[0] + 1, s.rect[3] - s.rect[1] + 1, tiles)) {
                s.hd[3] = kHdArt | (tiles->front().frame.hardAlpha ? hardAlphaFlags() : 0);
                if (tiles->front().frame.hardAlpha && edgeClampFor(static_cast<MapLightCategory>(s.light[0]))) {
                    s.hd[3] |= kHdEdgeClamp;
                }
            }
            return;
        }
        HdFrame hd;
        if (hdArtFrame(record.fid, rotation, frame, art->hash, art->width, art->height, &hd)) {
            s.hd[0] = hd.x;
            s.hd[1] = hd.y;
            s.hd[2] = hd.slice;
            s.hd[3] = kHdArt | (hd.hardAlpha ? hardAlphaFlags() : 0);
            if (hd.hardAlpha && edgeClampFor(static_cast<MapLightCategory>(s.light[0]))) {
                s.hd[3] |= kHdEdgeClamp;
            }
        }
    }

    // A lit blit, split around the egg the way roofs and walls are: outside
    // the egg rect as _dark_trans_buf_to_buf, inside through the egg mask.
    void addLitWithEgg(const Rect& rect, int originX, int originY, const AtlasEntry* art, int light, const DrawRecord& record, bool* full, int frame = 0, int rotation = 0)
    {
        Sprite lit = makeSprite(kOpLit, rect, originX, originY, art);
        lit.param = light / 512;
        std::vector<HdFrameTile> tiles;
        setHd(lit, record, frame, rotation, art, &tiles);

        Rect cut;
        if (record.egg && intersect(record.eggRect, rect, &cut)) {
            const AtlasEntry* egg = atlasFrame(record.eggFid, 0, 0, full);
            if (egg != nullptr) {
                lit.egg[0] = record.eggRect.left;
                lit.egg[1] = record.eggRect.top;
                lit.egg[2] = record.eggRect.right;
                lit.egg[3] = record.eggRect.bottom;
                addSprite(CommandKind::Raster, lit, &tiles);

                Sprite mask = makeSprite(kOpEgg, cut, originX, originY, art);
                mask.param = light / 512;
                memcpy(mask.hd, lit.hd, sizeof(mask.hd));
                memcpy(mask.light, lit.light, sizeof(mask.light));
                mask.egg[0] = record.eggRect.left;
                mask.egg[1] = record.eggRect.top;
                mask.egg[2] = record.eggRect.right;
                mask.egg[3] = record.eggRect.bottom;
                mask.eggSrc[0] = egg->x;
                mask.eggSrc[1] = egg->y;
                addSprite(CommandKind::PixelOp, mask, &tiles);
                return;
            }
        }
        addSprite(CommandKind::Raster, lit, &tiles);
    }

    void addFloor(const DrawRecord& record, bool* full)
    {
        const AtlasEntry* art = atlasFrame(record.fid, 0, 0, full);
        if (art == nullptr) {
            return;
        }

        // tileRenderFloor's clipping, quirks included (the width isn't
        // reduced when the refresh rect starts left of the window).
        int W = gState->mapWidth;
        int H = gState->mapHeight;
        int left = record.clip.left;
        int top = record.clip.top;
        int width = record.clip.right - record.clip.left + 1;
        int height = record.clip.bottom - record.clip.top + 1;
        int x = record.x;
        int y = record.y;
        if (left < 0) {
            left = 0;
        }
        if (top < 0) {
            top = 0;
        }
        if (left + width > W) {
            width = W - left;
        }
        if (top + height > H) {
            height = H - top;
        }
        if (x >= W || x > record.clip.right || y >= H || y > record.clip.bottom) {
            return;
        }

        int frameWidth = art->width;
        int frameHeight = art->height;
        int w;
        int h;
        if (left < x) {
            w = frameWidth + x <= left + width ? frameWidth : left + width - x;
        } else {
            int sx = left - x;
            x = left;
            w = std::min(frameWidth - sx, width);
        }
        if (top < y) {
            h = frameHeight + y <= top + height ? frameHeight : top + height - y;
        } else {
            int sy = top - y;
            y = top;
            h = std::min(frameHeight - sy, height);
        }
        if (w <= 0 || h <= 0) {
            return;
        }

        Rect rect = { x, y, x + w - 1, y + h - 1 };
        bool uniform = true;
        for (int i = 1; i < 10; i++) {
            if (record.vertexIntensities[i] != record.vertexIntensities[0]) {
                uniform = false;
                break;
            }
        }

        Sprite s = makeSprite(uniform ? kOpLit : kOpFloor, rect, record.x, record.y, art);
        setHd(s, record, 0, 0, art);
        if (uniform) {
            s.param = record.vertexIntensities[0] / 512;
        } else {
            s.param = static_cast<unsigned int>(gState->vertexLightList.size());
            gState->vertexLightList.insert(gState->vertexLightList.end(), record.vertexIntensities, record.vertexIntensities + 10);
        }
        addSprite(CommandKind::Raster, s);
    }

    void addRoof(const DrawRecord& record, bool* full)
    {
        const AtlasEntry* art = atlasFrame(record.fid, 0, 0, full);
        if (art == nullptr) {
            return;
        }
        Rect rect;
        Rect tileRect = { record.x, record.y, record.x + art->width - 1, record.y + art->height - 1 };
        if (!intersect(tileRect, record.clip, &rect)) {
            return;
        }
        addLitWithEgg(rect, record.x, record.y, art, record.light, record, full);
    }

    void addObject(const DrawRecord& record, bool* full)
    {
        const AtlasEntry* art = atlasFrame(record.fid, record.frame, record.rotation, full);
        if (art == nullptr) {
            return;
        }
        Rect rect;
        Rect objectRect = { record.x, record.y, record.x + record.width - 1, record.y + record.height - 1 };
        if (!intersect(objectRect, record.clip, &rect)) {
            return;
        }

        switch (record.blend) {
        case ObjectBlend::Interface:
            addSprite(CommandKind::Raster, makeSprite(kOpCopy, rect, record.x, record.y, art));
            return;
        case ObjectBlend::Normal:
            addLitWithEgg(rect, record.x, record.y, art, record.light, record, full, record.frame, record.rotation);
            return;
        default:
            break;
        }

        Sprite s = makeSprite(kOpTrans, rect, record.x, record.y, art);
        int light = record.light;
        unsigned int table = kBlendWall;
        unsigned int gray = 0;
        switch (record.blend) {
        case ObjectBlend::TransRed:
            table = kBlendRed;
            break;
        case ObjectBlend::TransWall:
            table = kBlendWall;
            light = 0x10000;
            break;
        case ObjectBlend::TransGlass:
            table = kBlendGlass;
            gray = 1;
            break;
        case ObjectBlend::TransSteam:
            table = kBlendSteam;
            break;
        case ObjectBlend::TransEnergy:
            table = kBlendEnergy;
            break;
        default:
            break;
        }
        s.param = light / 512;
        s.table = table | (gray << 8);
        s.tableColor = blendTableColor(table);
        std::vector<HdFrameTile> tiles;
        setHd(s, record, record.frame, record.rotation, art, &tiles);
        addSprite(CommandKind::PixelOp, s, &tiles);
    }

    void addOutline(const DrawRecord& record, bool* full)
    {
        const AtlasEntry* art = atlasFrame(record.fid, record.frame, record.rotation, full);
        if (art == nullptr) {
            return;
        }
        int frameWidth = record.width;
        int frameHeight = record.height;
        Rect objectRect = { record.x, record.y, record.x + frameWidth - 1, record.y + frameHeight - 1 };
        Rect visible;
        if (!intersect(objectRect, record.clip, &visible)) {
            return;
        }

        // objectDrawOutline's choice of colors.
        int paletted = (record.outline & OUTLINE_PALETTED) != 0 ? 1 : 0;
        int type = record.outline & OUTLINE_TYPE_MAX;
        int color;
        int count = 0;
        int band = 0;
        int blend = 0;
        switch (type) {
        case OUTLINE_TYPE_HOSTILE:
            color = 243;
            paletted = 0;
            count = 5;
            band = frameHeight / 5;
            break;
        case OUTLINE_TYPE_SAME_TEAM:
            color = COLOR_RED;
            blend = kBlendRed;
            break;
        case OUTLINE_TYPE_BODY:
            color = COLOR_GREY_2;
            blend = kBlendWall;
            break;
        case OUTLINE_TYPE_FRIENDLY:
            count = 4;
            band = frameHeight / 4;
            color = 229;
            paletted = 0;
            break;
        case OUTLINE_TYPE_ITEM:
            color = COLOR_LIGHT_GOLD_2;
            blend = kBlendRed;
            break;
        case OUTLINE_TYPE_BLOCKED:
            color = 61;
            paletted = 0;
            count = 1;
            band = frameHeight;
            break;
        default:
            color = COLOR_MAGENTA;
            paletted = 0;
            break;
        }

        Sprite s = makeSprite(0, objectRect, record.x, record.y, art);
        s.size[0] = frameWidth;
        s.size[1] = frameHeight;
        s.egg[0] = visible.left - record.x;
        s.egg[1] = visible.top - record.y;
        s.egg[2] = visible.right - record.x;
        s.egg[3] = visible.bottom - record.y;
        s.param = static_cast<unsigned int>(color & 0xFF) | (static_cast<unsigned int>(count) << 8) | (static_cast<unsigned int>(band) << 16);
        s.table = paletted != 0 ? (0x10000u | (static_cast<unsigned int>(blend) << 8)) : 0;
        addSprite(CommandKind::Outline, s);
    }

    void addText(const DrawRecord& record)
    {
        Rect rect;
        Rect textRect = { record.x, record.y, record.x + record.width - 1, record.y + record.height - 1 };
        if (!intersect(textRect, record.clip, &rect)) {
            return;
        }
        AtlasEntry entry = { 0, 0, record.width, record.height, 0, false, {} };
        if (!gState->textPacker.alloc(record.width, record.height, &entry.x, &entry.y)) {
            debugPrint("HD: map renderer: text texture full, %dx%d text skipped\n", record.width, record.height);
            return;
        }
        const std::vector<unsigned char>& pixels = drawTextPixels(record.textIndex);
        if (pixels.size() < static_cast<size_t>(record.width) * record.height) return;
        glUpload(gState->text, entry.x, entry.y, record.width, record.height, GL_RED_INTEGER, GL_UNSIGNED_BYTE, pixels.data());
        addSprite(CommandKind::Raster, makeSprite(kOpText, rect, record.x, record.y, &entry));
    }

    // One attempt at turning the draw list into sprites; false if the atlas
    // filled up on the way.
    bool buildOnce()
    {
        gState->spriteList.clear();
        gState->vertexLightList.clear();
        gState->commands.clear();
        gState->textPacker.reset(kTextWidth, kTextHeight);
        gState->stats.computeOps = 0;
        gState->stats.outlines = 0;
        gState->hasCycle = false;
        memset(gState->stats.opCounts, 0, sizeof(gState->stats.opCounts));

        Rect window = { 0, 0, gState->mapWidth - 1, gState->mapHeight - 1 };
        bool full = false;
        for (const DrawRecord& record : drawRecords()) {
            switch (record.op) {
            case DrawOp::Pass:
                break;
            case DrawOp::Fill: {
                Rect rect;
                if (intersect(record.clip, window, &rect)) {
                    Sprite s = makeSprite(kOpFill, rect, 0, 0, nullptr);
                    s.param = static_cast<unsigned int>(record.color);
                    if (record.color >= 0xE5 && record.color < 0xFF) {
                        gState->hasCycle = true;
                    }
                    addSprite(CommandKind::Raster, s);
                }
                break;
            }
            case DrawOp::Floor:
                addFloor(record, &full);
                break;
            case DrawOp::Roof:
                addRoof(record, &full);
                break;
            case DrawOp::Object:
                addObject(record, &full);
                break;
            case DrawOp::Outline:
                addOutline(record, &full);
                break;
            case DrawOp::Text:
                addText(record);
                break;
            }
            if (full) {
                return false;
            }
        }
        return true;
    }

    bool ensure(Target& t, int scale, GLenum format)
    {
        int w = gState->mapWidth * scale, h = gState->mapHeight * scale;
        if (t.texture && t.width == w && t.height == h) return true;
        glDeleteTextures(1, &t.texture);
        t = { glTexture(w, h, format, format == GL_R32UI ? GL_UNSIGNED_INT : GL_FLOAT), w, h };
        return glGetError() == GL_NO_ERROR;
    }
    void bindInputs()
    {
        GLuint textures[] = { gState->atlas, gState->text, gState->tables, gState->floorCells };
        for (int i = 0; i < 4; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, textures[i]);
            glBindSampler(i, 0);
        }
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D_ARRAY, hdArtPoolView());
        glBindSampler(4, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gState->sprites);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, gState->vertexLight);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, gState->basePalette);
        glUseProgram(gState->program);
    }
    void pass(int scale, int base, int mode)
    {
        GLuint p = gState->program;
        glUniform2i(glGetUniformLocation(p, "gTargetSize"), gState->mapWidth * scale, gState->mapHeight * scale);
        glUniform1i(glGetUniformLocation(p, "gScale"), scale);
        glUniform1i(glGetUniformLocation(p, "gSpriteBase"), base);
        glUniform1i(glGetUniformLocation(p, "gMode"), mode);
    }
    bool replay(int scale, bool hd)
    {
        bindInputs();
        Target& t = hd ? gState->hdTargets[scale] : gState->targets[scale];
        glBindImageTexture(hd ? 1 : 0, t.texture, 0, GL_FALSE, 0, GL_READ_WRITE, hd ? GL_RGBA16F : GL_R32UI);
        if (hd) {
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, t.texture);
            glBindSampler(6, 0);
        }
        // Clearing through our FBO avoids relying on ES 3.2 clear-texture.
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gState->readFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
        if (hd) {
            const float zero[4] = {};
            glClearBufferfv(GL_COLOR, 0, zero);
        } else {
            const GLuint zero[4] = {};
            glClearBufferuiv(GL_COLOR, 0, zero);
        }
        glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        for (const Command& c : gState->commands)
            for (int n = 0; n < c.count; n++) {
                int i = c.first + n;
                const Sprite& s = gState->spriteList[i];
                if (c.kind == CommandKind::Outline) {
                    if (hd) continue; // compositor takes tagged outline pixels from exact
                    pass(scale, i, 2);
                    glDispatchCompute((s.size[1] + 7) / 8, 1, 1);
                    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
                    pass(scale, i, 3);
                    glDispatchCompute((s.size[0] + 7) / 8, 1, 1);
                } else {
                    int w = (s.rect[2] - s.rect[0] + 1) * scale, h = (s.rect[3] - s.rect[1] + 1) * scale;
                    if (w <= 0 || h <= 0) continue;
                    pass(scale, i, hd ? 1 : 0);
                    glDispatchCompute((w + 7) / 8, (h + 7) / 8, 1);
                }
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            }
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
        return glGetError() == GL_NO_ERROR;
    }
} // namespace
bool mapRendererInit()
{
    mapRendererShutdown();
    if (!hdGlLoad()) return false;
    GlState saved;
    gState = new State;
    gState->program = glCompute(kShaderSource);
    if (!gState->program) {
        mapRendererShutdown();
        return false;
    }
    gState->atlas = glTexture(kAtlasSize, kAtlasSize, GL_R8UI, GL_UNSIGNED_BYTE);
    gState->shapeAtlas = glTexture(kAtlasSize, kAtlasSize, GL_RGBA8, GL_UNSIGNED_BYTE);
    gState->text = glTexture(kTextWidth, kTextHeight, GL_R8UI, GL_UNSIGNED_BYTE);
    gState->tables = glTexture(256, kTableRows, GL_R8UI, GL_UNSIGNED_BYTE);
    auto cells = buildFloorCells();
    gState->floorCells = glTexture(80, 41, GL_R16UI, GL_UNSIGNED_SHORT, cells.data());
    glGenBuffers(1, &gState->sprites);
    glGenBuffers(1, &gState->vertexLight);
    glGenBuffers(1, &gState->basePalette);
    glGenFramebuffers(1, &gState->readFbo);
    resetAtlas();
    return glGetError() == GL_NO_ERROR;
}
void mapRendererShutdown()
{
    if (!gState) return;
    GLuint tex[] = { gState->atlas, gState->shapeAtlas, gState->text, gState->tables, gState->floorCells };
    glDeleteTextures(5, tex);
    for (int i = 1; i <= 4; i++) {
        glDeleteTextures(1, &gState->targets[i].texture);
        glDeleteTextures(1, &gState->hdTargets[i].texture);
    }
    GLuint buf[] = { gState->sprites, gState->vertexLight, gState->basePalette };
    glDeleteBuffers(3, buf);
    glDeleteProgram(gState->program);
    glDeleteProgram(gState->gbuffer);
    glDeleteFramebuffers(1, &gState->copyFbo);
    glDeleteTextures(1, &gState->coverage.texture);
    glDeleteBuffers(1, &gState->occluderBuffer);
    glDeleteFramebuffers(1, &gState->readFbo);
    delete gState;
    gState = nullptr;
}
bool mapRendererBuild(int width, int height, int hdTier)
{
    if (!gState || width <= 0 || height <= 0) return false;
    GlState saved;
    gState->built = false;
    gState->mapWidth = width;
    gState->mapHeight = height;
    gState->stats.records = drawRecords().size();
    syncTables();
    if (gState->shapesWanted) gatherGlowPoints();
    gState->hdTier = hdTier >= 2 && hdTier <= 4 && hdArtActive() ? hdTier : 0;
    if (gState->hdTier) hdArtBeginBuild(gState->hdTier);
    bool ok = buildOnce();
    if (!ok) {
        resetAtlas();
        gState->stats.atlasResets++;
        ok = buildOnce();
    }
    if (gState->hdTier) hdArtEndBuild();
    if (!ok) return false;
    glBuffer(gState->sprites, gState->spriteList.data(), gState->spriteList.size() * sizeof(Sprite));
    glBuffer(gState->vertexLight, gState->vertexLightList.empty() ? nullptr : gState->vertexLightList.data(), gState->vertexLightList.size() * sizeof(int));
    gState->stats.sprites = gState->spriteList.size();
    gState->stats.rasterBatches = gState->commands.size();
    gState->stats.atlasUsed = float(gState->atlasPacker.nextY) / kAtlasSize;
    gState->built = glGetError() == GL_NO_ERROR;
    return gState->built;
}
bool mapRendererDraw(int scale)
{
    if (!gState || !gState->built || scale < 1 || scale > 4) return false;
    GlState saved;
    return ensure(gState->targets[scale], scale, GL_R32UI) && replay(scale, false);
}
GLuint mapRendererView(int scale) { return gState && scale >= 1 && scale <= 4 ? gState->targets[scale].texture : 0; }
bool mapRendererReadback(int scale, std::vector<unsigned char>& pixels, int* w, int* h, std::vector<unsigned char>* tags)
{
    if (!mapRendererView(scale)) return false;
    GlState saved;
    auto& t = gState->targets[scale];
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gState->readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
    std::vector<GLuint> data(size_t(t.width) * t.height * 4);
    glReadPixels(0, 0, t.width, t.height, GL_RGBA_INTEGER, GL_UNSIGNED_INT, data.data());
    pixels.resize(data.size() / 4);
    if (tags) tags->resize(pixels.size());
    for (size_t i = 0; i < pixels.size(); i++) {
        pixels[i] = data[i * 4] & 255;
        if (tags) (*tags)[i] = (data[i * 4] & 0x100) ? 1 : 0;
    }
    if (w) *w = t.width;
    if (h) *h = t.height;
    GLenum error = glGetError();
    if (error) debugPrint("HD readback error %x fbo %x texture %u\n", error, glCheckFramebufferStatus(GL_READ_FRAMEBUFFER), t.texture);
    return error == GL_NO_ERROR;
}
const MapRenderStats& mapRendererStats()
{
    static MapRenderStats empty;
    return gState ? gState->stats : empty;
}
long long mapRendererGpuBytes()
{
    if (!gState) return 0;
    long long n = 5ll * kAtlasSize * kAtlasSize + kTextWidth * kTextHeight + 256 * kTableRows + 6560;
    for (int i = 1; i <= 4; i++)
        n += 4ll * gState->targets[i].width * gState->targets[i].height + 8ll * gState->hdTargets[i].width * gState->hdTargets[i].height;
    return n + 4ll * gState->coverage.width * gState->coverage.height;
}
int mapRendererHdTier() { return gState ? gState->hdTier : 0; }
bool mapRendererHasCycle() { return gState && gState->hasCycle; }
void mapRendererCyclePhase0(int i, float rgb[3])
{
    for (int c = 0; c < 3; c++)
        rgb[c] = i >= 229 && i <= 254 ? (kCyclePhase0[i - 229][c] >> 2) * 4 / 255.f : 0;
}
bool mapRendererDrawHd(int scale, const float* palette)
{
    if (!gState || !gState->built || scale < 1 || scale > 4 || scale != gState->hdTier || !palette) return false;
    GlState saved;
    return uploadBasePalette(palette) && ensure(gState->hdTargets[scale], scale, GL_RGBA16F) && replay(scale, true);
}
GLuint mapRendererHdView(int scale) { return gState && scale >= 1 && scale <= 4 ? gState->hdTargets[scale].texture : 0; }
bool mapRendererHdReadback(int scale, std::vector<float>& rgba, int* w, int* h)
{
    if (!mapRendererHdView(scale)) return false;
    GlState saved;
    auto& t = gState->hdTargets[scale];
    glBindFramebuffer(GL_READ_FRAMEBUFFER, gState->readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture, 0);
    rgba.resize(size_t(t.width) * t.height * 4);
    glReadPixels(0, 0, t.width, t.height, GL_RGBA, GL_FLOAT, rgba.data());
    if (w) *w = t.width;
    if (h) *h = t.height;
    return glGetError() == GL_NO_ERROR;
}
void mapRendererWantShapes(bool wanted)
{
    if (gState) gState->shapesWanted = wanted;
}
GLuint mapRendererHdUav(int scale)
{
    if (!gState || scale < 1 || scale > 4) return 0;
    GlState saved;
    return ensure(gState->hdTargets[scale], scale, GL_RGBA16F) ? gState->hdTargets[scale].texture : 0;
}
// G-buffer implementation follows the same draw list and is defined below.
bool mapRendererDrawGbuffer(int scale, const float* palette, const MapGbufferDraw& draw)
{
    if (!gState || !gState->built || scale < 2 || scale > 4 || scale != gState->hdTier || !palette) return false;
    GlState saved;
    GLint maxImages;
    glGetIntegerv(GL_MAX_COMPUTE_IMAGE_UNIFORMS, &maxImages);
    if (maxImages < 7) return false;
    const GLenum defaultFormats[] = { GL_RGBA16F, GL_RGBA8, GL_RGBA8, GL_RGBA16F, GL_RGBA8, GL_RGBA8 };
    GLenum formats[6];
    for (int i = 0; i < 6; i++) {
        if (!draw.targets[i]) return false;
        glBindTexture(GL_TEXTURE_2D, draw.targets[i]);
        GLint f = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &f);
        formats[i] = f;
        if (formats[i] != defaultFormats[i] && !(i == 5 && formats[i] == GL_R8)) return false;
    }
    GLuint& program = gState->gbuffer;
    bool redCoverage = formats[5] == GL_R8;
    GLuint targets[6];
    std::copy(draw.targets, draw.targets + 6, targets);
    if (redCoverage) {
        // R8 is color-renderable in ES, but has no core image-store
        // qualifier. Accumulate coverage in RGBA8, then blit its red channel.
        if (!ensure(gState->coverage, scale, GL_RGBA8)) return false;
        targets[5] = gState->coverage.texture;
        formats[5] = GL_RGBA8;
    }
    if (!program) {
        std::string code = kShaderSource;
        code.resize(code.find("\nvoid main()"));
        code +=
#include "hd/map_gbuffer.inc"
            ;
        program = glCompute(code);
        if (!program) return false;
    }
    if (!uploadBasePalette(palette)) return false;
    bindInputs();
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, gState->shapeAtlas);
    glBindSampler(5, 0);
    for (int i = 0; i < 6; i++) {
        glActiveTexture(GL_TEXTURE6 + i);
        glBindTexture(GL_TEXTURE_2D, targets[i]);
        glBindSampler(6 + i, 0);
        glBindImageTexture(i, targets[i], 0, GL_FALSE, 0, GL_WRITE_ONLY, formats[i]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gState->readFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets[i], 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        const float zero[4] = {};
        glClearBufferfv(GL_COLOR, 0, zero);
    }
    glUniform4fv(glGetUniformLocation(program, "gCategory"), kLightCategoryCount, &draw.category[0][0]);
    glUniform1f(glGetUniformLocation(program, "gDetailScale"), draw.detailScale);
    glUniform2iv(glGetUniformLocation(program, "gOccluderOrigin"), 1, draw.occluderOrigin);
    glUniform2iv(glGetUniformLocation(program, "gOccluderSize"), 1, draw.occluderSize);
    glUniform2i(glGetUniformLocation(program, "gTargetSize"), gState->mapWidth * scale, gState->mapHeight * scale);
    glUniform1i(glGetUniformLocation(program, "gScale"), scale);
    if (draw.occluders) {
        if (draw.occluderSize[0] <= 0 || draw.occluderSize[1] <= 0) return false;
        if (!gState->occluderBuffer) glGenBuffers(1, &gState->occluderBuffer);
        std::vector<GLuint> zero(size_t(draw.occluderSize[0]) * draw.occluderSize[1]);
        glBuffer(gState->occluderBuffer, zero.data(), zero.size() * sizeof(GLuint));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, gState->occluderBuffer);
        glBindImageTexture(6, draw.occluders, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32UI);
    }
    glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    for (const Command& c : gState->commands)
        for (int j = 0; j < c.count; j++) {
            if (c.kind == CommandKind::Outline) continue;
            const Sprite& s = gState->spriteList[c.first + j];
            int w = (s.rect[2] - s.rect[0] + 1) * scale, h = (s.rect[3] - s.rect[1] + 1) * scale;
            if (w <= 0 || h <= 0) continue;
            glUniform1i(glGetUniformLocation(program, "gSpriteBase"), c.first + j);
            glUniform1i(glGetUniformLocation(program, "gMode"), 0);
            glDispatchCompute((w + 7) / 8, (h + 7) / 8, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            if (draw.occluders) {
                glUniform1i(glGetUniformLocation(program, "gMode"), 4);
                glDispatchCompute(8, 1, 1);
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            }
        }
    if (draw.occluders) {
        glUniform1i(glGetUniformLocation(program, "gMode"), 5);
        glDispatchCompute((draw.occluderSize[0] + 7) / 8, (draw.occluderSize[1] + 7) / 8, 1);
    }
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    if (redCoverage) {
        if (!gState->copyFbo) glGenFramebuffers(1, &gState->copyFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gState->readFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, targets[5], 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gState->copyFbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, draw.targets[5], 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
        int w = gState->mapWidth * scale, h = gState->mapHeight * scale;
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
    }
    return glGetError() == GL_NO_ERROR;
}
} // namespace fallout::hd
#endif
