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
    // Vanilla game whose base files (master.dat, critter.dat, patches, music)
    // this game shares: "fallout1", "fallout2" or "" (none; Fallout 2 is a base).
    const char* baseGame;
    // Creator-controlled page (GitHub repository or project page), resolved at runtime.
    const char* pictureSource;
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
// Shared vanilla base files. One copy per vanilla game: Fallout 2's is the top
// root itself, Fallout 1's is "<top>/base/fallout1". Pure (strings and file
// existence only); never creates, copies or modifies anything.
// Base directory of a game, or "" when it has none (Fallout 2, bad index).
std::string gameBasePath(const std::string& topRoot, int index);
// True when the game's base dir has master.dat and critter.dat (what the
// Fallout 1 import leaves there). False for games without a base dir.
bool gameBaseHasFiles(const std::string& topRoot, int index);
// For a read-only base file/dir setting [configured] (relative to the game's
// folder): the configured path when index is 0, it is absolute/empty, or it
// exists in the game folder (its own file always wins) or in no base; else the
// same name under the game's base dir. "\\" separators are accepted.
std::string gameResolveBaseFile(const std::string& topRoot, int index, const std::string& configured);
// App-level read-only file/folder (e.g. "ce.dat") that exists only at the top
// root: [rel] when it exists in the working dir (or there is no top root), else
// "<top>/rel" when that exists, else [rel]. Pure.
std::string gameResolveTopRootFile(const std::string& topRoot, const std::string& rel);
int gamePickerLoadPage(const std::string& root = "");
bool gamePickerSavePage(int page, const std::string& root = "");
} // namespace fallout
#endif
