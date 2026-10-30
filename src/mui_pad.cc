#include "mui_pad.h"
#include "dev_autotest.h"

#include <algorithm>
#include <cmath>
#include <stdio.h>

#include "actions.h"
#include "combat.h"
#include "game.h"
#include "interface.h"
#include "item.h"
#include "mui_gameskin.h"
#include "mui_screens.h"
#include "mui_skin.h"
#include "object.h"
#include "settings.h"
#include "svga.h"
#include "touch.h"
#include "touch_controls.h"

namespace fallout {

namespace {

    // Fingers of the taps the pad makes (the bottom screen's own id reaches
    // the bottom surface only).
    constexpr SDL_TouchID kTopPadTouchId = 1;
    constexpr SDL_FingerID kPadFingerId = 0x9AD;
    constexpr Uint32 kTapMs = 70;
    // The ring stays while the pad was used this long ago.
    constexpr Uint32 kRingShowMs = 60000;

    bool gTouchActive = false;
    MuiSurface gTouchSurface = MuiSurface::Top;
    float gTouchX = 0.0f;
    float gTouchY = 0.0f;
    Uint32 gTouchUpAt = 0;

    Uint32 gLastPadInput = 0;
    bool gPreferTop = false;
    bool gMapUiFocus = false;

    bool gCursorVisible = false;
    float gCursorX = 0.5f;
    float gCursorY = 0.5f;
    // The touchpad page is open; the left button held by it.
    bool gTouchpadOpen = false;
    bool gHoldActive = false;
    // The player's finger on the top screen, and until when its cursor stays.
    bool gFingerDown = false;
    float gFingerX = 0.0f;
    float gFingerY = 0.0f;
    Uint32 gFingerUntil = 0;
    constexpr Uint32 kFingerCursorMs = 450;

    // A move that found nothing scrolled the list; it is tried again once
    // the list has built the new rows.
    bool gRetryActive = false;
    MuiSurface gRetrySurface = MuiSurface::Top;
    MuiPadDir gRetryDir = MuiPadDir::Down;
    int gRetryFrames = 0;

    void pushFinger(Uint32 type, MuiSurface surface, float x, float y)
    {
        SDL_Event event = {};
        event.type = type;
        event.tfinger.touchId = surface == MuiSurface::Bottom ? kMuiBottomTouchId : kTopPadTouchId;
        event.tfinger.fingerId = kPadFingerId;
        event.tfinger.x = x;
        event.tfinger.y = y;
        event.tfinger.pressure = 1.0f;
        event.tfinger.timestamp = uiTicks();
        SDL_PushEvent(&event);
    }

    // A finger down now (normalized place of [surface]), up after [holdMs].
    void press(MuiSurface surface, float x, float y, Uint32 holdMs)
    {
        if (gTouchActive) {
            return;
        }
        gTouchActive = true;
        gTouchSurface = surface;
        gTouchX = x;
        gTouchY = y;
        gTouchUpAt = uiTicks() + holdMs;
        pushFinger(SDL_FINGERDOWN, surface, x, y);
    }

    const MuiPadWidget* findWidget(MuiSurface surface, const std::string& id)
    {
        for (const MuiPadWidget& widget : muiPadWidgets(surface)) {
            if (widget.id == id) {
                return &widget;
            }
        }
        return nullptr;
    }

