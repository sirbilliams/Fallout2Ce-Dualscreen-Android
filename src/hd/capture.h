#ifndef FALLOUT_HD_CAPTURE_H_
#define FALLOUT_HD_CAPTURE_H_

#include <string>

namespace fallout::hd {

// Redraws the whole map window with the draw-list recorder on and writes
// <dir>/<stem>.json (draw list, lights, tables, camera) and <dir>/<stem>.png
// (the 8-bit map window as an indexed PNG). An empty stem means
// <map>_e<elevation>_<game time>. Returns false and logs on failure.
bool captureMapView(const std::string& dir, const std::string& stem, std::string* jsonPathOut);

} // namespace fallout::hd

#endif /* FALLOUT_HD_CAPTURE_H_ */
