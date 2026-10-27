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

// The sun by day (6:00 to 18:00), the moon by night, on an arc over the
// horizon of [rect] where the game's clock [hour] (HHMM) has them.
void muiDualDayArc(MuiContext& ui, const MuiRect& rect, int hour);

// A Geiger counter's half dial at ([centerX], [baseY]) of [radius]: 0 to 1000
// rads in the game's radiation levels' colors, the needle at [rads],
// twitching while above 0.
void muiDualRadGauge(MuiContext& ui, float centerX, float baseY, float radius, float rads);

// One frame of the bottom screen's CRT power-on, [progress] 0..1.
void muiDualPowerOnFrame(MuiContext& ui, float progress);

// The bottom screen came on (the app started or came back, the screen or
// the setting was switched on): it switches on as a CRT.
void muiDualPowerOn();

// Dude's hit points now, 0..1 (1 outside a game).
float muiDualHitPointsFraction();

// Dude's HP as a bar over [rect]: green, yellow at half, red when low, with
// "HP 34/52" on it.
void muiDualHitPointsBar(MuiContext& ui, const MuiRect& rect);

} // namespace fallout

#endif /* FALLOUT_MUI_DUAL_H_ */