    // Distance from [rect] to [other] in direction, or a negative number
    // when [other] is not that way. Prefers widgets in line.
    float score(const MuiRect& rect, const MuiRect& other, MuiPadDir direction)
    {
        float along;
        float across;
        float overlap;
        switch (direction) {
        case MuiPadDir::Right:
            along = other.centerX() - rect.centerX();
            across = std::fabs(other.centerY() - rect.centerY());
            overlap = std::min(rect.bottom(), other.bottom()) - std::max(rect.y, other.y);
            break;
        case MuiPadDir::Left:
            along = rect.centerX() - other.centerX();
            across = std::fabs(other.centerY() - rect.centerY());
            overlap = std::min(rect.bottom(), other.bottom()) - std::max(rect.y, other.y);
            break;
        case MuiPadDir::Down:
            along = other.centerY() - rect.centerY();
            across = std::fabs(other.centerX() - rect.centerX());
            overlap = std::min(rect.right(), other.right()) - std::max(rect.x, other.x);
            break;
        default:
            along = rect.centerY() - other.centerY();
            across = std::fabs(other.centerX() - rect.centerX());
            overlap = std::min(rect.right(), other.right()) - std::max(rect.x, other.x);
            break;
        }

        if (along < 2.0f) {
            return -1.0f;
        }
        // A widget beside, not ahead (more across than along, no overlap).
        if (overlap <= 0.0f && across > along * 1.5f) {
            return -1.0f;
        }
        return along + across * (overlap > 0.0f ? 0.5f : 3.0f);
    }

    const MuiPadScroll* scrollOf(MuiSurface surface, const MuiPadWidget& widget, bool horizontal)
    {
        for (const MuiPadScroll& area : muiPadScrollAreas(surface)) {
            if (area.horizontal == horizontal && widget.id.rfind(area.id + ".", 0) == 0) {
                return &area;
            }
        }
        return nullptr;
    }

    bool towardsEdge(const MuiRect& rect, const MuiRect& area, MuiPadDir direction, float slack)
    {
        switch (direction) {
        case MuiPadDir::Down:
            return rect.bottom() >= area.bottom() - slack;
        case MuiPadDir::Up:
            return rect.y <= area.y + slack;
        case MuiPadDir::Right:
            return rect.right() >= area.right() - slack;
        default:
            return rect.x <= area.x + slack;
        }
    }

    float directionSign(MuiPadDir direction)
    {
        return direction == MuiPadDir::Down || direction == MuiPadDir::Right ? 1.0f : -1.0f;
    }

    bool isHorizontal(MuiPadDir direction)
    {
        return direction == MuiPadDir::Left || direction == MuiPadDir::Right;
    }

    // The widget a screen starts its focus on.
    const MuiPadWidget* initialWidget(MuiSurface surface)
    {
        const MuiPadWidget* preferred = findWidget(surface, muiPadPreferredId(surface));
        if (preferred != nullptr) {
            return preferred;
        }
        const std::vector<MuiPadWidget>& widgets = muiPadWidgets(surface);
        return widgets.empty() ? nullptr : &widgets.front();
    }

    void move(MuiSurface surface, MuiPadDir direction, bool retry)
    {
        const std::vector<MuiPadWidget>& widgets = muiPadWidgets(surface);
        std::string& focusId = muiPadFocusId(surface);
        const MuiPadWidget* current = findWidget(surface, focusId);

        if (current == nullptr) {
            const MuiPadWidget* first = initialWidget(surface);
            if (first != nullptr) {
                focusId = first->id;
            }
            return;
        }

        if (current->slider && isHorizontal(direction)) {
            muiPadSlide(surface, direction == MuiPadDir::Right ? 1 : -1);
            return;
        }

        const MuiPadWidget* best = nullptr;
        float bestScore = 0.0f;
        for (const MuiPadWidget& widget : widgets) {
            if (widget.id == current->id) {
                continue;
            }
            float value = score(current->rect, widget.rect, direction);
            if (value >= 0.0f && (best == nullptr || value < bestScore)) {
                best = &widget;
                bestScore = value;
            }
        }

        const MuiPadScroll* area = scrollOf(surface, *current, isHorizontal(direction));
        float step = (isHorizontal(direction) ? current->rect.w : current->rect.h) + 8.0f;

        if (best == nullptr) {
            // At the end of what is shown: the list scrolls to its next rows.
            if (area != nullptr && !retry) {
                muiPadScrollBy(surface, area->id, directionSign(direction) * step);
                gRetryActive = true;
                gRetrySurface = surface;
                gRetryDir = direction;
                gRetryFrames = 2;
            }
            return;
        }

        focusId = best->id;

        // The row reached is cut by the edge of its list: bring more of it.
        const MuiPadScroll* bestArea = scrollOf(surface, *best, isHorizontal(direction));
        if (bestArea != nullptr && towardsEdge(best->rect, bestArea->rect, direction, 2.0f)) {
            muiPadScrollBy(surface, bestArea->id, directionSign(direction) * step * 0.6f);
        }
    }

