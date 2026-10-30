// Exercise the real recognizer without a display, game data or SDL runtime.
// Compile this and src/touch.cc with FALLOUT_FORCE_TOUCH_ONLY defined.

#define SDL_MAIN_HANDLED

#include <stdio.h>
#include <stdlib.h>

#include "settings.h"
#include "touch.h"

#define CHECK(condition)                                                  \
    do {                                                                  \
        if (!(condition)) {                                               \
            fprintf(stderr, "FAIL: line %d: %s\n", __LINE__, #condition); \
            exit(EXIT_FAILURE);                                           \
        }                                                                 \
    } while (false)

namespace {

Uint32 gNow = 1000;
constexpr SDL_FingerID kPadFinger = 0x9AD;

} // namespace

namespace fallout {

Settings settings;

bool devAutotestIsEnabled() { return false; }
Uint32 uiTicks() { return gNow; }
int screenGetWidth() { return 640; }
int screenGetHeight() { return 480; }
float hudGetPixelsPerDp() { return 1.0f; }
bool touchControlsIsEnabled() { return true; }

} // namespace fallout

namespace {

using namespace fallout;

void reset()
{
    touch_forget_missing(nullptr, 0);
    Gesture gesture;
    while (touch_get_gesture(&gesture)) {
    }
    gNow += 1000;
}

SDL_TouchFingerEvent finger(SDL_FingerID id, float x = 0.25f, float y = 0.5f)
{
    SDL_TouchFingerEvent event = {};
    event.fingerId = id;
    event.touchId = 1;
    event.timestamp = gNow;
    event.x = x;
    event.y = y;
    return event;
}

void noGesture()
{
    Gesture gesture;
    CHECK(!touch_get_gesture(&gesture));
}

Gesture takeGesture(GestureType type, GestureState state)
{
    Gesture gesture;
    CHECK(touch_get_gesture(&gesture));
    CHECK(gesture.type == type);
    CHECK(gesture.state == state);
    CHECK(gesture.numberOfTouches == 1);
    noGesture();
    return gesture;
}

// Reproduce A/R3: the synthetic finger has left the provider's down list
// when FINGERUP is polled, before gesture recognition gets to see the end.
void completedTapSurvivesReconciliation(SDL_FingerID id)
{
    reset();
    auto down = finger(id);
    touch_handle_start(&down);
    CHECK(!touch_forget_missing(&id, 1, true));
    touch_process_gesture();
    noGesture();

    gNow += 70;
    auto up = finger(id);
    touch_handle_end(&up);
    CHECK(!touch_any_finger_down());
    CHECK(!touch_forget_missing(nullptr, 0, true));
    touch_process_gesture();
    auto gesture = takeGesture(kTap, kEnded);
    CHECK(gesture.x == 160 && gesture.y == 240);
    CHECK(gesture.time == gNow);
    touch_process_gesture();
    noGesture();
}

// A quick physical tap may have both events arrive in a single event poll.
void samePollTapSurvivesReconciliation()
{
    reset();
    auto down = finger(42);
    touch_handle_start(&down);
    gNow += 20;
    auto up = finger(42);
    touch_handle_end(&up);
    CHECK(!touch_forget_missing(nullptr, 0, true));
    touch_process_gesture();
    takeGesture(kTap, kEnded);
}

void missingHeldFingerIsStillCanceled()
{
    reset();
    auto down = finger(kPadFinger);
    touch_handle_start(&down);
    CHECK(touch_any_finger_down());
    gNow += 1000;
    CHECK(touch_forget_missing(nullptr, 0, true));
    CHECK(!touch_any_finger_down());
    touch_process_gesture();
    noGesture();
}

void missingFingerCancelsOtherPendingRelease()
{
    // Exercise both slot orders: the completed finger can be encountered
    // before or after the lost active finger during reconciliation.
    for (int order = 0; order < 2; order++) {
        reset();
        auto first = finger(45);
        auto second = finger(46, 0.5f);
        touch_handle_start(&first);
        touch_handle_start(&second);
        gNow += 70;
        auto up = finger(order == 0 ? 45 : 46);
        touch_handle_end(&up);
        CHECK(touch_forget_missing(nullptr, 0, true));
        CHECK(!touch_any_finger_down());
        touch_process_gesture();
        noGesture();
    }
}

// The default path is used for queue reset, focus loss and backgrounding.
// It must discard a received end rather than replay it in a new context.
void explicitResetCancelsCompletedTap()
{
    reset();
    auto down = finger(kPadFinger);
    touch_handle_start(&down);
    gNow += 70;
    auto up = finger(kPadFinger);
    touch_handle_end(&up);
    CHECK(touch_forget_missing(nullptr, 0));
    touch_process_gesture();
    noGesture();
}

void longPressEndsNormally()
{
    reset();
    auto down = finger(kPadFinger);
    touch_handle_start(&down);
    gNow += 600;
    touch_process_gesture();
    takeGesture(kLongPress, kBegan);

    gNow += 10;
    auto up = finger(kPadFinger);
    touch_handle_end(&up);
    CHECK(!touch_forget_missing(nullptr, 0, true));
    touch_process_gesture();
    takeGesture(kLongPress, kEnded);
    touch_process_gesture();
    noGesture();
}

void panEndsNormally()
{
    reset();
    auto down = finger(43);
    touch_handle_start(&down);
    gNow += 20;
    auto move = finger(43, 0.5f);
    touch_handle_move(&move);
    touch_process_gesture();
    takeGesture(kPan, kBegan);

    gNow += 20;
    auto up = finger(43, 0.5f);
    touch_handle_end(&up);
    CHECK(!touch_forget_missing(nullptr, 0, true));
    touch_process_gesture();
    takeGesture(kPan, kEnded);
    touch_process_gesture();
    noGesture();
}

} // namespace

int main()
{
    fallout::settings.touch.touch_hold_ms = 500;
    completedTapSurvivesReconciliation(kPadFinger);
    completedTapSurvivesReconciliation(44);
    samePollTapSurvivesReconciliation();
    missingHeldFingerIsStillCanceled();
    missingFingerCancelsOtherPendingRelease();
    explicitResetCancelsCompletedTap();
    longPressEndsNormally();
    panEndsNormally();
    puts("touch_gesture_test: all checks passed");
    return EXIT_SUCCESS;
}
