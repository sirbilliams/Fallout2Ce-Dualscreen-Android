# Stage 2 continuation verification (2026-10-29)

Reviewed and retained the seven uncommitted source changes from the interrupted
run. Continued on `qol-stage2`; no commits or game-data copies. The existing skin
is retained. All game runs were invoked under `timeout` and `xvfb-run`, with
`/home/bill/Projects/f2ds-run2` as their working directory. Captures and logs are
under `/tmp`, outside the game-data tree.

**Xvfb verification is NOT complete.** A late check of wrapper exit statuses
found that Xvfb cannot bind its local/unix listening sockets in this sandbox.
The installed `xvfb-run` retries ten times, then still launches the command;
SDL's automatic video selection evidently falls back to another backend.
Consequently the captures and gameplay assertions listed below are diagnostic
results, not compliant Xvfb proof. No further fallback game runs were performed
once this was discovered. `/tmp/stage2-xvfb-start.log` contains the socket errors;
`/tmp/stage2-xvfb-wrapper.log` shows the wrapper proceeding after all retries
failed. Even `timeout 20 xvfb-run -a true` returns 1 during cleanup.
No working pre-existing Xvfb display is exposed in the sandbox.

The desktop game target builds with:

```sh
timeout 180 cmake --build build-desktop --target fallout2-ce -j4
```

When Xvfb is available, run a proof scenario from the rig with the X11 backend
forced, so SDL cannot silently fall back:

```sh
timeout 240 xvfb-run -a -e /tmp/stage2-xvfb-errors.log \
  env SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
  /home/bill/Projects/f2ds-stage2/build-desktop/fallout2-ce \
  --dev-new-game --dev-autotest=/tmp/stage2-final2-barter \
  --dev-autotest-scenario=dualbarterproof \
  '[touch]dialogue_layout=1' '[touch]dialogue_tap_skip=1' \
  '[touch]barter_layout=1' '[touch]barter_confirm=1'
```

`dualbarterproof`, `dualtalkproof`, and `dualmoreproof` use the temple's real
Klint conversation, placing the player beside him. Barter stock is seeded in
memory by autotest actions. The 853-character long option is explicitly a
developer fixture passed through the engine's dialogue model. No scripts or
game assets are edited. Save slot 1 did not load in this environment; the proof
scenarios use new games instead.

| Check | Named evidence |
|---|---|
| Notice on top; bottom options clear | `/tmp/stage2-final-notice/t02_notice_top_options_clear.png` and `t02_notice_top_options_clear_bottom.png` |
| Long press, picker at one with total | `/tmp/stage2-final2-barter/b08_longpress_quantity_1_bottom.png` |
| Plus to five; minus to four | `b12_plus_5_bottom.png`, `b13_minus_4_bottom.png` in that barter directory |
| Move five | `b15_move_5_bottom.png`; table-count assertion passes |
| Drag opens picker; All moves remainder | `b17_drag_quantity_bottom.png`, `b18_all_3_bottom.png`, `b19_move_all_bottom.png`; table count reaches eight |
| Single item bypasses picker | `b21_single_skips_picker_bottom.png`; knife count on offer table is one |
| Caps, slider, All | `b23_caps_quantity_bottom.png`, `b24_caps_slider_bottom.png`, `b25_caps_all_bottom.png`; table count reaches 150 |
| Trader's price; moving one | `b31_trader_price_bottom.png`, `b32_request_one_red_bottom.png`; request count is one |
| Request total red | `/tmp/stage2-final2-barter/b32_request_one_red_bottom.png` |
| Lossy confirmation card | `/tmp/stage2-final2-barter/b34_confirm_card_bottom.png` |
| Card blocks underlying Talk button | `b34b_background_talk_blocked_bottom.png`; card remains open, then Cancel works |
| Game rejects bad offer; notice on top | `b37_rejected_notice_top.png` and `b37_rejected_notice_top_bottom.png` |
| Long option preview and sheet | `/tmp/stage2-final2-more/m02_long_option_fixture_bottom.png`, `m03_more_sheet_bottom.png` |
| Sheet scrolls; Back does not choose | `m03b_sheet_scrolled_bottom.png`, `m04_back_no_choice_bottom.png` |
| Reopen, then explicit second tap chooses | `m05_reopen_bottom.png`, `m06_second_tap_choose_bottom.png` |

For the voiced reply and actual `[Done]` option, run `dualendproof` with
`--dev-new-game --dev-map=arvillag.map` and the dialogue settings above.
Diagnostic evidence from that run (subject to the Xvfb limitation above):

- `/tmp/stage2-verified-end/e01_mid_speech_full_reply.png`: reply during speech.
- `e03_header_skip.png`, `e04_speech_after_skip.png`: after tapping the bottom
  header. `log.txt` records `speech playing: 1` before and `0` after, with the
  same reply and options.
- `e10_real_done_bottom.png`: Elder's actual scripted `[Done]` option and End
  conversation button. The log marks that option as ending the conversation.
- `e11_end_selected.png` and `e11_end_selected_bottom.png`: End selects it and
  exits dialogue.

**Requested mid-typing/full-text pair remains unavailable.** Replies are
displayed fully at once, as specified in stage 2. `dialogue_tap_skip` stops
voice/lip-sync; it does not reveal progressively typed characters. The captures
above prove speech skipping, not a nonexistent typing transition.

The standalone `tools/mui_preview/bottom_input_test.cc` was linked against the
desktop engine objects and run with `timeout 30 xvfb-run -a`. Its result is
`/tmp/stage2-bottom-input/final-result.log`: PASS for a direct bottom finger tap, a
quick segmented-control tap, frame posting, and a tap queued through SDL from
another thread. This test uses an SDL software renderer without initializing
video, so its input assertions pass despite the Xvfb wrapper failure. It is
headless input coverage, not an Xvfb or physical-device verification.

Both reported unrelated failures reproduce on `origin/main` at `e07eb208`:

- `/tmp/stage2-main-mapper.log`: compile `src/win32.cc` with `FALLOUT_MAPPER`
  against the desktop SDL headers; line 112 fails because `gamepadInit` is not
  declared. This reproduces the mapper translation-unit failure, not a full
  mapper build.
- `/tmp/stage2-main-save.log`: compile and run the origin/main
  `save_catalog_test.cc` with its three engine source dependencies; line 351
  fails at `at(10) == snapshot('F') && at(14) == snapshot('J')`.

The branch reproduces the same failures in `/tmp/stage2-branch-mapper.log` and
`/tmp/stage2-branch-save.log`. Origin/main sources were obtained with
`git archive`; no rig game data was copied.

The drag-by-distance autotest helper now routes finger events to the surface
where its widget was found. This was necessary to verify bottom slider changes
and sheet scrolling; ordinary tap/drag helpers already selected the surface.
