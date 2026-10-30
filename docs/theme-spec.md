# Bottom-screen theme: Fallout (spec)

Feedback: the bottom screen's vector icons and the modern monospace font look like a business app, not
Fallout. Direction: use the game's own interface art and fonts, read at run time from the player's own
install (`master.dat` / `patch000.dat`) through CE's art loader (`FrmImage`) and its interface font
loader (`font_manager.cc`). No game data is shipped or committed. Where the game has no art, draw in
the same style (pixel art, scratched metal, brass, amber / green CRT). Code: `src/mui_gameskin.{h,cc}`.

## Setting

`[touch] bottom_theme`: 0 = Fallout, 1 = classic (the old drawn-from-code look, unchanged), 2 = Pip-Boy 2000 (default on a fresh install).
Settings -> Display -> "Bottom screen theme" (ce.msg 960..962). The theme applies only while drawing the
bottom surface (`muiSkinFallout()` = bottom active, current surface Bottom, setting 0); the top screen
and phones keep the classic look. All stage 1 layout settings (`dashboard_layout`, `screen_nav`) work
unchanged: the theme only changes how things are drawn, never rects or logic.

## Inventory of the old look (what was replaced)

| Where | Old | Now (Fallout theme) |
|---|---|---|
| `mui_draw.cc` text (`muiDrawText`, `muiTextWidth`, `muiLineHeight`) | PT Mono TTF (stb_truetype) | game fonts, see Fonts |
| `mui_icons.cc` 70 line icons | vector strokes cached as textures | game art or pixel art, see Icons |
| `mui_skin.cc` plate / backdrop / well / button / label | generated noise metal, bevels, round rivets | game metal texture, pixel bevels, square rivets, 9-slice game button plates |
| `mui.cc` button, segmented | skin button + PT Mono label | skldx plate + font 3 capitals in button yellow |
| `mui.cc` toggle, slider | pill track + round knob | `optiup`/`optidn` lamp, `slider.frm` knob in a groove |
| `muiFillRoundRect` / `muiStrokeRoundRect` | rounded corners | square corners with a 2-pixel notch |
| `theme` colours (`mui.cc kTheme`) | green `5CE65C`, amber `FFC23A`, cream `F2DDA6`, red `A0321F` | unchanged palette, see Colours |

## Game assets used (all verified present in retail `master.dat`)

Interface (`art/intrface`, 8-bit FRM, game palette, colour 0 transparent):
- Metal: `ceditbkg` (640x480 worn steel; the top-left 256x200 is mirrored 2x2 into a seamless tile).
- Button plates: `skldxoff` / `skldxon` (86x33 blank plates, 9-slice, corner 5 px) for every button and tab.
- Lamp: `optiup` (dark) / `optidn` (amber glow), 34x34: toggles. Knob: `slider` (43x29).
- HUD tabs with lettering: `invbutup/dn` (INV), `chaup/dn` (CHA), `pipup/dn` (PIP), `mapup/dn` (MAP),
  `endturnu/d` (TURN), `endcmbtu/d` (CMBT). `dn` = the lit variant, used when the icon is amber.
- Action menu: `lookn`, `usegetn`, `pushn`, `rotaten`, `unloadn`, `dropn`, `canceln` (+ `h` = hover).
- Steppers: `splsoff/on` (+), `snegoff/on` (-).
Skilldex (`art/skilldex`, 140x117 vault-boy ink drawings on paper): `skills`, `sneak`, `lockpick`,
`steal`, `traps`, `doctor`, `science`, `repair`, `speech`, `barter`, `actionpt`, `hitpoint`, `unarmed`,
`carryamt`, `frienfoe`. Drawn as the game draws them, on their paper, with a black rim.
Inventory (`art/inven`, item pictures): `pistol`, `ltharmor`, `hypo01`, `10mmap`, `key`, `pack2c`,
`magnum`, `bullet`, `bag`, `clipbrd`, `disk1`, `disk3` for item filters, loot, save / load.
Not used: art with baked-in words in other languages (DONE, Berserk...) except where the word is the
function.

## Fonts (interface fonts, `font*.aaf`, anti-aliased, 8 levels)

