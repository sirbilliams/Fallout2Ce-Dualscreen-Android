#include "fps_limiter.h"

#include <SDL.h>

#include "perf_monitor.h"
#include "settings.h"
#include <algorithm>

namespace fallout {

FpsLimiter::FpsLimiter(unsigned int fps)
    : _fps(fps)
    , _ticks(0)
{
}

void FpsLimiter::mark()
{
    _ticks = SDL_GetPerformanceCounter();
}

void FpsLimiter::throttle() const
{
    unsigned int fps = std::min(_fps, settings.touch.perf_mode == "saver" ? 30u : 60u);
    double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    double period = frequency / std::max(fps, 1u);
    double now = static_cast<double>(SDL_GetPerformanceCounter());
    if (_lastFps != fps || _deadline == 0 || now > _deadline + period || now < _deadline - period * 2) {
        _deadline = static_cast<double>(_ticks) + period;
        _lastFps = fps;
    }
    // Fractional deadlines retain the 2/3 millisecond at 60 and 30 Hz.
    while (now < _deadline) {
        double remaining = (_deadline - now) * 1000.0 / frequency;
        Uint32 delay = remaining > 1.0 ? static_cast<Uint32>(remaining) : 0;
        if (delay) perfMonitorSleep(delay);
        SDL_Delay(delay);
        now = static_cast<double>(SDL_GetPerformanceCounter());
    }
    _deadline += period;
}

} // namespace fallout
