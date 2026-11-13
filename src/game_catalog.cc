#include "game_catalog.h"
#include "game_movie.h"
#include "save_storage.h"
#include <filesystem>
#include <fstream>
namespace fallout {
const std::array<GameCatalogEntry, 8>& gameCatalog()
{
    static const std::array<GameCatalogEntry, 8> entries = { {
        { "fallout2", "Fallout 2", "Black Isle Studios", "https://www.gog.com/en/game/fallout_2", GameDownloadKind::ProjectPage, false, "Language: installed game data", { MOVIE_IPLOGO, MOVIE_INTRO, MOVIE_CREDITS, -1 } },
        { "ettu", "Fallout 1 (Et Tu)", "rotators", "https://github.com/rotators/Fo1in2/releases", GameDownloadKind::GithubRelease, true, "English; requires Fallout 1 files", { -1, -1, -1, -1 } },
        { "sonora", "Fallout: Sonora", "Nevada Band / Cambragol / NovaRain", "https://github.com/NovaRain/Fallout-Sonora-English/releases", GameDownloadKind::GithubRelease, true, "English, Russian, Spanish", { -1, -1, -1, -1 } },
        { "resurrection", "Fallout 1.5: Resurrection", "Resurrection Team", "https://resurrection.cz/en/download/", GameDownloadKind::Direct, true, "English; Polish patch available", { -1, -1, -1, -1 } },
        { "nevada", "Fallout: Nevada", "Nevada Band", "https://www.nma-fallout.com/threads/fallout-nevada-extended.207311/", GameDownloadKind::ProjectPage, true, "Russian; English Fixed edition", { -1, -1, -1, -1 } },
        { "olympus2207", "Olympus 2207", "Nebesa Games / Dieselship Studio", "https://olympus2207.com/", GameDownloadKind::ProjectPage, true, "Russian, English", { -1, -1, -1, -1 } },
        { "megamod", "Fallout 2 Megamod", "MIB88 and contributors", "https://www.moddb.com/mods/mib88-megamod", GameDownloadKind::ProjectPage, true, "English", { -1, -1, -1, -1 } },
        { "yesterday", "Fallout: Yesterday", "PJ Hexer and contributors", "https://gitlab.com/falloutfans/fallouty", GameDownloadKind::ProjectPage, true, "English", { -1, -1, -1, -1 } },
    } };
    return entries;
}
int gameCatalogIntroCount(int index)
{
    if (index < 0 || index >= static_cast<int>(gameCatalog().size())) return 0;
    int count = 0;
    for (int movie : gameCatalog()[index].introMovies) {
        if (movie < 0) break;
        ++count;
    }
    return count;
}
int gameCatalogIndex(const std::string& id)
{
    for (size_t i = 0; i < gameCatalog().size(); ++i) {
        if (id == gameCatalog()[i].id) return static_cast<int>(i);
    }
    return 0;
}
static std::string gTopRoot;
static int gActive = 0;
std::string gameRootPath(const std::string& topRoot, const std::string& id)
{
    int index = gameCatalogIndex(id);
    if (index == 0) return topRoot;
    std::string games = std::string("games/") + gameCatalog()[index].id;
    return topRoot.empty() ? games : topRoot + "/" + games;
}
bool gameRootEnsure(const std::string& topRoot, const std::string& id)
{
    if (gameCatalogIndex(id) == 0) return true;
    return saveStorage::makeDirectory(topRoot.empty() ? "games" : topRoot + "/games")
        && saveStorage::makeDirectory(gameRootPath(topRoot, id));
}
int gameRootActivate(const std::string& topRoot)
{
    gActive = 0;
    gTopRoot.clear();
    int index = gamePickerLoadPage(topRoot);
    if (index == 0) return 0;
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string root = gameRootPath(topRoot, gameCatalog()[index].id);
    if (!fs::is_directory(root, ec)) return 0; // not installed yet
    fs::path top = topRoot.empty() ? fs::current_path(ec) : fs::absolute(topRoot, ec);
    fs::path target = fs::absolute(root, ec);
    if (ec) return 0;
    fs::current_path(target, ec);
    if (ec) return 0;
    gTopRoot = top.string();
    gActive = index;
    return index;
}
int gameActiveIndex() { return gActive; }
const std::string& gameTopRoot() { return gTopRoot; }
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
