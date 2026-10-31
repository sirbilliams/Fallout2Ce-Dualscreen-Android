# Fallout 2 CE Mobile + Dual Screen (unofficial)

[Русская версия](README.ru.md)

A fork of [Fallout 2: Community Engine](https://github.com/fallout2-ce/fallout2-ce) made for Android phones: every screen of the game rebuilt for a touch screen, in landscape, plus a **bottom-screen companion for dual-screen handhelds like the AYN Thor**. **Version 2.0** ([download](https://github.com/sirbilliams/Fallout2Ce-Dualscreen-Android/releases/tag/v2.0.0), [release notes](docs/release-notes/v2.0.0.md)).

This is an **unofficial, fan-made, modified version** of Fallout 2 CE. It is not affiliated with or endorsed by Bethesda Softworks, ZeniMax Media or Interplay. Fallout is a trademark of its owners. The app contains **no game data**: you need your own copy of Fallout 2 ([GOG](https://www.gog.com/game/fallout_2), [Steam](https://store.steampowered.com/app/38410), [Epic Games](https://store.epicgames.com/p/fallout-2)).

## Status

**Status: v2.0, fully playable.**
- The whole game plays on the Fallout 2 CE engine, with a touch UI for every screen.
- Bottom-screen companion on dual-screen devices: live map, HUD, inventory, character, Pip-Boy, dialogue and barter.
- Pip-Boy 2000 theme by default (Classic in Settings → Display).
- Mouse controls: cursor at the finger, a bottom-screen TOUCHPAD, a right-stick cursor.
- Full controller support, with every button remappable in Settings → Controls.

## Screenshots

<p><img src="docs/screenshots/v2-exploring-klamath.jpg" alt="Exploring Klamath on both screens (2.0)" width="49%"> <img src="docs/screenshots/v2-inventory.jpg" alt="Inventory on the bottom screen (2.0)" width="49%"></p>

<p><img src="docs/screenshots/v2-pipboy-quests.jpg" alt="Pip-Boy 2000 quests (2.0)" width="49%"> <img src="docs/screenshots/v2-world-map.jpg" alt="World map travel on the bottom screen (2.0)" width="49%"></p>

<p><img src="docs/screenshots/thor-dialogue.jpg" alt="Arroyo Elder dialogue on both screens" width="49%"> <img src="docs/screenshots/thor-barter.jpg" alt="Barter on both screens" width="49%"></p>

<p><img src="docs/screenshots/thor-combat.jpg" alt="Temple combat and bottom console" width="49%"> <img src="docs/screenshots/live-map-columns.jpg" alt="Live map in columns" width="49%"></p>

<p><img src="docs/screenshots/live-map-map-first.jpg" alt="Map first layout" width="49%"> <img src="docs/screenshots/live-map-slim.jpg" alt="Live map with slim navigation" width="49%"></p>

<p><img src="docs/screenshots/inventory-slim-actions.jpg" alt="Slim inventory item actions" width="49%"> <img src="docs/screenshots/fallout-theme.jpg" alt="Fallout theme dashboard" width="49%"></p>

## Pip-Boy 2000 theme

The bottom screen uses the Pip-Boy 2000 theme by default on first launch; switch back to Classic in Settings. It has a phosphor CRT look drawn from the game's own art.

The map got an upgrade too:
- Pinch and drag to zoom and pan, with a recenter button.
- A legend, and elevation switching.
- Your zoom level is remembered.
- A town area list to jump between a town's maps.

<p><img src="docs/screenshots/pipboy2000-quests.jpg" alt="Pip-Boy 2000 quests" width="32%"> <img src="docs/screenshots/pipboy2000-map.jpg" alt="Pip-Boy 2000 map with area list" width="32%"> <img src="docs/screenshots/pipboy2000-data.jpg" alt="Pip-Boy 2000 holodisk data" width="32%"></p>

## Dual screen (AYN Thor)

On a two-screen device the world gets the whole top screen and the bottom screen becomes a live console: the HUD, a live map that follows you, and the game's screens (inventory, character, Pip-Boy, map) open there while the world waits on top. Settings → Display has the **bottom screen layout** (Classic, Map first, Two columns) and **screen navigation** (Rail, Slim rail, Top bar).

Details: [docs/dualscreen.md](docs/dualscreen.md).

## Controls

- **Touch:** tap to act, long press for the action menu, drag to move items, two fingers to zoom the map. Optionally the cursor follows your finger on the top screen (tap = click, hold = right click).
- **Bottom-screen TOUCHPAD** (dashboard tool column): drag to move the cursor, tap to click, two-finger tap to right click, hold then drag to carry.
- **Controller:** right stick moves a cursor (A / R3 click, B / R3 held right click), left stick and d-pad move focus on screens, and every button has a map action (inventory, Pip-Boy, sneak, quick use...).
- **Remapping:** Settings → Controls has a picker for each of the 16 buttons and a reset. Stored in `[touch] gamepad_bindings`.

Details: [docs/mouse-controls.md](docs/mouse-controls.md), [docs/gamepad-stage3.md](docs/gamepad-stage3.md).

## What is changed (modified software notice)

On top of CE (its engine, fixes and sfall compatibility are kept and updated from upstream):

- **Touch UI.** The game's screens are replaced with screens made for fingers and the phone's resolution: dialogue, barter, inventory, loot, character and level-up, character creation, Pip-Boy (quests, holodisks with their voice, maps, videos, rest), automap, world map and town maps, skills, game menu, settings, save/load, main menu, dialog boxes, called shots. Tap to act, long press for the action menu, drag to move items, two fingers to zoom the map; mouse and controller controls are optional (see [Controls](#controls)).
- **HUD.** Buttons anchored to the screen's edges (sized in dp), the attack modes with reload, the message log, end turn/combat.
- **Tactical view in combat** (a button, can be turned off in the settings): figures turn see-through, the hexes you can reach and everyone's hex are drawn on the ground, so a tile behind an enemy can be picked and an enemy chosen by its hex.
- **Mod features built in** where the mods can't run on CE: Inventory Filter's filters and party tabs in loot and barter.
- **Saves.** A save list sorted by when saves were made, quick saves that never overwrite manual ones (their number is a setting), "Continue" in the main menu, saves interrupted midway are restored, optional marks for saves made with other game files or mods.
- **Android app.** The game's files are imported from a folder or any archive (.zip, .7z, .rar, .tar...) copied from a computer, with RPU and other mods and the saves; "Replace game" for new builds and mod updates; export and import of saves.
- **Dual screen.** The bottom-screen companion above (`[touch] dual_screen`, `dashboard_layout`, `screen_nav`). Dialogue and barter use the bottom screen too: head and reply on top, large numbered options (and the Barter / End shortcuts) below; barter gets the whole bottom screen with the head on top. Settings: `dialogue_layout` (0 top, 1 split, 2 split with the options mirrored dimmed on top), `dialogue_option_size`, `barter_layout` (0 top, 1 bottom), `dialogue_tap_skip`, `barter_confirm`. The layouts only apply while the bottom screen is in use.
- **The port's own improvements** are settings (`[enhancements]` in `fallout2.cfg`).

The full list of changes is the git history over upstream.

## Install on Android

1. Download the APK ([v2.0](https://github.com/sirbilliams/Fallout2Ce-Dualscreen-Android/releases/tag/v2.0.0), `Fallout2-Dualscreen.apk`; the newest development build is always at [latest](https://github.com/sirbilliams/Fallout2Ce-Dualscreen-Android/releases/tag/latest)) and install it. Updates install over it and keep your saves.
2. Copy your Fallout 2 folder from a computer to the phone: as is, or as one archive (cable, cloud drive, messenger). Mods installed there (for example [RPU](https://github.com/BGforgeNet/Fallout2_Restoration_Project)) and saves come along.
3. Start the app and choose that folder or archive.

Tested on a Redmi 15C (Android 16); the dual-screen companion is made for the AYN Thor. Android 7.0 or newer, arm64.

**Recommended setup: Fallout 2 + [Restoration Project (RPU)](https://github.com/BGforgeNet/Fallout2_Restoration_Project) 2.4.34.** The port is played and tested with it. RPU relies on sfall, which this engine reimplements: Fallout 2: Community Engine, the continuation of the original Fallout 2 Community Edition. Install RPU into the game folder on a computer, then copy the folder to the phone.

## Known limitations

- Landscape only; portrait is planned.
- Android only: no desktop, macOS or iOS builds are published. Desktop builds from source keep CE's original windows (`[touch] mobile_ui=0`) and are only used for development and tests.
- Mods with their own script windows are not covered by the touch UI (none of the popular ones use them).

## Building

Android: JDK 17, Android SDK with NDK 27.3.13750724.

```
cd os/android
./gradlew assembleRelease -PabiFilters=arm64-v8a
```

A release build is signed with your own key: point the Gradle property `fallout2ce.releaseKeystoreProperties` to a properties file with `storeFile`, `storePassword`, `keyAlias`, `keyPassword` (never commit it). Desktop builds are as upstream's (CMake).

## License

The engine is under the [Sustainable Use License](LICENSE.md), as upstream: free, non-commercial distribution only, with the license and notices kept. Third-party components: SDL 2, zlib, LodePNG (zlib license), libarchive (BSD), liblzma from XZ Utils (0BSD), stb (MIT / public domain), fpattern, PT Mono font (SIL OFL 1.1). The app shows all of them in Settings → About.

Credits: Alexander Batalov and the Fallout 2 CE contributors for the engine; Ocherk for the controller A / R3 world-click fix and its touch gesture tests; the sfall, RPU and Inventory Filter authors, whose features are reimplemented here by their behavior (no code taken).


Engine documentation (configuration options, sfall compatibility, desktop platforms): see the upstream [Fallout 2: Community Engine README](https://github.com/fallout2-ce/fallout2-ce#readme).
