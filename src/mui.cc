#include "mui.h"
#include "mui_display_settings.h"
#include "mui_dual.h"
#include "mui_gameskin.h"
#include "mui_notify.h"
#include "mui_skin.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "dev_autotest.h"
#include "game.h"
#include "game_charset.h"
#include "game_dialog.h"
#include "hud_layout.h"
#include "input.h"
#include "kb.h"
#include "message.h"
#include "platform_compat.h"
#include "settings.h"
#include "svga.h"
#include "touch.h"
#include "touch_log.h"

#include "bottom_screen.h"

namespace fallout {

namespace {

    // Classic Fallout 2 (see mui_skin.h): worn brown metal, recessed dark
    // monitors with green readouts, amber highlights, red button plates.
    const MuiTheme kTheme = {
        muiRgb(0x000000, 150), // dim
        muiRgb(0x0C0F09, 242), // panel
        muiRgb(0x6E5A3A), // panelBorder
        muiRgb(0x5CE65C), // text
        muiRgb(0x4A9A48), // textDim
        muiRgb(0xFFC23A), // accent
        muiRgb(0x7A6548, 250), // button
        muiRgb(0x3A2E20), // buttonPressed
        muiRgb(0x120D08), // buttonBorder
        muiRgb(0xF2DDA6), // buttonText
        muiRgb(0xA0321F), // buttonPrimary
        muiRgb(0xFFE9C4), // buttonPrimaryText
        3.0f, // radius
        20.0f, // padding
        12.0f, // gap
        52.0f, // buttonHeight
        1.5f, // borderWidth
        21.0f, // titleSize
        17.0f, // bodySize
        18.0f, // buttonTextSize
        10.0f, // touchSlop
        muiRgb(0x17120C), // background
        muiRgb(0x070A06, 248), // well
        muiRgb(0x24401E), // selected
        muiRgb(0xFF5A3C), // warning
        muiRgb(0x857055), // metal
    };

    // Text on amber-lit keys and switches.
    const MuiColor kLitText = muiRgb(0x241808);

    // Typed text not taken yet (see `MuiContext::takeTextInput`).
    std::u32string gTextInput;

    constexpr unsigned int kAppearDurationMs = 160;
    constexpr const char* kDragActiveId = "#drag";

    enum class PointerEventType {
        Down,
        Move,
        Up,
    };

    struct PointerEvent {
        PointerEventType type;
        float x;
        float y;
        // When it happened (SDL ticks of the finger event, not when it was
        // read: after a stall they're read late).
        unsigned int time;
    };

    // A touch pressed and released this long ago which the screens haven't
    // seen yet (the game stalled meanwhile) is dropped whole: replayed late it
    // would act twice (people tap again when nothing happens). The map's
    // gestures are dropped the same (touch.h).
    constexpr unsigned int kStaleTouchMs = kTouchStaleMs;

    struct Pointer {
        bool down = false;
        // Transitions of this frame.
        bool pressed = false;
        bool released = false;
        float x = 0.0f;
        float y = 0.0f;
        float startX = 0.0f;
        float startY = 0.0f;
        unsigned int downTime = 0;
        // Moved farther than touch slop since down.
        bool moved = false;
    };

    std::vector<MuiScreen*> gScreens;

    // Second finger while the first one is on the UI (pinch, see
    // `MuiContext::panZoom`); its position as it moves.
    struct SecondFinger {
        bool down = false;
        SDL_FingerID id = 0;
        float x = 0.0f;
        float y = 0.0f;
    };

    struct PanZoomState {
        bool active = false;
        bool pinching = false;
        float lastX = 0.0f;
        float lastY = 0.0f;
        float lastDistance = 0.0f;
        // A second finger joined during this touch (not a tap).
        bool pinched = false;
    };

    struct DragState {
        bool active = false;
        std::string source;
        int payload = 0;
    };

    struct ScrollState {
        float offset = 0.0f;
        // Content pixels per ms.
        float velocity = 0.0f;
        bool dragging = false;
        // Finger position along the scroll.
        float lastY = 0.0f;
        unsigned int lastTime = 0;
        unsigned int lastUpdate = 0;
    };

    // Everything about touches and widgets of one screen of the device: the
    // game's (top) and, on dual-screen devices, the bottom screen
    // (bottom_screen.h). Each has its own finger, widgets and scrolls; the
    // screens in `gScreens` say which one they are on (`MuiScreen::surface`).
    struct Surface {
        std::deque<PointerEvent> events;
        Pointer pointer;
        bool fingerCaptured = false;
        SDL_FingerID capturedFinger = 0;

        SecondFinger secondFinger;

        std::unordered_map<std::string, PanZoomState> panZooms;

        // Widget owning the current touch.
        std::string activeId;
        bool activeLongPressed = false;

        DragState drag;

        // Touch areas and widget rects of the last frame.
        std::vector<MuiRect> regions;
        std::vector<MuiRect> frameRegions;
        std::unordered_map<std::string, MuiRect> widgets;
        std::unordered_map<std::string, MuiRect> frameWidgets;

        std::unordered_map<std::string, ScrollState> scrolls;

        int outputWidth = 1;
        int outputHeight = 1;
        float pixelsPerDp = 1.0f;

        MuiRect safeRect;
        unsigned int safeRectTime = 0;
        bool safeRectValid = false;
    };

    Surface gSurfaces[kMuiSurfaceCount];

    // The surface being built, or whose touch is being handled.
    Surface* gSurface = &gSurfaces[static_cast<int>(MuiSurface::Top)];

    Surface& surfaceOf(MuiSurface surface)
    {
        return gSurfaces[static_cast<int>(surface)];
    }

    // Applies queued pointer events up to one down/up transition, so a quick
    // tap (down and up between two frames) is seen as press in one frame and
    // release in the next one.
    // Drops touches waiting in the queue which ended long ago (see
    // `kStaleTouchMs`): a down with its up in the queue, the up old.
    void dropStaleTouches(unsigned int now)
    {
        while (!gSurface->events.empty() && gSurface->events.front().type == PointerEventType::Down) {
            size_t up = 1;
            while (up < gSurface->events.size() && gSurface->events[up].type == PointerEventType::Move) {
                up++;
            }

            if (up >= gSurface->events.size() || gSurface->events[up].type != PointerEventType::Up
                || static_cast<int>(now - gSurface->events[up].time) <= static_cast<int>(kStaleTouchMs)) {
                return;
            }

            touchLogUiStaleDrop(static_cast<int>(up + 1));
            gSurface->events.erase(gSurface->events.begin(), gSurface->events.begin() + up + 1);
        }
    }

