# Mod Organizer step 2 (mod list, add from zip, profiles) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A MODS button on the main menu opens a screen where the player switches mods on/off, orders them, adds mods from a zip, and switches between profiles with their own mod list and saves.

**Architecture:** Installed mods live once in a shared store (`modstore/<id>/`). Each profile is a folder (`profiles/<name>/`) with its mod list, its saves and its mod ini files. At start the engine mounts the active profile's enabled mods in order through the existing `dbOpen()` chain (DAT, ZIP or loose folder; last opened wins). Any change restarts the game through the existing finish + `System.exit` + relaunch path, which lands on the main menu. The "Vanilla" profile is the current layout (`mods/`, `data/SAVEGAME`, `ddraw.ini`) and is never moved.

**Tech Stack:** C++17 engine (src/), mui UI, Android Java (os/android/app/src/main/java/com/alexbatalov/fallout2ce/), CMake + ctest (desktop), dev autotest scenarios.

**Spec:** docs/superpowers/specs/2026-11-11-mod-organizer-design.md

## Global Constraints
- Entry is a MODS button on the main menu only (mobile menu `src/mui_main_menu.cc` buttons[] and classic `mainMenuButtonSpecs`).
- Lower in the list wins file conflicts (opened later in `dbOpen` order).
- Nothing is copied over the original Fallout 2 files; disabling a mod fully undoes it.
- Each profile: own mod list + order, own saves, own mod settings files (ddraw.ini, mods ini files). Switching never touches another profile's saves.
- The current install becomes the "Vanilla" profile unchanged (no file moved, renamed or deleted).
- Save marks (none / ? / x) keep working per profile; loading a save made with different mods asks first.
- No third-party mod bundled in the APK or repo. Test fixtures are mods we create ourselves.
- Every test/build command runs under `timeout`. Commit locally on branch `mod-organizer`; no push.

## Review Focus
1. Mod zip with an extra top folder (`MyMod/data/...` or `MyMod/mods/x.dat`) - installs as one mod with the right root, not an empty mod. Test in Task 5.
2. Two mods with the same name - second gets a unique id (`name-2`), both listed. Test in Task 1.
3. Disabling a mod the current save uses - save shows x and loading asks first. Test in Task 3.
4. Deleting or renaming the active profile - not allowed for the active one; Vanilla can't be deleted. Test in Task 2.
5. App killed mid-install - no half mod in the store; temp folder cleaned on next start. Test in Task 5.

---

### Task 1: Mod list core (pure C++, no engine)

**Files:**
- Create: `src/mod_list.h`, `src/mod_list.cc`
- Create: `tests/mod_list_test.cc`
- Modify: `CMakeLists.txt` (add `mod_list.cc` to the game sources; `ce_add_test(mod_list_test tests/mod_list_test.cc src/mod_list.cc)` near :968-990)

**Interfaces:**
- Produces:
  - `struct ModEntry { std::string id; bool enabled; };`
  - `struct ModList { std::vector<ModEntry> entries; };` (order = load order, first = lowest priority)
  - `bool modListParse(const std::string& text, ModList* out);` format: one line per mod, `+id` enabled / `-id` disabled, `#` comments, blank lines skipped.
  - `std::string modListSerialize(const ModList& list);`
  - `void modListMove(ModList* list, size_t from, size_t to);`
  - `std::string modListUniqueId(const ModList& store, const std::string& wanted);` lowercases, replaces chars outside `[a-z0-9._-]` with `-`, appends `-2`, `-3`... on clash.
  - `std::vector<std::string> modListEnabledIds(const ModList& list);`

- [ ] **Step 1:** Write `tests/mod_list_test.cc`: round trip `"+rpu\n-npc_armor\n"` -> parse -> serialize equals input; comments/blank lines skipped; `modListMove` 0->2 on [a,b,c] gives [b,c,a]; `modListUniqueId` of "My Mod!" with existing "my-mod-" gives "my-mod--2"; `modListEnabledIds` of +a -b +c is [a,c].
- [ ] **Step 2:** Build target and run: `timeout 300 cmake --build <b> --target mod_list_test && timeout 60 ctest --test-dir <b> -R mod_list` -> FAIL (missing symbols).
- [ ] **Step 3:** Implement the five functions in `src/mod_list.cc`.
- [ ] **Step 4:** Rerun -> PASS; all 6 ctest tests pass.
- [ ] **Step 5:** Commit `Mods: mod list format and ordering`.

### Task 2: Profiles and per-profile save root

**Files:**
- Create: `src/mod_profiles.h`, `src/mod_profiles.cc`
- Modify: `src/settings.h:78-82`, `src/settings.cc:251-255` (new `[system] saves_root`, default empty = `master_patches_path`)
- Modify: `src/loadsave.cc:555,568,646,668,739,1330,2803,2909` (SAVEGAME paths use `modProfileSavesRoot()` instead of `_patches`/`master_patches_path`)
- Modify: `src/loadsave.cc:541-551` (`saves.txt` per profile: Vanilla keeps the current path, others `profiles/<name>/saves.txt` under `portDataPath`)
- Test: extend `tests/mod_list_test.cc` (path functions are pure given a root string)

