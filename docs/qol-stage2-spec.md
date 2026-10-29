# QOL stage 2: dialogue and barter on the bottom screen (spec)

Dual-screen (AYN Thor: top 1920 x 1080, bottom 1240 x 1080 at density 2.6, about 477 x 415 dp).
Stage 1 (`qol-stage1-spec.md`) laid out the dashboard; this stage moves the two screens a
conversation uses. Every change has a setting and the current look as default; nothing changes
on phones or single-screen devices (`muiBottomActive()` false).

## Where things are today (facts)

- `TalkScreen` (`mui_game_dialog.cc`) and `BarterScreen` (`mui_barter.cc`) do not override
  `surface()`, so both sit on the **top** screen (`MuiSurface::Top`), `coversScreen`, `modal`.
  The bottom screen shows the idle companion panel meanwhile. (`dualscreen.md` table "Dialogue /
  Barter" describes the target, not what is built.)
- Talk layout (`TalkScreen::build`): `muiDialogLayout` gives `content` + tab column; left 46%
  = head (`buildHead`, game buffer 388 x 200 via `gameDialogGetHeadImage`) over the reply
  (`buildReply`, scroll id `talk.reply`); right = options (`buildOptions`, scroll `talk.options`,
  16 dp text, min 46 dp, wrap via `muiWrapText`, reaction colors from
  `gameDialogGetOptionReaction`); tap -> `gameDialogChooseOption(index)` (any index, not only 1-9).
  Tabs (`muiDialogTabs`): Talk / Barter (`gameDialogSpeakerCanBarter`) / Review / Party.
  Options get no visible number; the game's "1." prefix exists only with sfall `numberOptions`.
- Keyboard digits and Esc are handled in the game's talk loop (`game_dialog.cc`, Esc ->
  `muiGameDialogBack`); the bottom window is `FLAG_NOT_FOCUSABLE`, so keys / pad stay with the game.
- Barter (`BarterScreen::build`): 8 columns across `content`: player 3 | offer 1 | request 1 |
  trader 3, square cells (`cellSize` = content.w / 8 less gaps, about 52 dp on 477 dp), rows:
  party tabs / trader name+caps, `muiItemFilterRow`, lists (`buildList`: scroll, tap moves,
  `ui.dragSource` / `dropped`, long press), totals (`offerValue`, `requestValue`, weight, caps),
  one OFFER button (`barterRequest(BarterRequest::Offer)`). Talk goes via the tab
  (`BarterRequest::Talk`). No head, no message line, no quantity UI of its own (see below).
- Quantity: `mui_quantity.cc` (`QuantityScreen`, `openedOn = muiBottomHoldsGame() ? Bottom : Top`);
  barter moves go through the game's `barterProcessUI` loop with `BARTER_ACTION_EVENT`.

## Settings

`[touch]` ints in `settings.h` / `settings.cc`, Settings -> Display after "Screen navigation",
`RowKind::Choice` rows, texts in `ce.msg` 930.. (with in-code fallbacks):

| Key | 0 (default) | 1 | 2 |
|---|---|---|---|
| `dialogue_layout` | Top (today) | Split | Split, options mirrored on top |
| `dialogue_option_size` | Normal (today, 16 dp) | Large (20 dp, min 56 dp) | |
| `barter_layout` | Top (today) | Bottom | |
| `dialogue_tap_skip` | Off | Tap the reply to skip typing / animation | |
| `barter_confirm` | Off | Confirm when offer is lossy | |

Layout keys only apply when `muiBottomActive()`; otherwise treated as 0. Proposal: after Bill's
device check, default `dialogue_layout` and `barter_layout` to 1 on dual-screen devices.
Texts: ce.msg 930..942 (layout / option size / barter / skip / confirm names and choices; check
free ids first).

## (a) Dialogue split

**Top screen** (`TopTalkScreen`): head full-height-ish (game buffer scaled, keep 388 x 200 aspect,
`muiFitRect`), speaker name, the NPC reply below it in large text (22 dp, scroll `talk.reply`,
bottom fade). Option list: **not drawn** in mode 1; in mode 2 drawn dimmed (alpha 0.35, no
touch, numbers kept) as a mirror for streams / spectators. Proposal: mode 1 hides it, because
the whole point is a clean cinematic top; mode 2 is the cheap alternative.

**Bottom screen** (`BottomTalkScreen`, `surface() = Bottom`, modal, tabs per `muiDialogLayout`):

