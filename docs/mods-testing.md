# Mod Organizer tests

Fixtures are self-made (no third-party mod). Plan: docs/superpowers/plans/2026-11-11-mod-organizer-step2.md.

## Set up a test game dir
```
cp -as ~/Projects/f2ds-run/. <run>/            # own copy, never the original
tools/mods-fixtures.sh <run>                   # modstore/fixture-a,b + profiles/T (active, +a +b)
```
fixture-a/-b override line 20 of `text/english/game/misc.msg` (main menu copyright line) with `MODTEST A` / `MODTEST B`
and carry `data/scripts/modtest.int` (counts as game logic for save compatibility). `CE_DAT_TOOL` = ce-dat-tool
(needed to copy the original misc.msg out of master.dat).

## Scenarios (src/dev_autotest.cc), run from `<run>` with `timeout`
```
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen timeout 240 <build>/fallout2-ce \
  --dev-autotest=$PWD/shots/mods --dev-new-game --dev-autotest-scenario=mods
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen timeout 240 <build>/fallout2-ce \
  --dev-autotest=$PWD/shots/modsui --dev-autotest-scenario=modsui      # title screen; re-run mods-fixtures.sh first
```
- `mods` (Task 3): +a -> `MODTEST A`; +a +b -> `MODTEST B` (and +b +a -> A); none -> original text; save made with +a,
  loaded with it disabled -> `saveCompatibilityCheck` is not kSame (kUnlikely, "x") with `kModRemoved fixture-a` listed.
  Needs `saveCompatibilityReset()` and `modProfileMount()`. Exit code 1 and a `FAIL` log line on any miss.
- `modsui` (Task 4): `mainmenu.mods` -> `mods.row.0.toggle` -> `mods.row.1.up` -> `mods.back`; asserts
  `profiles/T/modlist.txt` == `+fixture-b\n-fixture-a\n` and `muiModsRestartRequested()` (to be declared in mui.h by
  src/mui_mods.cc: true once Back left with changes; on desktop it replaces openGameFiles). Also checks `mods.profile`,
  `mods.add`, `mods.row.0.toggle`, `mods.row.1.drag` are present.

## Zip fixtures (Task 5)
`tools/mods-fixtures.sh --zips` regenerates `tests/fixtures/mods/`: `flat.zip` (data/ at root), `nested.zip`
(MyMod/data/...), `dat.zip` (mymod.dat), `empty.zip` (readme only), `withexe.zip` (data/ + setup.exe + ddraw.dll).
Expected: roots detected for flat/nested/dat, empty refused ("This zip has no Fallout 2 mod files"), exe/dll skipped and listed.
