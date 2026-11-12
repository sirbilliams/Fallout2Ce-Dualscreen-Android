#ifndef FALLOUT_HD_FIRST_RUN_H_
#define FALLOUT_HD_FIRST_RUN_H_

#include "config.h"

namespace fallout::hd {

// First start of a release install (PLAN.md R3).
//
// With [hd] auto_screen=1 (the default) the game's screen settings are set
// once, before the window opens: borderless fullscreen at the desktop
// resolution of the primary display, and the scale of the largest installed
// HD art tier that still gives the game its 640x480 minimum (e.g. 3x on
// 2560x1440 or 3440x1440, 2x on 1920x1080, 4x on 3840x2160). Then the key is
// set to 0, so later changes to [screen] stick. Installed tiers are found by
// the marker file each art pack ships (<hd_art_dir>/pack_<N>x.json) or, for
// packs built locally, by the interface bar's art at that tier.
//
// Runs on the loaded fallout2.cfg, after CE's own f2_res.ini migration and
// before command-line overrides (so a test's "[screen]..." still wins for its
// run). Returns true if it changed `config`; the caller writes the file.
bool firstRunScreenSetup(Config* config);

// What firstRunScreenSetup decided, for the debug log once it is open
// (empty if it didn't run).
const char* firstRunReport();

// Which HD art tiers (2..4) are installed, as a bit mask (bit N = tier N).
int installedArtTiers(Config* config);

} // namespace fallout::hd

#endif /* FALLOUT_HD_FIRST_RUN_H_ */
