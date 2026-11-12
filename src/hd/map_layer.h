#ifndef FALLOUT_HD_MAP_LAYER_H_
#define FALLOUT_HD_MAP_LAYER_H_

#ifdef FALLOUT_HD

#include "hd/gl.h"

#include <vector>

namespace fallout::hd {

// The GPU map layer (PLAN.md P3): keeps a GPU-rendered copy of the map
// window and tells the presenter where on screen it may replace CE's pixels.
//
//   [hd] map_gpu=1      draw the map on the GPU (needs renderer=d3d11)
//   [hd_debug] map_selftest_ms=N   run the map self-test every N ms
//
// The draw list is re-emitted at present time whenever CE drew into the map
// since the last emission. A screen pixel shows the GPU map only where
// (a) no window above the map and no opaque cursor pixel covers it, and
// (b) the 1x GPU map has the same palette index CE put on the screen there.
// (b) lets everything that bypasses the draw list (the grey options
// background, the black fill on map load, a scroll caught half way) fall
// back to CE's own pixels, and is the live exactness check.

struct MapComposite {
    bool active = false;
    int isoX = 0; // map window, logical screen pixels
    int isoY = 0;
    int isoWidth = 0;
    int isoHeight = 0;
    int scale = 1; // pixels of `scaled` per logical pixel
    GLuint mask = 0; // R8_UINT, map window sized
    GLuint exact = 0; // R32_UINT 1x map
    GLuint scaled = 0; // R32_UINT map at `scale` (exact mode)
    // HD mode (P4.2): RGBA16F map at `scale` in base palette space, shown
    // through the live palette's fade (gain and offset, P6.4) and CE's gamma
    // table (6-bit in, 6-bit out). Outline pixels (tagged in `exact`) and pixels where the
    // map disagrees with CE still come from the palette.
    GLuint hd = 0;
    float gain[3] = { 1.0f, 1.0f, 1.0f };
    float offset[3] = { 0.0f, 0.0f, 0.0f }; // fades toward white (P6.4), base units
    float gamma[64] = {};
    // Film grain on HD map pixels (P6.3, hd/map_post.h): amplitude in
    // display space, 0 = off. The compositor animates it.
    float grain = 0.0f;
    bool tint = false; // debug view: GPU map pixels tinted cyan
};

bool mapLayerRequested();
bool mapLayerInit();
void mapLayerShutdown();

// Starts [hd_debug] map_selftest_ms (from hd::init, after inputInit).
void mapLayerStartSelfTests();

// Called by the presenter before it draws a frame. `scale` is the number of
// window pixels per logical pixel (1..4). `selfTest`: a 1:1 composite for a
// self-test, not what the window shows.
MapComposite mapLayerPrepare(int scale, bool selfTest = false);

// Ctrl+F8: HD art on/off (off = the exact palette renderer). Returns a
// name for the new state.
const char* mapLayerToggleHd();

// Ctrl+F9 cycles the GPU map: on -> tinted (every pixel the GPU map
// supplies is tinted cyan, to see what it owns) -> off -> on. Returns a
// name for the new state.
const char* mapLayerCycleMode();

// The P3.4 self-test. Logs how many map pixels currently fall back to CE's,
// then refreshes the map on the CPU (tileWindowRefresh) and checks
// (a) 1x GPU map == the map window buffer, (b) map at 2x/3x/4x == 1x
// upscaled nearest, (c) the composited frame at 1:1 == the SDL path,
// (d) the mask == the pixels the window manager fills from the map window.
// Call from the game loop (it redraws the map). Returns the number of
// failures, or -1 if the map layer isn't active.
int mapLayerSelfTest(const char* label);

// P7b.4, the dialogue's map view: CE has just drawn the map window (centered
// on the speaker) and copies the rect (x, y, width, height; map window
// pixels) out of it. Renders the map now, as the compositor would show it
// (HD art, light, post effects) at the window's scale, and returns that
// rect's texels as RGBA8 (base palette space, before fades and gamma), plus
// per 1x pixel whether the GPU map's 1x pass gives CE's pixel there (and no
// outline is drawn on it): only those may show HD. False if the GPU map or
// HD art is off or the map can't be drawn now.
bool mapLayerSnapshot(int x, int y, int width, int height, std::vector<unsigned char>& rgba, std::vector<unsigned char>& owned, int* scale);

// Lines for the developer overlay (inside its ImGui window).
void mapLayerDrawOverlay();

} // namespace fallout::hd

#endif // FALLOUT_HD

#endif /* FALLOUT_HD_MAP_LAYER_H_ */
