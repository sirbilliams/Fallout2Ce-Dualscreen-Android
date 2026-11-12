# Dustlight HD port (Android + desktop) - lanes and contracts
Date: 2026-11-12

## Goal
- Port Dustlight (github.com/mrdushidush/dustlight, upstream commit c1752ecc, local copy ~/Projects/dustlight-study/src) HD map art + new lighting + bloom/tonemap to Fallout 2 CE dual-screen fork.
- Exclude HD UI skin (mui_* screens bypass CE window buffers).
- Feasibility study: ~/Projects/dustlight-study/feasibility.md.

## License
- Sustainable Use License (same as ours).
- Credit in docs/DUSTLIGHT-NOTICE.md.
- Art packs never bundled (UltraSharpV2 CC-BY-NC-SA); user downloads them.

## GPU API
- OpenGL ES 3.1 on both Android and desktop.
- Linked directly (Android GLESv3, desktop Mesa GLESv2).
- Headers vendored in third_party/khronos.
- HLSL SM5 shaders become GLSL ES 3.10.
- Desktop tests use ES 3.1 context (SDL + EGL, Mesa llvmpipe works headless).

## Presentation
- App draws with SDL_Renderer (svga.cc, world_view.cc compose texture).
- HD map renders into GL textures inside SDL's own GLES context: SDL_RenderFlush, raw GL into own FBOs, restore GL state, then result is composited where world_view composites today.
- Context must be created as ES 3.1 when HD is enabled.

## Build Flag
- FALLOUT_HD (CMake option HD, default OFF until lanes merge).
- Everything under src/hd/.
- Config section [hd] enabled=0 by default, tier=2, pack dir; [hd_light] as in Dustlight's map_lighting.h.

## Contract
- Base commit holds Dustlight's headers in src/hd/ with D3D handles replaced by GLuint (texture names) and _WIN32 guards replaced by FALLOUT_HD, plus src/hd/gl.h.
- Lanes keep these signatures; any change must be listed in the lane's final report.

## Fallbacks
- HD art missing or pixel-hash mismatch (e.g. RPU-changed frames) -> original art.
- BC7 without GL_EXT_texture_compression_bptc -> CPU decode to RGBA8.
- Lighting quality low/off for weak GPUs.
- HD off -> today's renderer, byte-identical.

## Lanes
*Each its own worktree + branch off dustlight-hd base:*
- **L1 hd-core**: Port portable modules (draw_recorder, light_scene, sprite_shape, cycle_scan, palette_fit, json_reader/writer, capture, first_run, present_stats, hd.cc control/config minus D3D) + CMake option + desktop unit tests.
- **L2 hd-hooks**: Add Dustlight's draw-list recording hooks (hd::record*) to diverged CE files: tile, object, text_object, art, heap, draw, game, game_config, main, text_font, worldmap etc. NOT svga.cc, movie.cc, world_view.cc. Compile against base headers with weak/stub impls if L1 absent. Hooks are no-ops when HD is off.
- **L3 hd-gles-map**: gl.cc, hd_art (pack loader, LRU page pool as GL texture array, BC7 upload/CPU fallback), map_renderer (draw-list replay, palette/fade/translucency/egg, dual-source blending replaced), map_layer, presentation integration in svga.cc/world_view.cc, desktop headless pixel tests vs the 8-bit frame.
- **L4 hd-gles-light**: map_lighting + map_post in GLES 3.1 compute (9 compute shaders), quality levels, tested on synthetic G-buffer textures; calls into map_renderer only through map_renderer.h.

## Later (not now)
- Android art-pack installer via ModDownloads (2x tier, ~1.5 GB, files/hd/).
- Settings toggle.
- Thor test.
- Merge order: L1 -> L2 -> L3 -> L4.

## Done means
- Desktop build + tests pass under airun with receipts.
- HD off is byte-identical to today.
- No Android gradle in the cx sandbox.
[OS-RECEIPT id=a892bd94 tool=fl model=gemma4:12b rc=0 time=24.0s]
[OS-RECEIPT id=203554a3 tool=fl-hdspec model=- rc=0 time=24.0s first="# Dustlight HD port (Android + desktop) - lanes and contracts"]

## Testing without the real pack
- The real art pack (Nexus mod 193) isn't on this PC yet. Tests use tiny synthetic packs written in Dustlight's format (`art/<type>/<name>/pack.json` + `albedo_<tier>x.dds`, BC7/DX10, mip 0), made by a test helper, never by downloading.
- Game data for headless runs: copy (never link or edit) from the rig the earlier tests used; see ~/Projects/f2ds-tc-research/sonora-report.md for how the base-game control run was set up.
- CMake: every lane adds its own files to the HD option block; merge conflicts there are expected and fine. L2's stub file is `src/hd/hooks_stub.cc`, dropped when L1 merges.
