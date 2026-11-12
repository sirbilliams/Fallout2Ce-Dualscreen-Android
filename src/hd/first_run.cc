#include "hd/first_run.h"

#include <stdio.h>
#include <string.h>

#include <string>

#include <SDL.h>

#include "hd/paths.h"

#include "svga.h"

namespace fallout::hd {

namespace {

    std::string gReport;

    // The game needs at least 640x480 logical pixels (svga.cc
    // _GNW95_init_mode_ex falls back to scale 1 otherwise).
    bool fits(int width, int height, int scale)
    {
        return width / scale >= 640 && height / scale >= 480;
    }

    bool fileExists(const std::string& path)
    {
        return path.size() < COMPAT_MAX_PATH && compat_file_exists(path.c_str());
    }

} // namespace

int installedArtTiers(Config* config)
{
    char* dir = nullptr;
    configGetString(config, "hd", "hd_art_dir", &dir, "hd");
    std::string root = nativePath(dir != nullptr && dir[0] != '\0' ? dir : "hd");
    while (!root.empty() && (root.back() == '\\' || root.back() == '/')) {
        root.pop_back();
    }
    int mask = 0;
    for (int tier = 2; tier <= 4; tier++) {
        char marker[64];
        snprintf(marker, sizeof(marker), "/pack_%dx.json", tier);
        char bar[64];
        snprintf(bar, sizeof(bar), "/art/intrface/iface/albedo_%dx.dds", tier);
        if (fileExists(root + marker) || fileExists(root + bar)) {
            mask |= 1 << tier;
        }
    }
    return mask;
}

bool firstRunScreenSetup(Config* config)
{
    int autoScreen = 0;
    configGetInt(config, "hd", "auto_screen", &autoScreen, 0);
    if (autoScreen == 0) {
        return false;
    }

    // SDL reports physical desktop pixels on every supported platform.
    // The caller initializes SDL video before querying the display mode.
    SDL_DisplayMode mode = {};
    if (SDL_GetDesktopDisplayMode(0, &mode) != 0 || mode.w < 640 || mode.h < 480) {
        gReport = "HD: first start: desktop size unknown, screen settings left as they are";
        configSetInt(config, "hd", "auto_screen", 0);
        return true;
    }
    int width = mode.w;
    int height = mode.h;

    int tiers = installedArtTiers(config);
    int scale = 0;
    for (int tier = 4; tier >= 2; tier--) {
        if ((tiers & (1 << tier)) != 0 && fits(width, height, tier)) {
            scale = tier;
            break;
        }
    }
    bool matched = scale != 0;
    if (!matched) {
        // No art for a scale that fits: the largest scale that does (the
        // original art, drawn by the HD renderer).
        for (scale = 4; scale > 1 && !fits(width, height, scale); scale--) {
        }
    }

    configSetInt(config, "screen", "resolution_x", width);
    configSetInt(config, "screen", "resolution_y", height);
    configSetInt(config, "screen", "windowed", static_cast<int>(WindowMode::WindowedFullscreen));
    configSetInt(config, "screen", "scale", scale);
    configSetInt(config, "hd", "auto_screen", 0);

    char text[256];
    snprintf(text, sizeof(text), "HD: first start: %dx%d borderless, scale %d (%s; installed tiers:%s%s%s%s)",
        width, height, scale,
        matched ? "matches the installed HD art" : "no HD art for a scale that fits",
        (tiers & 4) != 0 ? " 2x" : "", (tiers & 8) != 0 ? " 3x" : "", (tiers & 16) != 0 ? " 4x" : "",
        tiers == 0 ? " none" : "");
    gReport = text;
    return true;

}

const char* firstRunReport()
{
    return gReport.c_str();
}

} // namespace fallout::hd
