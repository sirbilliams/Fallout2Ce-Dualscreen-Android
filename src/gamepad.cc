#include "gamepad.h"

#include <math.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>

#include "combat.h"
#include "game_commands.h"
#include "mui.h"
#include "mui_pad.h"
#include "settings.h"
#include "svga.h"
#include "touch_hud.h"
#include "world_view.h"

namespace fallout {

namespace {

    // Stick travel ignored around the center (worn sticks rest off it).
    constexpr float kDeadZone = 0.2f;
    // The view moves this many screen widths a second at full tilt.
    constexpr float kPanScreensPerSecond = 0.9f;
    // The map cursor crosses this many screens a second at full tilt (the
    // stick's travel is squared: slow is precise, a push crosses the screen;
    // SpongeBob's right stick).
    constexpr float kCursorScreensPerSecond = 0.75f;
    // The cursor moving this far (of the screen) with R3 held isn't a right click.
    constexpr float kR3MoveLimit = 0.02f;
    // D-pad zoom steps.
    constexpr float kZoomStep = 1.15f;
    // Select held this long ends the combat (a tap ends the turn).
    constexpr Uint32 kEndTurnHoldMs = 600;
    // Held d-pad / left stick in screens: first repeat, then every.
    constexpr Uint32 kRepeatFirstMs = 380;
    constexpr Uint32 kRepeatMs = 130;

    // What a button does on the map (`[touch] gamepad_bindings`).
    enum class Action {
        None,
        Click,
        Context,
        Back,
        Menu,
        Inventory,
        Skilldex,
        Character,
        Pipboy,
        Automap,
        Sneak,
        Swap,
        UseItem,
        Combat,
        Focus,
        QuickUse,
        ZoomIn,
        ZoomOut,
    };

    enum PadButton {
        PAD_A,
        PAD_B,
        PAD_X,
        PAD_Y,
        PAD_START,
        PAD_BACK,
        PAD_LB,
        PAD_RB,
        PAD_LS,
        PAD_RS,
        PAD_UP,
        PAD_DOWN,
        PAD_LEFT,
        PAD_RIGHT,
        PAD_LT,
        PAD_RT,
        PAD_COUNT,
    };

    const char* const kButtonNames[PAD_COUNT] = { "a", "b", "x", "y", "start", "back", "lb", "rb", "ls", "rs", "up", "down", "left", "right", "lt", "rt" };

    struct ActionName {
        const char* name;
        Action action;
    };

    const ActionName kActionNames[] = {
        { "none", Action::None },
        { "click", Action::Click },
        { "context", Action::Context },
        { "back", Action::Back },
        { "menu", Action::Menu },
        { "inventory", Action::Inventory },
        { "skilldex", Action::Skilldex },
        { "character", Action::Character },
        { "pipboy", Action::Pipboy },
        { "automap", Action::Automap },
        { "sneak", Action::Sneak },
        { "swap", Action::Swap },
        { "useitem", Action::UseItem },
        { "combat", Action::Combat },
        { "focus", Action::Focus },
        { "quickuse", Action::QuickUse },
        { "zoomin", Action::ZoomIn },
        { "zoomout", Action::ZoomOut },
    };

    constexpr const char* kDefaultBindings = "a=click,b=back,x=context,y=inventory,start=menu,back=combat,lb=character,rb=focus,"
                                             "ls=sneak,rs=automap,up=zoomin,down=zoomout,left=quickuse,right=pipboy,lt=swap,rt=useitem";

    Action gBindings[PAD_COUNT];
    std::string gBindingsSource = "\x01";

    void parseBindings(const char* text)
    {
        std::string list = text;
        // Pairs "button=action", separated by commas or spaces.
        size_t position = 0;
        while (position < list.size()) {
            size_t end = list.find_first_of(", ;", position);
            if (end == std::string::npos) {
                end = list.size();
            }
            std::string pair = list.substr(position, end - position);
            position = end + 1;
            size_t equals = pair.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            std::string button = pair.substr(0, equals);
            std::string action = pair.substr(equals + 1);
            for (char& c : button) {
                c = static_cast<char>(tolower(c));
            }
            for (char& c : action) {
                c = static_cast<char>(tolower(c));
            }
            for (int index = 0; index < PAD_COUNT; index++) {
                if (button != kButtonNames[index]) {
                    continue;
                }
                for (const ActionName& entry : kActionNames) {
                    if (action == entry.name) {
                        gBindings[index] = entry.action;
                    }
                }
            }
        }
    }