    void processEvents()
    {
        gSurface->pointer.pressed = false;
        gSurface->pointer.released = false;

        // Only touches not begun on screen yet: a pressed one gets its end.
        if (!gSurface->pointer.down) {
            dropStaleTouches(SDL_GetTicks());
        }

        while (!gSurface->events.empty()) {
            PointerEvent event = gSurface->events.front();
            gSurface->events.pop_front();

            gSurface->pointer.x = event.x;
            gSurface->pointer.y = event.y;

            if (event.type == PointerEventType::Down) {
                gSurface->pointer.down = true;
                gSurface->pointer.pressed = true;
                gSurface->pointer.startX = event.x;
                gSurface->pointer.startY = event.y;
                // When the finger went down (queued ones: before now).
                gSurface->pointer.downTime = event.time;
                gSurface->pointer.moved = false;
                break;
            }

            if (event.type == PointerEventType::Up) {
                gSurface->pointer.down = false;
                gSurface->pointer.released = true;
                break;
            }

            float slop = kTheme.touchSlop * gSurface->pixelsPerDp;
            if (std::fabs(gSurface->pointer.x - gSurface->pointer.startX) > slop || std::fabs(gSurface->pointer.y - gSurface->pointer.startY) > slop) {
                gSurface->pointer.moved = true;
            }
        }
    }

    void updateSafeRect(unsigned int now)
    {
        if (gSurface->safeRectValid && now - gSurface->safeRectTime < 1000) {
            return;
        }

        // The bottom screen has no cutouts.
        if (gSurface == &surfaceOf(MuiSurface::Bottom)) {
            gSurface->safeRect = { 0.0f, 0.0f, static_cast<float>(gSurface->outputWidth), static_cast<float>(gSurface->outputHeight) };
            gSurface->safeRectTime = now;
            gSurface->safeRectValid = true;
            return;
        }

        // Cutout insets from the HUD layout metrics (logical pixels).
        HudMetrics metrics = hudMetricsGet();
        float scale = static_cast<float>(gSurface->outputWidth) / std::max(metrics.screenWidth, 1);
        gSurface->safeRect = {
            metrics.insetLeft * scale,
            metrics.insetTop * scale,
            gSurface->outputWidth - (metrics.insetLeft + metrics.insetRight) * scale,
            gSurface->outputHeight - (metrics.insetTop + metrics.insetBottom) * scale,
        };
        gSurface->safeRectTime = now;
        gSurface->safeRectValid = true;
    }

    // The topmost active screen on [surface], the one getting its input.
    MuiScreen* topScreen(MuiSurface surface)
    {
        for (auto it = gScreens.rbegin(); it != gScreens.rend(); ++it) {
            if ((*it)->isActive() && (*it)->surface() == surface) {
                return *it;
            }
        }
        return nullptr;
    }

    bool hasScreensOn(MuiSurface surface)
    {
        for (MuiScreen* screen : gScreens) {
            if (screen->surface() == surface) {
                return true;
            }
        }
        return false;
    }

