# Dustlight GLES map lane (L3)

Source: Dustlight c1752ecc; attribution and license remain in
[DUSTLIGHT-NOTICE.md](DUSTLIGHT-NOTICE.md). No art or UI skin is bundled.
Contract headers in `src/hd` are unchanged.

Build with `-DHD=ON` and enable `[hd] enabled=1`, `tier=2` (2–4),
`hd_art_dir=hd`, `vram_budget_mb=256` in fallout2.cfg. Disabled is the
existing SDL path. SDL's `opengles2` driver owns the ES 3.1+ context.

`world_view` composites the original world texture first, then flushes SDL
and overlays the GLES map before its overlay object and the touch HUD.
Each logical pixel must match the CPU buffer before HD may replace it;
outline tags and unrelated palettes select CE colors. With no selected HD
frames the entire composite stays palette-exact. This glue targets the
fork's enabled world view; disabled world view retains CE rendering.

The exact and HD passes replay Dustlight's draw list in order. Compute
reads the destination through a sampler and writes its own pixel through
an image; image/texture barriers order sprites, implementing Dustlight's
`color + destination * factor` equation without dual-source blending.
G-buffer coverage accepts R8 or RGBA8. Core ES cannot image-store R8, so
R8 uses an RGBA8 scratch texture and a framebuffer blit. Occluder max uses
SSBO atomics and resolves into R32UI; no image-atomic extension is needed.

BC7 texture arrays use EXT_texture_compression_bptc, otherwise the local
all-eight-mode decoder uploads RGBA8. `[hd_debug] bc7_cpu=1` forces that
fallback. Page count uses actual compressed/decoded storage, with the
upstream LRU/map-pinning and per-build upload budget. The pack worker
never calls GL. GPU APIs run on the SDL context's render thread.

`hd_map_test` uses SDL offscreen/EGL/llvmpipe, direct draw-list fixtures,
and generated pack.json/BC7 DDS files. It checks all BC7 modes against
Mesa, exact scaled pixels, table/egg/text/outline/cycle rules, both upload
paths at all tiers, hash fallback, G-buffer/R8/SSBO paths, SDL state and
overlay preservation, final HD substitution, fades, gamma and snapshots.
CTest supplies the headless environment and a 60-second timeout.

Until L1 merges, CMake selects lane-local portable parser/shape/palette
copies and `l3_draw_fixture.cc` (an empty recorder). The game therefore
uses CE fallback; tests supply real fixture lists. Resolve CMake overlap
when merging L1. Define `FALLOUT_HD_LIGHTING` on the game target after L4's
lighting/post sources are linked; map_layer then calls their unchanged
contracts. Tests compile this glue with identity L4 fixtures. Actual L4
lighting/post, Android/device performance, real packs and a complete
hooked game HD frame are not verified by this lane. Overlay text functions
are no-ops because this fork has no Dustlight ImGui overlay.
