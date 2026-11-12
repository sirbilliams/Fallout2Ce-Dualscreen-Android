#ifndef FALLOUT_HD_MAP_POST_H_
#define FALLOUT_HD_MAP_POST_H_

#ifdef FALLOUT_HD

#include <vector>

#include "hd/gl.h"

namespace fallout::hd {

// Post effects on the HD map (PLAN.md P6.3), applied after the light pass
// (or the P4 pass with the original light) in base palette space; the
// compositor then applies the fade and CE's gamma as before. The map only:
// the interface, dialogue and menus are never touched.
//
//   bloom      the light pass's bloom source (palette-cycle emissives, flames
//              and neon near lights) blurred over a chain of half-size levels
//              and added "screen"-style. Needs the new light.
//   grade      white balance, lift/gamma/gain, saturation, optional .cube LUT
//   vignette   darkens toward the map window's corners (elliptical)
//   grain      film grain, drawn by the compositor per frame (it moves)
//
//   [hd_post]
//   enabled=1                 everything below (Ctrl+F5 cycles: all on -> no
//                             bloom -> no grade -> no vignette -> no grain ->
//                             all off -> all on)
//   bloom=1 grade=1 vignette=1 grain=1
//   bloom_strength=1.5        how much of the blurred glow is added
//   bloom_spread=0.8          weight of each wider level (larger = wider halo)
//   grade_temperature=0       white balance, -1 (cool) .. 1 (warm)
//   grade_lift=0,0,0 grade_gamma=1,1,1 grade_gain=1,1,1
//   grade_saturation=1
//   grade_lut=                a .cube 3D LUT file, applied last (optional)
//   vignette_strength=0.2     darkening at the corners
//   grain_strength=0.025      grain amplitude in display space

bool mapPostInit();
void mapPostShutdown();

// Post effects switched on (any of them).
bool mapPostActive();

// Applies bloom, grade and vignette to `hd` (the HD map, RGBA16F, `width` x
// `height` texels). `bloom` is the light pass's bloom source of the same
// size, or null (no bloom). Returns the result's view; `hd` itself if
// nothing is on or something failed.
GLuint mapPostDraw(GLuint hd, GLuint bloom, int width, int height);

// The last mapPostDraw result, as floats (RGBA per texel), for the
// self-test's brightness drift. False if post is off.
bool mapPostReadback(std::vector<float>& rgba, int* width, int* height);

// Grain amplitude for the compositor (0 = off).
float mapPostGrain();

// Ctrl+F5. Returns a name for the new state.
const char* mapPostCycle();

// Lines for the developer overlay.
void mapPostDrawOverlay();

} // namespace fallout::hd

#endif // FALLOUT_HD

#endif /* FALLOUT_HD_MAP_POST_H_ */