    // Forgets the touches of [surface] (its screens are gone, or its
    // touches can't be trusted any more).
    void resetSurfaceInput(Surface& surface)
    {
        surface.regions.clear();
        surface.widgets.clear();
        surface.events.clear();
        surface.pointer = Pointer();
        surface.fingerCaptured = false;
        surface.secondFinger.down = false;
    }

} // namespace

void muiToScreen(float x, float y, int* screenX, int* screenY)
{
    float scale = gSurface->outputWidth > 0 ? static_cast<float>(screenGetWidth()) / gSurface->outputWidth : 1.0f;
    *screenX = static_cast<int>(x * scale);
    *screenY = static_cast<int>(y * scale);
}

const char* muiText(int id, const char* fallback)
{
    static MessageList* messageList = nullptr;
    static bool loaded = false;
    // The language loaded: texts asked for before the game's fonts are
    // known may be in another (game_charset.h). The texts of the one before
    // are kept, given out already.
    static std::string language;

    const char* uiLanguage = gameCharsetUiLanguage();
    if (messageList == nullptr || compat_stricmp(language.c_str(), uiLanguage) != 0) {
        messageList = new MessageList();
        language = uiLanguage;
        loaded = messageListInit(messageList) && messageListLoadInLanguage(messageList, "game\\ce.msg", uiLanguage);
    }

    if (loaded) {
        MessageListItem item;
        item.num = id;
        if (messageListGetItem(messageList, &item)) {
            return item.text;
        }
    }

    return fallback;
}

#if !FALLOUT_TOUCH_ONLY
bool muiIsEnabled()
{
    return settings.touch.mobile_ui;
}
#endif

const MuiTheme& muiTheme()
{
    static const MuiTheme safe = [] {
        MuiTheme theme = kTheme;
        theme.warning = muiRgb(0xFFB000);
        return theme;
    }();
    return settings.touch.colour_blind_safe ? safe : kTheme;
}

float MuiContext::appear() const
{
    if (current == nullptr) {
        return 1.0f;
    }

    unsigned int elapsed = now - current->openTime;
    return std::min(static_cast<float>(elapsed) / kAppearDurationMs, 1.0f);
}

void MuiContext::dim()
{
    MuiColor color = kTheme.dim;
    color.a = static_cast<Uint8>(color.a * appear());
    muiFillRect(screen, color);
}

void MuiContext::panel(const MuiRect& rect)
{
    region(rect);
    // A recessed monitor in the metal (mui_skin.h).
    muiSkinWell(*this, rect);
}

void MuiContext::region(const MuiRect& rect)
{
    gSurface->frameRegions.push_back(rect);
}

bool MuiContext::touchable(const std::string& id, const MuiRect& rect, bool* pressed, bool* longPressed)
{
    region(rect);
    gSurface->frameWidgets[id] = rect;

    if (pressed != nullptr) {
        *pressed = false;
    }

    if (longPressed != nullptr) {
        *longPressed = false;
    }

    if (!isInteractive) {
        return false;
    }

    // Not a touch begun before the screen showed (waiting in the queue while
    // it faded in, or held since the previous screen).
    if (gSurface->pointer.pressed && gSurface->activeId.empty() && rect.contains(gSurface->pointer.startX, gSurface->pointer.startY)
        && !(current != nullptr && static_cast<int>(gSurface->pointer.downTime - current->openTime) < 0)) {
        gSurface->activeId = id;
        gSurface->activeLongPressed = false;
    }

    if (gSurface->activeId != id) {
        return false;
    }

    // Finger may wander a bit outside while pressing.
    float slop = dp(kTheme.touchSlop);
    bool inside = rect.inset(-slop).contains(gSurface->pointer.x, gSurface->pointer.y);

    if (pressed != nullptr) {
        *pressed = gSurface->pointer.down && inside;
    }

    if (longPressed != nullptr && gSurface->pointer.down && !gSurface->pointer.moved && !gSurface->activeLongPressed
        && SDL_GetTicks() - gSurface->pointer.downTime >= touchLongPressMs()) {
        gSurface->activeLongPressed = true;
        *longPressed = true;
    }

    if (gSurface->activeLongPressed) {
        return false;
    }

    return gSurface->pointer.released && inside;
}

void MuiContext::mark(const std::string& id, const MuiRect& rect)
{
    gSurface->frameWidgets[id] = rect;
}

bool MuiContext::button(const std::string& id, const MuiRect& rect, const std::u32string& label, MuiButtonStyle style)
{
    bool pressed;
    bool clicked = touchable(id, rect, &pressed);

    bool primary = style == MuiButtonStyle::Primary;
    MuiColor textColor = primary && !pressed ? kTheme.buttonPrimaryText : kTheme.buttonText;

    // A metal button (mui_skin.h).
    muiSkinButton(*this, rect, pressed, primary);
    // Long labels (other languages, narrow buttons) get smaller.
    if (muiSkinFallout()) {
        // The game's lettering: heavy capitals in its button yellow.
        muiGameButtonLabel(*this, rect, label, primary && !pressed ? muiRgb(0xFFF0B8) : muiRgb(0xD8BE4A));
        return clicked;
    }
    float size = muiFitTextSize(label, rect.w - dp(kTheme.padding), dp(kTheme.buttonTextSize), dp(10.0f));
    muiDrawTextAligned(label, rect, size, textColor, MuiAlign::Center, MuiAlign::Center);

    return clicked;
}

float MuiContext::scroll(const std::string& id, const MuiRect& rect, float contentHeight)
{
    return scrollAlong(id, rect, contentHeight, false);
}

float MuiContext::scrollHorizontal(const std::string& id, const MuiRect& rect, float contentWidth)
{
    return scrollAlong(id, rect, contentWidth, true);
}

float MuiContext::scrollAlong(const std::string& id, const MuiRect& rect, float contentSize, bool horizontal)
{
    region(rect);
    gSurface->frameWidgets[id] = rect;

    ScrollState& state = gSurface->scrolls[id];
    float maxOffset = std::max(contentSize - (horizontal ? rect.w : rect.h), 0.0f);
    unsigned int dt = state.lastUpdate != 0 ? now - state.lastUpdate : 0;
    state.lastUpdate = now;

    if (isInteractive) {
        bool startedInside = rect.contains(gSurface->pointer.startX, gSurface->pointer.startY);

        if (gSurface->pointer.pressed && startedInside) {
            // Touch stops a fling.
            state.velocity = 0.0f;
            state.dragging = false;
            state.lastY = horizontal ? gSurface->pointer.x : gSurface->pointer.y;
            state.lastTime = now;
        }

        float slop = dp(kTheme.touchSlop);
        // Along the scroll and across it.
        float dy = horizontal ? gSurface->pointer.x - gSurface->pointer.startX : gSurface->pointer.y - gSurface->pointer.startY;
        float dx = horizontal ? gSurface->pointer.y - gSurface->pointer.startY : gSurface->pointer.x - gSurface->pointer.startX;
        bool ownedByChild = gSurface->activeId.empty() || gSurface->activeId.rfind(id + ".", 0) == 0;
        // Content that fits doesn't scroll, the finger stays with the child.
        if (gSurface->pointer.down && startedInside && !state.dragging && ownedByChild && maxOffset > 0.0f
            && std::fabs(dy) > slop && std::fabs(dy) > std::fabs(dx)) {
            state.dragging = true;
            gSurface->activeId = id;
            state.lastY = horizontal ? gSurface->pointer.x : gSurface->pointer.y;
            state.lastTime = now;
        }

        if (state.dragging && gSurface->activeId == id) {
            if (gSurface->pointer.down || gSurface->pointer.released) {
                float move = (horizontal ? gSurface->pointer.x : gSurface->pointer.y) - state.lastY;
                unsigned int elapsed = std::max(now - state.lastTime, 1u);
                state.offset -= move;
                if (move != 0.0f || elapsed > 50) {
                    // Smoothed finger speed for the fling.
                    state.velocity = state.velocity * 0.3f + (-move / elapsed) * 0.7f;
                    state.lastTime = now;
                }
                state.lastY = horizontal ? gSurface->pointer.x : gSurface->pointer.y;
            }

            if (gSurface->pointer.released) {
                state.dragging = false;
            }
        } else if (state.dragging) {
            state.dragging = false;
        }
    }

    if (!state.dragging && state.velocity != 0.0f && dt > 0) {
        state.offset += state.velocity * dt;
        state.velocity *= std::exp(-static_cast<float>(dt) / 325.0f);
        if (std::fabs(state.velocity) < 0.02f) {
            state.velocity = 0.0f;
        }
    }

    if (state.offset <= 0.0f || state.offset >= maxOffset) {
        state.offset = std::clamp(state.offset, 0.0f, maxOffset);
        if (!state.dragging) {
            state.velocity = 0.0f;
        }
    }

    return state.offset;
}

void MuiContext::setScroll(const std::string& id, float offset)
{
    ScrollState& state = gSurface->scrolls[id];
    state.offset = std::max(offset, 0.0f);
    state.velocity = 0.0f;
}

bool MuiContext::isScrolling(const std::string& id) const
{
    auto it = gSurface->scrolls.find(id);
    return it != gSurface->scrolls.end() && (it->second.dragging || it->second.velocity != 0.0f);
}

bool MuiContext::dragSource(const std::string& id, int payload, bool anyDirection)
{
    if (!isInteractive) {
        return false;
    }

    if (gSurface->drag.active) {
        return gSurface->drag.source == id;
    }

    if (gSurface->activeId != id || !gSurface->pointer.down || !gSurface->pointer.moved || gSurface->activeLongPressed) {
        return false;
    }

    float dx = gSurface->pointer.x - gSurface->pointer.startX;
    float dy = gSurface->pointer.y - gSurface->pointer.startY;
    if (!anyDirection && std::fabs(dx) <= std::fabs(dy)) {
        return false;
    }

    gSurface->drag.active = true;
    gSurface->drag.source = id;
    gSurface->drag.payload = payload;
    gSurface->activeId = kDragActiveId;
    return true;
}

bool MuiContext::dragging(int* payload) const
{
    if (!gSurface->drag.active) {
        return false;
    }

    if (payload != nullptr) {
        *payload = gSurface->drag.payload;
    }
    return true;
}

bool MuiContext::dropped(const MuiRect& rect, int* payload)
{
    if (!isInteractive || !gSurface->drag.active || !gSurface->pointer.released || !rect.contains(gSurface->pointer.x, gSurface->pointer.y)) {
        return false;
    }

    if (payload != nullptr) {
        *payload = gSurface->drag.payload;
    }

    // Once.
    gSurface->drag.source.clear();
    gSurface->drag.active = false;
    return true;
}

bool MuiContext::panZoom(const std::string& id, const MuiRect& rect, MuiPanZoom* view, bool* tapped)
{
    region(rect);
    gSurface->frameWidgets[id] = rect;

    if (tapped != nullptr) {
        *tapped = false;
    }

    PanZoomState& state = gSurface->panZooms[id];
    if (!isInteractive) {
        state.active = false;
        return false;
    }

    // Not a touch begun before the screen showed (see `touchable`).
    if (gSurface->pointer.pressed && gSurface->activeId.empty() && rect.contains(gSurface->pointer.startX, gSurface->pointer.startY)
        && !(current != nullptr && static_cast<int>(gSurface->pointer.downTime - current->openTime) < 0)) {
        gSurface->activeId = id;
        state = PanZoomState();
        state.active = true;
        state.lastX = gSurface->pointer.x;
        state.lastY = gSurface->pointer.y;
    }

    if (gSurface->activeId != id || !state.active) {
        return false;
    }

    if (gSurface->secondFinger.down && gSurface->pointer.down) {
        // Pinch: the content point between the fingers stays between them.
        float midX = (gSurface->pointer.x + gSurface->secondFinger.x) / 2.0f;
        float midY = (gSurface->pointer.y + gSurface->secondFinger.y) / 2.0f;
        float distance = std::hypot(gSurface->secondFinger.x - gSurface->pointer.x, gSurface->secondFinger.y - gSurface->pointer.y);

        if (state.pinching && state.lastDistance > 0.0f && distance > 0.0f) {
            float contentX = view->centerX + (state.lastX - rect.centerX()) / view->zoom;
            float contentY = view->centerY + (state.lastY - rect.centerY()) / view->zoom;
            view->zoom = std::clamp(view->zoom * distance / state.lastDistance, view->minZoom, view->maxZoom);
            view->centerX = contentX - (midX - rect.centerX()) / view->zoom;
            view->centerY = contentY - (midY - rect.centerY()) / view->zoom;
        }

        state.pinching = true;
        state.pinched = true;
        state.lastX = midX;
        state.lastY = midY;
        state.lastDistance = distance;
    } else if (gSurface->pointer.down) {
        // Drag (after a pinch from where the finger is, no jump).
        if (!state.pinching) {
            view->centerX -= (gSurface->pointer.x - state.lastX) / view->zoom;
            view->centerY -= (gSurface->pointer.y - state.lastY) / view->zoom;
        }
        state.pinching = false;
        state.lastX = gSurface->pointer.x;
        state.lastY = gSurface->pointer.y;
    }

    if (gSurface->pointer.released) {
        state.active = false;
        if (tapped != nullptr && !state.pinched && !gSurface->pointer.moved) {
            *tapped = true;
        }
    }

    return true;
}

std::u32string MuiContext::takeTextInput()
{
    std::u32string text;
    text.swap(gTextInput);
    return text;
}

float MuiContext::pointerX() const
{
    return gSurface->pointer.x;
}

float MuiContext::pointerY() const
{
    return gSurface->pointer.y;
}

bool MuiContext::pointerDown() const
{
    return gSurface->pointer.down;
}

float MuiContext::slider(const std::string& id, const MuiRect& rect, float value, float minValue, float maxValue, bool* dragging)
{
    // Taller touch area than the track.
    MuiRect touchRect = rect.inset(0.0f, -dp(10.0f));
    bool pressed;
    touchable(id, touchRect, &pressed);

    bool held = isInteractive && gSurface->activeId == id && gSurface->pointer.down && maxValue > minValue;
    if (held) {
        float ratio = std::clamp((gSurface->pointer.x - rect.x) / std::max(rect.w, 1.0f), 0.0f, 1.0f);
        value = minValue + ratio * (maxValue - minValue);
    }

    if (dragging != nullptr) {
        *dragging = held;
    }

    float ratio = maxValue > minValue ? std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f) : 0.0f;
    if (muiSkinFallout()) {
        float slot = dp(7.0f);
        MuiRect groove = { rect.x, rect.centerY() - slot / 2.0f, rect.w, slot };
        muiSkinWell(*this, groove);
        muiFillRect({ groove.x + 2.0f, groove.y + 2.0f, std::max((groove.w - 4.0f) * ratio, 0.0f), groove.h - 4.0f }, kTheme.accent.withAlpha(210));
        muiGameKnob(*this, rect.x + rect.w * ratio, rect.centerY(), pressed);
        return value;
    }
    float trackHeight = dp(6.0f);
    MuiRect track = { rect.x, rect.centerY() - trackHeight / 2.0f, rect.w, trackHeight };
    // A slot in the metal, lit amber up to the value; a metal knob.
    muiSkinWell(*this, track);
    muiFillRect({ track.x + dp(1.0f), track.y + dp(1.0f), std::max((track.w - dp(2.0f)) * ratio, 0.0f), track.h - dp(2.0f) }, kTheme.accent.withAlpha(210));

