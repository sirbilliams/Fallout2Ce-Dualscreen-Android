#ifndef FALLOUT_HD_PROFILE_HD_H_
#define FALLOUT_HD_PROFILE_HD_H_

// Per-profile HD settings. The art pack is installed once (hd/<N>x), but each
// non-Vanilla mod profile may carry its own HD on/off and lighting choice:
//
//   profiles/<name>/hd.ini
//     [hd]
//     enabled=1            0 | 1
//     [hd_light]
//     mode=new             new | original
//     quality=high         off | low | medium | high
//
// profiles/active.txt names the active profile. When it is a real profile, the
// keys present in its hd.ini replace the ones from fallout2.cfg (in memory
// only; they are never written back to fallout2.cfg). A missing or unreadable
// hd.ini, Vanilla, or an unknown profile means fallout2.cfg alone decides.
// hd_art_dir and every other key stay global.

#include <functional>
#include <string>
#include <vector>

namespace fallout::hd {

struct ProfileOverride {
    std::string section;
    std::string key;
    std::string value;
};

// Only the whitelisted keys above with valid values are returned.
std::vector<ProfileOverride> parseProfileHd(const std::string& text);

// Active profile under `root` ("" = working dir); empty for Vanilla, a missing
// active.txt, or a name without a profiles/<name> folder.
std::string profileActiveName(const std::string& root);

// Overrides of the active profile; empty when none apply.
std::vector<ProfileOverride> profileHdOverrides(const std::string& root);

// Calls set() for each override of the active profile; returns the count.
size_t profileHdApply(const std::string& root, const std::function<void(const ProfileOverride&)>& set);

} // namespace fallout::hd

#endif /* FALLOUT_HD_PROFILE_HD_H_ */
