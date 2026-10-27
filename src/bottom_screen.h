#ifndef FALLOUT_BOTTOM_SCREEN_H_
#define FALLOUT_BOTTOM_SCREEN_H_

namespace fallout {

// The second screen of a dual-screen device (Android only, see
// BottomScreen.java): the mobile UI's panels are drawn there while the game
// keeps the main window.

// True while the second screen's surface exists.
bool bottomScreenAvailable();

// The surface's size in physical pixels (0 x 0 without it).
void bottomScreenGetSize(int* width, int* height);

} // namespace fallout

#endif /* FALLOUT_BOTTOM_SCREEN_H_ */