    float thumbRadius = dp(pressed ? 13.0f : 11.0f);
    float thumbX = rect.x + rect.w * ratio;
    muiFillCircle(thumbX + dp(1.0f), rect.centerY() + dp(1.5f), thumbRadius, muiRgb(0x000000, 130));
    muiFillCircle(thumbX, rect.centerY(), thumbRadius, kTheme.button);
    muiFillCircle(thumbX - thumbRadius * 0.25f, rect.centerY() - thumbRadius * 0.25f, thumbRadius * 0.55f, muiRgb(0xE8D5A8, 70));
    muiStrokeCircle(thumbX, rect.centerY(), thumbRadius, dp(1.5f), pressed ? kTheme.accent : kTheme.buttonBorder);

    return value;
}

bool MuiContext::toggle(const std::string& id, const MuiRect& rect, bool* value)
{
    bool pressed;
    bool tapped = touchable(id, rect.inset(-dp(6.0f)), &pressed);
    if (tapped) {
        *value = !*value;
    }

    if (muiSkinFallout()) {
        muiGameLamp(*this, rect, *value, pressed);
        return tapped;
    }

    // Track with the knob at the side of the value.
    float height = std::min(rect.h, dp(24.0f));
    float width = std::min(rect.w, height * 1.9f);
    MuiRect track = { rect.right() - width, rect.centerY() - height / 2.0f, width, height };
    // A slot in the metal, lit amber while on; a metal knob.
    muiSkinWell(*this, track);
    if (*value) {
        muiFillRect(track.inset(dp(2.0f)), kTheme.accent.withAlpha(200));
    }
    if (pressed) {
        muiSkinBevel(track, dp(1.5f), kTheme.accent, kTheme.accent, false);
    }

    float radius = height / 2.0f - dp(2.0f);
    float knobX = *value ? track.right() - height / 2.0f : track.x + height / 2.0f;
    muiFillCircle(knobX + dp(1.0f), track.centerY() + dp(1.0f), radius, muiRgb(0x000000, 130));
    muiFillCircle(knobX, track.centerY(), radius, kTheme.button);
    muiFillCircle(knobX - radius * 0.25f, track.centerY() - radius * 0.25f, radius * 0.5f, muiRgb(0xE8D5A8, 80));
    muiStrokeCircle(knobX, track.centerY(), radius, dp(1.0f), kTheme.buttonBorder);

    return tapped;
}

