#include "hd_art_ui.h"
#include "hd/art_config.h"

#include "../game_config.h"

namespace fallout::hd {

namespace {
    HdArtCpu gArt;
    bool gTried = false;
}

bool uiArtActive()
{
    if (!gTried) {
        if (!gGameConfigInitialized) {
            return false; // asked too early; try again later
        }
        gTried = true;
        bool enabled = false;
        configGetBool(&gGameConfig, "hd", "enabled", &enabled, false);
        if (enabled) {
            gArt.open(hdArtConfiguredDir(), hdArtConfiguredTier());
        }
    }
    return gArt.isOpen();
}

bool uiArtLookup(const char* frmPath, int rotation, int frame, const unsigned char* indexed, int width, int height, HdRgba& out)
{
    return uiArtActive() && gArt.load(frmPath, rotation, frame, indexed, width, height, out);
}

} // namespace fallout::hd
