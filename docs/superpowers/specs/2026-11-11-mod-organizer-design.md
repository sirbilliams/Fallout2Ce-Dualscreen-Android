# Mod Organizer - design (2026-11-11)

## Goal
Let the player install, switch on/off, order and group Fallout 2 mods on the device (like Mod Organizer 2 on PC), including RPU from its official GitHub and mods from Nexus, without ever overwriting the original game files.

## Approved decisions
- **Entry**: MODS button on the main menu only (next to New Game/Load Game/Options).
- **Reloading**: Leaving the Mods screen after changes reloads game files and returns to the main menu.
- **Conflict Resolution**: Lower in list wins file conflicts.
- **RPU**: Never bundled in the APK or repo (no license shown). Requires a new game.
- **Profiles**: Switching never touches other profiles' saves. The current install becomes the "Vanilla" profile unchanged.
- **Safety**: Existing save composition marks (none / ? / x, src/save_composition.cc) keep working per profile; loading a save made with different mods asks first.

## Components
- **Mod List**: On/off switch per mod; drag to reorder.
- **Add from zip**: File picker accessing Downloads.
- **Install RPU**: Downloads latest release zip from github.com/BGforgeNet/Fallout2_Restoration_Project releases (v2.4.34 zip is about 870 MB) with progress bar, resume, and Wi-Fi warning.
- **Browse Nexus**: In-app WebView of Nexus Mods Fallout 2 pages. User signs in themselves.
- **Search-path layer**: For loose-file mods (open question for the implementer: how CE's db layer in src/dfile.cc / db patches can mount per-mod folders in order).

## Data layout
- **Mod Storage**: Each mod in its own folder under an app-internal mods store.
- **File Handling**: Nothing copied over the original Fallout 2 files; disabling fully undoes.
- **Engine Support**: sfall-style mods/*.dat with load order file mods_order.txt (src/sfall_ext.cc).
- **Nexus Downloads**: Files saved to an internal Downloads folder (status: "Downloaded, not installed").
- **Profiles**: Each profile has its own mod list + order, own saves, and own mod settings files (ddraw.ini, mods ini files).

## Flows
- **Nexus Flow**: Tapping "Manual download" is caught by the app. When the user leaves the browser, the app asks "Install <mod name>?" for each new download (Install / Later / Delete).
- **Profile Switching**: Switching profiles updates the active mod list, order, saves, and settings files.

## Error handling
- **RPU Download**: Includes resume and Wi-Fi warning.
- **Save Loading**: Prompt if a save was made with different mods.
- **Bad archive**: corrupt zip, zip with no Fallout 2 files (no .dat, no data/ tree), or an unsupported format (.7z/.rar) -> clear message, nothing installed.
- **Low storage**: check free space before download/unpack (RPU needs ~2x 870 MB while unpacking); refuse with the needed amount.
- **Interrupted install**: unpack into a temp folder, move into the mods store only when complete.
- **Windows-only parts**: sfall ddraw.dll and installer .exe files in a mod are skipped (the engine has its own sfall support); listed as skipped.

## Testing
- **Autotests**: Existing headless dev autotest scenarios (src/dev_autotest.cc) for enable/disable/order and profile switching.
- **Fixtures**: A fixture mod zip.
- **Hardware**: Thor test of each step.

## Build order
1. Ant fix (done separately)
2. Mod list + add from zip + profiles
3. RPU button
4. Nexus browser

## Open questions for the implementer
- RPU release zip layout (files at root vs a data/ subfolder; which ddraw.ini/mods ini changes it expects).
- Whether Android WebView download interception works for Nexus's manual-download links (redirects to a CDN URL).

## Out of scope
- In-app mod list from Nexus API
- Mod auto-updates
- Bundling any third-party mod
[OS-RECEIPT id=f047cb01 tool=fl model=gemma4:12b rc=0 time=23.8s]
