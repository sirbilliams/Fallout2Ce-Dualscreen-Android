#include "game_catalog.h"
#include "game_movie.h"
#include "save_storage.h"
#include <cctype>
#include <filesystem>
#include <fstream>
namespace fallout {
const std::array<GameCatalogEntry, 8>& gameCatalog()
{
    static const std::array<GameCatalogEntry, 8> entries = { {
        { "fallout2", "Fallout 2", "Black Isle Studios", "https://www.gog.com/en/game/fallout_2", GameDownloadKind::ProjectPage, false, "Language: installed game data", { MOVIE_IPLOGO, MOVIE_INTRO, MOVIE_CREDITS, -1 }, "", false, "https://fallout.bethesda.net/en/games/fallout-2" },
        { "ettu", "Fallout 1 (Et Tu)", "rotators", "https://github.com/rotators/Fo1in2/releases", GameDownloadKind::GithubRelease, true, "English; requires Fallout 1 files", { -1, -1, -1, -1 }, "fallout1", false, "https://github.com/rotators/Fo1in2" },
        { "sonora", "Fallout: Sonora", "Nevada Band / Cambragol / NovaRain", "https://github.com/NovaRain/Fallout-Sonora-English/releases", GameDownloadKind::GithubRelease, true, "English, Russian, Spanish", { -1, -1, -1, -1 }, "fallout2", true, "https://github.com/NovaRain/Fallout-Sonora-English" },
        { "resurrection", "Fallout 1.5: Resurrection", "Resurrection Team", "https://resurrection.cz/en/download/", GameDownloadKind::Direct, true, "English; Polish patch available", { -1, -1, -1, -1 }, "fallout2", false, "https://resurrection.cz/en/download/" },
        { "nevada", "Fallout: Nevada", "Nevada Band", "https://www.nma-fallout.com/threads/fallout-nevada-extended.207311/", GameDownloadKind::ProjectPage, true, "Russian; English Fixed edition", { -1, -1, -1, -1 }, "fallout2", false, "https://www.nma-fallout.com/threads/fallout-nevada-extended.207311/" },
        { "olympus2207", "Olympus 2207", "Nebesa Games / Dieselship Studio", "https://olympus2207.com/", GameDownloadKind::ProjectPage, true, "Russian, English", { -1, -1, -1, -1 }, "fallout2", false, "https://olympus2207.com/" },
        { "megamod", "Fallout 2 Megamod", "MIB88 and contributors", "https://www.moddb.com/mods/mib88-megamod", GameDownloadKind::ProjectPage, true, "English", { -1, -1, -1, -1 }, "fallout2", false, "https://www.moddb.com/mods/mib88-megamod" },
        { "yesterday", "Fallout: Yesterday", "PJ Hexer and contributors", "https://gitlab.com/falloutfans/fallouty/-/archive/main/fallouty-main.zip", GameDownloadKind::Direct, true, "English", { -1, -1, -1, -1 }, "fallout2", false, "https://gitlab.com/falloutfans/fallouty" },
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
    if (gameOwnDatsMissing(topRoot, index)) return 0; // incomplete install: picker offers Repair
    fs::path top = topRoot.empty() ? fs::current_path(ec) : fs::absolute(topRoot, ec);
    fs::path target = fs::absolute(root, ec);
    if (ec) return 0;
    fs::current_path(target, ec);
    if (ec) return 0;
    gTopRoot = top.string();
    gActive = index;
    return index;
}
std::string gameBasePath(const std::string& topRoot, int index)
{
    if (index <= 0 || index >= static_cast<int>(gameCatalog().size())) return "";
    std::string base = gameCatalog()[index].baseGame;
    if (base == "fallout2") return topRoot;
    if (base == "fallout1") return topRoot.empty() ? "base/fallout1" : topRoot + "/base/fallout1";
    return "";
}
bool gameBaseHasFiles(const std::string& topRoot, int index)
{
    namespace fs = std::filesystem;
    std::string base = gameBasePath(topRoot, index);
    if (base.empty()) return false;
    std::error_code ec;
    return fs::is_regular_file(base + "/master.dat", ec) && fs::is_regular_file(base + "/critter.dat", ec);
}
bool gameOwnDatsMissing(const std::string& topRoot, int index)
{
    namespace fs = std::filesystem;
    if (index <= 0 || index >= static_cast<int>(gameCatalog().size()) || !gameCatalog()[index].ownDats) return false;
    std::string root = gameRootPath(topRoot, gameCatalog()[index].id);
    std::error_code ec;
    return !fs::is_regular_file(root + "/master.dat", ec) || !fs::is_regular_file(root + "/critter.dat", ec);
}
bool gameNeedsRepair(const std::string& topRoot, int index)
{
    namespace fs = std::filesystem;
    if (!gameOwnDatsMissing(topRoot, index)) return false;
    std::error_code ec;
    return fs::is_regular_file(gameRootPath(topRoot, gameCatalog()[index].id) + "/.installed", ec);
}
std::string gameResolveBaseFile(const std::string& topRoot, int index, const std::string& configured)
{
    namespace fs = std::filesystem;
    if (index <= 0 || configured.empty() || configured[0] == '/' || configured[0] == '\\') return configured;
    if (configured.size() > 1 && configured[1] == ':') return configured;
    std::string rel = configured;
    for (char& c : rel)
        if (c == '\\') c = '/';
    while (!rel.empty() && rel.back() == '/')
        rel.pop_back();
    std::error_code ec;
    if (rel.empty() || fs::exists(rel, ec)) return configured;
    if (gameCatalog()[index].ownDats) {
        // The game ships its own dats: a missing one must stay missing (the
        // caller reports an incomplete install), never Fallout 2's. This covers
        // every .dat, including patchNNN.dat: Fallout 2's patch000.dat mounted
        // above the game's master.dat swaps in Fallout 2's scripts.lst.
        std::string lower = rel;
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".dat") == 0) return configured;
    }
    std::string base = gameBasePath(topRoot, index);
    if (base.empty()) return configured;
    if (std::string(gameCatalog()[index].baseGame) == "fallout1") {
        // Fallout 1 ships DAT1 archives, which the engine cannot open. Mods built
        // on it (Et Tu) run on Fallout 2's master/critter dat; Fallout 1's
        // content is unpacked loose into the game's data folder instead. Any
        // .dat therefore comes from the top root (Fallout 2); only loose files
        // (music, ...) still come from base/fallout1.
        std::string lower = rel;
        for (char& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.size() >= 4 && lower.compare(lower.size() - 4, 4, ".dat") == 0) {
            if (topRoot.empty()) return configured;
            std::string f2 = topRoot + "/" + rel;
            return fs::exists(f2, ec) ? f2 : configured;
        }
    }
    std::string candidate = base + "/" + rel;
    return fs::exists(candidate, ec) ? candidate : configured;
}
std::string gameResolveTopRootFile(const std::string& topRoot, const std::string& rel)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (topRoot.empty() || fs::exists(rel, ec)) return rel;
    std::string candidate = topRoot + "/" + rel;
    return fs::exists(candidate, ec) ? candidate : rel;
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
bool gamePickerWritePlayOnce(const std::string& id, const std::string& root)
{
    return !id.empty() && saveStorage::makeDirectory(profiles(root))
        && saveStorage::writeFile(profiles(root) + "/play-once.txt", id + "\n");
}
std::string gamePickerConsumePlayOnce(const std::string& root)
{
    std::string path = profiles(root) + "/play-once.txt";
    std::string id;
    {
        std::ifstream file(path);
        if (!file.good()) return "";
        std::getline(file, id);
    }
    saveStorage::removeTree(path);
    while (!id.empty() && (id.back() == '\r' || id.back() == ' '))
        id.pop_back();
    return id;
}
std::string gameSizeText(const std::string& sizeMsg, const std::string& size)
{
    std::string text = sizeMsg;
    for (const char* marker : { "%s", "download pending" }) {
        size_t at = text.find(marker);
        if (at != std::string::npos) {
            text.replace(at, std::string(marker).size(), size);
            break;
        }
    }
    return text;
}
std::string gameVersionDetail(bool incomplete, const std::string& incompleteMsg, const std::string& notInstalledMsg,
    const std::string& sizeMsg, const std::string& size)
{
    if (incomplete) return incompleteMsg;
    return size.empty() ? notInstalledMsg : gameSizeText(sizeMsg, size);
}
} // namespace fallout
