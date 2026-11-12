#ifndef FALLOUT_HD_ART_CONFIG_H_
#define FALLOUT_HD_ART_CONFIG_H_

// Shared reader for the HD art pack config, used by hd_art.cc (GL store),
// map_layer.cc and hd_art_ui.cc (CPU reader) so they never disagree.
//
//   [hd] hd_art_dir=hd/2x   pack root, relative to the game dir (default "hd").
//                           A trailing "<N>x" component (N = 2..4) selects the tier.
//   [hd] tier=2             tier when hd_art_dir has no "<N>x" suffix (default 2).

#include <algorithm>
#include <cctype>
#include <string>

#include "../game_config.h"

namespace fallout::hd {

inline std::string hdArtConfiguredDir()
{
    char* dir = nullptr;
    std::string root = "hd";
    if (configGetString(&gGameConfig, "hd", "hd_art_dir", &dir, "hd") && dir != nullptr && *dir != '\0') {
        root = dir;
    }
    while (root.size() > 1 && (root.back() == '/' || root.back() == '\\')) {
        root.pop_back();
    }
    return root;
}

inline int hdArtConfiguredTier()
{
    std::string root = hdArtConfiguredDir();
    size_t cut = root.find_last_of("/\\");
    std::string leaf = cut == std::string::npos ? root : root.substr(cut + 1);
    if (leaf.size() == 2 && leaf[0] >= '2' && leaf[0] <= '4' && std::tolower(static_cast<unsigned char>(leaf[1])) == 'x') {
        return leaf[0] - '0';
    }
    int tier = 2;
    configGetInt(&gGameConfig, "hd", "tier", &tier, 2);
    return std::clamp(tier, 2, 4);
}

} // namespace fallout::hd

#endif /* FALLOUT_HD_ART_CONFIG_H_ */
