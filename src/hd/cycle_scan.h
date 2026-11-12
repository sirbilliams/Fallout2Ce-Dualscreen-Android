#ifndef FALLOUT_HD_CYCLE_SCAN_H_
#define FALLOUT_HD_CYCLE_SCAN_H_

#include <string>

#include "map_defs.h"

namespace fallout::hd {

// Palette-cycle scan (PLAN.md P6.0), part of the scripted capture mode:
//
//   [hd_debug] cycle_scan=1   with capture_map=<any map> (the new-game load)
//
// Loads every map in maps.txt and counts, per elevation and cycle range
// (cycle.cc: slime, monitors, slow fire, fast fire, shoreline, alarm), the
// pixels of those colors in the art of its floor and roof tiles and objects
// (current frame and rotation), and finds the screen-sized area with the
// most, so test scenes can be picked per range. Writes
// <outDir>/cycle_scan.tsv, one row per map, elevation and range.
// Returns the process exit code.
int runCycleScan(int (*loadNewMap)(char* mapFileName), const std::string& firstMap, const std::string& outDir);

// The same scan on the loaded map's `elevation` for one range ("slow_fire",
// "shoreline", ...): the tile to center a capture on to show the most of
// it, among views the camera can reach (P7a.6, [hd_debug] capture_cycle).
// -1 if the elevation has none; `pixels` gets the view's count.
int cycleScanBestTile(MapElevation elevation, const char* range, int* pixels);

} // namespace fallout::hd

#endif /* FALLOUT_HD_CYCLE_SCAN_H_ */
