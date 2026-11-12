#include "game_catalog.h"
#include "game_movie.h"
#include "save_storage.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <unistd.h>
#define CHECK(c)                                                 \
    do {                                                         \
        if (!(c)) {                                              \
            std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); \
            std::abort();                                        \
        }                                                        \
    } while (false)
int main()
{
    using namespace fallout;
    const char* ids[] = { "fallout2", "ettu", "sonora", "resurrection", "nevada", "olympus2207", "megamod", "yesterday" };
    std::set<std::string> unique;
    CHECK(gameCatalog().size() == 8);
    for (int i = 0; i < 8; ++i) {
        const auto& game = gameCatalog()[i];
        CHECK(std::string(game.id) == ids[i]);
        CHECK(unique.insert(game.id).second);
        CHECK(gameCatalogIndex(game.id) == i);
        CHECK(game.title[0] && game.creator[0] && game.languageNote[0]);
        CHECK(std::string(game.projectUrl).find("https://") == 0);
        CHECK(game.experimental == (i != 0));
        auto expected = i == 1 || i == 2 ? GameDownloadKind::GithubRelease : i == 3 ? GameDownloadKind::Direct
                                                                                    : GameDownloadKind::ProjectPage;
        CHECK(game.downloadKind == expected);
    }
    // Fallout 2 plays the boot movies in the boot order; others list none.
    CHECK(gameCatalogIntroCount(0) == 3);
    CHECK(gameCatalog()[0].introMovies[0] == MOVIE_IPLOGO);
    CHECK(gameCatalog()[0].introMovies[1] == MOVIE_INTRO);
    CHECK(gameCatalog()[0].introMovies[2] == MOVIE_CREDITS);
    for (int i = 1; i < 8; ++i)
        CHECK(gameCatalogIntroCount(i) == 0);
    CHECK(gameCatalogIntroCount(-1) == 0);
    CHECK(gameCatalogIntroCount(8) == 0);
    CHECK(gameCatalogIndex("../../bad") == 0);
    char temp[] = "/tmp/game-catalog-XXXXXX";
    CHECK(mkdtemp(temp));
    std::string root(temp);
    CHECK(gamePickerLoadPage(root) == 0);
    for (int i = 0; i < 8; ++i) {
        CHECK(gamePickerSavePage(i, root));
        CHECK(gamePickerLoadPage(root) == i);
    }
    CHECK(!gamePickerSavePage(-1, root));
    CHECK(!gamePickerSavePage(8, root));
    CHECK(gamePickerLoadPage(root) == 7);
    CHECK(saveStorage::writeFile(root + "/profiles/last-game.txt", "unknown\n"));
    CHECK(gamePickerLoadPage(root) == 0);
    CHECK(saveStorage::writeFile(root + "/profiles/last-game.txt", ""));
    CHECK(gamePickerLoadPage(root) == 0);
    CHECK(!gamePickerSavePage(1, root + "/missing/parent"));
    CHECK(saveStorage::removeTree(root));
    return 0;
}
