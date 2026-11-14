#include "menu_branding.h"
#include <cstring>
#include <iostream>
#define CHECK(x)                                \
    do {                                        \
        if (!(x)) {                             \
            std::cerr << "failed: " #x << '\n'; \
            return 1;                           \
        }                                       \
    } while (0)
int main()
{
    using namespace fallout;
    CHECK(!strcmp(menuTextOrDefault(nullptr, "New game"), "New game"));
    CHECK(!strcmp(menuTextOrDefault("", "New game"), "New game"));
    CHECK(!strcmp(menuTextOrDefault("Nueva", "New game"), "Nueva"));
    CHECK(!strcmp(mainMenuDefaultLabel(9), "Intro"));
    CHECK(!strcmp(mainMenuDefaultLabel(14), "Exit"));
    CHECK(mainMenuDefaultLabel(-1) == nullptr && mainMenuDefaultLabel(15) == nullptr);
    CHECK(menuBrandingTitle("Fallout: Sonora", "FALLOUT II 1.02") == "Fallout: Sonora");
    CHECK(menuBrandingTitle("", "") == "Fallout 2");
    CHECK(menuBrandingTitle("", "FALLOUT II 1.02") == "Fallout 2");
    CHECK(menuBrandingTitle("", "Fallout: Sonora 1.5") == "Fallout: Sonora 1.5");
    return 0;
}
