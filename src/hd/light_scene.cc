#include "hd/light_scene.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <unordered_map>

#include "art.h"
#include "debug.h"
#include "light.h"
#include "map.h"
#include "obj_types.h"
#include "object.h"
#include "scripts.h"
#include "tile.h"

namespace fallout::hd {

namespace {

    constexpr int kMaxRadius = 8; // light.cc's limit
    constexpr int kHexGridWidth = 200;

    // Default mount heights (screen px) and fallback colors, as in the
    // Python compositor.
    constexpr float kLampHeight = 42.0f;
    constexpr float kFireHeight = 22.0f;
    constexpr float kCritterHeight = 30.0f;
    constexpr float kMaxLampHeight = 160.0f;
    constexpr float kLampColor[3] = { 1.0f, 0.94f, 0.84f };
    constexpr float kFireColor[3] = { 1.0f, 0.86f, 0.68f };

    // Light color: bright, saturated art pixels within this distance (screen
    // px) of the light's column, from its hex up to a little above its mount
    // height, weighted by brightness, mixed half and half with the fallback.
    // Brightness is the largest channel, not luma: neon reds and blues have
    // luma ~0.3 and would never count.
    constexpr float kColorRadius = 40.0f;
    constexpr float kColorAbove = 16.0f;
    constexpr float kColorValue = 0.55f;
    constexpr float kColorSaturation = 0.25f;
    constexpr int kColorMinPixels = 6;

    struct CachedLight {
        int fid;
        int tile;
        int elevation;
        float z;
        LightKind kind;
        float color[3];
        int colorPixels; // art pixels the color came from (0 = fallback)
    };

    LightScene gScene;
    // CE's tile-list accessor is private in this fork. Snapshot the public
    // elevation iterator once: nested art/light visits must not disturb it.
    std::map<int, std::vector<Object*>> gObjectsByTile;
    // By object: map objects can share an id (Klamath's toxic caves have
    // dozens of lights in groups with one id), which made them evict each
    // other and get described again on every build.
    std::unordered_map<const Object*, CachedLight> gCache;

    bool isFireArt(int fid)
    {
        FrmId frmId(fid);
        const char* path = frmId.filePath();
        if (path == nullptr) {
            return false;
        }
        char lower[64];
        size_t n = std::min(strlen(path), sizeof(lower) - 1);
        for (size_t i = 0; i < n; i++) {
            lower[i] = static_cast<char>(tolower(static_cast<unsigned char>(path[i])));
        }
        lower[n] = '\0';
        if (strstr(lower, "fire") != nullptr || strstr(lower, "torch") != nullptr || strstr(lower, "firpit") != nullptr) {
            return true;
        }
        // Art drawn with the fire cycle colors (238-247): burning barrels.
        // Not "barrel" in the name: Klamath's toxic caves are full of lit
        // radioactive barrels, which made everything bright around them a
        // "flame" (P6).
        CacheEntry* handle;
        Art* art = artLock(frmId, &handle);
        if (art == nullptr) {
            return false;
        }
        int width = 0;
        int height = 0;
        artGetSize(art, 0, ROTATION_NE, &width, &height);
        unsigned char* data = artGetFrameData(art, 0, ROTATION_NE);
        int flames = 0;
        for (int i = 0; data != nullptr && i < width * height; i++) {
            flames += data[i] >= 238 && data[i] <= 247 ? 1 : 0;
        }
        artUnlock(handle);
        return flames >= 4;
    }

    // Where CE draws the object's current frame (objectGetRect without the
    // outline margin). False if it has no art or no hex.
    bool spriteRect(Object* object, Art* art, int* left, int* top, int* width, int* height)
    {
        int sx;
        int sy;
        if (object->tile == -1 || tileToScreenXY(object->tile, &sx, &sy) != 0) {
            return false;
        }
        artGetSize(art, object->frame, object->rotation, width, height);
        sx += 16 + art->xOffsets[object->rotation] + object->x;
        sy += 8 + art->yOffsets[object->rotation] + object->y;
        *left = sx - *width / 2;
        *top = sy - *height + 1;
        return true;
    }

    template <typename Visit>
    void forEachObjectNear(int tile, int elevation, int range, Visit visit)
    {
        int row = tile / kHexGridWidth;
        int column = tile % kHexGridWidth;
        for (int r = row - range; r <= row + range; r++) {
            for (int c = column - range; c <= column + range; c++) {
                if (r < 0 || r >= kHexGridWidth || c < 0 || c >= kHexGridWidth) {
                    continue;
                }
                auto found = gObjectsByTile.find(r * kHexGridWidth + c);
                if (found == gObjectsByTile.end()) {
                    continue;
                }
                for (Object* object : found->second) {
                    if (object->elevation == elevation) {
                        visit(object, r * kHexGridWidth + c == tile);
                    }
                }
            }
        }
    }

