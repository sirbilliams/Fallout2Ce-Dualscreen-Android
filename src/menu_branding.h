#ifndef FALLOUT_MENU_BRANDING_H_
#define FALLOUT_MENU_BRANDING_H_

#include <string>

namespace fallout {

// CE: Total conversions (Sonora) may ship a misc.msg / ce.msg without the main
// menu entries, or with empty ones. Text that is missing or empty falls back to
// the built-in English one, so the buttons never come out blank.
inline const char* menuTextOrDefault(const char* text, const char* fallback)
{
    return (text != nullptr && text[0] != '\0') ? text : fallback;
}

// Built-in English labels of the classic menu buttons, by their misc.msg
// number (9 Intro ... 14 Exit); nullptr for other numbers.
inline const char* mainMenuDefaultLabel(int messageNumber)
{
    switch (messageNumber) {
    case 9:
        return "Intro";
    case 10:
        return "New Game";
    case 11:
        return "Load Game";
    case 12:
        return "Options";
    case 13:
        return "Credits";
    case 14:
        return "Exit";
    }
    return nullptr;
}

// The title shown on the bottom screen outside a game. [catalogTitle] is the
// running game's catalog title ("" for the base game, index 0). A total
// conversion started in place of Fallout 2 (no catalog entry) still names itself
// through its custom version string; the stock "FALLOUT II 1.02" one means
// Fallout 2.
inline std::string menuBrandingTitle(const std::string& catalogTitle, const std::string& versionString)
{
    if (!catalogTitle.empty()) return catalogTitle;
    if (!versionString.empty() && versionString.find("FALLOUT II") == std::string::npos
        && versionString.find("Fallout 2") == std::string::npos && versionString.find("Fallout II") == std::string::npos) {
        return versionString;
    }
    return "Fallout 2";
}

} // namespace fallout

#endif
