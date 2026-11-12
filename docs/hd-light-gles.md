# GLES map light and post (lane L4)

Ported from Dustlight c1752ecc; see DUSTLIGHT-NOTICE.md and ADR-0012/0013
in the read-only upstream. No UI skin or ImGui dependency.

`hd-light` is a static CMake target when `HD=ON`. L1/L3 link it and supply
`lightSceneGather`, `mapRendererWantShapes`, `mapRendererDrawGbuffer`,
`mapRendererHdUav`, and `mapRendererDrawHd`. The lane test supplies those
functions only in its own translation unit. No renderer implementation is
included in the module. Init/shutdown require a current ES 3.1 context;
shutdown must precede context destruction. Presentation owns GL state
restoration (as for the renderer itself). `mapLightingDrawOverlay` and
`mapPostDrawOverlay` intentionally have no UI implementation.

All configuration keys documented in map_lighting.h and map_post.h are
read at init, with Dustlight's defaults and category tuning. `sun_path`,
`sun_swing`, `sun_low`, `knee` and `tonemap_white` also follow upstream.

| hd_light quality | Enabled effects, subject to individual switches |
|---|---|
| off | Original-light renderer, no lighting compute or bloom source |
| low | Normals, colors, pools, tonemap, night; no shadows/sun/AO/GI/bloom |
| medium | Low plus shadows, sun, AO, bloom; no GI |
| high (default) | All, including four radiance cascades |

`mode=original` always calls the original-light renderer; Ctrl+F7 toggles
mode but quality=off remains off. With all light switches zero, the light
pass returns the smoothed CE input field without day attenuation or HDR
limiting. This is field equivalence, whereas mode=original uses CE's
per-object light. Unlit materials, palette emissives and coverage retain
the renderer's rules. Post has its separate `hd_post.enabled` switch;
grade/vignette/grain may still be requested with original lighting.

G-buffer textures are allocated by L4 and populated by L3: albedo and
surface RGBA16F; premultiplied base/detail normals, material and coverage
RGBA8. Coverage has glow and cycle emission in G/B, as in Dustlight's
actual shader (the inherited header's R8 comment is stale). Normals and
heights come from the 1x silhouette through L3, not from this light pass.
The R32UI occluder grid stores positive float heights as bits. Half-size
occluder maxima use R32F with nearest texel fetches; shadow layers and
sun/AO use RGBA16F because
R16F/RG16F are not ES 3.1 image-store formats. Gaussian field gathering
replaces additive splatting, preserving its 3-sigma footprint and metric.
All compute writes have explicit image/texture/framebuffer barriers. Only
image units 0 and 1 are used, within the minimum ES 3.1 limits. Shadow
filtering uses half floats, without requiring float32 linear filtering.

Shader edits go in `src/hd/shaders/*.glsl`, then run
`python3 tools/hd/embed_shaders.py`. Individual uniforms intentionally avoid
HLSL cbuffer versus std140 vec3 packing differences. No runtime shader
files or translation tools are needed.

`hd_light_test` creates a surfaceless EGL ES context with llvmpipe and a
synthetic six-target G-buffer. It checks original/off identity, all-switches-
off field identity, a lamp's falloff and heightfield shadow, light colors,
quality dispatch selection, finite shoulder/AgX results, and bloom/post.
The test wraps compute dispatches for EXT_disjoint_timer_query readings
and dispatch+finish elapsed time. Driver query results of 1 ns are recorded
as reported, not treated as credible performance measurements. Elapsed
time includes synchronization and initial shader JIT. No performance goal
is inferred. Actual pack fidelity and Android performance require L3 and
later device testing.

## Lane validation, 2026-11-12

HD=ON full engine build: `[OS-RECEIPT id=5f511cec]`.
HD=OFF full engine build: `[OS-RECEIPT id=73e82d23]`.
HD=OFF ctest: 7/7, `[OS-RECEIPT id=e1a73103]`.
HD=ON ctest: 8/8, `[OS-RECEIPT id=1f6b109b]`.

Mesa 25.2.8, llvmpipe LLVM 20.1.2 (256 bits), ES 3.2 context requested as
ES 3.1; 128x64 synthetic map, 288x448 ground grid, 320x144 shadow layers.
No real game/art-pack run or Android test was performed in this lane.
The synthetic fake is confined to tests/hd_light_test.cc. Developer overlay
functions have no UI implementation. No contract signatures changed.

Every EXT timer query returned 0.000001 ms (1 ns), with no disjoint flag.
This is the driver result, not a credible GPU benchmark. The companion
elapsed times below include dispatch, glFinish, query retrieval and first-use
JIT, averaged across the test's differing feature configurations.

| Pass | Query ms | Dispatch + finish ms | Samples |
|---|---:|---:|---:|
| csDown | 0.000001 | 4.289 | 3 |
| csField | 0.000001 | 14.803 | 17 |
| csGiCascade | 0.000001 | 14.543 | 16 |
| csGiResolve | 0.000001 | 8.710 | 4 |
| csGiScene | 0.000001 | 34.927 | 4 |
| csLight | 0.000001 | 76.467 | 17 |
| csOccluderMax | 0.000001 | 17.861 | 17 |
| csPost | 0.000001 | 23.778 | 1 |
| csShadowMaps | 0.000001 | 13.941 | 8 |
| csSunAo | 0.000001 | 21.953 | 17 |
| csUp | 0.000001 | 8.167 | 2 |

Full logs and intermediate failed attempts: `/home/bill/Projects/f2ds-hd-run/light/evidence/`.
