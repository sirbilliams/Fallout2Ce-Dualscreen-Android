#ifndef FALLOUT_MUI_DISPLAY_SETTINGS_H
#define FALLOUT_MUI_DISPLAY_SETTINGS_H
#include <SDL.h>
#include "settings.h"
namespace fallout {
inline Uint32 muiLastDisplayActivity = 0;
inline void muiDisplayActivity() { muiLastDisplayActivity = SDL_GetTicks(); }
inline bool muiBottomDimmed() {
    if (muiLastDisplayActivity == 0) muiDisplayActivity();
    return settings.touch.bottom_dim_seconds > 0
        && SDL_GetTicks() - muiLastDisplayActivity >= Uint32(settings.touch.bottom_dim_seconds) * 1000;
}
}
#endif
