#ifndef FALLOUT_HD_PRESENT_STATS_H_
#define FALLOUT_HD_PRESENT_STATS_H_

#include <SDL.h>

namespace fallout::hd {

// CPU time spent getting frames to the screen, for comparing the SDL and
// D3D11 presentation paths (PLAN.md Phase 2: "frame time <= baseline").
//
// svga.cc wraps the work each path does in a PresentTimer: the SDL path's
// 8-bit -> RGB blits (per dirty rect and per palette change) and its texture
// upload + present; the D3D11 path's upload, draw and present. Every 600
// frames the totals go to debug.log.

class PresentTimer {
public:
    PresentTimer()
        : _start(SDL_GetPerformanceCounter())
    {
    }
    ~PresentTimer();

private:
    Uint64 _start;
};

// Counts one presented frame (call once per renderPresent).
void presentStatsFrame();

} // namespace fallout::hd

#endif /* FALLOUT_HD_PRESENT_STATS_H_ */
