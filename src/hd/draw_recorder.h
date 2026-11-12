#ifndef FALLOUT_HD_DRAW_RECORDER_H_
#define FALLOUT_HD_DRAW_RECORDER_H_

#include <set>

namespace fallout {
struct Object;
struct Rect;
} // namespace fallout

namespace fallout::hd {

class JsonWriter;

// Draw-list recorder (PLAN.md P1.2).
//
// The map renderer calls these from its real draw sites (tile.cc, object.cc,
// text_object.cc, tile_hires_stencil.cc) with the decisions it has already
// made: position, clip rectangle, light, blend mode, egg. Recording happens
// only while a capture forces a full redraw; at any other time every hook is a
// single branch on gDrawRecorderActive and the renderer runs unchanged.
//
// The recorded list is complete enough to re-rasterize the 8-bit map window
// from the original FRMs plus the dumped tables (tools/pipeline replay test).
//
// Phase 3 uses the same hooks as the GPU draw-list emitter: an emission is a
// recording of a full-window traversal during which gDrawEmitOnly is set and
// every draw site skips its CPU blit (and tileRefreshGame skips its clear and
// the window refresh). The draw sites check the flag right after their hook.

extern bool gDrawRecorderActive;
extern bool gDrawEmitOnly;

// Sections of tileRefreshGame, in draw order.
enum class DrawPass {
    Floors,
    FlatObjects,
    EdgeSquaresUnder,
    Objects,
    EdgeSquaresOver,
    Roofs,
    Outlines,
    Text,
    Overlay,
    Stencil,
};

void drawRecorderBegin();
void drawRecorderEnd();

// Records a full-window traversal with gDrawEmitOnly set (P3 emitter). Does
// nothing visible: the map window buffer and the screen are untouched.
// Returns false if the map wasn't traversed (tiles disabled, no map).
bool drawEmitFullFrame();
void drawRecorderWrite(JsonWriter& json);

// Every fid the recorded draws reference (art, egg masks).
std::set<int> drawRecorderFids();

void recordPass(DrawPass pass);

// bufferFill of a window rectangle (map clear, hi-res stencil).
void recordFill(const Rect& rect, int color);

// tileRenderFloor, after the vertex intensities are known. `x`/`y` are the
// unclipped tile position, `clip` the refresh rectangle. `intensityMap` is
// tile.cc's persistent 80-wide interpolation buffer, snapshotted once per
// capture so the replay starts from the same state.
void recordFloor(int fid, int x, int y, const Rect& clip, int tile, const int* vertexIntensities, const int* intensityMap, int intensityMapSize);

// tileRenderRoof, once the tile is known to intersect `clip`. `eggRect` is
// the egg's screen rectangle; the egg cut-out applies wherever it intersects.
void recordRoof(int fid, int x, int y, const Rect& clip, int light, const Rect& eggRect);

// _obj_render_object, once the object is known to intersect `clip`. The
// object's sx/sy are its unclipped top-left corner.
void recordObject(Object* object, int fid, int frameWidth, int frameHeight, const Rect& clip, int light);

// _obj_render_object took the egg branch for the object recorded last.
void recordObjectEgg(int eggFid, const Rect& eggRect);

// objectDrawOutline, once the outline is known to intersect `clip`
// (already expanded by one pixel and clipped to the window).
void recordOutline(Object* object, int fid, int frameWidth, int frameHeight, const Rect& clip);

// textObjectsRenderInRect: a transparent blit of a pre-rendered text buffer.
void recordText(int x, int y, int width, int height, const unsigned char* data, const Rect& clip);

} // namespace fallout::hd

#endif /* FALLOUT_HD_DRAW_RECORDER_H_ */
