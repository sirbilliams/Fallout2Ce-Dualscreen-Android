#include "db.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fallout {
int debugPrint(const char*, ...) { return 0; }
// No script-created filesystem aliases in this database mounting test.
bool sfallFileSystemResolveAlias(const char*, char*, size_t) { return false; }
} // namespace fallout

int main()
{
    using namespace fallout;
    namespace fs = std::filesystem;
    char tmp[] = "/tmp/loose-patch-XXXXXX";
    assert(mkdtemp(tmp));
    fs::path root(tmp);
    auto put = [&](const char* folder, const char* text) {
        fs::path dir = root / folder / "scripts";
        fs::create_directories(dir);
        std::ofstream(dir / "test.int") << text;
    };
    put("base", "base");
    put("patch001.dat", "patch");
    put("data", "override");
    auto check = [&](const char* expected) {
        File* f = fileOpen("scripts\\test.int", "rb");
        assert(f);
        char text[32] = {};
        fileRead(text, 1, std::char_traits<char>::length(expected), f);
        assert(std::string(text) == expected);
        fileClose(f);
    };
    assert(dbOpen((root / "base").c_str()) == 0);
    assert(dbOpen((root / "patch001.dat").c_str()) == 0);
    check("patch");
    assert(dbOpen((root / "data").c_str()) == 0);
    check("override");
    dbCloseAll();
    fs::remove_all(root);
    std::puts("loose patch directory + override precedence passed");
}
