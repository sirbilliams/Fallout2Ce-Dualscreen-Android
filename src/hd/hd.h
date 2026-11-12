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
//   capture_dir=hd_captures   relative to the game data root
//   map_gpu=1                 GL map lane, hd/map_layer.h
//   tier=2                    art scale; interpreted by the GL art/map lanes
//   hd_art=1                  HD art in the GPU map, hd/hd_art.h
//   hd_art_dir=hd             art packs, relative to the game data root
//   vram_budget_mb=512        GPU memory for HD art
//   edge_fill=0               preserve the 1x tiling art mask's saw-tooth edge
//   edge_clamp=1              keep edge cells within the original colors
//
//   auto_screen=0 (opt-in 1)  first start: borderless at the desktop resolution,
//                             scale from the installed art (default 1, set to 0
//                             once done; hd/first_run.h)
//
//   Hotkeys: Ctrl+F8 HD art and Ctrl+F7 lighting comparison (GL lanes).
//   With enabled=1: Ctrl+F12 capture, Ctrl+F10 map self-test, Ctrl+F9 map
//   on/tinted/off, Ctrl+F6 light effect, Ctrl+F5 post effects, Ctrl+F3 fades.
//   D3D presenter/ImGui overlay, frame-sequence capture and HD UI are dropped.
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
//   cycle_scan=1              capture mode: scan every map for palette-cycle
//                             colors instead (hd/cycle_scan.h)
//   ui_walk=1                 UI walk (P7b): Ctrl+F1 opens the world map, Ctrl+F2 a
//                             dialogue with the nearest critter that has one; the
//                             Pip-Boy opens without one (the walk's save has none)
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
