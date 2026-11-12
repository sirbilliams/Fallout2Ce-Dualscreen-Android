#include "mod_list.h"

#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace fallout {

bool modListParse(const std::string& text, ModList* out)
{
    if (out == nullptr) return false;
    ModList parsed;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
        if (line.size() < 2 || (line[0] != '+' && line[0] != '-')) return false;
        std::string id = line.substr(1);
        if (id == "." || id == ".." || id.find_first_of("/\\\r\n\t:#;") != std::string::npos
            || id.front() == ' ' || id.back() == ' ' || id.find('\0') != std::string::npos) return false;
        parsed.entries.push_back({ id, line[0] == '+' });
    }
    *out = std::move(parsed);
    return true;
}

std::string modListSerialize(const ModList& list)
{
    std::string text;
    for (const auto& entry : list.entries)
        text += (entry.enabled ? "+" : "-") + entry.id + "\n";
    return text;
}

void modListMove(ModList* list, size_t from, size_t to)
{
    if (list == nullptr || from >= list->entries.size() || to >= list->entries.size() || from == to) return;
    auto entry = list->entries[from];
    list->entries.erase(list->entries.begin() + from);
    list->entries.insert(list->entries.begin() + to, std::move(entry));
}

std::string modListUniqueId(const ModList& store, const std::string& wanted)
{
    std::string base;
    for (unsigned char ch : wanted) {
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        base += ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-') ? ch : '-';
    }
    if (base.empty() || base == "." || base == "..") base = "mod";
    std::unordered_set<std::string> ids;
    for (const auto& entry : store.entries)
        ids.insert(entry.id);
    std::string id = base;
    for (size_t suffix = 2; ids.count(id); ++suffix)
        id = base + "-" + std::to_string(suffix);
    return id;
}

std::vector<std::string> modListEnabledIds(const ModList& list)
{
    std::vector<std::string> ids;
    for (const auto& entry : list.entries)
        if (entry.enabled) ids.push_back(entry.id);
    return ids;
}

} // namespace fallout
