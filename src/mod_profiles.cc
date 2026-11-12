#include "mod_profiles.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include "settings.h"

namespace fallout {
namespace fs = std::filesystem;
const char* const kModProfileVanilla = "Vanilla";

namespace {
    fs::path root;
    std::string active = kModProfileVanilla;

    bool validName(const std::string& name)
    {
        return !name.empty() && name != "active.txt" && name != "profiles.txt"
            && name != "." && name != ".." && name.front() != '.'
            && name.front() != ' ' && name.back() != ' ' && name.back() != '.'
            && name.find_first_of("/\\:*?\"<>|\r\n\t") == std::string::npos
            && std::none_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32; });
    }

    fs::path profilePath(const std::string& name) { return root / "profiles" / name; }

    bool readText(const fs::path& path, std::string* text)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        text->assign(std::istreambuf_iterator<char>(input), {});
        return !input.bad();
    }

    bool writeText(const fs::path& path, const std::string& text)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        if (ec) return false;
        fs::path temp = path;
        temp += ".tmp";
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        output << text;
        output.close();
        if (!output) return false;
        fs::rename(temp, path, ec);
        if (ec) {
            std::error_code ignored;
            fs::remove(temp, ignored);
        }
        return !ec;
    }

    bool profileExists(const std::string& name)
    {
        if (name == kModProfileVanilla) return true;
        std::error_code ec;
        auto status = fs::symlink_status(profilePath(name), ec);
        return validName(name) && !ec && fs::is_directory(status);
    }

    std::vector<std::string> directories(const fs::path& path)
    {
        std::vector<std::string> names;
        std::error_code ec;
        fs::directory_iterator it(path, ec), end;
        while (!ec && it != end) {
            auto name = it->path().filename().string();
            auto status = it->symlink_status(ec);
            if (!ec && fs::is_directory(status) && name.compare(0, 5, ".tmp-") != 0) names.push_back(name);
            it.increment(ec);
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    bool saveNames()
    {
        std::string text;
        for (const auto& name : modProfileNames())
            text += name + "\n";
        return writeText(root / "profiles/profiles.txt", text);
    }

    bool copySettings(const fs::path& source, const fs::path& dest)
    {
        std::error_code ec;
        if (fs::exists(source / "ddraw.ini", ec)) {
            fs::copy_file(source / "ddraw.ini", dest / "ddraw.ini", ec);
            if (ec) return false;
        }
        if (ec) return false;
        if (!fs::exists(source / "mods", ec)) return !ec;
        fs::recursive_directory_iterator it(source / "mods", ec), end;
        while (!ec && it != end) {
            if (it->is_regular_file(ec) && it->path().extension() == ".ini") {
                auto target = dest / "mods" / it->path().lexically_relative(source / "mods");
                fs::create_directories(target.parent_path(), ec);
                if (ec) return false;
                fs::copy_file(it->path(), target, ec);
            }
            if (!ec) it.increment(ec);
        }
        return !ec;
    }
} // namespace

void modProfileSetRoot(const std::string& path)
{
    root = path;
    active = kModProfileVanilla;
}

std::vector<std::string> modProfileNames()
{
    auto names = directories(root / "profiles");
    names.erase(std::remove_if(names.begin(), names.end(), [](const std::string& name) {
        return name == kModProfileVanilla || !validName(name);
    }),
        names.end());
    names.insert(names.begin(), kModProfileVanilla);
    return names;
}

const std::string& modProfileActive()
{
    std::string text;
    if (readText(root / "profiles/active.txt", &text)) {
        auto end = text.find_last_not_of("\r\n");
        text = end == std::string::npos ? "" : text.substr(0, end + 1);
        active = profileExists(text) ? text : kModProfileVanilla;
    } else {
        active = kModProfileVanilla;
    }
    return active;
}

bool modProfileIsVanilla() { return modProfileActive() == kModProfileVanilla; }

std::string modProfileSavesRoot()
{
    if (!modProfileIsVanilla()) return profilePath(active).generic_string();
    const auto& path = settings.system.saves_root.empty() ? settings.system.master_patches_path : settings.system.saves_root;
    return (root / path).generic_string();
}

bool modProfileCreate(const std::string& name, bool copyModsFromActive)
{
    if (!validName(name) || profileExists(name)) return false;
    ModList list;
    fs::path source = modProfileIsVanilla() ? root : profilePath(active);
    if (copyModsFromActive && !modProfileLoadList(&list)) return false;
    std::error_code ec;
    auto dest = profilePath(name);
    if (!fs::create_directories(dest, ec) || ec) return false;
    fs::create_directory(dest / "SAVEGAME", ec);
    bool ok = !ec && writeText(dest / "modlist.txt", modListSerialize(list)) && copySettings(source, dest) && saveNames();
    if (!ok) fs::remove_all(dest, ec);
    return ok;
}

bool modProfileSetActive(const std::string& name)
{
    if (!profileExists(name)) return false;
    // Selecting Vanilla in an untouched install must not create profiles/.
    std::error_code ec;
    if (name == kModProfileVanilla && !fs::exists(root / "profiles", ec)) return !ec;
    if (!writeText(root / "profiles/active.txt", name + "\n")) return false;
    active = name;
    return true;
}

bool modProfileRename(const std::string& from, const std::string& to)
{
    if (from == kModProfileVanilla || from == modProfileActive() || !profileExists(from)
        || !validName(to) || profileExists(to)) return false;
    std::error_code ec;
    fs::rename(profilePath(from), profilePath(to), ec);
    if (ec) return false;
    if (saveNames()) return true;
    fs::rename(profilePath(to), profilePath(from), ec);
    return false;
}

bool modProfileDelete(const std::string& name)
{
    if (name == kModProfileVanilla || name == modProfileActive() || !profileExists(name)) return false;
    std::error_code ec;
    fs::remove_all(profilePath(name), ec);
    return !ec && saveNames();
}

bool modProfileLoadList(ModList* out)
{
    if (out == nullptr) return false;
    std::string text;
    if (!modProfileIsVanilla()) {
        return readText(profilePath(active) / "modlist.txt", &text) && modListParse(text, out);
    }
    auto path = root / "mods/mods_order.txt";
    if (!readText(path, &text)) {
        std::error_code ec;
        if (fs::exists(path, ec) || ec) return false;
        ModList list;
        fs::directory_iterator it(root / "mods", ec), end;
        while (!ec && it != end) {
            if (it->is_regular_file(ec) && it->path().extension() == ".dat") list.entries.push_back({ it->path().filename().string(), true });
            if (!ec) it.increment(ec);
        }
        std::sort(list.entries.begin(), list.entries.end(), [](const ModEntry& a, const ModEntry& b) { return a.id < b.id; });
        *out = std::move(list);
        return true;
    }
    std::istringstream input(text);
    std::string line, converted;
    while (std::getline(input, line)) {
        auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == ';') continue;
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
        bool disabled = line.front() == '#';
        auto id = line.substr(disabled ? 1 : 0);
        // Only #name.dat is a disabled entry; ordinary comments stay comments.
        if (disabled && fs::path(id).extension() != ".dat") continue;
        if (!disabled && line.find_first_of("#;") != std::string::npos) continue;
        converted += (disabled ? "-" : "+") + id + "\n";
    }
    return modListParse(converted, out);
}

bool modProfileSaveList(const ModList& list)
{
    ModList checked;
    if (!modListParse(modListSerialize(list), &checked) || checked.entries.size() != list.entries.size()) return false;
    if (!modProfileIsVanilla()) return writeText(profilePath(active) / "modlist.txt", modListSerialize(list));
    std::string text;
    for (const auto& entry : list.entries)
        text += (entry.enabled ? "" : "#") + entry.id + "\n";
    return writeText(root / "mods/mods_order.txt", text);
}

std::vector<std::string> modStoreIds() { return directories(root / "modstore"); }

} // namespace fallout
