#include "mod_list.h"
#include "mod_profiles.h"
#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fallout { Settings settings; }
using namespace fallout;
namespace fs = std::filesystem;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

static void put(const fs::path& path, const std::string& text)
{
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << text;
    CHECK(output.good());
}

static std::string get(const fs::path& path)
{
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

int main()
{
    ModList list;
    CHECK(modListParse("+rpu\n-npc_armor\n", &list));
    CHECK(modListSerialize(list) == "+rpu\n-npc_armor\n");
    CHECK(modListParse("# header\n\n  +a\r\n-b\n+c\n", &list));
    CHECK(modListEnabledIds(list) == (std::vector<std::string> { "a", "c" }));
    modListMove(&list, 0, 2);
    CHECK(modListSerialize(list) == "-b\n+c\n+a\n");
    modListMove(&list, 2, 0);
    CHECK(modListSerialize(list) == "+a\n-b\n+c\n");
    modListMove(&list, 99, 0);
    CHECK(list.entries.size() == 3);
    CHECK(!modListParse("bad\n", &list));
    CHECK(list.entries.size() == 3);
    CHECK(!modListParse("+../escape\n", &list));
    CHECK(!modListParse("+..\n", &list));
    CHECK(!modListParse("+\n", &list));
    CHECK(!modListParse("", nullptr));
    ModList store { { { "my-mod-", true }, { "my-mod--2", false } } };
    CHECK(modListUniqueId(ModList { { { "my-mod-", true } } }, "My Mod!") == "my-mod--2");
    CHECK(modListUniqueId(store, "My Mod!") == "my-mod--3");
    CHECK(modListUniqueId(store, "..") == "mod");
    store.entries.push_back({ modListUniqueId(store, "My Mod!"), true });
    CHECK(store.entries.size() == 3 && store.entries[2].id == "my-mod--3");

    char temp[] = "/tmp/f2ds-mod-core-XXXXXX";
    CHECK(mkdtemp(temp) != nullptr);
    fs::path root(temp);
    auto cwd = fs::current_path();
    fs::current_path(root);
    modProfileSetRoot("");
    CHECK(modProfileActive() == "Vanilla");
    CHECK(modProfileSavesRoot() == "data");
    CHECK(modProfileNames() == (std::vector<std::string> { "Vanilla" }));
    CHECK(modProfileLoadList(&list) && list.entries.empty());
    CHECK(modProfileSetActive("Vanilla"));
    CHECK(!fs::exists("profiles"));
    settings.system.saves_root = "custom-saves";
    CHECK(modProfileSavesRoot() == "custom-saves");
    settings.system.saves_root.clear();
    put("mods/mods_order.txt", "a.dat\nb.dat\n");
    CHECK(modProfileLoadList(&list));
    CHECK(modListSerialize(list) == "+a.dat\n+b.dat\n");
    list.entries[1].enabled = false;
    CHECK(modProfileSaveList(list));
    CHECK(get("mods/mods_order.txt") == "a.dat\n#b.dat\n");
    CHECK(modProfileLoadList(&list));
    CHECK(modListSerialize(list) == "+a.dat\n-b.dat\n");
    put("data/SAVEGAME/SLOT01/SAVE.DAT", "vanilla save");
    put("ddraw.ini", "[test]\nvalue=1\n");
    put("mods/nested/test.ini", "setting=2\n");
    put("mods/ignored.dat", "not copied");
    CHECK(!modProfileCreate("../bad", false));
    CHECK(!modProfileCreate("active.txt", false));
    CHECK(!modProfileCreate("Vanilla", false));
    CHECK(!modProfileSetActive("absent"));
    CHECK(modProfileCreate("RPU run", true));
    CHECK(!modProfileCreate("RPU run", false));
    CHECK(get("profiles/RPU run/modlist.txt") == "+a.dat\n-b.dat\n");
    CHECK(get("profiles/RPU run/ddraw.ini") == get("ddraw.ini"));
    CHECK(get("profiles/RPU run/mods/nested/test.ini") == "setting=2\n");
    CHECK(!fs::exists("profiles/RPU run/mods/ignored.dat"));
    CHECK(fs::is_empty("profiles/RPU run/SAVEGAME"));
    CHECK(modProfileSetActive("RPU run"));
    CHECK(modProfileSavesRoot() == "profiles/RPU run");
    CHECK(!modProfileDelete("Vanilla"));
    CHECK(!modProfileRename("Vanilla", "X"));
    CHECK(!modProfileDelete("RPU run"));
    CHECK(!modProfileRename("RPU run", "X"));
    CHECK(modProfileLoadList(&list));
    list.entries.push_back({ "fixture", true });
    CHECK(modProfileSaveList(list));
    CHECK(get("mods/mods_order.txt") == "a.dat\n#b.dat\n");
    put("profiles/RPU run/SAVEGAME/SLOT01/SAVE.DAT", "profile save");
    CHECK(modProfileCreate("Empty", false));
    CHECK(get("profiles/Empty/modlist.txt").empty());
    CHECK(modProfileCreate("Copied", true));
    CHECK(get("profiles/Copied/modlist.txt") == modListSerialize(list));
    CHECK(fs::is_empty("profiles/Copied/SAVEGAME"));
    CHECK(modProfileRename("Copied", "Renamed"));
    CHECK(modProfileDelete("Renamed"));
    CHECK(modProfileSetActive("Vanilla"));
    CHECK(modProfileRename("RPU run", "RPU renamed"));
    CHECK(get("profiles/RPU renamed/SAVEGAME/SLOT01/SAVE.DAT") == "profile save");
    CHECK(get("data/SAVEGAME/SLOT01/SAVE.DAT") == "vanilla save");
    CHECK(modProfileDelete("RPU renamed"));
    put("profiles/active.txt", "../bad\n");
    CHECK(modProfileIsVanilla());
    put("modstore/z/data/a", "z");
    put("modstore/a/data/a", "a");
    put("modstore/.tmp-incomplete/data/a", "staged");
    put("modstore/.hidden/data/a", "installed");
    CHECK(modStoreIds() == (std::vector<std::string> { ".hidden", "a", "z" }));
    modProfileSetRoot(root.string());
    CHECK(modProfileSavesRoot() == (root / "data").string());
    CHECK(modProfileSetActive("Empty"));
    CHECK(modProfileSavesRoot() == (root / "profiles/Empty").string());
    fs::current_path(cwd);
    fs::remove_all(root);
    std::puts("mod list and profile tests passed");
}
