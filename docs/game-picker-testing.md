# Game picker step 1

The picker is the first screen: with the mobile UI the opening movies no
longer autoplay at boot (`skip_opening_movies` still applies without the
picker). Fallout 2's page has an Intro button beside Play that plays the
boot movies (iplogo, intro, credits; a tap or key skips each) and returns
to the same page. Pad: X plays it too. Catalog `introMovies` lists them per
game; other games list none until their data is known, so no button.
String id 419 in ce.msg.

The picker is the first screen, before the mui main menu. Desktop
`mobile_ui=0` retains the classic menu. All eight pages share a catalog;
only Fallout 2 can Play or open its existing Mod Organizer in this step.
No external pictures or downloads are included.

Selection lives in `profiles/last-game.txt` as a catalog ID, with Fallout 2
as the missing/invalid-file fallback. Browsing saves the selected page;
Play records Fallout 2 again. Existing profiles and game files stay in place.
Fallout 2 displays configured language and total master/critter archive MiB;
the data version is explicitly unknown. Other games have pending metadata.

## Checks

- `game_catalog_test`: intro movie lists (Fallout 2 boot order, others empty), catalog order, IDs, official HTTPS project URLs,
  download kinds, experimental flags, required fields, persistence round trip,
  missing/invalid state and failed writes.
- `--dev-autotest-scenario=gamepicker`: 1920x1080 top and simulated
  1240x1080 bottom. Asserts Play, Install, coming-soon notice, shoulder,
  both-screen swipes, d-pad, saved selection, Mods and A → main menu.
- `--dev-autotest-scenario=gamepickerintro`: needs the game's three opening
  movies (a copy of sonora-run plus F1 IPLOGO/INTRO under `data/art/cuts` was
  used; credits come from master.dat). Taps Intro, checks a decoded frame,
  skips each of the 3 movies by tap, asserts the picker (Intro and Play) is
  back, repeats via pad X, then A on Play reaches the main menu. Set
  `skip_opening_movies=0` to prove boot no longer plays them.
- `--dev-autotest-scenario=gamepickerphone`: same checks on one surface.
- Use a fresh output folder and begin with Fallout 2 selected. Both scripts
  visit all eight pages. `log.txt` records assertions and captures.
- The shared autotest logs a world-coordinate MISMATCH at the title screen:
  no map/dude tile is loaded there; picker checks use widgets and files.
- Older menu autotests now need to choose Play before their menu actions.

Run from a copied game directory (never the original fixture):

```sh
airun picker-dual timeout 180 env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=offscreen \
  /home/bill/Projects/f2ds-picker-build/fallout2-ce \
  --dev-autotest="$PWD/shots/picker" --dev-autotest-scenario=gamepicker
```

Android hooks are guarded by `__ANDROID__`: `installGame(id, officialUrl)`
and `openGameProjectPage(id, projectUrl)` call inert Java methods. Native mui
shows the localized coming-soon dialog. `addFallout1Files(id)` reserves the
future import contract. Mods on uninstalled games shows the same notice.

Offline Gradle validation in this sandbox is blocked: the normal Gradle
cache is read-only; a writable copy reaches daemon startup, where local
socket creation is forbidden. No APK or Java test success is claimed.
NDK arm64 syntax checking covers every changed C++ translation unit,
including JNI, separately from the blocked Gradle command.

Catalog data follows `~/Projects/f2ds-tc-research/research.md`. Additional
credit checks: [Olympus creators](https://www.nuclear-city.com/topic/1155-%D0%BE%D0%B1%D0%BD%D0%BE%D0%B2%D0%BB%D0%B5%D0%BD%D0%B8%D0%B5-%D0%BC%D0%BE%D0%B4%D0%B8%D1%84%D0%B8%D0%BA%D0%B0%D1%86%D0%B8%D0%B8-olympus-2207-%D0%B4%D0%BE-%D0%B2%D0%B5%D1%80%D1%81%D0%B8%D0%B8-12/),
[Yesterday creator](https://www.patreon.com/fallouty/about?l=en-GB).
