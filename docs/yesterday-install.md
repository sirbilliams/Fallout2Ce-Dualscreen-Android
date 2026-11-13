# Fallout: Yesterday installer

The picker downloads the public GitLab `falloutfans/fallouty` main archive into
`mod-downloads`, then atomically installs it into `games/yesterday`. It requires
player-owned `master.dat`, `critter.dat`, and `patch000.dat` in the shared Fallout 2
root. It does not copy or replace those base files.

The inspected archive was 4,481,987,989 bytes, SHA-256
`b4dd0742ba85b75cb5fde024b2c6045558046cc3f9a8dfacc135170c7cef5f87`.
Because main is a moving branch, those values are estimates/provenance, not a
required checksum. Installation opens the ZIP, requires `fallouty-main/` and the
expected configs, patch folder, patch002.dat and data tree, rejects duplicate
mapped paths/traversal, and checks extracted entry sizes and CRCs. Preflight
reserves archive size + 10.3 GB unpacked + 64 MiB. Extraction checks the actual
central-directory sizes against current free space again.

Files are lowercased for Android. Windows EXE/DLL/BAT files and `_tools/` are
excluded. `patch001.dat/`, `patch002.dat`, `data/`, `appearance/`, mod configs and
translations remain. Root `sfall.dat` becomes `mods/sfall.dat` with an explicit
`mods_order.txt` entry because CE does not load the Windows DLL's root archive.

`fallouty.cfg` supplies `fallout2.cfg`, with native relative base/patch settings
and slash-normalized paths. Both configs receive the normalized content;
`ddraw.ini` selects `fallout2.cfg`. Other ddraw settings remain intact. CE reads
`ConfigFile` in `game_config.cc`; `game_config_migration.cc` migrates `StartingMap`
and other supported content keys. CE already accepts a directory named
`patch001.dat` through `dbOpen` → `xbaseOpen`; `loose_patch_test` verifies mounting
and the priority of loose `data/` overrides, without an engine behavior change.

Git LFS pointer text is discarded from staging and each skipped path is logged
to stderr and `yesterday-install.log`. The inspected archive contains five media
pointers (TITLE/OVRRUN movies and three ambient MP3s); this installer uses the
explicit skip policy rather than fetching LFS. INTRO/MESAVERD movies are absent
from the public package too. A successful install does not prove gameplay
compatibility: opcode gaps, hero appearance support and other engine limitations
remain separate work. No Windows binary is executed.

Validation: eight synthetic Yesterday JUnit cases plus the existing installer
suite; host loose-patch and catalog tests. The inspected real ZIP's mapped layout
also passed validation (75,053 selected files, 9,521,794,852 bytes, 393 filtered
entries, five LFS pointers, no case collisions). Android Java sources compiled
against SDK 35 with generated resources and a BuildConfig.DEBUG fixture. Gradle
`:app:testDebugUnitTest` and `:app:assembleDebug` were attempted but sandbox socket
restrictions stop Gradle before tasks run; no APK or device test was completed.
