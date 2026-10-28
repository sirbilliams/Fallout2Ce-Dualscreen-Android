# QOL stage 1: bottom-screen layouts (spec)

Dual-screen bottom screen (AYN Thor, 1240 x 1080 at density 2.6, about 477 x 415 dp).
Two new settings, each with the current look kept as the default.

## Settings

`[touch]` in `settings.h` / `settings.cc` (next to `dual_screen`), ints clamped 0..2:

| Key | 0 (default) | 1 | 2 |
|---|---|---|---|
| `dashboard_layout` | Classic (today) | Map first | Two columns |
| `screen_nav` | Rail (today, 52 dp) | Slim rail (40 dp) | Top bar |

Settings screen (`mui_preferences.cc`), section Display, right after the "Dual screen" toggle:
two `RowKind::Choice` rows with `optionsFromCe = true`, wired through `Values` like
`mapFilter` (get / set / defaults from `TouchSettings()` / apply). Texts in
`files/ce.dat/text/english/game/ce.msg` (fallbacks in code too):

- 920 "Bottom screen layout", 921 "Classic", 922 "Map first", 923 "Two columns"
- 924 "Screen navigation", 925 "Rail", 926 "Slim rail", 927 "Top bar"

Both only change the bottom screen (`ui.surface == MuiSurface::Bottom`). Phones and the top
screen keep today's layouts.

## Dashboard layouts (`hud_layout.cc`, `touch_hud.cc`)

`hudLayoutDashboard()` returns the table for `settings.touch.dashboard_layout`. The classic
table stays as is.

**Map first**: the live map gets the whole width between a top row and a bottom strip.

- Top row: Screens at TopLeft, System at TopRight (as classic).
- Bottom strip, BottomLeft: Tools then Log in one row (PartyOrders, Skills, Highlight, Sneak, Log).
- Bottom strip, BottomRight, framed (padding 6): line 1 Indicators (fill, 22); line 2 Status
  (fill, 24); line 3 Modes (fill, 44), SwapHands (52 x 44), Weapon (150 x 44).
- Combat group (combat only): Right edge, as classic (`avoidOthers`).
- Live map rect: left = safe left, right = combat group's left (or safe right), top = top row
  bottom, bottom = top of the higher of the two bottom groups; gaps as today (6 dp).

**Two columns**: map on the left, a weapon / status column on the right.

- Top row: Screens TopLeft, System TopRight.
- Tools: Left edge column (as classic), Log BottomLeft (as classic).
- Weapon group: Right edge, framed (padding 6), lines stacked: Indicators (fill, 22), Status
  (fill, 24), Modes (fill, 44), SwapHands (52 x 64) + Weapon (110 x 64). Width about 180 dp
  in total.
- Combat group: Bottom, `avoidOthers` (comes up between the left column and the weapon
  column only in combat).
- Live map rect: left = tools right, right = weapon group left, top = top row bottom,
  bottom = combat group top when shown, else safe bottom.

Live map rect computation in `touch_hud.cc` (the `ui.surface == MuiSurface::Bottom` block):
a switch on the layout, classic keeps today's code. In the new layouts, the live map doesn't
draw its own HP bar (Status shows HP); classic keeps it.

The log / notification area right of the log button must still fit: in map first it goes
between the bottom left group and the weapon group if there's at least 120 dp, otherwise
above the bottom strip, left aligned, over the map (as notifications are today).

## Screen navigation (`mui_game_dialog.cc`)

`muiDialogLayout(ui, &content, &tabs)` is used by every full screen. On the bottom screen:

- Rail: today (52 dp column at the right).
- Slim rail: same, 40 dp wide.
- Top bar: `tabs` is a row across the top of the safe rect, 44 dp high; `content` is the rest
  below it (10 dp gap). Full width for content.

`muiGameScreenTabs` and `muiDialogTabs` lay their square buttons out along the longer side of
the rect they get: a column when it's taller than wide (as now), a row when it's wider.
In a row, back sits at the left and the game screens follow it; button size = rect height.
Badges (inventory AP cost, Pip-Boy unavailable) work the same in both.

Screens that place things relative to the rail (character `buildTabs`, anything using
`tabs.x` for its own layout) must use `content` instead, so nothing overlaps in any mode.

## Fixes found in the first real shots (any setting)

- Character screen (bottom): stat names and values overlap (labels drawn at fixed offsets).
  Lay label and value out in the row's rect: label start-aligned, value end-aligned, label
  shrinks (`muiFitTextSize`) to the space left. Same for the derived stats column.
- Map screen (bottom): the map name tabs hide under "Details" / "Motion sensor"; the tab row
  gets the width left of those buttons (scroll or shrink names).
- Inventory (bottom): the stats column is clipped ("67/1", "2/3"); fit or shrink the text to
  the column.

## Verification

- Desktop build: `cmake --build build-desktop --target fallout2-ce`.
- Screenshots: `~/Projects/f2ds-run`, autotest scenario `dualscreen` (bottom frames in
  `<step>_bottom.png`), run once per combination worth checking (all 3 dashboard layouts with
  rail; all 3 nav modes with the classic dashboard), by editing `[touch]` in that folder's
  `fallout2.cfg`.
- Nothing overlaps or is clipped in the captures; the classic + rail captures look like
  before apart from the three fixes.
