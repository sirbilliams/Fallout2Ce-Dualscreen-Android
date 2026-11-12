#ifndef FALLOUT_HD_DRAW_LIST_H_
#define FALLOUT_HD_DRAW_LIST_H_

#include <vector>

#include "geometry.h"

namespace fallout {
struct Object;
} // namespace fallout

namespace fallout::hd {

// The map draw list (private to src/hd). draw_recorder.cc fills it from the
// draw-site hooks; captures serialize it (P1.2) and the GPU map renderer
// draws it (P3). One record per decision the CPU renderer made, in draw order.

enum class DrawOp {
    Pass,
    Fill,
    Floor,
    Roof,
    Object,
    Outline,
    Text,
};

// Which blit _obj_render_object ends in.
enum class ObjectBlend {
    Normal,
    Interface,
    TransRed,
    TransWall,
    TransGlass,
    TransSteam,
    TransEnergy,
};

struct DrawRecord {
    DrawOp op;
    int pass = 0; // DrawPass, for DrawOp::Pass
    int fid = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    Rect clip = {};
    int color = 0;
    int light = 0;
    int tile = -1;
    int vertexIntensities[10] = {};

    // Objects and outlines.
    Object* object = nullptr;
    int objectId = 0;
    int pid = 0;
    int frame = 0;
    int rotation = 0;
    unsigned int flags = 0;
    unsigned int extendedFlags = 0;
    int elevation = 0;
    int offsetX = 0;
    int offsetY = 0;
    int outline = 0;
    ObjectBlend blend = ObjectBlend::Normal;

    // Egg cut-out: objects that took the egg branch, and every roof.
    bool egg = false;
    int eggFid = 0;
    Rect eggRect = {};

    // Text pixels: index into the text buffer list.
    int textIndex = -1;
};

const std::vector<DrawRecord>& drawRecords();
const std::vector<unsigned char>& drawTextPixels(int textIndex);

// True when the last recording ran with gDrawEmitOnly (no CPU blits) and
// reached the floor pass, i.e. the map was actually traversed.
bool drawListComplete();

// Set by every hook that fires outside an emission: CE drew into the map
// window since the flag was last cleared.
bool drawMapDirty();
void drawClearMapDirty();

} // namespace fallout::hd

#endif /* FALLOUT_HD_DRAW_LIST_H_ */