bool MuiContext::segmented(const std::string& id, const MuiRect& rect, const std::vector<std::u32string>& labels, int* selected)
{
    if (labels.empty()) {
        return false;
    }

    // One text size for all, as large as fits; each cell as wide as its
    // label with the room left shared equally.
    float padding = dp(12.0f);
    float size = dp(kTheme.buttonTextSize) * 0.9f;
    float textWidth = 0.0f;
    for (const std::u32string& label : labels) {
        textWidth += muiTextWidth(label, size);
    }
    float room = rect.w - padding * labels.size();
    if (textWidth > room && textWidth > 0.0f) {
        size = std::max(size * room / textWidth, dp(10.0f));
        textWidth = 0.0f;
        for (const std::u32string& label : labels) {
            textWidth += muiTextWidth(label, size);
        }
    }
    float extra = std::max(room - textWidth, 0.0f) / labels.size();

    bool changed = false;
    float radius = dp(kTheme.radius);
    // Joined metal keys; the chosen one lit amber.
    bool game = muiSkinFallout();
    if (!game) {
        muiSkinButton(*this, rect, false, false);
    }

    float x = rect.x;
    for (size_t index = 0; index < labels.size(); index++) {
        float width = index + 1 == labels.size() ? rect.right() - x : muiTextWidth(labels[index], size) + padding + extra;
        MuiRect cell = { x, rect.y, width, rect.h };
        x += width;

        bool pressed;
        if (touchable(id + "." + std::to_string(index), cell, &pressed) && *selected != static_cast<int>(index)) {
            *selected = static_cast<int>(index);
            changed = true;
        }

        bool on = *selected == static_cast<int>(index);
        if (game) {
            muiSkinButton(*this, cell.inset(1.0f, 0.0f), on || pressed, false);
            muiGameButtonLabel(*this, cell, labels[index], on ? muiRgb(0xFFE890) : muiRgb(0xB8A040));
            continue;
        }
        if (on) {
            muiFillRect(cell.inset(dp(3.0f)), kTheme.accent.withAlpha(220));
        } else if (pressed) {
            muiFillRect(cell.inset(dp(3.0f)), kTheme.buttonPressed);
        }
        if (index != 0) {
            muiFillRect({ cell.x, cell.y + dp(4.0f), std::max(dp(1.0f), 1.0f), cell.h - dp(8.0f) }, muiRgb(0x000000, 120));
        }

        MuiColor color = on ? kLitText : kTheme.buttonText;
        muiDrawTextAligned(labels[index], cell, size, color, MuiAlign::Center, MuiAlign::Center);
    }

    (void)radius;
    return changed;
}

bool MuiContext::tappedOutside(const MuiRect& rect)
{
    if (!isInteractive || !gSurface->pointer.released || !gSurface->activeId.empty()) {
        return false;
    }

    // Not the finger which was down when the screen opened (it opened on a
    // long press, the finger is lifted elsewhere).
    if (current != nullptr && static_cast<int>(gSurface->pointer.downTime - current->openTime) < 0) {
        return false;
    }

    return !rect.contains(gSurface->pointer.startX, gSurface->pointer.startY) && !rect.contains(gSurface->pointer.x, gSurface->pointer.y) && !gSurface->pointer.moved;
}

void muiPush(MuiScreen* screen)
{
    muiRemove(screen);
    screen->finished = false;
    screen->openTime = SDL_GetTicks();
    gScreens.push_back(screen);

    // New screen doesn't continue a touch that started below it.
    surfaceOf(screen->surface()).activeId.clear();
}

void muiRemove(MuiScreen* screen)
{
    auto it = std::find(gScreens.begin(), gScreens.end(), screen);
    if (it != gScreens.end()) {
        gScreens.erase(it);
        surfaceOf(screen->surface()).activeId.clear();
    }
}

