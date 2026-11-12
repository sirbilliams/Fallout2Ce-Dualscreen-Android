#include "hd/profile_hd.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fallout::hd {

namespace fs = std::filesystem;

namespace {

    std::string trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
    }

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool valueOk(const std::string& section, const std::string& key, const std::string& v)
    {
        if (section == "hd" && key == "enabled") return v == "0" || v == "1";
        if (section == "hd_light" && key == "mode") return v == "new" || v == "original";
        if (section == "hd_light" && key == "quality") return v == "off" || v == "low" || v == "medium" || v == "high";
        return false;
    }

    bool readText(const fs::path& path, std::string* text)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        text->assign(std::istreambuf_iterator<char>(in), {});
        return true;
    }

} // namespace

std::vector<ProfileOverride> parseProfileHd(const std::string& text)
{
    std::vector<ProfileOverride> out;
    std::istringstream in(text);
    std::string line, section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[') {
            size_t end = line.find(']');
            section = end == std::string::npos ? "" : lower(trim(line.substr(1, end - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        ProfileOverride o { section, lower(trim(line.substr(0, eq))), lower(trim(line.substr(eq + 1))) };
        if (!valueOk(o.section, o.key, o.value)) continue;
        // The last assignment wins.
        auto it = std::find_if(out.begin(), out.end(), [&](const ProfileOverride& e) { return e.section == o.section && e.key == o.key; });
        if (it != out.end()) {
            it->value = o.value;
        } else {
            out.push_back(std::move(o));
        }
    }
    return out;
}

std::string profileActiveName(const std::string& root)
{
    std::string text;
    if (!readText(fs::path(root) / "profiles/active.txt", &text)) return "";
    std::string name = trim(text.substr(0, text.find_first_of("\r\n")));
    if (name.empty() || name == "Vanilla" || name == "." || name == ".."
        || name.find_first_of("/\\") != std::string::npos) return "";
    std::error_code ec;
    if (!fs::is_directory(fs::path(root) / "profiles" / name, ec)) return "";
    return name;
}

std::vector<ProfileOverride> profileHdOverrides(const std::string& root)
{
    std::string name = profileActiveName(root);
    std::string text;
    if (name.empty() || !readText(fs::path(root) / "profiles" / name / "hd.ini", &text)) return {};
    return parseProfileHd(text);
}

size_t profileHdApply(const std::string& root, const std::function<void(const ProfileOverride&)>& set)
{
    auto list = profileHdOverrides(root);
    for (const auto& o : list) set(o);
    return list.size();
}

} // namespace fallout::hd