    // The defaults, then the setting over them; read again when it changes.
    void loadBindings()
    {
        const std::string& custom = settings.touch.gamepad_bindings;
        if (custom == gBindingsSource) {
            return;
        }
        gBindingsSource = custom;
        for (Action& action : gBindings) {
            action = Action::None;
        }
        parseBindings(kDefaultBindings);
        parseBindings(custom.c_str());
    }

    bool gStarted = false;
    Uint32 gLastTick = 0;
    Uint32 gSelectDownTime = 0;
    bool gSelectFired = false;

    // The map cursor (0..1 of the top screen).
    float gCursorX = 0.5f;
    float gCursorY = 0.5f;

    // R3 in stick mode: a short press clicks, held (`pad_r3_hold_ms`) it right clicks.
    bool gR3Down = false;
    bool gR3Fired = false;
    Uint32 gR3DownTime = 0;
    float gR3X = 0.0f;
    float gR3Y = 0.0f;

    // Held direction in screens, for repeats.
    bool gHeld = false;
    MuiPadDir gHeldDir = MuiPadDir::Down;
    PadButton gHeldButton = PAD_COUNT;
    Uint32 gNextRepeat = 0;
    // The left stick pushed past half way (a direction press).
    bool gStickHeld = false;

    // Automated tests: a virtual right stick for some frames.
    float gDevStickX = 0.0f;
    float gDevStickY = 0.0f;
    int gDevStickFrames = 0;

    // The map is played: no screen of the mobile UI takes input (on either
    // screen of the device). Commands for the map wait otherwise.
    bool onMap()
    {
        return !muiHasModal();
    }

    bool uiFocusOn()
    {
        return settings.touch.gamepad_ui;
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

    bool toPadButton(SDL_GameControllerButton button, PadButton* out)
    {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_A:
            *out = PAD_A;
            return true;
        case SDL_CONTROLLER_BUTTON_B:
            *out = PAD_B;
            return true;
        case SDL_CONTROLLER_BUTTON_X:
            *out = PAD_X;
            return true;
        case SDL_CONTROLLER_BUTTON_Y:
            *out = PAD_Y;
            return true;
        case SDL_CONTROLLER_BUTTON_START:
            *out = PAD_START;
            return true;
        case SDL_CONTROLLER_BUTTON_BACK:
            *out = PAD_BACK;
            return true;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            *out = PAD_LB;
            return true;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            *out = PAD_RB;
            return true;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            *out = PAD_LS;
            return true;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            *out = PAD_RS;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            *out = PAD_UP;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            *out = PAD_DOWN;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            *out = PAD_LEFT;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            *out = PAD_RIGHT;
            return true;
        default:
            return false;
        }
    }

    bool toDirection(PadButton button, MuiPadDir* direction)
    {
        switch (button) {
        case PAD_UP:
            *direction = MuiPadDir::Up;
            return true;
        case PAD_DOWN:
            *direction = MuiPadDir::Down;
            return true;
        case PAD_LEFT:
            *direction = MuiPadDir::Left;
            return true;
        case PAD_RIGHT:
            *direction = MuiPadDir::Right;
            return true;
        default:
            return false;
        }
    }

    void showCursor()
    {
        muiPadTouch();
        muiPadCursorGet(&gCursorX, &gCursorY);
        muiPadSetCursor(true, gCursorX, gCursorY);
    }

    // Right stick moves the cursor, A / R3 click, B / held R3 right click.
    bool stickMode()
    {
        return settings.touch.pad_cursor;
    }

    void zoomMap(float factor)
    {
        worldViewZoomBy(factor, screenGetWidth() / 2.0f, screenGetHeight() / 2.0f);
    }