bool muiHasModal()
{
    for (MuiScreen* screen : gScreens) {
        if (screen->modal && screen->isActive()) {
            return true;
        }
    }
    return false;
}

bool muiHasModalOn(MuiSurface surface)
{
    for (MuiScreen* screen : gScreens) {
        if (screen->modal && screen->isActive() && screen->surface() == surface) {
            return true;
        }
    }
    return false;
}

bool muiHidesHud()
{
    // Screens on the HUD's screen of the device (the bottom one while it's
    // active: the elevator there hides it).
    MuiSurface hudSurface = muiHudSurface();
    for (MuiScreen* screen : gScreens) {
        if (screen->hidesHud() && screen->isActive() && screen->surface() == hudSurface) {
            return true;
        }
    }
    return false;
}

bool muiCoversScreen()
{
    for (MuiScreen* screen : gScreens) {
        if (screen->coversScreen() && screen->isActive() && screen->surface() == MuiSurface::Top) {
            return true;
        }
    }
    return false;
}

MuiSurface muiCurrentSurface()
{
    return gSurface == &surfaceOf(MuiSurface::Bottom) ? MuiSurface::Bottom : MuiSurface::Top;
}

HudMetrics muiSurfaceMetrics(MuiSurface surface)
{
    if (surface == MuiSurface::Top) {
        return hudMetricsGet();
    }

    const Surface& bottom = surfaceOf(MuiSurface::Bottom);
    float scale = bottom.outputWidth > 0 ? static_cast<float>(screenGetWidth()) / bottom.outputWidth : 1.0f;

    HudMetrics metrics;
    metrics.screenWidth = screenGetWidth();
    metrics.screenHeight = static_cast<int>(std::lround(bottom.outputHeight * scale));
    metrics.pixelsPerDp = bottom.pixelsPerDp * scale;
    metrics.insetLeft = 0;
    metrics.insetTop = 0;
    metrics.insetRight = 0;
    metrics.insetBottom = 0;
    return metrics;
}

bool muiCoversSurface(MuiSurface surface)
{
    for (MuiScreen* screen : gScreens) {
        if (screen->coversScreen() && screen->isActive() && screen->surface() == surface) {
            return true;
        }
    }
    return false;
}

MuiSurface muiHudSurface()
{
    return muiBottomActive() ? MuiSurface::Bottom : MuiSurface::Top;
}

bool muiBottomHoldsGame()
{
    return muiHasModalOn(MuiSurface::Bottom);
}

// Whether the bottom screen is in use, taken once a frame (`muiRenderBottom`)
// so every screen sees the same answer for the whole frame, whatever the
// bottom screen's window does meanwhile (it comes and goes on Android's UI
// thread).
static bool gBottomActive = false;

bool muiBottomActive()
{
    return gBottomActive;
}

MuiSurface muiGameScreenSurface()
{
    return muiBottomActive() ? MuiSurface::Bottom : MuiSurface::Top;
}

// Switch between full screens asked for by this screen (see
// `muiHoldFrameForSwitch`).
static bool gSwitchPending = false;
static MuiScreen* gSwitchFrom = nullptr;

void muiHoldFrameForSwitch(MuiScreen* from)
{
    gSwitchPending = true;
    gSwitchFrom = from;
}

bool muiHoldsFrame()
{
    // Safety net: never hold longer than this.
    constexpr unsigned int kMaxHoldMs = 500;
    static unsigned int holdStart = 0;

    bool hold = false;
    for (MuiScreen* screen : gScreens) {
        if (screen->holdsFrame()) {
            hold = true;
            break;
        }
    }

    // Until the next screen covers everything.
    if (gSwitchPending) {
        bool arrived = false;
        for (MuiScreen* screen : gScreens) {
            if (screen != gSwitchFrom && screen->isActive() && screen->coversScreen()) {
                arrived = true;
                break;
            }
        }

        if (arrived) {
            gSwitchPending = false;
        } else {
            hold = true;
        }
    }

    if (!hold) {
        holdStart = 0;
        return false;
    }

    unsigned int now = SDL_GetTicks();
    if (holdStart == 0) {
        holdStart = now;
    }

    if (now - holdStart >= kMaxHoldMs) {
        // The next screen didn't come (e.g. not enough action points).
        gSwitchPending = false;
        return false;
    }
    return true;
}

