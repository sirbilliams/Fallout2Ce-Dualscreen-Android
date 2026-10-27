#include "bottom_screen.h"

#include <string.h>

#include <vector>

namespace fallout {

// Desktop builds have no second screen; tests make one (bottom_screen.h
// `bottomScreenSetTest`) to run the bottom screen's drawing and touches
// without a device.

namespace {

    int gWidth = 0;
    int gHeight = 0;
    float gDensity = 1.0f;
    std::vector<unsigned char> gFrame;

} // namespace

bool bottomScreenAvailable()
{
    return gWidth > 0 && gHeight > 0;
}

void bottomScreenGetSize(int* width, int* height)
{
    *width = gWidth;
    *height = gHeight;
}

float bottomScreenGetDensity()
{
    return gDensity;
}

void bottomScreenPostFrame(const unsigned char* pixels, int width, int height, int pitch)
{
    if (width != gWidth || height != gHeight) {
        return;
    }
    gFrame.resize(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; y++) {
        memcpy(gFrame.data() + static_cast<size_t>(y) * width * 4, pixels + static_cast<size_t>(y) * pitch, static_cast<size_t>(width) * 4);
    }
}

void bottomScreenSetTest(int width, int height, float density)
{
    gWidth = width;
    gHeight = height;
    gDensity = density;
    gFrame.clear();
}

const unsigned char* bottomScreenTestFrame()
{
    return gFrame.empty() ? nullptr : gFrame.data();
}

} // namespace fallout
