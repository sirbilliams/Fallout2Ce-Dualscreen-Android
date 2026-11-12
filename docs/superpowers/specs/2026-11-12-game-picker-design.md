# Game picker — approved design (2026-11-12)

## Launch and navigation
- Show every launch after intro videos and before the main menu.
- Open on the last-played game; Play / controller A enters its main menu.
- Ordered pages: Fallout 2, Fallout 1 (Et Tu), Fallout: Sonora,
  Fallout 1.5: Resurrection, Fallout: Nevada, Olympus 2207,
  Fallout 2 Megamod, Fallout: Yesterday.
- Swipe either screen; stick, d-pad and shoulder buttons change pages.
- Animate page changes with a smooth slide; show page dots below.

## Presentation
- Thor top: 1920x1080; bottom: 1240x1080.
- Top: large logo, game art, credit “by <creator> - <project page>”.
- Bottom: installed status, version, size, language; large Play,
  Install / Open project page, and Mods buttons.
- Mark games unverified on this engine Experimental.
- Never bundle project pictures in the APK or repository.
- Download pictures from the creator’s GitHub/project page on installation,
  keep them with that game and credit their creator.
- Before install fetch a small preview from the same source;
  show a styled title offline. Fill gaps using the game’s own title art.

## Isolation and mods
- Each game owns its base files, saves, settings, Mod Organizer and profiles.
- Existing Vanilla/modpack profiles become Fallout 2’s profiles.
- Fallout 2 Mods retains Install RPU, Browse Nexus and Downloads.
- Switching games restarts the app, as profile switching does today.
- Main-menu Mods opens the current game’s Mod Organizer.

## Installation
- Background ModDownloads; official URLs only, Wi-Fi warning, space check.
- One tap: Et Tu (github.com/rotators/Fo1in2/releases),
  Sonora (github.com/NovaRain/Fallout-Sonora-English/releases),
  Resurrection (resurrection.cz/en/download/).
- Other games: Open project page in the in-app browser, generalising
  NexusActivity to any URL; Downloads collects the browser’s downloads.
- Et Tu needs user-owned Fallout 1 files: Add Fallout 1 files,
  following the Fallout 2 import flow.
- Catalog research: ~/Projects/f2ds-tc-research/research.md.
- Research sizes: Et Tu ~393 MB ZIP; Sonora ~1382 MB ZIP;
  Resurrection ~341 MB ZIP. Others unknown; Yesterday ~20 GB free space
  is an unverified research estimate. Resolve live sizes before installing.
- Et Tu archive has Fallout1in2/ wrapper; Sonora uses mods/*.dat and
  mods_order.txt; Resurrection extracts over a Fallout 2 base.

## Build sequence
1. Native mui picker; only Fallout 2 playable. Data-driven eight-game
   catalog, persisted selection beside profiles, original game-file art.
   Other pages use styled titles, Not installed and a Coming in the next
   update stub. Reserve Android install/project-page/import hooks.
   Keep desktop builds free of JNI. ce.msg IDs start at 398, with fallbacks.
2. Per-game folders and isolated profiles, saves and settings.
3. One-tap installers and downloaded pictures.
4. Et Tu and Sonora: PC validation, then Thor validation.
5. Remaining game pages and compatibility work.

## Step 1 acceptance
- Fallout 2 Play reaches main menu; Mods reaches existing organizer.
- Both screens navigate the same page, with slide animation and pad focus.
- Catalog unit test; desktop configure/build/ctest; Android offline phone
  unit tests and arm64 assemble; dev-autotest offscreen picker screenshots.
- Copy the desktop fixture before use; never modify f2ds-main-run.
- Local commits only; no push, adb, Thor or visible desktop windows.