void muiRunModal(MuiScreen* screen)
{
    muiPush(screen);

    while (!screen->finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        if (keyCode == KEY_ESCAPE) {
            screen->back();
        } else if (keyCode != -1) {
            screen->key(keyCode);
        }

        devAutotestTick();

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    muiRemove(screen);
}

static bool gBackRequested = false;

void muiRequestBack()
{
    gBackRequested = true;
}

// A requested Back goes to the topmost modal screen, or to the overlays
// when there's none.
static void muiDispatchBack(const std::vector<MuiScreen*>& screens)
{
    for (auto it = screens.rbegin(); it != screens.rend(); ++it) {
        if ((*it)->isActive() && (*it)->modal) {
            (*it)->back();
            return;
        }
    }

    for (auto it = screens.rbegin(); it != screens.rend(); ++it) {
        if ((*it)->isActive()) {
            (*it)->back();
        }
    }
}

// Builds the screens of [surface] into the renderer's current target, an
// output of [width] x [height] pixels at [density] (dp scale).
static void muiRenderSurface(SDL_Renderer* renderer, MuiSurface surface, int width, int height, float density)
{
    gSurface = &surfaceOf(surface);

    // The bottom screen is always drawn (its backdrop, the companion panel);
    // the top only with screens, or dimmed while the bottom one holds the
    // game.
    bool draws = surface == MuiSurface::Bottom || hasScreensOn(surface) || muiBottomHoldsGame();
    if (!draws) {
        // Touches may be captured by a screen that is gone. Its pointer
        // events are dropped: a screen shown later would take them as taps.
        resetSurfaceInput(*gSurface);
        gSurface = &surfaceOf(MuiSurface::Top);
        return;
    }

    gSurface->outputWidth = width;
    gSurface->outputHeight = height;
    gSurface->pixelsPerDp = density * static_cast<float>(settings.touch.hud_scale) / 100.0f;

    unsigned int now = SDL_GetTicks();
    updateSafeRect(now);
    processEvents();

    // Draw in output pixels.
    int logicalWidth;
    int logicalHeight;
    SDL_RenderGetLogicalSize(renderer, &logicalWidth, &logicalHeight);
    SDL_RenderSetLogicalSize(renderer, 0, 0);

    muiDrawBegin(renderer);

    gSurface->frameRegions.clear();
    gSurface->frameWidgets.clear();

    MuiContext context;
    context.pixelsPerDp = gSurface->pixelsPerDp;
    context.surface = surface;
    context.screen = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) };
    context.safe = gSurface->safeRect;
    context.now = now;

    // The world on top is frozen while the game runs a screen on the bottom
    // one: dimmed, so it doesn't look like it is still playing. The bottom
    // screen gets its backdrop.
    if (surface == MuiSurface::Top) {
        muiDualTopDim(context);
    } else {
        muiDualBackdrop(context);
    }

    // Copy: screens may push/remove screens while building.
    std::vector<MuiScreen*> screens = gScreens;

    MuiScreen* top = topScreen(surface);
    for (MuiScreen* screen : screens) {
        if (!screen->isActive() || screen->surface() != surface) {
            continue;
        }
        context.current = screen;
        context.isInteractive = screen == top;
        screen->build(context);
    }

    // The bottom screen: the companion panel when nothing is on it, a screen
    // coming up fades in, low HP pulses at the edges.
    if (surface == MuiSurface::Bottom) {
        context.current = nullptr;
        context.isInteractive = false;
        if (top == nullptr) {
            muiDualCompanion(context);
        }
        muiDualBottomOverlay(context, top);
    }

    // Over every screen, not touchable: with the HUD's log.
    bool dialogNoticesOnTop = muiBottomActive()
        && ((gameDialogIsTalking() && settings.touch.dialogue_layout != 0)
            || (gameDialogIsInBarter() && settings.touch.barter_layout != 0));
    if (surface == (dialogNoticesOnTop ? MuiSurface::Top : muiHudSurface())) {
        context.current = nullptr;
        context.isInteractive = false;
        muiNotifyRender(context);
    }

    if (surface == MuiSurface::Bottom && muiBottomDimmed()) {
        muiFillRect(context.screen, muiRgb(0, 190));
    }
    if (surface == MuiSurface::Top && settings.touch.top_scanlines) {
        for (float y = 0; y < context.screen.h; y += 4.0f)
            muiFillRect({ 0, y, context.screen.w, 1 }, muiRgb(0, 55));
    }
    muiDrawEnd();

    if (logicalWidth != 0 && logicalHeight != 0) {
        SDL_RenderSetLogicalSize(renderer, logicalWidth, logicalHeight);
    }

    if (gSurface->pointer.released) {
        gSurface->activeId.clear();
        // Dropped outside of any target.
        gSurface->drag.active = false;
        gSurface->drag.source.clear();
    }

    gSurface->regions = gSurface->frameRegions;
    gSurface->widgets = gSurface->frameWidgets;

    gSurface = &surfaceOf(MuiSurface::Top);
}

void muiRender(SDL_Renderer* renderer)
{
    if (gScreens.empty()) {
        // Nothing to go back from.
        gBackRequested = false;
    }

    // As a screen's own Back button (while building), whichever screen of
    // the device it is on.
    if (gBackRequested) {
        gBackRequested = false;
        std::vector<MuiScreen*> screens = gScreens;
        muiDispatchBack(screens);
    }

    int width;
    int height;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        return;
    }

    muiRenderSurface(renderer, MuiSurface::Top, width, height, hudGetScreenDensity());
}

namespace {
    // The bottom screen's frame: drawn into this texture with the game's
    // renderer (its textures and caches are shared), read back and handed
    // to the bottom screen's window.
    SDL_Texture* gBottomTexture = nullptr;
    int gBottomTextureWidth = 0;
    int gBottomTextureHeight = 0;
    unsigned int gBottomTextureGeneration = 0;
    std::vector<unsigned char> gBottomPixels;
    // Both surfaces follow the same paced presentation (60 Hz or 30 Hz).
} // namespace

void muiRenderBottom(SDL_Renderer* renderer)
{
    bool wasActive = gBottomActive;
    gBottomActive = settings.touch.dual_screen && bottomScreenAvailable();
    if (gBottomActive && !wasActive) {
        muiDualPowerOn();
    }
    if (!muiBottomActive()) {
        resetSurfaceInput(surfaceOf(MuiSurface::Bottom));
        return;
    }

    int width;
    int height;
    float density;
    bottomScreenGetSize(&width, &height);
    density = bottomScreenGetDensity();
    if (width <= 0 || height <= 0) {
        return;
    }

    // Textures were dropped with the renderer (`muiResetRenderer` freed
    // this one): a new one.
    if (gBottomTextureGeneration != muiDrawGeneration()) {
        gBottomTexture = nullptr;
    }
    // A new texture for a new size.
    if (gBottomTexture != nullptr && (width != gBottomTextureWidth || height != gBottomTextureHeight)) {
        SDL_DestroyTexture(gBottomTexture);
        gBottomTexture = nullptr;
    }
    if (gBottomTexture == nullptr) {
        gBottomTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, width, height);
        if (gBottomTexture == nullptr) {
            return;
        }
        gBottomTextureWidth = width;
        gBottomTextureHeight = height;
        gBottomTextureGeneration = muiDrawGeneration();
    }

    // No logical size (the game's 640 x 480 scaling) while drawing there:
    // switched off before the target changes, so the texture's viewport is
    // all of it, and back on after (SDL keeps each target's viewport).
    int logicalWidth;
    int logicalHeight;
    SDL_RenderGetLogicalSize(renderer, &logicalWidth, &logicalHeight);
    SDL_RenderSetLogicalSize(renderer, 0, 0);

    SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, gBottomTexture) != 0) {
        if (logicalWidth != 0 && logicalHeight != 0) {
            SDL_RenderSetLogicalSize(renderer, logicalWidth, logicalHeight);
        }
        return;
    }

    SDL_RenderSetClipRect(renderer, nullptr);
    // The game clears its frames with the renderer's color: kept.
    Uint8 clearR;
    Uint8 clearG;
    Uint8 clearB;
    Uint8 clearA;
    SDL_GetRenderDrawColor(renderer, &clearR, &clearG, &clearB, &clearA);
    SDL_SetRenderDrawColor(renderer, 4, 9, 5, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, clearR, clearG, clearB, clearA);

    muiRenderSurface(renderer, MuiSurface::Bottom, width, height, density);

    gBottomPixels.resize(static_cast<size_t>(width) * height * 4);
    SDL_Rect all = { 0, 0, width, height };
    bool read = SDL_RenderReadPixels(renderer, &all, SDL_PIXELFORMAT_ABGR8888, gBottomPixels.data(), width * 4) == 0;

    SDL_SetRenderTarget(renderer, previousTarget);
    if (logicalWidth != 0 && logicalHeight != 0) {
        SDL_RenderSetLogicalSize(renderer, logicalWidth, logicalHeight);
    }

    if (read) {
        bottomScreenPostFrame(gBottomPixels.data(), width, height, width * 4);
    }
}

