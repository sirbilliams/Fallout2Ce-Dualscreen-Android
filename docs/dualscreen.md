# Dual-screen design (AYN Thor)

How the two screens divide Fallout 2 between them, and what the engine has to change to get
there. Aim: DS-like. The top screen is the game world, never covered by menus; the bottom screen
is always a live touch panel.

The Thor's screens: top 1920x1080 (16:9, touch), bottom 1240x1080 (about 3.9", nearly square,
touch). Display choice: `BottomDisplay.java` (built-in bottom screen only, never a TV / cast).

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
| Dialogue | talking head (or the map around the speaker) and the reply | options, review, barter / party tabs |
| Barter | talking head | the trade tables |
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

## Open questions

- **Quick use from the dashboard** (a stimpak without opening the inventory): changes the game's
  rules (no AP cost for opening the inventory). Default: no, tapping opens the real inventory.
  Could become an option.
- **Top while a screen is open:** dimmed world, or screen-specific content (item details,
  character portrait). Start with the dimmed world.
