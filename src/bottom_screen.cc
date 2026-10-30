#include "bottom_screen.h"
#include "dev_autotest.h"

#include <stdint.h>
#include <string.h>

#include <mutex>

#include <SDL.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "mui.h"

namespace fallout {

namespace {

    constexpr const char* kLogTag = "BottomScreen";

    // Finger ids of the bottom screen's touches: Android numbers each
    // screen's pointers from 0, and the game looks fingers up by id alone
    // (input.cc), so the bottom's are moved out of the top's range.
    constexpr SDL_FingerID kFingerBase = 0x7B000000;

    // The surface from BottomScreen.java. Set and dropped on the UI thread,
    // drawn into from the game's thread: guarded by `gMutex`.
    std::mutex gMutex;
    ANativeWindow* gWindow = nullptr;
    int gWidth = 0;
    int gHeight = 0;
    // The window's buffers are set to the surface's size and format.
    bool gGeometrySet = false;
    float gDensity = 2.0f;

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

float bottomScreenGetDensity()
{
    std::lock_guard<std::mutex> lock(gMutex);
    return gDensity;
}

void bottomScreenPostFrame(const unsigned char* pixels, int width, int height, int pitch)
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (gWindow == nullptr || width != gWidth || height != gHeight) {
        return;
    }

    if (!gGeometrySet) {
        if (ANativeWindow_setBuffersGeometry(gWindow, width, height, WINDOW_FORMAT_RGBA_8888) != 0) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag, "setBuffersGeometry failed");
            return;
        }
        gGeometrySet = true;
    }

    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(gWindow, &buffer, nullptr) != 0) {
        return;
    }

    int rows = height < buffer.height ? height : buffer.height;
    int columns = width < buffer.width ? width : buffer.width;
    unsigned char* destination = static_cast<unsigned char*>(buffer.bits);
    size_t destinationPitch = static_cast<size_t>(buffer.stride) * 4;
    for (int y = 0; y < rows; y++) {
        memcpy(destination + y * destinationPitch, pixels + static_cast<size_t>(y) * pitch, static_cast<size_t>(columns) * 4);
    }

    ANativeWindow_unlockAndPost(gWindow);
}

} // namespace fallout

extern "C" JNIEXPORT void JNICALL Java_com_alexbatalov_fallout2ce_BottomScreen_nativeSurfaceChanged(JNIEnv* env, jclass, jobject surface, jint width, jint height, jfloat density)
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
    gGeometrySet = false;
    if (density > 0.0f) {
        gDensity = density;
    }
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "surface %dx%d, density %.2f", width, height, gDensity);
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
    gGeometrySet = false;
}

// A touch on the bottom screen (UI thread): into the game's event queue as
// an SDL finger event of the bottom screen's touch device (mui.h
// `kMuiBottomTouchId`), where the mobile UI's bottom surface takes it.
// [action] 0 down, 1 move, 2 up; [x] and [y] 0..1 across the screen.
extern "C" JNIEXPORT void JNICALL Java_com_alexbatalov_fallout2ce_BottomScreen_nativeTouch(JNIEnv*, jclass, jint action, jint pointerId, jfloat x, jfloat y)
{
    using namespace fallout;

    SDL_Event event;
    SDL_zero(event);
    switch (action) {
    case 0:
        event.type = SDL_FINGERDOWN;
        break;
    case 1:
        event.type = SDL_FINGERMOTION;
        break;
    default:
        event.type = SDL_FINGERUP;
        break;
    }
    event.tfinger.touchId = kMuiBottomTouchId;
    event.tfinger.fingerId = kFingerBase + pointerId;
    event.tfinger.x = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    event.tfinger.y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
    event.tfinger.pressure = action == 2 ? 0.0f : 1.0f;
    event.tfinger.timestamp = uiTicks();
    SDL_PushEvent(&event);
}
