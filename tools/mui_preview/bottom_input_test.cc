// Bottom-screen input test (desktop, no device, no game data): a modal screen
// on the bottom surface with a button and a filter row, taps injected as
// bottom_screen.cc does on the device (SDL finger events of the bottom
// screen's touch device), frames drawn as renderPresent does. Prints what the
// widgets got.
#include <SDL.h>

#include <stdio.h>

#include <thread>

#include "bottom_screen.h"
#include "input.h"
#include "mui.h"
#include "mui_draw.h"

using namespace fallout;

namespace fallout {
bool gProgramIsActive = true;
}

namespace {

constexpr SDL_FingerID kFingerBase = 0x7B000000;
constexpr int kWidth = 1240;
constexpr int kHeight = 1080;

class TestScreen : public MuiScreen {
public:
    TestScreen() { modal = true; }
    MuiSurface surface() override { return MuiSurface::Bottom; }
    bool coversScreen() override { return true; }

    int taps = 0;
    int segment = 0;
    int segmentChanges = 0;

    void build(MuiContext& ui) override
    {
        if (ui.touchable("test.button", { 100.0f, 100.0f, 300.0f, 200.0f })) {
            taps++;
        }
        if (ui.segmented("test.segments", { 100.0f, 500.0f, 600.0f, 100.0f }, { U"A", U"B", U"C" }, &segment)) {
            segmentChanges++;
        }
    }
};

void inject(Uint32 type, int pointer, float x, float y)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    event.tfinger.touchId = kMuiBottomTouchId;
    event.tfinger.fingerId = kFingerBase + pointer;
    event.tfinger.x = x / kWidth;
    event.tfinger.y = y / kHeight;
    event.tfinger.pressure = type == SDL_FINGERUP ? 0.0f : 1.0f;
    event.tfinger.timestamp = SDL_GetTicks();
    bool taken = muiHandleFingerEvent(&event);
    if (!taken) {
        printf("  event %u not taken by the mobile UI\n", type);
    }
}

// As bottom_screen.cc `nativeTouch` does it: pushed into SDL's queue from
// another thread (Android's UI thread).
void push(Uint32 type, int pointer, float x, float y)
{
    std::thread thread([=]() {
        SDL_Event event;
        SDL_zero(event);
        event.type = type;
        event.tfinger.touchId = kMuiBottomTouchId;
        event.tfinger.fingerId = kFingerBase + pointer;
        event.tfinger.x = x / kWidth;
        event.tfinger.y = y / kHeight;
        event.tfinger.pressure = type == SDL_FINGERUP ? 0.0f : 1.0f;
        event.tfinger.timestamp = SDL_GetTicks();
        if (SDL_PushEvent(&event) != 1) {
            printf("  push failed: %s\n", SDL_GetError());
        }
    });
    thread.join();
}

void frames(SDL_Renderer* renderer, int count)
{
    for (int index = 0; index < count; index++) {
        // The game's input pass, as `inputGetInput` runs it.
        _GNW95_process_message();
        muiRenderBottom(renderer);
        SDL_Delay(17);
    }
}

} // namespace

int main(int, char**)
{
    SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS);
    SDL_Surface* target = SDL_CreateRGBSurfaceWithFormat(0, kWidth, kHeight, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(target);

    bottomScreenSetTest(kWidth, kHeight, 2.6f);

    TestScreen screen;
    // Pushed long before the touches, as the loot and inventory screens.
    muiPush(&screen);
    frames(renderer, 10);

    // A tap on the button.
    inject(SDL_FINGERDOWN, 0, 250.0f, 200.0f);
    frames(renderer, 6);
    inject(SDL_FINGERUP, 0, 250.0f, 200.0f);
    frames(renderer, 6);
    printf("button taps: %d (want 1)\n", screen.taps);

    // A quick tap (down and up before the next frame) on segment C.
    inject(SDL_FINGERDOWN, 1, 600.0f, 550.0f);
    inject(SDL_FINGERUP, 1, 600.0f, 550.0f);
    frames(renderer, 8);
    printf("segment: %d (want 2), changes: %d (want 1)\n", screen.segment, screen.segmentChanges);

    printf("frame posted: %s\n", bottomScreenTestFrame() != nullptr ? "yes" : "no");

    // The device's way: through SDL's queue and the game's input pass.
    int tapsBefore = screen.taps;
    push(SDL_FINGERDOWN, 0, 250.0f, 200.0f);
    frames(renderer, 6);
    push(SDL_FINGERUP, 0, 250.0f, 200.0f);
    frames(renderer, 6);
    printf("button taps through SDL's queue: %d (want 1)\n", screen.taps - tapsBefore);

    bool ok = screen.taps == 2 && screen.segment == 2;
    printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
