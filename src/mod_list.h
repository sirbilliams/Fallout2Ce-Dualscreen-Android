#ifndef FALLOUT_MOD_LIST_H_
#define FALLOUT_MOD_LIST_H_

#include <cstddef>
#include <string>
#include <vector>

namespace fallout {

// CE: Mod Organizer - one installed mod in a profile's list.
struct ModEntry {
    std::string id;
    bool enabled;
};

// Order = load order: first = lowest priority, last wins conflicts.
struct ModList {
    std::vector<ModEntry> entries;
};

// Text format: one line per mod, "+id" enabled, "-id" disabled; "#" comments
// and blank lines skipped.
bool modListParse(const std::string& text, ModList* out);
std::string modListSerialize(const ModList& list);
void modListMove(ModList* list, size_t from, size_t to);
// Lowercase, chars outside [a-z0-9._-] -> '-', then "-2", "-3"... on clash.
std::string modListUniqueId(const ModList& store, const std::string& wanted);
std::vector<std::string> modListEnabledIds(const ModList& list);

} // namespace fallout

#endif /* FALLOUT_MOD_LIST_H_ */