    bool listUsable(Object* item)
    {
        if (item == nullptr || (item->flags & OBJECT_EQUIPPED) != 0) {
            return false;
        }
        if (itemGetType(item) == ITEM_TYPE_DRUG) {
            return true;
        }
        ItemProtoTypeId id = ProtoId(item).protoId<ItemProtoTypeId>();
        return id == ItemProtoTypeId::DoctorsBag || id == ItemProtoTypeId::FirstAidKit
            || id == ItemProtoTypeId::ParamedicsBag || id == ItemProtoTypeId::FieldMedicsFirstAidKit;
    }

    // The ring of items to use on the player (what the inventory's "use"
    // does on the figure: 2 AP in combat).
    class QuickUseScreen : public MuiScreen {
    public:
        QuickUseScreen()
        {
            modal = true;
            screenSurface = muiGameScreenSurface();
        }

        MuiSurface surface() override { return screenSurface; }

        void back() override { finished = true; }

        void build(MuiContext& ui) override
        {
            const MuiTheme& theme = muiTheme();
            ui.dim();

            if (items.empty()) {
                MuiItemFilterState filter;
                for (const MuiListItem& entry : muiInventoryItems(gDude, filter)) {
                    if (listUsable(entry.item)) {
                        items.push_back(entry);
                    }
                    if (items.size() >= 8) {
                        break;
                    }
                }
                if (items.empty()) {
                    nothing = true;
                }
            }

            MuiRect screen = ui.safeRect();
            float radius = std::min(screen.w, screen.h) * 0.30f;
            float cell = std::min(radius * 0.62f, ui.dp(96.0f));
            float cx = screen.centerX();
            float cy = screen.centerY();
            float panelRadius = radius + cell * 0.8f;
            MuiRect plate = { cx - panelRadius, cy - panelRadius, panelRadius * 2.0f, panelRadius * 2.0f };
            themedPanel(ui, plate, true);

            std::u32string title = muiDecodeUtf8(nothing ? "NOTHING TO USE" : "QUICK USE");
            muiDrawTextAligned(title, { cx - radius, cy - ui.dp(40.0f), radius * 2.0f, ui.dp(24.0f) }, ui.dp(18.0f), theme.accent, MuiAlign::Center, MuiAlign::Center);

            int count = static_cast<int>(items.size());
            for (int index = 0; index < count; index++) {
                // First item up, then clockwise.
                float angle = -1.5707963f + index * 6.2831853f / std::max(count, 1);
                MuiRect rect = { cx + std::cos(angle) * radius - cell / 2.0f, cy + std::sin(angle) * radius - cell / 2.0f, cell, cell };
                bool pressed;
                std::string id = "quickuse." + std::to_string(index);
                if (ui.touchable(id, rect, &pressed)) {
                    use(items[index].item);
                    finished = true;
                    return;
                }
                muiDrawItemCell(ui, rect, items[index].item, items[index].quantity, pressed);

                if (muiPadFocusId(screenSurface) == id) {
                    char name[256];
                    snprintf(name, sizeof(name), "%s", itemGetName(items[index].item));
                    muiDrawTextAligned(muiDecodeGameText(name), { cx - radius, cy - ui.dp(12.0f), radius * 2.0f, ui.dp(26.0f) }, ui.dp(16.0f), theme.text, MuiAlign::Center, MuiAlign::Center);
                }
            }

            if (ui.button("quickuse.cancel", { cx - ui.dp(60.0f), cy + radius * 0.35f, ui.dp(120.0f), ui.dp(36.0f) }, U"CANCEL")) {
                finished = true;
            }
        }

