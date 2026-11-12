#include "mod_profiles.h"

#include <algorithm>
#include <filesystem>

#include "db.h"
#include "debug.h"
#include "platform_compat.h"
#include "save_compatibility.h"

namespace fallout {

void modProfileMount()
{
    saveCompatibilityReset();
    if (modProfileIsVanilla()) {
        return;
    }

    ModList list;
    if (!modProfileLoadList(&list)) {
        debugPrint("Unable to read active profile mod list\n");
        return;
    }

    namespace fs = std::filesystem;
    // Saves and the store share the same root (including modProfileSetRoot).
    fs::path store = fs::path(modProfileSavesRoot()).parent_path().parent_path() / "modstore";
    for (const std::string& id : modListEnabledIds(list)) {
        // A list must never mount files outside its store entry.
        if (id.empty() || id == "." || id == ".." || id.find_first_of("/\\:") != std::string::npos) {
            debugPrint("Invalid mod id: %s\n", id.c_str());
            continue;
        }
        fs::path root = store / id;
        std::vector<fs::path> archives;
        std::error_code ec;
        fs::directory_iterator it(root, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && compat_stricmp(it->path().extension().string().c_str(), ".dat") == 0) {
                archives.push_back(it->path());
            }
        }
        std::sort(archives.begin(), archives.end());
        if (fs::is_directory(root / "data", ec)) {
            archives.push_back(root / "data");
        }
        for (const fs::path& path : archives) {
            debugPrint("Loading profile mod: %s\n", path.string().c_str());
            if (dbOpen(path.string().c_str()) == -1) {
                debugPrint("Unable to mount profile mod: %s\n", path.string().c_str());
            }
        }
    }
}

} // namespace fallout