    // Kind, mount height and color of a light, from the art on and around
    // its hex. World-anchored (not read from the screen), so it doesn't
    // change while the view scrolls.
    void describe(Object* light, float lightX, float lightY, const float* palette, CachedLight* out)
    {
        out->fid = light->fid;
        out->tile = light->tile;
        out->elevation = light->elevation;
        out->colorPixels = 0;
        if (FrmId(light->fid).objectType() == OBJ_TYPE_CRITTER) {
            out->kind = LightKind::Critter;
            out->z = kCritterHeight;
            out->color[0] = out->color[1] = out->color[2] = 1.0f;
            return;
        }

        bool fire = isFireArt(light->fid);
        float tallest = 0.0f;
        forEachObjectNear(light->tile, light->elevation, 1, [&](Object* object, bool sameHex) {
            if (object == light) {
                return;
            }
            ObjectType type = FrmId(object->fid).objectType();
            if (sameHex && (type == OBJ_TYPE_SCENERY || type == OBJ_TYPE_MISC) && isFireArt(object->fid)) {
                fire = true;
            }
            if (type == OBJ_TYPE_SCENERY || type == OBJ_TYPE_WALL) {
                CacheEntry* handle;
                Art* art = artLock(FrmId(object->fid), &handle);
                if (art != nullptr) {
                    tallest = std::max(tallest, static_cast<float>(artGetHeight(art, object->frame, object->rotation)));
                    artUnlock(handle);
                }
            }
        });
        out->kind = fire ? LightKind::Fire : LightKind::Lamp;
        out->z = fire ? kFireHeight : std::min(std::max(0.85f * tallest, kLampHeight), kMaxLampHeight);

        // Color around the light's column (neon signs hang on the wall above
        // the hex, lamp heads sit at the top, fires at the bottom).
        float px = lightX;
        float top = lightY - out->z - kColorAbove;
        float bottom = lightY;
        double sum[3] = {};
        double weight = 0.0;
        int count = 0;
        forEachObjectNear(light->tile, light->elevation, 3, [&](Object* object, bool) {
            ObjectType type = FrmId(object->fid).objectType();
            if (type == OBJ_TYPE_CRITTER || type == OBJ_TYPE_INTERFACE || (object->flags & OBJECT_HIDDEN) != 0) {
                return;
            }
            CacheEntry* handle;
            Art* art = artLock(FrmId(object->fid), &handle);
            if (art == nullptr) {
                return;
            }
            int left;
            int spriteTop;
            int width;
            int height;
            unsigned char* data = artGetFrameData(art, object->frame, object->rotation);
            if (data != nullptr && spriteRect(object, art, &left, &spriteTop, &width, &height)) {
                int x0 = std::max(0, static_cast<int>(floorf(px - kColorRadius)) - left);
                int x1 = std::min(width - 1, static_cast<int>(ceilf(px + kColorRadius)) - left);
                int y0 = std::max(0, static_cast<int>(floorf(top - kColorRadius)) - spriteTop);
                int y1 = std::min(height - 1, static_cast<int>(ceilf(bottom + kColorRadius)) - spriteTop);
                for (int y = y0; y <= y1; y++) {
                    for (int x = x0; x <= x1; x++) {
                        unsigned char index = data[y * width + x];
                        float sy = static_cast<float>(spriteTop + y);
                        float dx = left + x - px;
                        float dy = sy - std::min(std::max(sy, top), bottom);
                        if (index == 0 || dx * dx + dy * dy > kColorRadius * kColorRadius) {
                            continue;
                        }
                        const float* c = palette + index * 4;
                        float value = std::max({ c[0], c[1], c[2] });
                        float saturation = value - std::min({ c[0], c[1], c[2] });
                        if (value > kColorValue && saturation > kColorSaturation) {
                            for (int k = 0; k < 3; k++) {
                                sum[k] += c[k] * value;
                            }
                            weight += value;
                            count++;
                        }
                    }
                }
            }
            artUnlock(handle);
        });

        const float* fallback = fire ? kFireColor : kLampColor;
        if (count < kColorMinPixels || weight <= 0.0) {
            memcpy(out->color, fallback, sizeof(out->color));
            return;
        }
        float color[3];
        float peak = 1e-3f;
        for (int k = 0; k < 3; k++) {
            color[k] = static_cast<float>(sum[k] / weight);
            peak = std::max(peak, color[k]);
        }
        for (int k = 0; k < 3; k++) {
            out->color[k] = 0.5f * color[k] / peak + 0.5f * fallback[k];
        }
        out->colorPixels = count;
    }

