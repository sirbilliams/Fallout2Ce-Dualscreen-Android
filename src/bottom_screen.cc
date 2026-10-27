#include "bottom_screen.h"

#include <stdint.h>

#include <mutex>

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

namespace fallout {

namespace {

    constexpr const char* kLogTag = "BottomScreen";

    // The surface from BottomScreen.java. Set and dropped on the UI thread,
    // read from the game's thread: guarded by `gMutex`.
    std::mutex gMutex;
    ANativeWindow* gWindow = nullptr;
    int gWidth = 0;
    int gHeight = 0;

    // A pixel of an RGBX_8888 buffer (bytes R, G, B, X in memory).
    uint32_t pixel(uint8_t r, uint8_t g, uint8_t b)
    {
        return 0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
    }

    // Until the panels draw there: a pattern showing the screen works end to
    // end (Pip-Boy green, a border at the very edges, a red mark in the top
    // left corner for orientation, a cross at the center).
    void drawTestPattern(ANativeWindow* window)
    {
        if (ANativeWindow_setBuffersGeometry(window, 0, 0, WINDOW_FORMAT_RGBX_8888) != 0) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag, "setBuffersGeometry failed");
            return;
        }

        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(window, &buffer, nullptr) != 0) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag, "lock failed");
            return;
        }

        const uint32_t background = pixel(10, 31, 10);
        const uint32_t border = pixel(47, 209, 47);
        const uint32_t mark = pixel(220, 40, 40);
        const int borderWidth = 6;
        const int markSize = 48;
        const int crossHalf = buffer.width < buffer.height ? buffer.width / 6 : buffer.height / 6;
        const int centerX = buffer.width / 2;
        const int centerY = buffer.height / 2;

        uint32_t* rows = static_cast<uint32_t*>(buffer.bits);
        for (int y = 0; y < buffer.height; y++) {
            uint32_t* row = rows + static_cast<size_t>(y) * buffer.stride;
            for (int x = 0; x < buffer.width; x++) {
                uint32_t color = background;
                if (x < borderWidth || y < borderWidth || x >= buffer.width - borderWidth || y >= buffer.height - borderWidth) {
                    color = border;
                } else if (x < borderWidth + markSize && y < borderWidth + markSize) {
                    color = mark;
                } else if ((x >= centerX - 2 && x < centerX + 2 && y > centerY - crossHalf && y < centerY + crossHalf)
                    || (y >= centerY - 2 && y < centerY + 2 && x > centerX - crossHalf && x < centerX + crossHalf)) {
                    color = border;
                }
                row[x] = color;
            }
        }

        ANativeWindow_unlockAndPost(window);
    }

} // namespace

bool bottomScreenAvailable()
{
    std::lock_guard<std::mutex> lock(gMutex);
    return gWindow != nullptr;
}

void bottomScreenGetSize(int* width, int* height)
{
    std::lock_guard<std::mutex> lock(gMutex);
    *width = gWindow != nullptr ? gWidth : 0;
    *height = gWindow != nullptr ? gHeight : 0;
}

} // namespace fallout

extern "C" JNIEXPORT void JNICALL Java_com_alexbatalov_fallout2ce_BottomScreen_nativeSurfaceChanged(JNIEnv* env, jclass, jobject surface, jint width, jint height)
{
    using namespace fallout;

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "no native window for the surface");
        return;
    }

    std::lock_guard<std::mutex> lock(gMutex);
    if (gWindow != nullptr) {
        ANativeWindow_release(gWindow);
    }
    gWindow = window;
    gWidth = width;
    gHeight = height;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "surface %dx%d", width, height);

    drawTestPattern(gWindow);
}

extern "C" JNIEXPORT void JNICALL Java_com_alexbatalov_fallout2ce_BottomScreen_nativeSurfaceDestroyed(JNIEnv*, jclass)
{
    using namespace fallout;

    std::lock_guard<std::mutex> lock(gMutex);
    if (gWindow != nullptr) {
        ANativeWindow_release(gWindow);
        gWindow = nullptr;
    }
    gWidth = 0;
    gHeight = 0;
}
