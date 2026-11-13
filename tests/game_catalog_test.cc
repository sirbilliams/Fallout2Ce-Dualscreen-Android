#include "game_catalog.h"
#include "game_movie.h"
#include "save_storage.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
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

    // Game roots: Fallout 2 unchanged, others under games/<id>.
    CHECK(gameRootPath("", "fallout2") == "");
    CHECK(gameRootPath("/data/app", "fallout2") == "/data/app");
    CHECK(gameRootPath("", "sonora") == "games/sonora");
    CHECK(gameRootPath("/data/app", "sonora") == "/data/app/games/sonora");
    CHECK(gameRootPath("/data/app", "../../bad") == "/data/app");
    char top[] = "/tmp/game-roots-XXXXXX";
    CHECK(mkdtemp(top));
    std::string base(top);
    auto exists = [](const std::string& p) { return access(p.c_str(), F_OK) == 0; };
    // Fallout 2 only run: nothing is created, selection stays at the top.
    CHECK(gameRootEnsure(base, "fallout2"));
    CHECK(gamePickerSavePage(0, base));
    CHECK(gameRootActivate(base) == 0 && gameActiveIndex() == 0 && gameTopRoot().empty());
    CHECK(!exists(base + "/games"));
    CHECK(exists(base + "/profiles/last-game.txt"));
    // Selected but not installed: still Fallout 2, nothing created.
    CHECK(gamePickerSavePage(2, base));
    CHECK(gameRootActivate(base) == 0 && !exists(base + "/games"));
    // Second game: lazily created, isolated data.
    char* cwd = getcwd(nullptr, 0);
    CHECK(cwd);
    CHECK(gameRootEnsure(base, "sonora"));
    CHECK(exists(base + "/games/sonora"));
    CHECK(gameRootActivate(base) == 2 && gameActiveIndex() == 2);
    char* now = getcwd(nullptr, 0);
    CHECK(std::string(now) == std::string(realpath((base + "/games/sonora").c_str(), nullptr)));
    CHECK(gameTopRoot() == base);
    // Game B writes (relative to its root) never reach game A and back.
    CHECK(saveStorage::makeDirectory("profiles") && saveStorage::makeDirectory("profiles/P"));
    CHECK(saveStorage::writeFile("profiles/active.txt", "P\n"));
    CHECK(saveStorage::writeFile("fallout2.cfg", "b\n"));
    CHECK(saveStorage::writeFile("data_b.sav", "b\n"));
    CHECK(!exists(base + "/profiles/active.txt") && !exists(base + "/fallout2.cfg") && !exists(base + "/profiles/P"));
    // Selection is still read and written at the top root.
    CHECK(gamePickerSavePage(0, gameTopRoot()));
    CHECK(exists(base + "/profiles/last-game.txt"));
    CHECK(!exists(base + "/games/sonora/profiles/last-game.txt"));
    CHECK(gamePickerLoadPage(gameTopRoot()) == 0);
    CHECK(chdir(cwd) == 0);
    CHECK(saveStorage::writeFile(base + "/fallout2.cfg", "a\n"));
    CHECK(!exists(base + "/games/sonora/profiles/last-game.txt"));
    CHECK(exists(base + "/games/sonora/fallout2.cfg"));
    std::ifstream cfgB(base + "/games/sonora/fallout2.cfg");
    std::string line;
    std::getline(cfgB, line);
    CHECK(line == "b");
    free(now);
    free(cwd);
    CHECK(saveStorage::removeTree(base));

    // Shared vanilla base files.
    const char* bases[] = { "", "fallout1", "fallout2", "fallout2", "fallout2", "fallout2", "fallout2", "fallout2" };
    for (int i = 0; i < 8; ++i)
        CHECK(std::string(gameCatalog()[i].baseGame) == bases[i]);
    char sh[] = "/tmp/game-base-XXXXXX";
    CHECK(mkdtemp(sh));
    std::string tp(sh);
    CHECK(gameBasePath(tp, 0).empty() && gameBasePath(tp, 99).empty());
    CHECK(gameBasePath(tp, 2) == tp && gameBasePath(tp, 1) == tp + "/base/fallout1");
    CHECK(saveStorage::makeDirectory(tp + "/games") && saveStorage::makeDirectory(tp + "/games/sonora"));
    CHECK(saveStorage::makeDirectory(tp + "/games/ettu") && saveStorage::makeDirectory(tp + "/base"));
    CHECK(saveStorage::makeDirectory(tp + "/base/fallout1"));
    CHECK(saveStorage::writeFile(tp + "/master.dat", "f2"));
    CHECK(saveStorage::writeFile(tp + "/base/fallout1/master.dat", "f1"));
    // gameBaseHasFiles: needs master.dat and critter.dat in the Fallout 1 base.
    {
        char hb[] = "/tmp/game-basefiles-XXXXXX";
        CHECK(mkdtemp(hb));
        std::string hp(hb);
        CHECK(!gameBaseHasFiles(hp, 1) && !gameBaseHasFiles(hp, 0) && !gameBaseHasFiles(hp, 99));
        CHECK(saveStorage::makeDirectory(hp + "/base") && saveStorage::makeDirectory(hp + "/base/fallout1"));
        CHECK(saveStorage::writeFile(hp + "/base/fallout1/master.dat", "m"));
        CHECK(!gameBaseHasFiles(hp, 1));
        CHECK(saveStorage::writeFile(hp + "/base/fallout1/critter.dat", "c"));
        CHECK(gameBaseHasFiles(hp, 1));
        CHECK(!gameBaseHasFiles(hp, 0)); // Fallout 2 has no base dir
    }
    // Index 0: unchanged, even when a base file exists.
    CHECK(gameResolveBaseFile(tp, 0, "master.dat") == "master.dat");
    // Sonora (Fallout 2 based): no local file -> top root; the path is relative to the game folder.
    char* cw = getcwd(nullptr, 0);
    CHECK(cw);
    CHECK(chdir((tp + "/games/sonora").c_str()) == 0);
    CHECK(gameResolveBaseFile(tp, 2, "master.dat") == tp + "/master.dat");
    CHECK(gameResolveBaseFile(tp, 2, "critter.dat") == "critter.dat"); // in no base: unchanged
    CHECK(gameResolveBaseFile(tp, 2, "") == "");
    CHECK(gameResolveBaseFile(tp, 2, "/abs/master.dat") == "/abs/master.dat");
    // Write paths (master/critter patches) are never passed to the resolver; a
    // game-folder 'data' that exists locally always stays as configured.
    CHECK(saveStorage::makeDirectory("data"));
    CHECK(gameResolveBaseFile(tp, 2, "data") == "data");
    CHECK(saveStorage::writeFile("master.dat", "own"));
    CHECK(gameResolveBaseFile(tp, 2, "master.dat") == "master.dat"); // local wins
    CHECK(chdir((tp + "/games/ettu").c_str()) == 0);
    CHECK(gameResolveBaseFile(tp, 1, "master.dat") == tp + "/base/fallout1/master.dat");
    CHECK(gameResolveBaseFile(tp, 1, "sound\\music\\") == "sound\\music\\");
    // Top-root-only app files (ce.dat): missing in the game folder -> top root.
    CHECK(gameResolveTopRootFile(tp, "ce.dat") == "ce.dat"); // absent everywhere
    CHECK(saveStorage::makeDirectory(tp + "/ce.dat"));
    CHECK(gameResolveTopRootFile(tp, "ce.dat") == tp + "/ce.dat");
    CHECK(gameResolveTopRootFile("", "ce.dat") == "ce.dat");
    CHECK(saveStorage::makeDirectory("ce.dat")); // game folder's own wins
    CHECK(gameResolveTopRootFile(tp, "ce.dat") == "ce.dat");
    CHECK(chdir(cw) == 0);
    free(cw);
    CHECK(saveStorage::removeTree(tp));
    return 0;
}
