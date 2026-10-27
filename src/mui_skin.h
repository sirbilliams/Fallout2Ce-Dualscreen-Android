#ifndef FALLOUT_MUI_SKIN_H_
#define FALLOUT_MUI_SKIN_H_

#include "mui.h"

namespace fallout {

// The classic Fallout 2 look of the mobile UI: worn metal plates with bevels
// and rivets, recessed monitor wells with green readouts, amber highlights.
// Drawn from code (a metal texture made once per renderer), so it needs no
// art of its own and scales to any screen.

// A full screen's background: the metal plate, darker towards the edges.
void muiSkinBackdrop(MuiContext& ui, const MuiRect& rect);

// A raised metal plate over [rect]: texture, bevel, outline, and with
// [rivets] a rivet in each corner.
void muiSkinPlate(MuiContext& ui, const MuiRect& rect, bool rivets = true);

// A recessed monitor well over [rect] (lists, maps, readouts): dark glass,
// the shadow of its rim on the top and left, a lit lip on the bottom and
// right.
void muiSkinWell(MuiContext& ui, const MuiRect& rect);

// A metal button over [rect]: raised, pressed in while [pressed]; [primary]
// - the game's red button plate.
void muiSkinButton(MuiContext& ui, const MuiRect& rect, bool pressed, bool primary);

// The bevel of [rect]: [light] along the top and left, [dark] along the
// bottom and right ([inset] - the other way round), [thickness] pixels.
void muiSkinBevel(const MuiRect& rect, float thickness, MuiColor light, MuiColor dark, bool inset);

// A rivet centered at ([x], [y]).
void muiSkinRivet(MuiContext& ui, float x, float y);

} // namespace fallout

#endif /* FALLOUT_MUI_SKIN_H_ */