| Role | Font | Native height | Used for |
|---|---|---|---|
| Label | font1 (spaced sans) | 9 px | tags, readouts, name plates (x2-x3) |
| Body | font2 (condensed) | 24 px | lists, text, numbers, dialogue (x1 to x4 in quarter steps) |
| Heading | font3 (heavy capitals) | 16 px | button and tab lettering (x1-x2.5, fitted to the button) |
| Title | font4 (grunge capitals) | 22 px | big titles (`MuiFontRole::Title`) |

Legacy `muiDrawText(size)` calls pick by size: below 26 output px -> Label at 2x/3x; otherwise Body at
`size * 1.05 / 24` rounded to a quarter. Integer scales use nearest filtering (crisp pixels), others
linear. Every string gets the game's black drop shadow (1-2 px). Metrics (`muiTextWidth`,
`muiLineHeight`) come from the same font, so wrapping and fitting (`muiWrapText`, `muiFitTextSize`)
keep working. The game's charset applies (`gameCharsetEncode`); a missing glyph shows `?`,
middle dot becomes `-`.

## Colours (from the game, kept in `kTheme`)

Phosphor green `5CE65C` text on `070A06` glass; amber `FFC23A` accents and lit lamps; button yellow
`D8BE4A` (the game's DONE lettering); cream `F2DDA6` pressed labels; red `A0321F` for primary
buttons (tinted onto the plate); warning `FF5A3C`; rust metal tint per use (`E6DFD0` backdrop,
`FFF8E8` plates). Item and vault-boy art keeps its own colours.

## Elements

- Backdrop: tiled game steel, dark seams, edge shading. Panels (cards): steel, black outline, lit top /
  left edge, square brass rivets (2x2 px pixels). Wells (lists, maps, readouts): near-black green glass,
  scanlines, shadow from the rim, lit lower lip (in style, not an art frame: the game's bezels are 40 px
  thick and would eat the layouts).
- Nav rail / tabs / buttons: 9-slice `skldx` plates at 2x; pressed = `skldxon` + dark wash; primary =
  red tint; active = amber rim (existing). Labels in font 3.
- Cards / list rows: plate or well, text in Body, numbers in Body or Label.
- Sliders: groove (well) lit amber to the value, `slider` knob. Toggles: lamp. Segmented: a row of
  buttons, the chosen one lit.
- Round rects -> square with notched corners; circles (LEDs, dials) stay round.

## Icons (`muiGameDrawIcon`, table `specOf`)

HUD tabs, end turn, end combat: the game's lettered buttons at the largest whole scale that fits.
Skills / stats / party: vault-boy drawings. Items (filters, quick save, reload, loot): item art scaled
to fit. Action menu and steppers: the game's own. Pixel art in-style (9 px grid, black drop shadow,
tinted): save, load and menu are the word in font 3; pixel art: back, play, pause, stop, expand (arrows to the corners), collapse, edit, eye (highlight), swap hands, spread,
regroup, target, tactical tiles, cancel. No icon falls back to the vector set in this theme unless the
art is missing from the install (then the classic icon is drawn).

## API for other screens (stage 2 and later)

`mui_gameskin.h`: `themedPanel`, `themedWell`, `themedButton`, `themedText`, `themedIcon` (these
dispatch to the Fallout or the classic look by themselves); `muiSkinFallout()`;
`muiGameDrawRole(text, x, y, MuiFontRole, scale, color)` and `muiGameFitScale` for role-based text;
`muiGameDrawArt` / `muiGameDrawInk` / `muiGameDrawNineSlice` for any `art/intrface|skilldex|inven` FRM
by name (missing art returns false: draw the in-style fallback); `muiGameButtonLabel`, `muiGameLamp`,
`muiGameKnob`. Existing `muiSkin*`, `muiDraw*` calls already follow the theme.

## Verification

Desktop build, rig `~/Projects/f2ds-theme-run` (`shots.sh <name> <dashboard_layout> <screen_nav>
<bottom_theme> [scenario]`), scenario `dualscreen`; captures in `~/Projects/f2ds-theme-shots/`.
Check: nothing clipped or overlapping vs classic, text legible at 1240x1080, no vector icons left.
