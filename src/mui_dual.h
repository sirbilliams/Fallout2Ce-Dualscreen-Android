#ifndef FALLOUT_MUI_DUAL_H_
#define FALLOUT_MUI_DUAL_H_

#include "mui.h"

namespace fallout {

// Dual-screen devices (AYN Thor): the look that ties the two screens
// together, drawn by mui.cc around each surface's screens.

// Bottom screen, before its screens: the Pip-Boy green CRT backdrop
// (gradient, scanlines, vignette).
void muiDualBackdrop(MuiContext& ui);

// Bottom screen with nothing on it (dialogue and the world map on top,
// menus, loading): the companion panel - the game's clock and date, where,
// who, HP; the title outside a game.
void muiDualCompanion(MuiContext& ui);

// Bottom screen, over its screens: a screen opening there fades in
// ([top] - its topmost active screen), and the edges pulse red while dude's
// HP is low.
void muiDualBottomOverlay(MuiContext& ui, MuiScreen* top);

// Top screen while the game runs a screen on the bottom one: the frozen
// world fades darker and a chip points down to it.
void muiDualTopDim(MuiContext& ui);

// Dude's hit points now, 0..1 (1 outside a game).
float muiDualHitPointsFraction();

// Dude's HP as a bar over [rect]: green, yellow at half, red when low, with
// "HP 34/52" on it.
void muiDualHitPointsBar(MuiContext& ui, const MuiRect& rect);

} // namespace fallout

#endif /* FALLOUT_MUI_DUAL_H_ */
