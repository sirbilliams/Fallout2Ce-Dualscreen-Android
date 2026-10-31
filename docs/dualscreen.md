# Dual-screen design (AYN Thor)

How the two screens divide Fallout 2 between them, and what the engine has to change to get
there. Aim: DS-like. The top screen is the game world, never covered by menus; the bottom screen
is always a live touch panel.

The Thor's screens: top 1920x1080 (16:9, touch), bottom 1240x1080 (about 3.9", nearly square,
touch). Display choice: `BottomDisplay.java` (built-in bottom screen only, never a TV / cast).

## Status

Built and in use on the AYN Thor (v2.0). Since then: mouse controls ([mouse-controls.md](mouse-controls.md)), button remapping ([gamepad-stage3.md](gamepad-stage3.md)), toasts on the top screen so they never cover bottom-screen controls.

- Two surfaces in the mobile UI (`mui.cc`): each screen of the device has its own finger, widgets
  and scrolls; `MuiScreen::surface()` says where a screen goes.
- The bottom surface is drawn into its own texture with the game's renderer every 33 ms, read back
  and posted to the bottom screen's window (`muiRenderBottom`, `bottom_screen.cc`).
- Bottom touches come from `BottomScreen.java` as SDL finger events of their own touch device
  (`kMuiBottomTouchId`) and finger range, and reach the bottom surface only.
- The touch HUD moves to the bottom screen (`hudLayoutDashboard`): screens, system, tools, log,
  weapon / status / indicators, combat buttons; the top screen is the world alone.
- The dashboard's live map between the HUD's groups follows dude (pan / pinch hold it 5 s), with
  a button for the full map screen.
- Inventory, character, Pip-Boy, map, loot and elevator open on the bottom screen; the frozen
  world on top is dimmed and its touches go nowhere. Item menus (radial), the quantity picker and
  popups follow the screen that opened them.
- Gamepad basics (`gamepad.h`).
- The look (`mui_dual.cc`): a Pip-Boy CRT backdrop on the bottom screen (gradient, scanlines,
  vignette); the companion panel when nothing else is on it (game clock and date, where, who,
  HP; the title outside a game); screens fade in there; the edges pulse red at 25% HP or less;
  the frozen world on top fades darker with a chip pointing down.
- The live map: dude as a pulsing marker with an arrow where he faces, the camera eased after
  him, a chip with the map's name and the game's clock, an HP bar along the bottom, and in
  combat an amber (your turn, AP left) or red (enemy turn) banner and border.
- Settings -> Display -> Dual screen (`[touch] dual_screen`, on).

- Classic Fallout 2 theme (`mui_skin.cc`, the palette in `mui.cc`): worn metal plates drawn from
  a generated texture, bevels and rivets, recessed monitor wells with green readouts, amber
  highlights and lit switches, red primary buttons. Full screens get the bolted metal backdrop,
  lists and readouts the wells; the bottom screen is a metal console, its companion panel a
  monitor in a riveted bezel.
- Detailed maps (`automapGetView`, `MuiMapView`), on the map screen and the live map: the explored
  floor in its tiles' own colors (squares near anything seen, so it survives a reload), doors,
  containers, stairs / ladders / elevators, party members, and names of where exits lead; a legend
  on the map screen.
