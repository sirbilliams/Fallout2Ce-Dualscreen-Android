// Per-profile HD override merge (src/hd/profile_hd.cc).
#include "hd/profile_hd.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace fs = std::filesystem;
using namespace fallout::hd;

static int failures = 0;
#define CHECK(c)                                                            \
    do {                                                                    \
        if (!(c)) {                                                         \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                \
            failures++;                                                     \
        }                                                                   \
    } while (0)

using Cfg = std::map<std::string, std::string>; // "section.key" -> value

static void write(const fs::path& p, const std::string& t)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << t;
}

// Effective value: fallout2.cfg (global) then the profile's overrides.
static Cfg merged(Cfg global, const std::string& root)
{
    profileHdApply(root, [&](const ProfileOverride& o) { global[o.section + "." + o.key] = o.value; });
    return global;
}

int main()
{
    fs::path root = fs::temp_directory_path() / ("hd_profile_test_" + std::to_string(std::rand()));
    fs::remove_all(root);
    fs::create_directories(root);
    std::string r = root.string();

    // parse: whitelist, validation, case, last wins
    auto o = parseProfileHd("; c\n[HD]\nEnabled = 1\nhd_art_dir=evil\nenabled=0\n[hd_light]\nmode=ORIGINAL\nquality=ultra\nquality=low\nx=1\n");
    CHECK(o.size() == 3);
    CHECK(o.size() == 3 && o[0].key == "enabled" && o[0].value == "0");
    CHECK(o.size() == 3 && o[1].key == "mode" && o[1].value == "original");
    CHECK(o.size() == 3 && o[2].key == "quality" && o[2].value == "low");
    CHECK(parseProfileHd("[hd]\nenabled=2\n").empty());
    for (const char* v : { "half", "1x", "full" }) {
        auto r = parseProfileHd(std::string("[hd_light]\nresolution=") + v + "\n");
        CHECK(r.size() == 1 && r[0].key == "resolution" && r[0].value == v);
    }
    CHECK(parseProfileHd("[hd_light]\nresolution=quarter\n").empty());
    CHECK(parseProfileHd("enabled=1\n").empty()); // no section

    Cfg on { { "hd.enabled", "1" }, { "hd_light.quality", "high" }, { "hd.hd_art_dir", "hd/2x" } };
    Cfg off { { "hd.enabled", "0" } };

    // no profiles at all: global decides
    CHECK(merged(on, r) == on);

    // Vanilla / missing active
    write(root / "profiles/active.txt", "Vanilla\n");
    write(root / "profiles/P/hd.ini", "[hd]\nenabled=0\n");
    CHECK(profileActiveName(r).empty());
    CHECK(merged(on, r) == on);

    // active profile without hd.ini -> fall back to global
    write(root / "profiles/active.txt", "Q\r\n");
    fs::create_directories(root / "profiles/Q");
    CHECK(profileActiveName(r) == "Q");
    CHECK(merged(on, r) == on);
    CHECK(merged(off, r) == off);

    // profile on, global off
    write(root / "profiles/active.txt", "P\n");
    write(root / "profiles/P/hd.ini", "[hd]\nenabled=1\n[hd_light]\nquality=medium\nmode=new\n");
    Cfg m = merged(off, r);
    CHECK(m["hd.enabled"] == "1");
    CHECK(m["hd_light.quality"] == "medium");
    CHECK(m["hd_light.mode"] == "new");

    // profile off, global on; hd_art_dir and unlisted keys stay global
    write(root / "profiles/P/hd.ini", "[hd]\nenabled=0\n");
    m = merged(on, r);
    CHECK(m["hd.enabled"] == "0");
    CHECK(m["hd_light.quality"] == "high");
    CHECK(m["hd.hd_art_dir"] == "hd/2x");

    // unknown / unsafe active names
    write(root / "profiles/active.txt", "Nope\n");
    CHECK(merged(on, r) == on);
    write(root / "profiles/active.txt", "../P\n");
    CHECK(merged(on, r) == on);

    fs::remove_all(root);
    std::printf(failures == 0 ? "hd_profile_test OK\n" : "hd_profile_test FAILED\n");
    return failures == 0 ? 0 : 1;
}
