// Standalone lane only. CMake excludes this when L1's draw_recorder arrives.
#include "hd/draw_list.h"
#include "hd/draw_recorder.h"
namespace fallout::hd {
const std::vector<DrawRecord>& drawRecords()
{
    static std::vector<DrawRecord> records;
    return records;
}
const std::vector<unsigned char>& drawTextPixels(int)
{
    static std::vector<unsigned char> pixels;
    return pixels;
}
bool drawListComplete() { return false; }
bool drawMapDirty() { return false; }
void drawClearMapDirty() { }
bool drawEmitFullFrame() { return false; }
} // namespace fallout::hd
