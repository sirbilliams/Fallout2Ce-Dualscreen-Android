# Mouse-style controls (SpongeBob EOTM feel)

Settings (`[touch]`, Settings > Controls > Mouse controls): `pad_cursor` (1), `pad_r3_hold_ms` (400), `touch_cursor` (1),
`two_finger_click` (1), `touch_hold_ms` (0 = the system's touch & hold delay), `cursor_speed` (100 %).
The Android setup guide's Controls page has the right-stick switch.

In this port there is no mouse; a "right click" is the long press: the action menu of what is under the cursor.

| Input | Does |
|---|---|
| Finger on the top screen | cursor drawn at the finger (stays 450 ms after it lifts); tap = the usual tap |
| Two-finger tap / touch and hold | right click (action menu) |
| Right stick | cursor, squared travel, 0.75 screen widths a second at full tilt, 20 % dead zone |
| A, R3 (short) | left click |
| B, R3 held `pad_r3_hold_ms` (fires on the threshold, no left click after) | right click |
| TOUCHPAD button (dashboard tool column) | bottom-screen trackpad page, Back closes it |
| Pad: drag | relative cursor move, gain 0.45 + min(1.8, 0.9 * speed) |
| Pad: tap (under 320 ms) | left click |
| Pad: hold still 380 ms, then drag | carry (left button held: view pan, item drags on top screens) |
| Pad: two-finger tap | right click |

The left stick / d-pad UI focus is unchanged. With `pad_cursor=0` the right stick does nothing on the map and R3 /
A / B keep their `gamepad_bindings`. Test: `--dev-autotest-scenario=mouse` (shots of both themes).

## Controller taps on the map (fix by Ocherk, 2026-10-30)
A and R3 click on the map through a synthetic finger (`mui_pad.cc`). That finger isn't SDL's, so the end-of-poll
"lost finger" check used to cancel its received release before `touch_process_gesture()` read the tap: A / R3 did
nothing in the world on a real Thor (autotests skip that check, so they never saw it). Now
`touch_forget_missing(..., preserveEnded)` keeps ended touches until gesture recognition on that one check only;
explicit resets, focus / background loss and a new finger still cancel, and a held finger that goes missing also
drops the other fingers' pending releases (no stray single tap from a broken two-finger gesture). Bottom-screen
fingers (BottomScreen.java, always delivered with their ends) are unaffected. Unit test: `tests/touch_gesture_test.cc`
(9 scenarios, runs in the UI regression workflow). Patch, analysis and test by Ocherk
([r/decomps](https://www.reddit.com/r/decomps/comments/1xiefpz/)), confirmed on an AYN Thor.
