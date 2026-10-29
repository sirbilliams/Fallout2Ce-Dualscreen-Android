# Saves lane: items 7–9

Implemented without committing game data or creating a commit.

* Save manager now opens on the bottom display. Rows use the original SAVE.DAT thumbnails and physical slot numbers; details show location, in-game date/time, real creation time, and level. Level is stored in ce.sav for new saves; older/Steam saves without this optional metadata show `?` rather than an invented level. Quicksave and Quickload are large buttons. Save (including overwrite), load, and delete ask for confirmation on the same display. Returning from Android import refreshes the visible rows.
* Import opens the existing Android `ImportActivity` with `saves_import`. Its folder/archive sources accept a SAVEGAME folder, bare SLOTnn folders, or a whole Steam installation. `GameSaves` copies every file inside each slot, including sfallgv.sav and nested map/prototype files. Add remaps collisions into free slots; Replace all has its existing explicit confirmation. SLOT1000 is excluded from automatic import placement. The desktop build shows an explanatory dialog because the SAF picker belongs to Android.
* Loot retains the engine's Take all, weight/carry-weight checks and stealing restrictions. Each side now has a persistent item name/weight/value preview; touching or dragging updates it. Buttons use the Fallout skin. Transfers of stacks reuse `muiQuantitySelect` through the existing inventory transfer function.
* `[enhancements] auto_quicksave_on_suspend` defaults to true and has a Game settings toggle. Focus loss, minimization (lid) and SDL app background events pause tickers; foreground events resume. Safe suspends write a transactional save to SLOT1000, outside manual/rotating allocations. An explicit ce.sav ownership flag distinguishes automatic saves, even if a manual save has the same description. A pre-existing save there is moved to a free manual slot first; if it cannot be preserved, saving is skipped.

## Safety condition

There was no general `canSave` predicate in `lsgSaveGame`: manual saves are reached through the game's input loops. `_combat_turn_run` in combat.cc waits while `_combat_turn_running > 0` before ordinary input. `combatSaveIsSafe` exposes that same counter check and requires the player's turn when combat is active. Suspend additionally requires normal game state, no dialogue/barter/modal GameMode flags, enabled game UI, no player animation, a loaded map, and no load in progress. Unsafe suspends pause without saving; they do not queue a surprise save after resuming. Save failures are silent and keep the previous transactional slot.

## Validation

Desktop Debug target `fallout2-ce` builds with the existing vendored dependency sources. The host already provides the build tools; distrobox itself is unavailable in this sandbox.

Run the rig serially (it shares mutable game state):

```
LOADARGS=--dev-new-game .rig/run.sh saves-reviewed saves-bottom mobile_ui=1 dual_screen=1 bottom_theme=0
LOADARGS=--dev-new-game .rig/run.sh loot-proof2 loot-bottom mobile_ui=1 dual_screen=1 bottom_theme=0
LOADARGS=--dev-new-game .rig/run.sh suspend-verified suspend-bottom mobile_ui=1 dual_screen=1 bottom_theme=0
```

`saves-bottom` covers save/load/delete confirmations and cancellation, confirmed overwrite/load/delete, level/thumbnail assertions, and the import entry. `loot-bottom` asserts that the shared quantity picker exists, moves two from three, checks one remains, and checks Take all empties the target. `suspend-bottom` injects real SDL background/foreground and minimized/focus events, checks paused game time, verifies the dedicated slot's creation order, and rejects dialogue/barter modes, an active combat animation counter, disabled setting, and the open save manager. It also proves a foreign save in SLOT1000 is preserved, even when named Automatic suspend.

`tests/suspend_slot_test.cc` passes through `cmake --build build-desktop --target suspend_slot_test` and `ctest --test-dir build-desktop -R suspend_slot_test --output-on-failure` (under timeout). It can also be compiled standalone with src/save_catalog.cc, src/save_records.cc and src/save_storage.cc (`-std=c++17 -Isrc`). It checks reserved slots across manual allocation, rotating quicksaves and quick-range changes.

Java `GameSavesTest` + `GameFilesTest`: 22 passing tests, including folder and zip imports preserving sfallgv.sav/nested maps and avoiding overwrites. Compiled against the locally installed Android 23 jar and JUnit 4.12, run with JUnitCore. Android's native picker/activity was not exercised on hardware here; its desktop dialog screenshot proves the entry only.

Screenshots remain in `.rig/shots/` (untracked). No original game files were copied. The rig's maps/proto directory links were replaced with local directories containing links to read-only source assets, so generated SAV/automap files stay in the writable rig.

Inspected Fallout shots: `.rig/shots/suspend-verified/u06_dedicated_slot_bottom.png`, `.rig/shots/saves-reviewed/s19_overwrite_confirm_bottom.png`, `.rig/shots/loot-proof2/l09_open_loot_bottom.png`, `.rig/shots/loot-proof2/l09c_quantity_bottom.png`. Classic flow also finishes without FAIL or missing widgets in `.rig/shots/saves-classic/log.txt`; its manager screenshot was inspected.