**Interfaces:**
- Consumes: `ModList`, `modListParse/Serialize` (Task 1).
- Produces:
  - Disk layout (relative to the game working dir): `modstore/<id>/` (one installed mod), `profiles/profiles.txt` (one name per line), `profiles/active.txt` (active name; missing = "Vanilla"), `profiles/<name>/modlist.txt`, `profiles/<name>/SAVEGAME/`, `profiles/<name>/ddraw.ini`, `profiles/<name>/mods/*.ini`.
  - `const std::string& modProfileActive();`
  - `bool modProfileIsVanilla();`
  - `std::string modProfileSavesRoot();` Vanilla -> `master_patches_path` (today's `data`); others -> `profiles/<name>`.
  - `bool modProfileCreate(const std::string& name, bool copyModsFromActive);` (new profile starts with an empty SAVEGAME)
  - `bool modProfileSetActive(const std::string& name);`
  - `bool modProfileRename(const std::string& from, const std::string& to);` refuses Vanilla and the active profile.
  - `bool modProfileDelete(const std::string& name);` refuses Vanilla and the active profile; the UI asks first and offers Export saves.
  - `bool modProfileLoadList(ModList* out);` / `bool modProfileSaveList(const ModList& list);` Vanilla's list is built from today's `mods/mods_order.txt` (`+name.dat` per line) and saved back there in the same format, so Vanilla stays readable by `sfallLoadMods()`.

- [ ] **Step 1:** Tests: `modProfileSavesRoot()` is `data` with no active.txt and `profiles/RPU run` after `modProfileSetActive("RPU run")`; rename/delete of Vanilla and of the active profile return false; a temp dir with an existing `mods/mods_order.txt` of `a.dat\nb.dat` loads as [+a.dat,+b.dat].
- [ ] **Step 2:** Run -> FAIL.
- [ ] **Step 3:** Implement `mod_profiles.cc`; add `saves_root` setting; route loadsave.cc save paths through `modProfileSavesRoot()`.
- [ ] **Step 4:** Run ctest -> PASS. Run the existing autotest `--dev-autotest-scenario=gamefiles` and a save/load scenario with no `profiles/` dir -> same results as main (Vanilla unchanged).
- [ ] **Step 5:** Commit `Mods: profiles with their own saves`.

### Task 3: Mount the active profile's mods

**Files:**
- Modify: `src/game.cc:1394-1470` (`gameDbInit`: Vanilla calls `sfallLoadMods()` as today; other profiles call `modProfileMount()` instead)
- Modify: `src/mod_profiles.cc` (add `modProfileMount`)
- Modify: `src/save_compatibility.cc:24,60-65` (add `void saveCompatibilityReset();`; count loose-folder mods: hash of sorted relative file paths + sizes; skip `ce.dat` as today)
- Modify: sfall/ddraw ini loading so a non-Vanilla profile reads `profiles/<name>/ddraw.ini` and `profiles/<name>/mods/*.ini` (find the ddraw.ini open in `src/sfall_config.cc`)
- Test: new autotest scenario `mods` in `src/dev_autotest.cc` (register in `devAutotestSetScenario` :2657)

**Interfaces:**
- Consumes: `modProfileLoadList`, `modListEnabledIds`.
- Produces: `void modProfileMount();` calls `dbOpen("modstore/<id>/<x>")` for each enabled id in order, where `<x>` is each `*.dat` in the mod's folder (alphabetical) and then its `data/` folder if present.

- [ ] **Step 1:** Fixture (created by the test setup, not committed game data): `modstore/fixture-a/data/text/english/game/misc.msg` overriding one line to `MODTEST A`, `fixture-b` overriding the same line to `MODTEST B`. Autotest asserts: profile "T" with `+fixture-a` -> line reads `MODTEST A`; `+fixture-a +fixture-b` -> `MODTEST B`; `-fixture-a -fixture-b` -> original text; a save made with `+fixture-a` then loaded with it disabled -> `saveCompatibilityCheck` returns the x/changed result.
- [ ] **Step 2:** Run `timeout 120 ./fallout2-ce --dev-autotest=<out> --dev-autotest-scenario=mods` in the f2ds-run copy -> FAIL.
- [ ] **Step 3:** Implement `modProfileMount`, the gameDbInit branch, the ini redirect and the compatibility changes.
- [ ] **Step 4:** Rerun -> PASS; ctest all pass; `doubleattack` and `gamefiles` scenarios still pass.
- [ ] **Step 5:** Commit `Mods: mount the active profile's mods in order`.

### Task 4: Mods screen and main menu button

**Files:**
- Create: `src/mui_mods.cc` (declare `void muiModsScreenRun();` in `src/mui.h` next to :407)
- Modify: `src/mainmenu.h:6-19` (`MAIN_MENU_MODS`), `src/mui_main_menu.cc:145,172-189` (MODS button `{ "mainmenu.mods", kTextMods, "Mods", MAIN_MENU_MODS }`; fix the `* 3.0f` layout height for the extra button), `src/mainmenu.cc:65-71,106` (classic menu row), `src/main.cc` switch near :242 (`mainMenuBeginSubscreen(); muiModsScreenRun(); mainMenuCancelSubscreen();`)
- Test: autotest scenario `modsui` tapping widget ids

**Interfaces:**
- Consumes: Task 2 profile functions, Task 1 list functions, `openGameFiles(const char*)` JNI helper (src/mui_preferences.cc:513) for restart and for "Add from zip" (mode `"mod"`, Task 5).
- Produces widget ids: `mods.profile` (picker: switch / New / Rename / Delete), `mods.row.<i>.toggle`, `mods.row.<i>.drag` (drag handle; long-press row and drag, plus `mods.row.<i>.up` / `.down` buttons for controller/precise use), `mods.add`, `mods.back`. Leaving with changes saves the list and restarts via the existing restart path (same as Replace game); leaving without changes just closes.

- [ ] **Step 1:** Autotest `modsui`: open main menu -> tap `mainmenu.mods` -> toggle row 0 -> tap `mods.row.1.up` -> `mods.back`; assert `profiles/T/modlist.txt` content and that a restart was requested (on desktop: a flag/log line instead of `openGameFiles`).
- [ ] **Step 2:** Run -> FAIL.
- [ ] **Step 3:** Implement the screen in the style of `mui_preferences.cc` Game files section; button text through the existing string tables (en + ru).
- [ ] **Step 4:** Rerun -> PASS; `mainmenu` scenario screenshots still fit (check the 4-button layout on the Thor top screen size).
- [ ] **Step 5:** Commit `Mods: MODS button and mods screen`.

### Task 5: Add from zip

**Files:**
- Modify: `os/android/app/src/main/java/com/alexbatalov/fallout2ce/MainActivity.java:209` (`openGameFiles("mod")`), `ImportActivity.java:157-171,367` (mode `mod`: `chooseArchive()` then install), `GameImport.java:253,434,458` (target = `modstore/<id>`; `find(source, true)`)
- Create: `os/android/app/src/main/java/com/alexbatalov/fallout2ce/ModInstall.java` (staging + root detection + checks)
- Modify: strings.xml (en, ru) for the messages below

**Interfaces:**
- Consumes: `NativeArchive.open`, `GameImport.find/plan/install`, `GameFiles` staged commit; `modListUniqueId` rules mirrored in Java (same regex, same `-2` suffix).
- Produces: `static Result ModInstall.install(Context, Uri zip, File gameDir, Progress)`; on success appends `+<id>` at the end of the active profile's list (Vanilla: the .dat into `mods/` + `mods_order.txt`, as Replace game does today).
- Rules: mod root = the shallowest folder containing `*.dat`, `data/` or `mods/`; skip `*.exe`, `ddraw.dll`, `*.dll`; unpack to `modstore/.tmp-<id>/` then rename; delete leftover `.tmp-*` on start; refuse when free space < 2x the archive's unpacked size; messages: "This zip has no Fallout 2 mod files", "Can't read this archive (.7z and .rar aren't supported)", "Not enough space: needs <N> MB free".

- [ ] **Step 1:** Instrumented/unit tests (JVM test under `os/android/app/src/test` if the project has one, else a desktop autotest via the native archive): zips `flat.zip` (data/ at root), `nested.zip` (MyMod/data/...), `dat.zip` (x.dat), `empty.zip` (readme only), `withexe.zip` (data/ + setup.exe) -> roots detected, empty refused with message, exe skipped and listed.
- [ ] **Step 2:** Run -> FAIL.
- [ ] **Step 3:** Implement `ModInstall` and the `mod` mode.
- [ ] **Step 4:** Rerun -> PASS; build APK `timeout 1200 ./gradlew --no-daemon assemblePhone` in the omtbuild box.
- [ ] **Step 5:** Commit `Mods: add a mod from a zip`.

### Task 6: Thor check and docs

**Files:**
- Modify: `docs/android-roadmap.md` (Mods section), README.md/README.ru.md controls section (one line: MODS button)

- [ ] **Step 1:** CI-signed APK installed over the existing app on the Thor (keeps saves). Check: Vanilla profile shows the same saves as before (count matches); create profile "Test", add a small self-made fixture mod zip from Downloads, enable it, see the change, disable it, see it gone; switch back to Vanilla, saves untouched (checksums of `data/SAVEGAME` before/after). Screenshots of both screens.
- [ ] **Step 2:** Commit `Mods: docs`.
