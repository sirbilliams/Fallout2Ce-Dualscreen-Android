#include "hd/present_stats.h"

#include "config.h"
#include "debug.h"
#include "game_config.h"
// D3D presenter selection is dropped: CE owns SDL/GLES presentation.

namespace fallout::hd {

namespace {

    // Frames per "present cost" line (~10 s at 60 fps); [hd_debug]
    // present_report_frames shortens it for the combat benchmark (P7a.6).
    int gFramesPerReport = 0;

    Uint64 gTicks = 0;
    Uint64 gWindowTicks = 0;
    int gFrames = 0;
    int gWindowFrames = 0;
    Uint32 gWindowStart = 0;
    // The longest gap between two frames in this window and when it ended
    // (ms since SDL init): one long hitch and many slow frames look the same
    // in an average (P7a.6, the combat-start dip).
    Uint64 gLastFrame = 0;
    Uint64 gLongest = 0;
    Uint32 gLongestAt = 0;

} // namespace

PresentTimer::~PresentTimer()
{
    Uint64 elapsed = SDL_GetPerformanceCounter() - _start;
    gTicks += elapsed;
    gWindowTicks += elapsed;
}

void presentStatsFrame()
{
    if (gFramesPerReport == 0) {
        configGetInt(&gGameConfig, "hd_debug", "present_report_frames", &gFramesPerReport, 600);
        gFramesPerReport = gFramesPerReport > 0 ? gFramesPerReport : 600;
    }
    if (gWindowFrames == 0) {
        gWindowStart = SDL_GetTicks();
        gLongest = 0;
    }
    Uint64 now = SDL_GetPerformanceCounter();
    if (gLastFrame != 0 && now - gLastFrame > gLongest) {
        gLongest = now - gLastFrame;
        gLongestAt = SDL_GetTicks();
    }
    gLastFrame = now;
    gFrames++;
    gWindowFrames++;
    if (gWindowFrames < gFramesPerReport) {
        return;
    }

    double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    double windowMs = gWindowTicks * 1000.0 / frequency / gWindowFrames;
    double totalMs = gTicks * 1000.0 / frequency / gFrames;
    double fps = gWindowFrames * 1000.0 / (SDL_GetTicks() - gWindowStart + 1);
    debugPrint("HD: present cost (%s): %.3f ms/frame over the last %d frames (%.1f fps), %.3f ms/frame over %d frames; longest frame %.1f ms at %u ms\n",
        "sdl/gles", windowMs, gWindowFrames, fps, totalMs, gFrames, gLongest * 1000.0 / frequency, gLongestAt);
    gWindowTicks = 0;
    gWindowFrames = 0;
}

} // namespace fallout::hd
