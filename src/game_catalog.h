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
int gamePickerLoadPage(const std::string& root = "");
bool gamePickerSavePage(int page, const std::string& root = "");
} // namespace fallout
#endif