- Touches of life: the bottom screen switches on as a CRT (line, opening, flash) when it comes on;
  big monitors have a rolling phosphor band and glass glare; stamped brass name plates (STATUS,
  AUTOMAP); the companion panel's sun / moon arc over the game's clock and a Geiger gauge
  (square-root dial in the game's radiation levels, needle twitching while radiated); log
  messages type out as on a teletype.
- The companion panel shows game details only while a game is played (`mainIsPlaying`): the
  startup screen shows just the title.
- `tools/mui_preview`: renders the skin and a made-up map off-screen, to tune the look without a
  device.

Next: dialogue split (head and reply on top, options below), barter, the map cursor for the pad,
d-pad focus on the bottom screen, quick use (option), screen layouts for the square bottom
screen.

## What the engine does today (facts the design rests on)

1. **Game screens freeze the world.** Inventory (`inventory.cc`), Pip-Boy (`pipboy.cc`),
   character (`character_editor.cc`), automap (`automap.cc`, which also disables the iso view),
   dialogue and barter (`game_dialog.cc`), loot, world map, elevator, called shot and the
   quantity picker each run their own loop (`muiRunModal` or the game's own). Nothing outside
   them moves while they are open. This is the original game's design, not the port's.
2. **The game's rules decide what may open** (`game.cc` key handlers ->
   `gameCommandExecute`):
   - inventory in combat: only on the player's turn, costs `inventoryGetInvenApCost()` AP
     (`inventoryCheckOpen`);
   - Pip-Boy: not in combat;
   - automap, character, skills, menu: any time.
3. **The touch HUD posts the game's commands** (`touch_hud.cc`, `GameCommandType`), exactly
   as the original bar's buttons did. Any button that posts the same command gets every rule
   above for free.
4. **The mobile UI is immediate mode on the game thread** (`mui.h`): each screen rebuilds,
   draws and handles its touches every frame through a `MuiContext` that carries the screen
   rect and pixel density. One screen stack, one finger tracker, one render pass, called from
   `renderPresent` (`svga.cc`). Reading game state while drawing is therefore safe: no other
   thread touches it.
5. **Screens are flagged** `modal` (take every touch), `coversScreen` (world not drawn under
   them), `holdsFrame`, `hidesHud`.
6. **The dialogue screen is already split** in two halves: speaker (talking head) and reply on
   the left, options on the right, tabs at the right edge.
7. **No gamepad.** The touch-only build (`FALLOUT_TOUCH_ONLY`) has no controller input; on the
   Thor its pad does nothing in game today.

## The interaction model

Three situations, each with a fixed split.

### 1. Exploring (world running)

- **Top:** the world, full screen. Tap to walk / use / talk as now (`touch_controls.cc`). No
  HUD buttons over it.
- **Bottom:** the dashboard.
  - Status strip: HP, AP bulbs, active weapon with its attack modes and ammo, swap hands,
    indicators (SNEAK, LEVEL, RADIATED...).
  - A live panel, chosen with tabs: **Map** (automap tracking the player, pan / pinch, tap an
    elevation), **Pip-Boy** (date and time, quests, holodisks: reading only), **Character**
    (stats, skills, perks: reading only), **Inventory** (what is carried and equipped).
  - Action row: Skills, Highlight, Sneak, Log, Quick save, Quick load, Menu.

Live panels only *read* game state, so they never pause the world. Anything that would change
it posts the game's own command and goes through the real screen (situation 2): tapping "Open
inventory" in the Inventory panel opens the game's inventory with its rules (turn, AP cost).

### 2. A game screen is open (world frozen by the game)

The screen the game opens goes to the bottom. The top keeps the frozen world, dimmed, or shows
what the screen is about:

| Game screen | Top | Bottom |
|---|---|---|
| Dialogue (`dialogue_layout` 1, default on dual screen) | talking head (or the map around the speaker) and the large reply; mode 2 also a dimmed copy of the options | numbered options (wrapped, reaction tint, scroll thumb), Barter / End shortcuts, review, barter / party tabs |
| Barter (`barter_layout` 1, default) | talking head, trader, caps, last reply (nothing touchable) | the trade: 2-column lists, tables, Talk / OFFER footer, red request total when the trade is lossy |

Setting 0 for either keeps the whole screen on the top (phones and single-screen devices always do).
| Loot | world, container in view | the loot screen |
| Inventory, character, Pip-Boy, automap | world, dimmed | the screen |
| World map | the world map, large | towns, car, travel controls |
| Called shot, use item on, quantity, elevator | world, dimmed | the picker |
| Main menu, load / save, options, movies, slides | the screen (full) | idle panel |

### 3. Combat (turn-based)

- **Top:** the world; tap targets as now.
- **Bottom:** the dashboard in combat form. AP bulbs large, attack modes and the weapon,
  **End turn** and **End combat** as big buttons, the combat log. Pip-Boy tab greyed with the
  game's reason; Inventory tab shows its AP cost; on enemies' turns the panel shows whose turn
  it is and actions are disabled, as the game does.

## Input rules

- Each screen owns its touches. Bottom-screen touches never reach the world; top-screen touches
  reach the world or a top-screen panel.
- Two people can't fight over one finger tracker: each screen has its own pointer, active
  widget, scroll and drag state.
- Back (Android back, the pad's B later) goes to the topmost modal screen, wherever it is shown
  (as `muiDispatchBack` does now).
- The bottom window never takes key focus (`FLAG_NOT_FOCUSABLE`), so the pad and keys stay
  with the game.
- Gamepad (later step): left stick moves a cursor on the top screen, A taps, B back, shoulder
  buttons cycle the bottom tabs, a button for End turn.

## Engine changes

1. **Two surfaces.** `MuiSurface { Top, Bottom }`. Each `MuiScreen` says which surface it is
   on (virtual `surface()`, Top by default; game screens return Bottom when the bottom screen
   is there, Top otherwise, so single-screen phones are unchanged). The globals of `mui.cc`
   (pointer, events, regions, widgets, active id, drag, pan / zoom, scroll) become one state
   per surface.
2. **Bottom render pass.** `muiRenderBottom` builds the Bottom screens with a `MuiContext`
   whose screen rect is the bottom surface, into an SDL target texture of the bottom screen's
   size; the frame is read back and posted to the bottom window (`bottom_screen.cc`). Same
   renderer, so the existing texture caches stay valid. Only redrawn when something changed or
   while animating. `coversScreen` works per surface: a covering screen on the bottom no longer
   stops the world being drawn on top.
3. **Bottom touches.** `BottomScreen.java` forwards the panel's touches over JNI;
   `SDL_PushEvent` (thread-safe) puts them in the game's event queue as finger events with a
   reserved touch id, coordinates relative to the bottom screen. `muiHandleFingerEvent` sends
   that touch id to the Bottom surface only.
4. **Dashboard screen.** A non-modal Bottom screen showing the status strip, the tabs and the
   action row. Buttons post `GameCommandType` commands like the HUD.
5. **Panel views.** The automap, Pip-Boy, character and inventory screens get their drawing
   split into a view function taking a rect and a read-only flag, used by both the full screen
   and the dashboard's live panel.
6. **Top HUD.** In dual mode the HUD elements now on the dashboard aren't drawn on top.
7. **Square layouts.** The screens were laid out for a wide landscape phone (tabs at the right
   edge, two columns). The bottom screen is nearly square: each screen needs a layout pass for
   it.

## Quick use (option)

Setting **Quick use from the dashboard**, off by default. On: the Inventory panel's items in the
quick-use list (healing, drugs, explosives, the ones the player pins) use straight from the
dashboard without opening the inventory. Off: tapping an item opens the game's inventory.

Quick use goes through the game's own item use (the same functions the inventory calls), so
using the item keeps the game's rules: its AP cost in combat, only on the player's turn, the
item's own effects and scripts. What it skips is the inventory's own opening cost in combat
(`inventoryGetInvenApCost`), which is why it is an option and not the default.

## Gamepad (AYN Thor and any SDL game controller)

Why it fits well: the touch controls don't emulate a mouse. Map gestures call the player's
commands with an explicit target (an object or a tile, `touch_controls.h`), so a pad calls the
same commands with the same rules. Today the pad does nothing because the game controller
subsystem is never started (`SDL_Init` in `win32.cc` has no `SDL_INIT_GAMECONTROLLER`).

### On the map (top screen)

- **Left stick:** a target cursor on the map. It snaps to what can be acted on near it (people,
  doors, containers, items on the ground), else marks a tile.
- **A:** the primary action on the target, as a tap: walk there (again on the same tile: run),
  talk, use, pick up, loot.
- **Hold A:** the radial action menu (`touchControlsChooseActionMenuItem`), chosen with the
  stick.
- **L1 / R1:** previous / next interactable in view; in combat, previous / next enemy.
- **Right stick:** move the view; L3 / R3 zoom out / in.
- **Combat:** the touch rule maps directly. A on a target selects it (shows hit chance and AP
  cost), A again attacks. Y cycles attack modes, X reloads, L2 swaps hands, holding Select ends
  the turn (held, so it can't happen by accident).

### On the bottom screen and menus

- **D-pad:** moves a focus ring between the bottom screen's widgets. The mobile UI already
  records every widget's id and rect each frame (for its automated tests), so focus can move
  spatially (the nearest widget in the pressed direction) with no per-screen work.
- **A:** taps the focused widget; **B:** back (`muiRequestBack`).
- **Shoulder buttons in a game screen:** previous / next tab (inventory, character, Pip-Boy,
  map).

### System

- **Start:** the game's menu (Esc), as in neutron2.
- **Android back:** trapped (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`): back in the game, never closes
  the app.
- Same conventions as neutron2-android: A acts, B goes back, Start is the menu.

## Open question

- **Top while a screen is open:** dimmed world, or screen-specific content (item details,
  character portrait). Start with the dimmed world.