        void use(Object* item)
        {
            if (isInCombat()) {
                if (gDude->data.critter.combat.ap < 2) {
                    return;
                }
                if (_action_use_an_item_on_object(gDude, gDude, item) != -1) {
                    gDude->data.critter.combat.ap -= 2;
                    interfaceRenderActionPoints(gDude->data.critter.combat.ap, _combat_free_move);
                }
            } else {
                _action_use_an_item_on_object(gDude, gDude, item);
            }
        }

        MuiSurface screenSurface = MuiSurface::Top;
        std::vector<MuiListItem> items;
        bool nothing = false;
    };

} // namespace

void muiPadTouch()
{
    gLastPadInput = uiTicks();
    if (gLastPadInput == 0) {
        gLastPadInput = 1;
    }
}

bool muiPadUiTarget(MuiSurface* surface)
{
    bool bottomModal = muiBottomActive() && muiHasModalOn(MuiSurface::Bottom);
    bool topModal = muiHasModalOn(MuiSurface::Top);
    if (bottomModal && topModal) {
        *surface = gPreferTop ? MuiSurface::Top : MuiSurface::Bottom;
        return true;
    }
    if (bottomModal) {
        *surface = MuiSurface::Bottom;
        return true;
    }
    if (topModal) {
        *surface = MuiSurface::Top;
        return true;
    }

    // The map: the HUD's screen takes the pad when asked to.
    MuiSurface hud = muiHudSurface();
    if (gMapUiFocus && muiPadHasScreen(hud)) {
        *surface = hud;
        return true;
    }
    return false;
}

bool muiPadSwitchFocus()
{
    bool bottomModal = muiBottomActive() && muiHasModalOn(MuiSurface::Bottom);
    bool topModal = muiHasModalOn(MuiSurface::Top);
    if (bottomModal && topModal) {
        gPreferTop = !gPreferTop;
        return true;
    }
    if (bottomModal || topModal) {
        return false;
    }
    if (!muiPadHasScreen(muiHudSurface())) {
        return false;
    }
    gMapUiFocus = !gMapUiFocus;
    return true;
}

void muiPadMove(MuiSurface surface, MuiPadDir direction)
{
    // The first press shows the ring where it stands.
    bool shown = gLastPadInput != 0 && uiTicks() - gLastPadInput < kRingShowMs;
    muiPadTouch();
    if (shown) {
        move(surface, direction, false);
    }
}

void muiPadActivate(MuiSurface surface, bool longPress)
{
    muiPadTouch();
    const MuiPadWidget* widget = findWidget(surface, muiPadFocusId(surface));
    float x;
    float y;
    if (widget != nullptr) {
        if (widget->slider) {
            return;
        }
        x = widget->rect.centerX();
        y = widget->rect.centerY();
    } else {
        MuiRect rect;
        if (muiPadPanZoomRect(surface, &rect)) {
            x = rect.centerX();
            y = rect.centerY();
        } else {
            const MuiPadWidget* first = initialWidget(surface);
            if (first != nullptr) {
                muiPadFocusId(surface) = first->id;
            }
            return;
        }
    }

    int width;
    int height;
    muiPadOutputSize(surface, &width, &height);
    if (width <= 0 || height <= 0) {
        return;
    }
    press(surface, x / width, y / height, longPress ? touchLongPressMs() + 150 : kTapMs);
}

void muiPadStick(MuiSurface surface, float x, float y, float seconds)
{
    int width;
    int height;
    muiPadOutputSize(surface, &width, &height);
    float speed = std::min(width, height) * 0.9f * seconds;

    MuiRect rect;
    if (muiPadPanZoomRect(surface, &rect)) {
        // Stick right: the view goes right, the content moves left.
        muiPadPan(surface, x * speed, y * speed, 1.0f);
        return;
    }

    if (y == 0.0f) {
        return;
    }

    // The list of the focused widget, else the first vertical one.
    const MuiPadWidget* widget = findWidget(surface, muiPadFocusId(surface));
    const MuiPadScroll* target = nullptr;
    if (widget != nullptr) {
        target = scrollOf(surface, *widget, false);
    }
    if (target == nullptr) {
        for (const MuiPadScroll& area : muiPadScrollAreas(surface)) {
            if (!area.horizontal) {
                target = &area;
                break;
            }
        }
    }
    if (target != nullptr) {
        muiPadScrollBy(surface, target->id, y * speed);
    }
}

void muiPadZoom(MuiSurface surface, float factor)
{
    muiPadPan(surface, 0.0f, 0.0f, factor);
}

void muiPadTapTop(float x, float y, bool longPress)
{
    muiPadTouch();
    press(MuiSurface::Top, x, y, longPress ? touchLongPressMs() + 150 : kTapMs);
}

void muiPadSetCursor(bool visible, float x, float y)
{
    gCursorVisible = visible;
    gCursorX = x;
    gCursorY = y;
}

void muiPadCursorGet(float* x, float* y)
{
    *x = gCursorX;
    *y = gCursorY;
}

void muiPadCursorMove(float dx, float dy)
{
    gCursorX = std::clamp(gCursorX + dx, 0.0f, 1.0f);
    gCursorY = std::clamp(gCursorY + dy, 0.0f, 1.0f);
    gCursorVisible = true;
    muiPadTouch();
    if (gHoldActive && gTouchActive) {
        gTouchX = gCursorX;
        gTouchY = gCursorY;
        pushFinger(SDL_FINGERMOTION, MuiSurface::Top, gCursorX, gCursorY);
    }
}

void muiPadRightClickTop(float x, float y)
{
    muiPadTouch();
    if (!muiHasModal() && touchControlsRightClick(static_cast<int>(x * screenGetWidth()), static_cast<int>(y * screenGetHeight()))) {
        return;
    }
    if (muiHasModalOn(MuiSurface::Top)) {
        press(MuiSurface::Top, x, y, touchLongPressMs() + 150);
    }
}

void muiPadTopHold(bool down)
{
    if (down) {
        if (gTouchActive) {
            return;
        }
        gHoldActive = true;
        press(MuiSurface::Top, gCursorX, gCursorY, 600000);
    } else if (gHoldActive) {
        gHoldActive = false;
        if (gTouchActive) {
            gTouchActive = false;
            pushFinger(SDL_FINGERUP, MuiSurface::Top, gTouchX, gTouchY);
        }
    }
}

void muiPadSetTouchpadOpen(bool open)
{
    gTouchpadOpen = open;
    if (open) {
        gCursorVisible = true;
    } else {
        muiPadTopHold(false);
    }
}

void muiPadFingerCursor(bool down, float x, float y)
{
    gFingerDown = down;
    gFingerX = x;
    gFingerY = y;
    gFingerUntil = uiTicks() + kFingerCursorMs;
}

void muiPadTick()
{
    Uint32 now = uiTicks();
    if (gTouchActive && !gHoldActive && static_cast<int>(now - gTouchUpAt) >= 0) {
        gTouchActive = false;
        pushFinger(SDL_FINGERUP, gTouchSurface, gTouchX, gTouchY);
    }

    // A modal screen closing returns the pad to the map, not to the HUD.
    if (muiHasModal()) {
        gMapUiFocus = false;
    }
}

void muiPadFrame(MuiContext& ui, MuiSurface surface, bool hasScreen)
{
    // A real finger on the screen hides the ring.
    if (ui.pointerDown() && !gTouchActive) {
        gLastPadInput = 0;
    }

    // Slider steps are for the frame they were asked in.
    muiPadSlide(surface, 0);

    if (gRetryActive && gRetrySurface == surface) {
        if (--gRetryFrames <= 0) {
            gRetryActive = false;
            move(surface, gRetryDir, true);
        }
    }

    bool recent = gLastPadInput != 0 && uiTicks() - gLastPadInput < kRingShowMs;

    // The cursor: of the pad (recent input, the map only), of the touchpad
    // page (over any screen of the top), or at the player's finger.
    float cursorX = gCursorX;
    float cursorY = gCursorY;
    bool drawCursor = recent && gCursorVisible && !muiHasModal();
    if (gTouchpadOpen && gCursorVisible) {
        drawCursor = true;
    }
    if (settings.touch.touch_cursor && (gFingerDown || static_cast<int>(gFingerUntil - uiTicks()) > 0) && gFingerUntil != 0) {
        drawCursor = true;
        cursorX = gFingerX;
        cursorY = gFingerY;
    }
    if (surface == MuiSurface::Top && drawCursor) {
        // The cursor: a thin amber cross with a dark edge, as the game's own cursors.
        float cx = cursorX * ui.screenRect().w;
        float cy = cursorY * ui.screenRect().h;
        float arm = ui.dp(14.0f);
        float gap = ui.dp(4.0f);
        MuiColor dark = muiRgb(0x000000, 220);
        MuiColor amber = muiRgb(0xFFD84A);
        for (int pass = 0; pass < 2; pass++) {
            float w = pass == 0 ? ui.dp(4.0f) : ui.dp(2.0f);
            MuiColor color = pass == 0 ? dark : amber;
            muiDrawLine(cx - arm, cy, cx - gap, cy, w, color);
            muiDrawLine(cx + gap, cy, cx + arm, cy, w, color);
            muiDrawLine(cx, cy - arm, cx, cy - gap, w, color);
            muiDrawLine(cx, cy + gap, cx, cy + arm, w, color);
        }
        muiFillCircle(cx, cy, ui.dp(1.5f), amber);
    }

    MuiSurface target;
    if (!hasScreen || !recent || !muiPadUiTarget(&target) || target != surface) {
        return;
    }

    std::string& focusId = muiPadFocusId(surface);
    const MuiPadWidget* widget = findWidget(surface, focusId);
    if (widget == nullptr) {
        // A new screen: the ring starts on its first widget (not on maps).
        MuiRect rect;
        const MuiPadWidget* first = initialWidget(surface);
        if (first == nullptr || muiPadPanZoomRect(surface, &rect)) {
            return;
        }
        focusId = first->id;
        widget = first;
    }

    // The ring: a dark edge, a pulsing phosphor green line, bright corners.
    float pulse = 0.5f + 0.5f * std::sin(uiTicks() / 160.0f);
    MuiRect rect = widget->rect.inset(-ui.dp(2.0f));
    MuiColor green = muiRgb(0x5CFF5C, static_cast<Uint8>(190 + 65 * pulse));
    MuiColor dark = muiRgb(0x021002, 230);
    muiStrokeRoundRect(rect.inset(-ui.dp(1.5f)), ui.dp(3.0f), ui.dp(1.5f), dark);
    muiStrokeRoundRect(rect, ui.dp(2.0f), ui.dp(2.5f), green);
    muiStrokeRoundRect(rect.inset(ui.dp(2.5f)), ui.dp(1.0f), ui.dp(1.0f), dark);
    float corner = std::min(ui.dp(14.0f), std::min(rect.w, rect.h) / 2.5f);
    float t = ui.dp(3.5f);
    MuiColor bright = muiRgb(0xC8FFC8);
    muiFillRect({ rect.x - t / 2, rect.y - t / 2, corner, t }, bright);
    muiFillRect({ rect.x - t / 2, rect.y - t / 2, t, corner }, bright);
    muiFillRect({ rect.right() - corner + t / 2, rect.y - t / 2, corner, t }, bright);
    muiFillRect({ rect.right() - t / 2, rect.y - t / 2, t, corner }, bright);
    muiFillRect({ rect.x - t / 2, rect.bottom() - t / 2, corner, t }, bright);
    muiFillRect({ rect.x - t / 2, rect.bottom() - corner + t / 2, t, corner }, bright);
    muiFillRect({ rect.right() - corner + t / 2, rect.bottom() - t / 2, corner, t }, bright);
    muiFillRect({ rect.right() - t / 2, rect.bottom() - corner + t / 2, t, corner }, bright);
}

void muiPadQuickUseRun()
{
    QuickUseScreen screen;
    muiRunModal(&screen);
}

} // namespace fallout
