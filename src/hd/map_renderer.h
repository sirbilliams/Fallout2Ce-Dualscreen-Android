#ifndef FALLOUT_HD_MAP_RENDERER_H_
#define FALLOUT_HD_MAP_RENDERER_H_

#ifdef FALLOUT_HD

#include <vector>

#include "hd/gl.h"

namespace fallout::hd {

// GPU map renderer, exact mode (PLAN.md P3.2).
//
// Draws the current draw list (hd/draw_list.h) the way CE's CPU renderer
// does, in palette indices: same art, same tables (intensityColorTable,
// colorMixAddTable, the five translucency tables), same per-pixel rules. The
// target is R32_UINT at scale N; every decision is made per logical pixel, so
// the N× image is exactly the 1× image upscaled nearest, and the 1× image
// must equal the map window buffer after a full CPU refresh.
//
// Overwrite ops (fills, lit sprites, floors, interface objects, text) are
// rasterized in draw order; ops that read the destination (translucency, the
// egg cut-out, outlines) run as compute passes on the same target.

// MapRenderStats::opCounts: fill, lit, floor, interface, text, trans, egg, outline.
constexpr int kMapOpCount = 8;
constexpr int kMapOpOutline = 7;

struct MapRenderStats {
    int records = 0;
    int sprites = 0;
    int rasterBatches = 0;
    int computeOps = 0;
    int outlines = 0;
    int opCounts[kMapOpCount] = {};
    int atlasUploads = 0;
    int atlasResets = 0;
    float atlasUsed = 0.0f; // fraction of the atlas rows in use
};

bool mapRendererInit();
void mapRendererShutdown();

// Turns the draw list into GPU commands for a map window of the given size
// (logical pixels) and uploads what they need (art, tables, text). With
// `hdTier` 2..4 and the HD art store active (hd/hd_art.h), the sprites also
// get their HD art for the HD pass at that scale.
bool mapRendererBuild(int width, int height, int hdTier = 0);

// Draws the built commands into the target for `scale` (1..4).
bool mapRendererDraw(int scale);

// The target for `scale` (R32_UINT palette indices), or null.
GLuint mapRendererView(int scale);

// Copies the target for `scale` back as one byte per pixel; `tags` (if
// given) gets 1 where an outline wrote the pixel.
bool mapRendererReadback(int scale, std::vector<unsigned char>& pixels, int* width, int* height, std::vector<unsigned char>* tags = nullptr);

const MapRenderStats& mapRendererStats();

// Video memory held by the renderer's textures (targets, atlas, tables).
long long mapRendererGpuBytes();

// HD color pass (PLAN.md P4.2). The tier the last build prepared HD art
// for (0 = none); whether the built frame shows palette-cycle colors (it
// must be redrawn when they change).
int mapRendererHdTier();
bool mapRendererHasCycle();

// The palette-cycle color of `index` (229..254) at phase 0, in base space:
// what the HD art was upscaled with (P6.1).
void mapRendererCyclePhase0(int index, float rgb[3]);

// Draws the built commands in color into the RGBA16F target for `scale`
// (must equal the build's HD tier). `palette` = 256 x RGBA floats, the
// colors in base space (color.pal before fades and gamma, live cycle
// colors). Outline pixels are left to the exact pass (tagged there with
// 0x100 above the index).
bool mapRendererDrawHd(int scale, const float* palette);
GLuint mapRendererHdView(int scale);
bool mapRendererHdReadback(int scale, std::vector<float>& rgba, int* width, int* height);

// New light (PLAN.md P5.2). Light categories of map pixels, set per sprite
// and tuned per category (hd/map_lighting.h); must match the shaders.
enum MapLightCategory {
    kLightNone, // fills, text, interface art: never lit
    kLightFloor,
    kLightRoof,
    kLightWall,
    kLightScenery,
    kLightCritter,
    kLightItem,
    kLightMisc,
    // Trees, cacti, bushes, rocks, barrels (by art name, [hd_light]
    // round_body > 0): a rounded body and more of the sun's shape.
    kLightRound,
    kLightCategoryCount,
};
constexpr int kLightFlat = 1; // sprite flag: a flat object, lying on the floor
// Sprite flag: within 2 hexes of a lamp or fire (not a critter's light). Its
// bright, saturated pixels (flames, neon) never get darker than the art (a
// fire bowl at noon must not dim) and feed the bloom (P6). Static art: the
// palette-cycle colors are handled separately (psGbuffer's Cycle).
constexpr int kLightGlow = 2;

// The new light needs each frame's shape (hd/sprite_shape.h), computed when
// frames enter the atlas. Off by default; turning it on fills in shapes for
// frames already there on their next use.
void mapRendererWantShapes(bool wanted);

struct MapGbufferDraw {
    // Albedo (RGBA16F), base normal (RGBA8), detail normal (RGBA8),
    // surface (RGBA16F), material (RGBA8), coverage (R8); see psGbuffer.
    GLuint targets[6] = {};
    // R32_UINT 1x ground grid (float heights as bits), cleared by the
    // caller; ground point (x, y) is texel (x, y) + origin.
    GLuint occluders = 0;
    int occluderOrigin[2] = {};
    int occluderSize[2] = {};
    // Per category: detail strength, relief weight, shape weight, unused.
    float category[kLightCategoryCount][4] = {};
    float detailScale = 1.0f;
    // Lower-resolution light: the targets are ceil(target / lightDiv) pixels
    // and each is shaded at the middle texel of its lightDiv x lightDiv block.
    int lightDiv = 1;
};

// Draws the built commands unlit into the G-buffer at `scale` (must equal
// the build's HD tier) and scatters the occluder heightfield.
bool mapRendererDrawGbuffer(int scale, const float* palette, const MapGbufferDraw& draw);

// Draws just the albedo (RGBA16F) and coverage (RGBA8) of the built commands
// unlit at `scale`, into textures of the HD target's size: the HD art the
// lower-resolution light is applied to (hd/map_lighting.h, resolution).
bool mapRendererDrawHdAlbedo(int scale, const float* palette, GLuint albedo, GLuint coverage);

// The HD target at `scale` for the lighting pass to write (created if
// needed; same texture as mapRendererHdView).
GLuint mapRendererHdUav(int scale);

} // namespace fallout::hd

#endif // FALLOUT_HD

#endif /* FALLOUT_HD_MAP_RENDERER_H_ */