    void gatherHexes(int width, int height, int margin, int marginBelow)
    {
        gScene.hexes.clear();
        int refX;
        int refY;
        if (tileToScreenXY(gCenterTile, &refX, &refY) != 0) {
            return;
        }
        // tileToScreenXY moves about (24, -6) px per column (flipped, as it
        // counts them) and (16, 12) per row; invert that for the range of
        // hexes the extended view can contain.
        int refColumn = kHexGridWidth - 1 - gCenterTile % kHexGridWidth;
        int refRow = gCenterTile / kHexGridWidth;
        float minA = 1e9f;
        float maxA = -1e9f;
        float minB = 1e9f;
        float maxB = -1e9f;
        const float xs[2] = { static_cast<float>(-margin), static_cast<float>(width + margin) };
        const float ys[2] = { static_cast<float>(-margin), static_cast<float>(height + marginBelow) };
        for (float x : xs) {
            for (float y : ys) {
                float dx = x - refX;
                float dy = y - refY;
                float a = (12.0f * dx - 16.0f * dy) / 384.0f;
                float b = (6.0f * dx + 24.0f * dy) / 384.0f;
                minA = std::min(minA, a);
                maxA = std::max(maxA, a);
                minB = std::min(minB, b);
                maxB = std::max(maxB, b);
            }
        }
        int c0 = std::max(0, refColumn + static_cast<int>(floorf(minA)) - 3);
        int c1 = std::min(kHexGridWidth - 1, refColumn + static_cast<int>(ceilf(maxA)) + 3);
        int r0 = std::max(0, refRow + static_cast<int>(floorf(minB)) - 3);
        int r1 = std::min(kHexGridWidth - 1, refRow + static_cast<int>(ceilf(maxB)) + 3);
        for (int r = r0; r <= r1; r++) {
            for (int c = c0; c <= c1; c++) {
                int tile = r * kHexGridWidth + (kHexGridWidth - 1 - c);
                int sx;
                int sy;
                if (tileToScreenXY(tile, &sx, &sy) != 0) {
                    continue;
                }
                float x = sx + 16.0f;
                float y = sy + 8.0f;
                if (x < -margin || x > width + margin || y < -margin || y > height + marginBelow) {
                    continue;
                }
                gScene.hexes.push_back({ x, y, lightGetTrueTileIntensity(gElevation, tile) / 65536.0f });
            }
        }
    }

} // namespace

const LightScene& lightSceneGather(int width, int height, int margin, int marginBelow, const float* palette)
{
    gObjectsByTile.clear();
    for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
        if (hexGridTileIsValid(object->tile)) {
            gObjectsByTile[object->tile].push_back(object);
        }
    }
    gScene.ambient = lightGetAmbientIntensity() / 65536.0f;
    gScene.timeOfDay = gameTimeGetHour();
    gScene.elevation = gElevation;
    if (tileToScreenXY(0, &gScene.worldX, &gScene.worldY) != 0) {
        gScene.worldX = 0;
        gScene.worldY = 0;
    }
    gatherHexes(width, height, margin, marginBelow);

    gScene.lights.clear();
    float reach = (kMaxRadius + 1) * 32.0f;
    for (const auto& entry : gObjectsByTile) {
        for (Object* object : entry.second) {
            if (object->elevation != gElevation || object->lightDistance <= 0 || object->lightIntensity <= 0) {
                continue;
            }
            int sx;
            int sy;
            if (tileToScreenXY(object->tile, &sx, &sy) != 0) {
                continue;
            }
            float x = sx + 16.0f;
            float y = sy + 8.0f;
            if (x < -reach || x > width + reach || y < -reach || y > height + marginBelow + reach) {
                continue;
            }

            auto it = gCache.find(object);
            if (it == gCache.end() || it->second.fid != object->fid || it->second.tile != object->tile || it->second.elevation != object->elevation) {
                CachedLight fresh;
                describe(object, x, y, palette, &fresh);
                it = gCache.insert_or_assign(object, fresh).first;
                const char* kinds[] = { "lamp", "fire", "critter" };
                debugPrint("HD: light %d at tile %d: %s, z %.0f, color %.2f %.2f %.2f (%d px), radius %d, intensity %.2f\n",
                    object->id, object->tile, kinds[static_cast<int>(fresh.kind)], fresh.z,
                    fresh.color[0], fresh.color[1], fresh.color[2], fresh.colorPixels,
                    object->lightDistance, object->lightIntensity / 65536.0f);
            }
            const CachedLight& cached = it->second;
            SceneLight light;
            light.x = x;
            light.y = y;
            light.z = cached.z;
            light.radius = static_cast<float>(std::min(object->lightDistance, kMaxRadius));
            light.intensity = std::min(object->lightIntensity, 65536) / 65536.0f;
            memcpy(light.color, cached.color, sizeof(light.color));
            light.kind = cached.kind;
            light.objectId = object->id;
            gScene.lights.push_back(light);
        }
    }
    return gScene;
}

void lightSceneReset()
{
    gCache.clear();
    gObjectsByTile.clear();
}

} // namespace fallout::hd
