#include "game_catalog.h"
#include "save_storage.h"
#include <fstream>
namespace fallout {
const std::array<GameCatalogEntry, 8>& gameCatalog()
{
    static const std::array<GameCatalogEntry, 8> entries = {{
        { "fallout2", "Fallout 2", "Black Isle Studios", "https://www.gog.com/en/game/fallout_2", GameDownloadKind::ProjectPage, false, "Language: installed game data" },
        { "ettu", "Fallout 1 (Et Tu)", "rotators", "https://github.com/rotators/Fo1in2/releases", GameDownloadKind::GithubRelease, true, "English; requires Fallout 1 files" },
        { "sonora", "Fallout: Sonora", "Nevada Band / Cambragol / NovaRain", "https://github.com/NovaRain/Fallout-Sonora-English/releases", GameDownloadKind::GithubRelease, true, "English, Russian, Spanish" },
        { "resurrection", "Fallout 1.5: Resurrection", "Resurrection Team", "https://resurrection.cz/en/download/", GameDownloadKind::Direct, true, "English; Polish patch available" },
        { "nevada", "Fallout: Nevada", "Nevada Band", "https://www.nma-fallout.com/threads/fallout-nevada-extended.207311/", GameDownloadKind::ProjectPage, true, "Russian; English Fixed edition" },
        { "olympus2207", "Olympus 2207", "Nebesa Games / Dieselship Studio", "https://olympus2207.com/", GameDownloadKind::ProjectPage, true, "Russian, English" },
        { "megamod", "Fallout 2 Megamod", "MIB88 and contributors", "https://www.moddb.com/mods/mib88-megamod", GameDownloadKind::ProjectPage, true, "English" },
        { "yesterday", "Fallout: Yesterday", "PJ Hexer and contributors", "https://gitlab.com/falloutfans/fallouty", GameDownloadKind::ProjectPage, true, "English" },
    }};
    return entries;
}
int gameCatalogIndex(const std::string& id)
{
    for (size_t i = 0; i < gameCatalog().size(); ++i) {
        if (id == gameCatalog()[i].id) return static_cast<int>(i);
    }
    return 0;
}
static std::string profiles(const std::string& root)
{
    return root.empty() ? "profiles" : root + "/profiles";
}
int gamePickerLoadPage(const std::string& root)
{
    std::ifstream file(profiles(root) + "/last-game.txt");
    std::string id;
    std::getline(file, id);
    return gameCatalogIndex(id);
}
bool gamePickerSavePage(int page, const std::string& root)
{
    if (page < 0 || page >= static_cast<int>(gameCatalog().size())) return false;
    return saveStorage::makeDirectory(profiles(root))
        && saveStorage::writeFile(profiles(root) + "/last-game.txt", std::string(gameCatalog()[page].id) + "\n");
}
} // namespace fallout
