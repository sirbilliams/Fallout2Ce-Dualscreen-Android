#ifndef FALLOUT_BOTTOM_SCREEN_H_
#define FALLOUT_BOTTOM_SCREEN_H_

namespace fallout {

// The second screen of a dual-screen device (Android only, see
// BottomScreen.java): the mobile UI's bottom surface (mui.h `MuiSurface`) is
// drawn there while the game keeps the main window.

// True while the second screen's surface exists.
bool bottomScreenAvailable();

// The surface's size in physical pixels (0 x 0 without it).
void bottomScreenGetSize(int* width, int* height);

// The bottom display's dp scale (densityDpi / 160), as
// `hudGetScreenDensity` for the top.
float bottomScreenGetDensity();

// Shows a frame: [width] x [height] pixels, 4 bytes each (R, G, B, A in
// memory), rows [pitch] bytes apart. Game thread. Nothing when the surface
// is gone or of another size (a frame of the new size comes next).
void bottomScreenPostFrame(const unsigned char* pixels, int width, int height, int pitch);

#ifndef __ANDROID__
// Desktop builds (bottom_screen_desktop.cc): no second screen unless a test
// makes one of [width] x [height] at [density]; 0 x 0 takes it away.
void bottomScreenSetTest(int width, int height, float density);

// The last frame posted to the test screen (R, G, B, A rows, its width * 4
// bytes apart), empty before any.
const unsigned char* bottomScreenTestFrame();
#endif

} // namespace fallout

#endif /* FALLOUT_BOTTOM_SCREEN_H_ */