```
+--------------------------------------------------+----+
| Dr. Troy              (name)     [rev] [barter]  |Talk|   header row 36 dp: speaker, mini
+--------------------------------------------------+Barter
| 1  Tell me about the Vault.                  >   |Review
+--------------------------------------------------+Party
| 2  I'd like to buy something. (Barter)           |
+--------------------------------------------------+
| 3  [Intelligence] This engine looks odd, it ma-  |   large buttons: number badge 36 dp,
|    kes a clicking noise when you start it, why?  |   text wraps to 4 lines, min 56 dp,
+--------------------------------------------------+   gap 8 dp, reaction color on border
| 4  Goodbye.                                      |
+--------------------------------------------------+
|  [ Barter ]                        [ End ] (scrl)|   footer 52 dp: Barter, End shortcuts
+--------------------------------------------------+
```

- Option rows: number badge (index + 1, any count; > 9 shown too), text wrapped with
  `muiWrapText`, text size 16 dp (Normal) or 20 dp (Large), row height from wrapped lines,
  reaction (`gameDialogGetOptionReaction`) tints the badge and left bar (good/neutral/bad).
  Skill-check / stat-check tags (text starting `[`) keep their prefix.
- Many options or one huge option: list scrolls (`talk.options`, as now); a row taller than
  the area is allowed to scroll inside the area, a scroll thumb is drawn on the right; the
  scroll resets on new reply (already in `TalkScreen::build`).
- **Barter** footer button: only when `gameDialogSpeakerCanBarter()`, same call as the tab
  (`gameDialogRequestBarter`). **End**: no vanilla exit exists (`no_exit_hotkey=1`); show it only
  when an option is the "[Done]" / goodbye one (`messageListId == -2` or the last option with
  reaction neutral and msg 650); it simply selects that option. If there is none, the button
  is hidden (never invent an exit; it could skip a script).
- **Review** (tab): opens in the bottom content area as today (`buildReview`), scroll to end.
- **Party** tab: party panel (`buildParty`) fills the bottom; stats (`buildPartyStats`) go on
  the top under the head instead of the reply.
- Input: tap = `gameDialogChooseOption(index)`; digits, Esc, Enter, d-pad up/down + A keep
  working through the game's loop (no bottom focus needed). Gamepad focus ring on the bottom list is
  stage 3; stage 2 only must not break the keyboard path: `ui` gets `focusIndex` reserved.
- Reply typing: with `dialogue_tap_skip=1`, a tap on the top reply (top touches reach the
  top screen) or any bottom tap on the header shows the full reply at once (see (c)).

## (b) Barter on the bottom screen

