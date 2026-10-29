# Display, battery and readability settings

All options are in Settings > Display and persist in `[touch]` in fallout2.cfg.

| Key | Default | Values |
| --- | --- | --- |
| `perf_mode` | `balanced` | `balanced`: 60 Hz, `saver`: 30 Hz |
| `bottom_dim_seconds` | `60` | 0 disables dimming; otherwise 1–600 seconds |
| `bottom_text_size` | `100` | 100–130 percent, bottom game-font measurements and drawing |
| `colour_blind_safe` | `0` | Amber/blue warnings and reaction cues; `+`, `=`, `!` reactions, labelled low HP and `!` barter requests |
| `top_scanlines` | `0` | Optional top-screen CRT overlay |

The game clock and animation timing remain time based. Both surfaces are composed
for each paced presentation; the former independent 33 ms bottom-screen gate is
removed. Fractional high-resolution deadlines avoid truncating 60 Hz to 16 ms.
Slow frames rebase the deadline rather than accumulating a catch-up burst. Lower
rate loops (for example 24 fps movies) keep their lower limit. Display pacing is
a target: a device still needs to finish rendering within its frame budget.

Idle dimming is a composited dark overlay, not a change to Android's hardware
brightness. Touch, keys, pad buttons and stick activity above the dead zone wake
it. The waking event is still delivered normally.

Dashboard, navigation, dialogue layout and bottom theme preview immediately.
The fixed thumbnail remains visible while scrolling the Display list; the bottom
surface also reflects the selected theme. Apply saves changes. Back > No restores
the original display values, as does destruction of an uncommitted settings screen.
Game font role widths, heights and glyph drawing share the same bottom text scale;
the classic theme retains its existing fonts. Reaction cues respect Empathy.

## Headless verification

Build in this lane with system dependencies (available in the current container):

```
timeout 120 cmake -S . -B build-desktop -DFALLOUT_VENDORED=OFF -DCMAKE_BUILD_TYPE=Release
timeout 240 cmake --build build-desktop -j8 --target fallout2-ce
LOADARGS=--dev-new-game timeout 100 .rig/run.sh display-final displayproof
LOADARGS=--dev-new-game timeout 100 .rig/run.sh layouts-final layoutpreview
LOADARGS=--dev-new-game timeout 100 .rig/run.sh readable-final readabilityproof
LOADARGS=--dev-new-game timeout 100 .rig/run.sh pacing-final framepace
```

Each scenario creates both top and bottom PNGs and a `log.txt` in `.rig/shots/<name>`.
`displayproof` asserts dimming, touch/pad wake, scanline preview and discard.
`layoutpreview` positions the list without fling timing, chooses all four
layout/theme options through widgets and asserts
both live values and restoration. `readabilityproof` shows normal/large fonts,
scanlines, low HP, lossy barter totals and a synthetic Empathy reaction fixture.
`framepace` logs 182-frame intervals and asserts timing within the headless budget.

The supplied save initially failed because the rig's writable maps/proto paths
were symlinked outside the lane. Tests use a new game and lane-local scratch
maps/proto directories instead; no game data was copied. Initial `before`, `after`
and `display` runs reached the main menu and are not gameplay evidence.

For the matched `framepace` comparison, the baseline rebuilt the original
`fps_limiter.cc` and `mui.cc` presentation paths with the new measurement scenario;
other code and the rig were identical. The final paths were restored and rebuilt.

| Presentation | Balanced ms/frame | Saver ms/frame |
| --- | ---: | ---: |
| Original limiter + bottom gate (`pacing-before/log.txt`) | 17.0 | 17.0 (mode did not exist) |
| Fractional pacing + unified surfaces (`pacing-after/log.txt`) | 17.4 | 33.9 |

The final timing-assertion rerun (`pacing-final/log.txt`) measured 17.34 ms
balanced and 33.82 ms saver; both targets passed.

These software/offscreen measurements include screenshot overhead. They establish
the 30 Hz cap and comparable 60 Hz throughput, not on-device battery savings or
hardware-vsync jitter. Key reviewed shots are `d06_idle_dim_bottom.png`,
`d08_touch_wake_bottom.png`, `d11_pad_wake_bottom.png`, `l10_dialogue_preview.png`,
`l13_theme_preview_bottom.png`, `r02_low_hp_label_bottom.png`,
`r09_total_warning_bottom.png` and `r12_empathy_reactions_bottom.png` in the final
scenario directories.