void muiResetRenderer()
{
    // The renderer is going away, its textures with it.
    if (gBottomTexture != nullptr) {
        SDL_DestroyTexture(gBottomTexture);
        gBottomTexture = nullptr;
    }
    muiDrawReset();
}

void muiHandleTextInput(const char* text)
{
    gTextInput += muiDecodeUtf8(text);
}

bool muiHandleFingerEvent(const SDL_Event* event)
{
    muiDisplayActivity();
    const SDL_TouchFingerEvent& finger = event->tfinger;

    // The bottom screen's fingers (bottom_screen.cc) are its own: all of
    // them go to its screens, none to the map.
    bool bottom = finger.touchId == kMuiBottomTouchId;
    Surface& surface = surfaceOf(bottom ? MuiSurface::Bottom : MuiSurface::Top);
    if (bottom && !muiBottomActive()) {
        return true;
    }

    if (!bottom && gSdlRenderer != nullptr) {
        SDL_GetRendererOutputSize(gSdlRenderer, &surface.outputWidth, &surface.outputHeight);
    }
    float x = finger.x * surface.outputWidth;
    float y = finger.y * surface.outputHeight;

    // A screen of the game is open on the bottom screen: the world on top is
    // frozen, its touches go nowhere (as under a modal screen).
    bool modal = bottom ? true : muiHasModalOn(MuiSurface::Top) || muiBottomHoldsGame();
    bool topModal = !bottom && muiHasModalOn(MuiSurface::Top);
    if (!bottom && !topModal && muiBottomHoldsGame()) {
        return true;
    }

    if (event->type == SDL_FINGERDOWN) {
        if (surface.fingerCaptured) {
            // Second finger: pinch gestures, more are ignored.
            if (!surface.secondFinger.down) {
                surface.secondFinger = { true, finger.fingerId, x, y };
            }
            return true;
        }

        bool capture = modal;
        if (!capture && hasScreensOn(MuiSurface::Top)) {
            for (const MuiRect& rect : surface.regions) {
                if (rect.contains(x, y)) {
                    capture = true;
                    break;
                }
            }
        }

        if (!capture) {
            return false;
        }

        surface.fingerCaptured = true;
        surface.capturedFinger = finger.fingerId;
        surface.events.push_back({ PointerEventType::Down, x, y, finger.timestamp });
        return true;
    }

    if (surface.secondFinger.down && finger.fingerId == surface.secondFinger.id) {
        surface.secondFinger.x = x;
        surface.secondFinger.y = y;
        if (event->type == SDL_FINGERUP) {
            surface.secondFinger.down = false;
        }
        return true;
    }

    if (!surface.fingerCaptured || finger.fingerId != surface.capturedFinger) {
        // Other fingers while UI is modal don't reach the game either.
        return surface.fingerCaptured || modal;
    }

    if (event->type == SDL_FINGERMOTION) {
        surface.events.push_back({ PointerEventType::Move, x, y, finger.timestamp });
    } else if (event->type == SDL_FINGERUP) {
        surface.events.push_back({ PointerEventType::Up, x, y, finger.timestamp });
        surface.fingerCaptured = false;
    }

    return true;
}

bool muiForgetMissingFingers(const SDL_FingerID* down, int count)
{
    auto isDown = [&](SDL_FingerID id) {
        for (int index = 0; index < count; index++) {
            if (down[index] == id) {
                return true;
            }
        }
        return false;
    };

    bool forgotten = false;

    // SDL lists the fingers of the device's own touch screen: the top's.
    // The bottom screen's come from BottomScreen.java, always with their
    // ends; they are let go only when everything is (the game went away).
    Surface& top = surfaceOf(MuiSurface::Top);
    Surface& bottom = surfaceOf(MuiSurface::Bottom);
    std::vector<Surface*> surfaces = { &top };
    if (down == nullptr && count == 0) {
        surfaces.push_back(&bottom);
    }

    for (Surface* surface : surfaces) {
        // Released far outside: nothing is tapped.
        if (surface->fingerCaptured && !isDown(surface->capturedFinger)) {
            surface->events.push_back({ PointerEventType::Up, -1.0e6f, -1.0e6f, SDL_GetTicks() });
            surface->fingerCaptured = false;
            forgotten = true;
        }

        if (surface->secondFinger.down && !isDown(surface->secondFinger.id)) {
            surface->secondFinger.down = false;
            forgotten = true;
        }
    }

    return forgotten;
}

bool muiGetWidgetCenter(const char* id, float* x, float* y, bool* onBottom)
{
    if (onBottom != nullptr) {
        *onBottom = false;
    }

    // Automated tests tap the top screen first, then the bottom one.
    const Surface& top = surfaceOf(MuiSurface::Top);
    auto it = top.widgets.find(id);
    if (it != top.widgets.end()) {
        *x = it->second.centerX() / top.outputWidth;
        *y = it->second.centerY() / top.outputHeight;
        return true;
    }

    const Surface& bottom = surfaceOf(MuiSurface::Bottom);
    it = bottom.widgets.find(id);
    if (it == bottom.widgets.end() || bottom.outputWidth <= 0) {
        return false;
    }

    *x = it->second.centerX() / bottom.outputWidth;
    *y = it->second.centerY() / bottom.outputHeight;
    if (onBottom != nullptr) {
        *onBottom = true;
    }
    return true;
}

} // namespace fallout
