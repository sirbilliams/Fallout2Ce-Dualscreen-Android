#ifndef FALLOUT_MUI_PAD_H_
#define FALLOUT_MUI_PAD_H_

#include <string>
#include <vector>

#include <SDL.h>

#include "mui.h"

namespace fallout {

// Gamepad control of the mobile UI (QOL stage 3, docs/gamepad-stage3.md).
//
// Every touchable widget of the interactive screen of a surface is a focus
// target; the d-pad / left stick moves a ring between them by geometry, A taps
// the focused widget and X long presses it (both as a finger would, so the
// screens need no pad code), B goes back. The map has a cursor on the top
// screen instead (gamepad.cc).

enum class MuiPadDir {
    Left,
    Right,
    Up,
    Down,
};

struct MuiPadWidget {
    std::string id;
    MuiRect rect;
    // A slider: left / right change its value.
    bool slider = false;
};

struct MuiPadScroll {
    std::string id;
    MuiRect rect;
    bool horizontal = false;
};

// mui.cc: what the pad needs of the surfaces.
const std::vector<MuiPadWidget>& muiPadWidgets(MuiSurface surface);
const std::vector<MuiPadScroll>& muiPadScrollAreas(MuiSurface surface);
bool muiPadHasScreen(MuiSurface surface);
std::string& muiPadFocusId(MuiSurface surface);
const std::string& muiPadPreferredId(MuiSurface surface);
bool muiPadScrollBy(MuiSurface surface, const std::string& id, float delta);
void muiPadSlide(MuiSurface surface, int direction);
// Moves the content of a pan and zoom view by output pixels, zooms by a factor.
void muiPadPan(MuiSurface surface, float dx, float dy, float zoom);
bool muiPadPanZoomRect(MuiSurface surface, MuiRect* rect);
void muiPadOutputSize(MuiSurface surface, int* width, int* height);

// mui.cc calls: at the end of a surface's build (the focus ring, the map
// cursor), once a frame (finger releases of taps the pad made).
void muiPadFrame(MuiContext& ui, MuiSurface surface, bool hasScreen);
void muiPadTick();

// Where the pad acts now: a surface's screens (true), or the map with its
// cursor (false).
bool muiPadUiTarget(MuiSurface* surface);
// Switches between the screens of both surfaces, or from the map to the
// HUD's screen and back. False when there is nothing to switch to.
bool muiPadSwitchFocus();

// Focus moves; a screen with no focus yet takes the first widget.
void muiPadMove(MuiSurface surface, MuiPadDir direction);
// A tap / long press on the focused widget (or the center of a pan and zoom
// view without focus).
void muiPadActivate(MuiSurface surface, bool longPress);
// Right stick over screens: scrolls the focused list or pans a map view.
void muiPadStick(MuiSurface surface, float x, float y, float seconds);
void muiPadZoom(MuiSurface surface, float factor);

// A finger tap at a place of the top screen (0..1) for the map cursor.
void muiPadTapTop(float x, float y, bool longPress);
// The map cursor: drawn on the top screen while visible.
void muiPadSetCursor(bool visible, float x, float y);
// The cursor's place (0..1 of the top screen), shared by the right stick and
// the bottom screen's touchpad (mui_touchpad.cc). Moves it by (dx, dy) of the
// screen and shows it.
void muiPadCursorGet(float* x, float* y);
void muiPadCursorMove(float dx, float dy);
// A right click at the cursor: the map's action menu, or a long press over a
// screen.
void muiPadRightClickTop(float x, float y);
// The touchpad holds the left button at the cursor (a finger of the top
// screen stays down; moves of the cursor drag it), or lets go.
void muiPadTopHold(bool down);
// The touchpad page is open: the cursor shows whatever else is going on.
void muiPadSetTouchpadOpen(bool open);
// A finger of the player is at a place of the top screen (the cursor follows
// it, and stays a moment after it lifts).
void muiPadFingerCursor(bool down, float x, float y);
// Some pad input happened (the ring shows).
void muiPadTouch();

// Quick use: the player's usable items on a ring, the pad picks one.
void muiPadQuickUseRun();

} // namespace fallout

#endif /* FALLOUT_MUI_PAD_H_ */