The trade needs the full bottom (head is not needed there; the top keeps the head, the
trader's name and the last message line).

```
+--------------------------------------------------+----+
| [You][Dogmeat][Sulik]       Trader   $1450      |Talk|  header 30 dp: party tabs / trader+caps
| [All][Wpn][Arm][Drg][Amo][Msc]  | [All][...]    |Barter
+----------------------+--+--+---------------------+Review
| YOUR ITEMS    wt 120 | OFFER| REQUEST | THEIR ITEMS|
| [#][#][#][#]         | [#]  | [#]     | [#][#][#][#]|   2 side lists 2 columns of 64 dp
| [#][#][#][#]         | [#]  | [#]     | [#][#][#][#]|   cells; tables 1 column
| ...                  |      |         | ...         |
| $ 320        wt 80   |$ 120 |  $ 180  | wt 40       |
+----------------------+------+---------+-------------+
| msg: "Not enough caps"    [ Talk ]     [ OFFER ]     |  footer 52 dp
+--------------------------------------------------------+
```

Changes against `BarterScreen`: `surface() = muiGameScreenSurface()` (setting 1) as inventory
and loot do; two columns per side (cells 64 dp+, `kSideColumns` 3 -> 2, more scroll, better
tap targets) with the tables as the 2 narrow middle columns; an OFFER and a **Talk** button in
a footer (Talk = `BarterRequest::Talk`, same as the tab, as the game's Talk button); a message
line (`muiBarterMessage`) in the footer; totals as chips above each table, red when the
request value is higher than the offer (`view.requestValue > view.offerValue`, game rule; the
engine still decides whether the trade is accepted); caps are items (`PID_MONEY`) so they
show as a cell with quantity, shown first (`muiItemFilterRow` money-first stays).

- Move: tap = move one stack (as now); **long press** or drag = quantity picker
  (`QuantityScreen` already on the bottom when `muiBottomHoldsGame()`) with +/- , slider, All,
  and a total line showing the price of that count; stacks of 1 skip the picker.
  Drag keeps working (`dragSource` / `dropped` / `drawDragged`); dropping on a party tab gives
  the item (existing). A tap on an item in a table returns it.
- Top screen during barter: `TopBarterBackdrop`: the head (`gameDialogGetHeadImage`), the
  trader's name and caps, last barter message. Nothing touchable.
- Edge cases: trader with 0 items (empty list text from existing strings, no crash);
  party-member barter (weights instead of $, `view.partyMemberBarter`) keeps its labels;
  200+ items scroll (cells virtualised by the existing clip); filter rows must fit 2 columns
  (shrink via `muiFitTextSize`); `barter_confirm=1` asks before OFFER if the offer value is
  lower than the request by more than 30% or if the player offers all their weapons/armor
  (dialog box, texts 941/942).

## (c) Other awkward flows

- **"Done"-style confirmations:** vanilla review window "Done" and the quantity picker's
  "Done"/"Cancel" are already replaced. Remaining: the game's `[Done]` dialogue option reads
  as a bare bullet; show it as a distinct "End conversation" row (own style, localised
  `gameDialogGetOptionText`, never reword the text).
- **Skipping text:** NPC replies are shown at once but lip-sync / voice runs; add tap on
  the head (top) or the reply (bottom, header) with `dialogue_tap_skip` to stop the speech
  (`gameDialogSkipSpeech`: wraps the same key the game uses to cut a line; check
  `soundEffectsDelay` use in `game_dialog.cc` first). Off by default.
- **Accidental choice:** options are tap-to-choose with no confirm; a press that turns into a
  scroll cancels (already via `isScrolling`); add a 120 ms ignore after a new reply appears so
  the finger that tapped the last option does not hit the next one (`lastReply` change).

## Edge cases

- Mods with their own dialogue windows / sfall `numberOptions` / custom message lists: the new
  screens read only `gameDialog*` getters, not windows; if `gameDialogGetOptionCount()` is 0
  or the window is the game's own (a script calls `gdialog_...` custom), fall back to the
  Top layout for that talk (no setting change) and show nothing on the bottom; README limit
  "mods with own script windows" stays.
- Very long texts: reply scrolls with a fade; options wrap up to the height of the area;
  option text over 600 chars is truncated to 6 lines with a "more" tap that opens it in a
  scroll sheet (still selecting needs a second tap).
- Controller-only: keys 1-9, Enter, Esc, d-pad up/down + A already drive the game's loop; the
  bottom list shows the numbers so they match; focus ring is stage 3 (stage 2 only keeps it
  working). Barter by pad is stage 3.
- Single screen or the bottom window lost mid-talk (`muiBottomActive()` flips): the screen
  that `surface()` returns is computed once per frame (gBottomActive), so the talk moves to
  the top without resetting `lastReply` and scrolls.
- Frame hold (`holdsFrame`) must work for Top<->Bottom screens: when `barter_layout` differs
  from `dialogue_layout`, hold the last frame on both surfaces for the switch.

## Implementation steps (Codex-sized)

1. Settings: 5 keys, `ce.msg` texts, `mui_preferences.cc` rows, config round-trip.
   Check: build; `fallout2.cfg` shows keys; defaults keep behaviour.
2. Split `TalkScreen` into shared model + `buildHead` / `buildReply` (top) and
   `buildOptions` (bottom) with `surface()` per setting; Top mode untouched.
   Check: autotest `talk` scenario, top + bottom frames; top mode identical to before.
3. Bottom option list: number badges, large size, scroll thumb, footer (Barter, End),
   reaction tint, 120 ms ignore. Check: `talk` frames with 1, 4 and 20 options and a long
   option; tap tests in `bottom_input_test.cc`.
4. Mirrored mode (dimmed list on top) and Review/Party tabs on the bottom.
   Check: frames; Party talk with a companion.
5. Barter on the bottom: `surface()`, 2-column lists, footer with OFFER / Talk / message,
   totals chips; top backdrop with the head. Check: `barter` scenario frames on both
   surfaces; offer and talk buttons work; stage 1 nav modes (rail, slim, top bar) don't overlap.
6. Quantity: long press / drag opens `QuantityScreen` with price line; stacks of 1 skip.
   Check: move 1 / 5 / all of a stack; caps stack; party give still works.
7. `dialogue_tap_skip`, `barter_confirm`, "End conversation" row. Check: autotest + manual.
8. Docs: README, `dualscreen.md`, `android-touch-notes.md`. Check: docs match behavior.

## Verification

- Desktop build `cmake --build build-desktop --target fallout2-ce`.
- Screenshots: `~/Projects/f2ds-run`, `--dev-autotest-scenario=talk` and `barter` (bottom
  frames `<step>_bottom.png`) for every setting combination worth checking. `tools/mui_preview`
  has no game data, so it cannot show conversations; mockups above are ASCII only.
- Nothing clipped or overlapping in 1240 x 1080 captures; classic (all 0) captures unchanged.
