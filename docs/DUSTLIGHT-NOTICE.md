# Notice

**Dustlight** is a **modified version** of
[Fallout 2 Community Edition](https://github.com/fallout2-ce/fallout2-ce),
which is licensed under the Sustainable Use License v1.0 (see `LICENSE.md`).

- Modified by `mrdushidush` (GitHub), starting 2026-10-31.
- New code lives mainly in `src/hd/` and `tools/`. Changes to upstream files are
  kept to thin hooks. Every design decision is recorded in `docs/decisions/`.
- New code in this repository is licensed under the same Sustainable Use License
  v1.0 (see ADR-0007).

This repository contains no game data. Running it requires your own copy of
Fallout 2. "Fallout" is a trademark of its respective owner; this is an
unofficial, non-commercial project.

The GLES 3.1 map lighting and post shaders in `src/hd/shaders/` are ports
of Dustlight commit `c1752ecc`, specifically the embedded HLSL in
`map_lighting.cc` and `map_post.cc`. Their CE light-field, shadow, radiance
cascade, AgX, bloom and grading algorithms retain Dustlight's design and
comments. The G-buffer contract follows Dustlight's `map_renderer.cc`.
