#include "gamepad.h"

#include <math.h>

#include "combat.h"
#include "game_commands.h"
#include "mui.h"
#include "svga.h"
#include "world_view.h"

namespace fallout {

namespace {

    // Stick travel ignored around the center (worn sticks rest off it).
    constexpr float kDeadZone = 0.22f;
    // The view moves this many screen widths a second at full tilt.
    constexpr float kPanScreensPerSecond = 0.9f;
    // D-pad zoom steps.
    constexpr float kZoomStep = 1.15f;
    // Select held this long ends the turn.
    constexpr Uint32 kEndTurnHoldMs = 600;

    bool gStarted = false;
    Uint32 gLastTick = 0;
    Uint32 gSelectDownTime = 0;
    bool gSelectFired = false;

    // The map is played: no screen of the mobile UI takes input (on either
    // screen of the device). Commands for the map wait otherwise.
    bool onMap()
    {
        return !muiHasModal();
    }

    void openController(int index)
    {
        if (!SDL_IsGameController(index)) {
            return;
        }
        // SDL keeps it open; its events come with its instance id.
        SDL_GameControllerOpen(index);
    }

    float axis(SDL_GameController* controller, SDL_GameControllerAxis which)
    {
        float value = SDL_GameControllerGetAxis(controller, which) / 32767.0f;
        if (fabsf(value) < kDeadZone) {
            return 0.0f;
        }
        // Rescaled past the dead zone, so movement starts smoothly.
        float sign = value < 0.0f ? -1.0f : 1.0f;
        float magnitude = (fabsf(value) - kDeadZone) / (1.0f - kDeadZone);
        return sign * (magnitude > 1.0f ? 1.0f : magnitude);
    }

    void buttonDown(SDL_GameControllerButton button)
    {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_B:
            muiRequestBack();
            break;
        case SDL_CONTROLLER_BUTTON_START:
            if (onMap()) {
                gameCommandPost(GameCommandType::Menu);
            } else {
                muiRequestBack();
            }
            break;
        case SDL_CONTROLLER_BUTTON_BACK:
            gSelectDownTime = SDL_GetTicks();
            gSelectFired = false;
            break;
        default:
            break;
        }

        if (!onMap()) {
            return;
        }

        switch (button) {
        case SDL_CONTROLLER_BUTTON_Y:
            gameCommandPost(GameCommandType::Inventory);
            break;
        case SDL_CONTROLLER_BUTTON_X:
            gameCommandPost(GameCommandType::Skilldex);
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            gameCommandPost(GameCommandType::Character);
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            gameCommandPost(GameCommandType::Pipboy);
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            gameCommandPost(GameCommandType::Automap);
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            gameCommandPost(GameCommandType::Sneak);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            worldViewZoomBy(kZoomStep, screenGetWidth() / 2.0f, screenGetHeight() / 2.0f);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            worldViewZoomBy(1.0f / kZoomStep, screenGetWidth() / 2.0f, screenGetHeight() / 2.0f);
            break;
        default:
            break;
        }
    }

    void buttonUp(SDL_GameControllerButton button)
    {
        if (button == SDL_CONTROLLER_BUTTON_BACK) {
            gSelectDownTime = 0;
        }
    }

    // Triggers are axes: pressed past half way, once until released.
    bool gLeftTrigger = false;
    bool gRightTrigger = false;

    void triggers(SDL_GameController* controller)
    {
        bool left = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384;
        bool right = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384;
        if (left && !gLeftTrigger && onMap()) {
            gameCommandPost(GameCommandType::SwapHands);
        }
        if (right && !gRightTrigger && onMap()) {
            gameCommandPost(GameCommandType::UseItem);
        }
        gLeftTrigger = left;
        gRightTrigger = right;
    }

} // namespace

void gamepadInit()
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        return;
    }
    gStarted = true;

    for (int index = 0; index < SDL_NumJoysticks(); index++) {
        openController(index);
    }
}

bool gamepadHandleEvent(const SDL_Event* event)
{
    if (!gStarted) {
        return false;
    }

    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED:
        openController(event->cdevice.which);
        return true;
    case SDL_CONTROLLERDEVICEREMOVED: {
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(event->cdevice.which);
        if (controller != nullptr) {
            SDL_GameControllerClose(controller);
        }
        return true;
    }
    case SDL_CONTROLLERBUTTONDOWN:
        buttonDown(static_cast<SDL_GameControllerButton>(event->cbutton.button));
        return true;
    case SDL_CONTROLLERBUTTONUP:
        buttonUp(static_cast<SDL_GameControllerButton>(event->cbutton.button));
        return true;
    default:
        return false;
    }
}

void gamepadTick()
{
    if (!gStarted) {
        return;
    }

    Uint32 now = SDL_GetTicks();
    float seconds = gLastTick != 0 ? (now - gLastTick) / 1000.0f : 0.0f;
    gLastTick = now;
    // A stall (loading) doesn't throw the view across the map.
    if (seconds > 0.1f) {
        seconds = 0.1f;
    }

    // Select held: end turn, once per press.
    if (gSelectDownTime != 0 && !gSelectFired && now - gSelectDownTime >= kEndTurnHoldMs) {
        gSelectFired = true;
        if (onMap() && isInCombat()) {
            gameCommandPost(GameCommandType::EndTurn);
        }
    }

    for (int index = 0; index < SDL_NumJoysticks(); index++) {
        if (!SDL_IsGameController(index)) {
            continue;
        }
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(index));
        if (controller == nullptr) {
            continue;
        }

        triggers(controller);

        if (!onMap() || seconds <= 0.0f) {
            continue;
        }

        float x = axis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
        float y = axis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
        if (x != 0.0f || y != 0.0f) {
            float speed = screenGetWidth() * kPanScreensPerSecond * seconds;
            // Stick right: the view goes right, the map moves left.
            worldViewPanBy(-x * speed, -y * speed);
        }
    }
}

} // namespace fallout