    // [button] pressed while the map is played (the cursor's mode).
    void runMapAction(Action action, bool down)
    {
        if (action == Action::Combat) {
            if (down) {
                gSelectDownTime = SDL_GetTicks();
                gSelectFired = false;
            } else {
                // A tap ends the turn; held, the combat (see gamepadTick).
                if (gSelectDownTime != 0 && !gSelectFired && isInCombat()) {
                    gameCommandPost(GameCommandType::EndTurn);
                }
                gSelectDownTime = 0;
            }
            return;
        }

        if (!down) {
            return;
        }

        switch (action) {
        case Action::Click:
            showCursor();
            muiPadTapTop(gCursorX, gCursorY, false);
            break;
        case Action::Context:
            showCursor();
            muiPadRightClickTop(gCursorX, gCursorY);
            break;
        case Action::Back:
            muiRequestBack();
            break;
        case Action::Menu:
            gameCommandPost(GameCommandType::Menu);
            break;
        case Action::Inventory:
            gameCommandPost(GameCommandType::Inventory);
            break;
        case Action::Skilldex:
            gameCommandPost(GameCommandType::Skilldex);
            break;
        case Action::Character:
            gameCommandPost(GameCommandType::Character);
            break;
        case Action::Pipboy:
            gameCommandPost(GameCommandType::Pipboy);
            break;
        case Action::Automap:
            gameCommandPost(GameCommandType::Automap);
            break;
        case Action::Sneak:
            gameCommandPost(GameCommandType::Sneak);
            break;
        case Action::Swap:
            gameCommandPost(GameCommandType::SwapHands);
            break;
        case Action::UseItem:
            gameCommandPost(GameCommandType::UseItem);
            break;
        case Action::Focus:
            if (uiFocusOn()) {
                muiPadTouch();
                muiPadSwitchFocus();
            }
            break;
        case Action::QuickUse:
            gameCommandPost(GameCommandType::Hud, kHudEventPadQuickUse);
            break;
        case Action::ZoomIn:
            zoomMap(kZoomStep);
            break;
        case Action::ZoomOut:
            zoomMap(1.0f / kZoomStep);
            break;
        default:
            break;
        }
    }

    // [button] pressed while a screen has the pad.
    void runUiButton(PadButton button, MuiSurface surface)
    {
        MuiPadDir direction;
        if (toDirection(button, &direction)) {
            muiPadMove(surface, direction);
            gHeld = true;
            gHeldDir = direction;
            gHeldButton = button;
            gNextRepeat = SDL_GetTicks() + kRepeatFirstMs;
            return;
        }

        switch (button) {
        case PAD_A:
            muiPadActivate(surface, false);
            break;
        case PAD_X:
            muiPadActivate(surface, true);
            break;
        case PAD_B:
            // The HUD's screen taken from the map gives the pad back.
            if (onMap()) {
                muiPadSwitchFocus();
            } else {
                muiRequestBack();
            }
            break;
        case PAD_START:
            if (onMap()) {
                gameCommandPost(GameCommandType::Menu);
            } else {
                muiRequestBack();
            }
            break;
        default:
            if (gBindings[button] == Action::Focus) {
                muiPadTouch();
                muiPadSwitchFocus();
            }
            break;
        }
    }

    void press(PadButton button, bool down)
    {
        loadBindings();

        MuiSurface surface;
        if (uiFocusOn() && muiPadUiTarget(&surface)) {
            if (down) {
                runUiButton(button, surface);
            }
            return;
        }

        if (!onMap()) {
            // A screen the pad has no focus on (the setting is off): Back only.
            if (down && (button == PAD_B || button == PAD_START)) {
                muiRequestBack();
            }
            return;
        }

        if (stickMode()) {
            if (button == PAD_RS) {
                if (down) {
                    muiPadCursorGet(&gR3X, &gR3Y);
                    gR3Down = true;
                    gR3Fired = false;
                    gR3DownTime = SDL_GetTicks();
                } else {
                    if (gR3Down && !gR3Fired) {
                        runMapAction(Action::Click, true);
                    }
                    gR3Down = false;
                }
                return;
            }
            if (button == PAD_B) {
                runMapAction(Action::Context, down);
                return;
            }
            if (button == PAD_A) {
                runMapAction(Action::Click, down);
                return;
            }
        }

        runMapAction(gBindings[button], down);
    }

