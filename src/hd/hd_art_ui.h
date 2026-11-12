#ifndef FALLOUT_HD_HD_ART_UI_H_
#define FALLOUT_HD_HD_ART_UI_H_

#include "hd_art_cpu.h"

namespace fallout::hd {

// HD art for the SDL based mui_* screens (CPU decoded RGBA, no GL).
//
//   [hd] enabled=1       HD on (default 0)
//   [hd] tier=2          2, 3 or 4 (default 2)
//   [hd] hd_art_dir=hd   pack root, relative to the game folder (default "hd")
//
// Reads fallout2.cfg (gGameConfig) once, on first use.
bool uiArtActive();

// The HD frame for the art CE has in hand, or false (draw the original).
bool uiArtLookup(const char* frmPath, int rotation, int frame, const unsigned char* indexed, int width, int height, HdRgba& out);

} // namespace fallout::hd

#endif /* FALLOUT_HD_HD_ART_UI_H_ */
