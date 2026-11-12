#ifndef FALLOUT_HD_HD_H_
#define FALLOUT_HD_HD_H_

#include "config.h"

namespace fallout::hd {

// Dustlight engine module (PLAN.md). Upstream files call only the
// functions in this header and in hd/draw_recorder.h; everything else in
// src/hd is private to the module.
//
// Configuration lives in fallout2.cfg:
//
//   [hd]
//   enabled=1                 developer module on: captures, self-tests, all
//                             hotkeys (default 0; the HD renderer itself runs
//                             either way, from the keys below)
//   capture_dir=<data_root>\captures
//   renderer=d3d11            Direct3D 11 presentation (default; sdl = CE's own), hd/d3d11_presenter.h
//   map_gpu=1                 the map drawn on the GPU (default 1; d3d11 only), hd/map_layer.h
//   hd_art=1                  HD art in the GPU map (default 1), hd/hd_art.h
//   hd_art_dir=<data_root>\hdpack   HD art packs (default "hd")
//   vram_budget_mb=512        GPU memory for HD art
//   ui_hd=1                   HD art in CE's own screens (default 1, hd/ui_layer.h)
//   edge_fill=0               tiling HD art (floors, roofs, walls) keeps the 1x mask's saw-tooth edge
//   edge_clamp=1              tiling HD art's edge cells kept within the original colors
//                             around them: 1 floors (default), 2 floors and roofs, 3 walls
//                             too, 0 off (0.9.0's look, dashed lines on some floor seams)
//   ui_hard_alpha=1           HD UI art edges thresholded instead of blended
//   auto_screen=1             first start: borderless at the desktop resolution,
//                             scale from the installed art (default 1, set to 0
//                             once done; hd/first_run.h)
//
//   Hotkeys (main game loop), for every player: Ctrl+F8 HD art on / off,
//   Ctrl+F7 new / original light. With enabled=1 also: Ctrl+F12 capture,
//   Ctrl+F11 developer overlay, Ctrl+F10 map self-test, Ctrl+F9 GPU map and
//   HD UI on / tinted / off, Ctrl+F6 one light effect off at a time,
//   Ctrl+F5 post effects (all on, one off at a time, all off; hd/map_post.h).
//
//   [hd_debug]                scripted capture mode (PLAN.md P1.3), usually
//                             passed as command-line overrides, e.g.
//                             fallout2-ce.exe "[hd_debug]capture_map=arvillag.map"
//   capture_map=arvillag.map  start straight into this map, capture, quit
//   capture_elevation=0       -1 (default) keeps the map's entrance elevation
//   capture_tile=20100        hex to center the camera on, -1 = map default
//   capture_dude_tile=-1      hex to move the player to, -1 = map default
//   capture_time=2200         time of day HHMM, -1 = new-game default
//   capture_settle_ms=2000    run the game this long before capturing
//   capture_no_cursor=1       no hex cursor in the picture (store stills, clip)
//   capture_name=arvillag_day output file stem (default <map>_e<elev>_<time>)
//   inject_keys=8000:394      feed key codes (kb.h) into the input queue this many
//                             ms after start; drives hotkeys in unattended tests
//   map_stress=1              capture mode: translucency, outlines, floating text,
//                             scrolling and moving critters for the GPU map test
//   map_selftest_ms=1500      run the GPU map self-test this often
//   present_selftest_ms=1500  run the presenter readback test this often
//   cycle_scan=1              capture mode: scan every map for palette-cycle
//                             colors instead (hd/cycle_scan.h)
//   ui_walk=1                 UI walk (P7b): Ctrl+F1 opens the world map, Ctrl+F2 a
//                             dialogue with the nearest critter that has one; the
//                             Pip-Boy opens without one (the walk's save has none)
//   frame_seq_count=60        Ctrl+F4 records this many frames (hd/d3d11_presenter.h)
//   fade_test=1               Ctrl+F3 runs the game's fades over the map (P6.4)

// First start of a release install (hd/first_run.h): screen settings from
// the desktop and the installed HD art. Called by game_config.cc on the
// loaded fallout2.cfg; true = the file should be written.
bool firstRunScreenSetup(Config* config);

// Called at the end of gameInitWithOptions / start of gameExit.
void init(bool isMapper);
void exit();

bool enabled();

// Debug hotkeys from the main game loop (gameHandleKey). Returns true when
// the key belonged to the HD module.
bool handleKey(int keyCode);

// [hd_debug] ui_walk=1 (P7b).
bool uiWalk();

// Scripted capture mode (PLAN.md P1.3): [hd_debug] capture_map is set.
bool scriptedCaptureRequested();

// Loads the capture map through `loadNewMap` (main.cc's new-game loader),
// captures, and returns the process exit code. The caller shuts the game down.
int runScriptedCapture(int (*loadNewMap)(char* mapFileName));

// Scripted play (P7a.6 combat benchmark): capture_map plus [hd_debug]
// capture_play=1. prepareScriptedPlay sets the map up as a capture would
// (elevation, tile, time) and returns 0; main.cc then runs the real game
// loop, driven by [hd_debug] inject_keys, until [hd_debug] quit_after_ms
// (any run: a clean exit that many ms after init).
bool scriptedPlayRequested();
int prepareScriptedPlay(int (*loadNewMap)(char* mapFileName));

} // namespace fallout::hd

#endif /* FALLOUT_HD_HD_H_ */
