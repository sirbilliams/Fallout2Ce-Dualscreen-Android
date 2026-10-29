# Gamepad stage 3: full pad control

Settings (`[touch]`): `gamepad_ui` (default 1) turns screen focus on; `gamepad_bindings` is a list
of `button=action` pairs over the defaults, e.g. `gamepad_bindings=rb=pipboy,left=skilldex`.
Buttons: a b x y start back lb rb ls rs up down left right lt rt. Actions: none click context back
menu inventory skilldex character pipboy automap sneak swap useitem combat focus quickuse zoomin zoomout.

## Model
- Screens (either surface, bottom first on a Thor): every touchable widget of the interactive screen is a
  focus target (`mui_pad.cc`). The d-pad or left stick moves a green phosphor ring by geometry; a list at its
  end scrolls and moves on. Held directions repeat. A = tap, X = long press (quantity picker, item menu),
  B / Start = back, left / right on a slider change it. Right stick scrolls lists and pans map views
  (automap, world map); L2/R2 zoom them. The ring starts on the screen's content (`ui.padPrefer`).
- When both surfaces hold a screen, `focus` (RB) switches between them. On the map RB gives the pad to the
  HUD / dashboard (nav rail, weapon, modes) and B returns it.
- Map (no screen): right stick moves a cursor on the top screen, A is a tap there (walk / click / talk),
  X a long press (the action ring), left stick pans, the cursor pushed to an edge pans too.
- Quick use: `quickuse` (d-pad left) opens a ring of drugs and medical kits (themed, modal on the game
  screen's surface), pad or touch picks one; it costs 2 AP in combat like the inventory's use.

## Default map bindings
| Button | Action | Button | Action |
|---|---|---|---|
| A | click / walk | Select | tap: end turn, held: end combat |
| X | context (long press) | Start | game menu |
| Y | inventory | B | back |
| LB | character | RB | focus (map / HUD) |
| L3 | sneak | R3 | automap |
| D-pad up / down | zoom | D-pad left / right | quick use / Pip-Boy |
| L2 | swap hands | R2 | use active item |
Left stick: pan view. Right stick: cursor.

In screens: d-pad / left stick = move focus, A tap, X long press, B / Start back, right stick scroll / pan,
RB switch surface (when two screens).

## Tests
Dev hook: autotest actions `DEV_AUTOTEST_ACTION_PAD` (SDL button number) and `..._PAD_STICK`
(`gamepadDevPress`, `gamepadDevStick`). Scenarios: `padtalk`, `padbarter` (`--dev-new-game`), `padmap`.
Shots in `.rig/shots/padtalk|padbarter|padmap`.
