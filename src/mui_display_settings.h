#ifndef FALLOUT_MUI_DISPLAY_SETTINGS_H
#define FALLOUT_MUI_DISPLAY_SETTINGS_H
#include "dev_autotest.h"
#include "settings.h"
#include <SDL.h>
namespace fallout {
inline Uint32 muiLastDisplayActivity = 0;
inline void muiDisplayActivity() { muiLastDisplayActivity = uiTicks(); }
inline bool muiBottomDimmed()
{
    if (muiLastDisplayActivity == 0) muiDisplayActivity();
    return settings.touch.bottom_dim_seconds > 0
        && uiTicks() - muiLastDisplayActivity >= Uint32(settings.touch.bottom_dim_seconds) * 1000;
}
} // namespace fallout
#endif
