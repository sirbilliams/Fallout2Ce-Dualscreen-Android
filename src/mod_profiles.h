#ifndef FALLOUT_MOD_PROFILES_H_
#define FALLOUT_MOD_PROFILES_H_

#include <string>
#include <vector>

#include "mod_list.h"

namespace fallout {

// CE: Mod Organizer profiles. Layout relative to the game working dir:
//   modstore/<id>/                 one installed mod (shared by profiles)
//   profiles/profiles.txt          one profile name per line
//   profiles/active.txt            active name; missing = "Vanilla"
//   profiles/<name>/modlist.txt    ModList text
//   profiles/<name>/SAVEGAME/      the profile's saves
//   profiles/<name>/ddraw.ini, profiles/<name>/mods/*.ini
// "Vanilla" = today's layout (mods/ + mods/mods_order.txt, data/SAVEGAME,
// ddraw.ini), never moved.

extern const char* const kModProfileVanilla; // "Vanilla"

// Root dir everything above is relative to ("" = working dir). Tests set it.
void modProfileSetRoot(const std::string& root);

std::vector<std::string> modProfileNames(); // Vanilla first
const std::string& modProfileActive();
bool modProfileIsVanilla();
// Vanilla -> settings.system.master_patches_path; others -> "profiles/<name>".
std::string modProfileSavesRoot();
bool modProfileCreate(const std::string& name, bool copyModsFromActive);
bool modProfileSetActive(const std::string& name);
// Both refuse Vanilla and the active profile.
bool modProfileRename(const std::string& from, const std::string& to);
bool modProfileDelete(const std::string& name);
// Vanilla reads/writes mods/mods_order.txt ("name.dat" lines, all enabled;
// disabled ones written as "#name.dat").
bool modProfileLoadList(ModList* out);
bool modProfileSaveList(const ModList& list);
// Store contents (every modstore/<id> folder), alphabetical.
std::vector<std::string> modStoreIds();
// Mount the active profile's enabled mods in order via dbOpen():
// each modstore/<id>/*.dat (alphabetical) then modstore/<id>/data/ if present.
void modProfileMount();

} // namespace fallout

#endif /* FALLOUT_MOD_PROFILES_H_ */
