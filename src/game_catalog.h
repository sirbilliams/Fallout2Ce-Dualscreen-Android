#ifndef FALLOUT_GAME_CATALOG_H_
#define FALLOUT_GAME_CATALOG_H_
#include <array>
#include <string>
namespace fallout {
enum class GameDownloadKind { GithubRelease,
    Direct,
    ProjectPage };
struct GameCatalogEntry {
    const char* id;
    const char* title;
    const char* creator;
    const char* projectUrl;
    GameDownloadKind downloadKind;
    bool experimental;
    const char* languageNote;
    // Opening movies (GameMovie ids), -1 terminated; the picker's Intro
    // button plays them in order. Empty when the game's data is unknown.
    std::array<int, 4> introMovies;
};
// Number of intro movies of a catalog entry.
int gameCatalogIntroCount(int index);
const std::array<GameCatalogEntry, 8>& gameCatalog();
// Unknown or malformed selections fall back to Fallout 2.
int gameCatalogIndex(const std::string& id);
// Game roots. Fallout 2 keeps the app data root (the working directory at
// start) exactly as before; every other game owns "<top>/games/<id>" with its
// own base files, profiles, saves, fallout2.cfg and mods. "" = working dir.
// Unknown ids fall back to Fallout 2.
std::string gameRootPath(const std::string& topRoot, const std::string& id);
// Creates the game's root when it is not Fallout 2 (lazily, never for Fallout 2).
bool gameRootEnsure(const std::string& topRoot, const std::string& id);
// Called once at start, before any config is read: when the persisted
// selection (at the top root) is an existing non-Fallout 2 game root, makes
// it the working directory. Returns the active catalog index.
int gameRootActivate(const std::string& topRoot = "");
int gameActiveIndex();
// Top app data root while a game folder is the working dir, else "".
// The selection file always lives here.
const std::string& gameTopRoot();
int gamePickerLoadPage(const std::string& root = "");
bool gamePickerSavePage(int page, const std::string& root = "");
} // namespace fallout
#endif