    void buttonDown(SDL_GameControllerButton button)
    {
        PadButton padButton;
        if (toPadButton(button, &padButton)) {
            press(padButton, true);
        }
    }

    void buttonUp(SDL_GameControllerButton button)
    {
        PadButton padButton;
        if (!toPadButton(button, &padButton)) {
            return;
        }
        if (gHeld && gHeldButton == padButton) {
            gHeld = false;
        }
        press(padButton, false);
    }

    // Triggers are axes: pressed past half way, once until released.
    bool gLeftTrigger = false;
    bool gRightTrigger = false;

    void triggers(SDL_GameController* controller)
    {
        bool left = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16384;
        bool right = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384;
        if (left && !gLeftTrigger) {
            press(PAD_LT, true);
        }
        if (right && !gRightTrigger) {
            press(PAD_RT, true);
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

void gamepadDevPress(int button, bool down)
{
    PadButton padButton;
    if (button == 100) {
        press(PAD_LT, down);
    } else if (button == 101) {
        press(PAD_RT, down);
    } else if (toPadButton(static_cast<SDL_GameControllerButton>(button), &padButton)) {
        press(padButton, down);
    }
}

void gamepadDevStick(float x, float y, int frames)
{
    gDevStickX = x;
    gDevStickY = y;
    gDevStickFrames = frames;
}

void gamepadTick()
{
    if (!gStarted) {
        return;
    }
    loadBindings();

    Uint32 now = SDL_GetTicks();
    float seconds = gLastTick != 0 ? (now - gLastTick) / 1000.0f : 0.0f;
    gLastTick = now;
    // A stall (loading) doesn't throw the view across the map.
    if (seconds > 0.1f) {
        seconds = 0.1f;
    }

    // Select held: end the combat, once per press.
    if (gSelectDownTime != 0 && !gSelectFired && now - gSelectDownTime >= kEndTurnHoldMs) {
        gSelectFired = true;
        if (onMap() && isInCombat()) {
            gameCommandPost(GameCommandType::EndCombat);
        }
    }

    // R3 held: the right click, on the threshold (no left click after).
    if (gR3Down && !gR3Fired && static_cast<int>(now - gR3DownTime) >= settings.touch.pad_r3_hold_ms) {
        float x;
        float y;
        muiPadCursorGet(&x, &y);
        if (onMap() && fabsf(x - gR3X) < kR3MoveLimit && fabsf(y - gR3Y) < kR3MoveLimit) {
            gR3Fired = true;
            runMapAction(Action::Context, true);
        }
    }

    float leftX = 0.0f;
    float leftY = 0.0f;
    float rightX = 0.0f;
    float rightY = 0.0f;
    float triggerZoom = 0.0f;
    bool dpadHeld[PAD_COUNT] = {};

    for (int index = 0; index < SDL_NumJoysticks(); index++) {
        if (!SDL_IsGameController(index)) {
            continue;
        }
        SDL_GameController* controller = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(index));
        if (controller == nullptr) {
            continue;
        }

        triggers(controller);

        if (fabsf(axis(controller, SDL_CONTROLLER_AXIS_LEFTX)) > fabsf(leftX)) {
            leftX = axis(controller, SDL_CONTROLLER_AXIS_LEFTX);
        }
        if (fabsf(axis(controller, SDL_CONTROLLER_AXIS_LEFTY)) > fabsf(leftY)) {
            leftY = axis(controller, SDL_CONTROLLER_AXIS_LEFTY);
        }
        if (fabsf(axis(controller, SDL_CONTROLLER_AXIS_RIGHTX)) > fabsf(rightX)) {
            rightX = axis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
        }
        if (fabsf(axis(controller, SDL_CONTROLLER_AXIS_RIGHTY)) > fabsf(rightY)) {
            rightY = axis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
        }
        triggerZoom += SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) / 32767.0f
            - SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) / 32767.0f;
        dpadHeld[PAD_UP] |= SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0;
        dpadHeld[PAD_DOWN] |= SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0;
        dpadHeld[PAD_LEFT] |= SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0;
        dpadHeld[PAD_RIGHT] |= SDL_GameControllerGetButton(controller, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0;
    }

    if (gDevStickFrames > 0) {
        gDevStickFrames--;
        rightX = gDevStickX;
        rightY = gDevStickY;
        if (seconds <= 0.0f) {
            seconds = 0.016f;
        }
    }

    if (seconds <= 0.0f) {
        return;
    }

    MuiSurface surface;
    if (uiFocusOn() && muiPadUiTarget(&surface)) {
        // The left stick is a d-pad: past half way it presses a direction,
        // and repeats while held.
        MuiPadDir stickDir = MuiPadDir::Down;
        bool stick = false;
        if (fabsf(leftX) > 0.5f || fabsf(leftY) > 0.5f) {
            stick = true;
            if (fabsf(leftX) > fabsf(leftY)) {
                stickDir = leftX > 0 ? MuiPadDir::Right : MuiPadDir::Left;
            } else {
                stickDir = leftY > 0 ? MuiPadDir::Down : MuiPadDir::Up;
            }
        }
        if (stick && !gStickHeld) {
            muiPadMove(surface, stickDir);
            gHeld = true;
            gHeldDir = stickDir;
            gHeldButton = PAD_COUNT;
            gNextRepeat = now + kRepeatFirstMs;
        }
        gStickHeld = stick;

        if (gHeld) {
            bool still = gHeldButton == PAD_COUNT ? stick && stickDir == gHeldDir : dpadHeld[gHeldButton];
            if (!still) {
                gHeld = false;
            } else if (static_cast<int>(now - gNextRepeat) >= 0) {
                muiPadMove(surface, gHeldDir);
                gNextRepeat = now + kRepeatMs;
            }
        }

        if (rightX != 0.0f || rightY != 0.0f) {
            muiPadTouch();
            muiPadStick(surface, rightX, rightY, seconds);
        }
        if (fabsf(triggerZoom) > 0.1f) {
            muiPadZoom(surface, powf(1.9f, triggerZoom * seconds));
        }
        return;
    }

    gHeld = false;
    gStickHeld = false;

    if (!onMap()) {
        return;
    }

    // The map: left stick pans the view, right stick moves the cursor.
    if (leftX != 0.0f || leftY != 0.0f) {
        float speed = screenGetWidth() * kPanScreensPerSecond * seconds;
        // Stick right: the view goes right, the map moves left.
        worldViewPanBy(-leftX * speed, -leftY * speed);
    }

    muiPadCursorGet(&gCursorX, &gCursorY);
    if (stickMode() && (rightX != 0.0f || rightY != 0.0f)) {
        showCursor();
        // Squared travel (acceleration), the same distance on both axes.
        // (screen widths a second; equal pixels a second on both axes)
        float cursorSpeed = kCursorScreensPerSecond * seconds * settings.touch.cursor_speed / 100.0f;
        float moveX = rightX * fabsf(rightX) * cursorSpeed;
        float moveY = rightY * fabsf(rightY) * cursorSpeed * screenGetWidth() / std::max(screenGetHeight(), 1);
        muiPadCursorMove(moveX, moveY);
        muiPadCursorGet(&gCursorX, &gCursorY);
        // Pushed against an edge, the view follows.
        constexpr float kEdge = 0.04f;
        float speed = screenGetWidth() * kPanScreensPerSecond * 0.6f * seconds;
        float panX = 0.0f;
        float panY = 0.0f;
        if (gCursorX < kEdge && rightX < 0.0f) {
            panX = -rightX * speed;
        } else if (gCursorX > 1.0f - kEdge && rightX > 0.0f) {
            panX = -rightX * speed;
        }
        if (gCursorY < kEdge && rightY < 0.0f) {
            panY = -rightY * speed;
        } else if (gCursorY > 1.0f - kEdge && rightY > 0.0f) {
            panY = -rightY * speed;
        }
        if (panX != 0.0f || panY != 0.0f) {
            worldViewPanBy(panX, panY);
        }
    }
    muiPadSetCursor(true, gCursorX, gCursorY);
}

} // namespace fallout
