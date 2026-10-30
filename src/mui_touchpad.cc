#include "mui_touchpad.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "dev_autotest.h"
#include "mui.h"
#include "mui_draw.h"
#include "mui_pad.h"
#include "settings.h"
#include "svga.h"
#include "touch.h"

namespace fallout {

namespace {

    // Feel of SpongeBob's touchpad. Distances are in pixels of a 1240 wide
    // screen (the Thor's bottom one), the cursor's in the top screen's
    // 640 wide frame of that port.
    constexpr float kLayoutWidth = 1240.0f;
    constexpr float kReferenceWidth = 640.0f;
    // A finger may wander this far and still tap / hold.
    constexpr float kSlop = 22.0f;
    constexpr Uint32 kTapMs = 320;
    constexpr Uint32 kHoldMs = 380;
    constexpr Uint32 kClickMs = 70;
    constexpr Uint32 kTwoFingerTapMs = 450;
    // Gain: 0.45 crawling .. 2.25 flicking (finger speed in pixels a millisecond).
    constexpr float kGainMin = 0.45f;
    constexpr float kGainSpeed = 0.9f;
    constexpr float kGainMax = 1.8f;

    class TouchpadScreen : public MuiScreen {
    public:
        TouchpadScreen()
        {
            modal = false;
        }

        MuiSurface surface() override { return MuiSurface::Bottom; }
        bool isActive() override;
        void build(MuiContext& ui) override;
        void back() override { close(); }

        void open();
        void close();
        bool isOpen() const { return opened; }

    private:
        bool opened = false;
        bool wasDown = false;
        bool tracking = false;
        bool slopLeft = false;
        bool held = false;
        bool two = false;
        float startX = 0.0f;
        float startY = 0.0f;
        float lastX = 0.0f;
        float lastY = 0.0f;
        Uint32 startTime = 0;
        Uint32 lastTime = 0;
        float speed = 0.0f;
        // The left button of a click goes up at this time (0 - none).
        Uint32 clickUpAt = 0;
        // The dots drift with the finger.
        float driftX = 0.0f;
        float driftY = 0.0f;

        void begin(const MuiContext& ui, float x, float y);
        void move(const MuiContext& ui, float x, float y, bool second);
        void end(const MuiContext& ui, float x, float y);
        void clickLeft(Uint32 now);
    };

    TouchpadScreen gScreen;
    bool gInitialized = false;

    bool TouchpadScreen::isActive()
    {
        bool active = opened && muiBottomActive() && !muiHasModalOn(MuiSurface::Bottom);
        muiPadSetTouchpadOpen(active);
        return active;
    }

    void TouchpadScreen::open()
    {
        opened = true;
        wasDown = false;
        tracking = false;
        held = false;
        clickUpAt = 0;
        openTime = uiTicks();
    }

    void TouchpadScreen::close()
    {
        opened = false;
        tracking = false;
        held = false;
        clickUpAt = 0;
        muiPadSetTouchpadOpen(false);
    }

    void TouchpadScreen::clickLeft(Uint32 now)
    {
        muiPadTopHold(true);
        clickUpAt = now + kClickMs;
    }

    void TouchpadScreen::begin(const MuiContext& ui, float x, float y)
    {
        tracking = true;
        slopLeft = false;
        held = false;
        two = false;
        startX = lastX = x;
        startY = lastY = y;
        startTime = lastTime = ui.now;
        speed = 0.0f;
        muiPadTouch();
    }

    void TouchpadScreen::move(const MuiContext& ui, float x, float y, bool second)
    {
        float scale = kLayoutWidth / std::max(ui.screenRect().w, 1.0f);
        if (second && !slopLeft && !held) {
            two = true;
        }

        if (!held && !slopLeft && !two && static_cast<int>(ui.now - startTime) >= static_cast<int>(kHoldMs)
            && std::hypot(x - startX, y - startY) * scale < kSlop) {
            // Hold still: the left button goes down, the next moves drag.
            held = true;
            muiPadTopHold(true);
        }

        if (!slopLeft && std::hypot(x - startX, y - startY) * scale >= kSlop) {
            // Not a tap or a hold any more (a held button stays down); the
            // cursor starts from here, no jump by the slop.
            slopLeft = true;
            lastX = x;
            lastY = y;
            lastTime = ui.now;
        }

        if (two || (!slopLeft && !held)) {
            return;
        }

        float dx = (x - lastX) * scale;
        float dy = (y - lastY) * scale;
        float dt = std::max(static_cast<float>(ui.now - lastTime), 1.0f);
        lastX = x;
        lastY = y;
        lastTime = ui.now;
        if (dx == 0.0f && dy == 0.0f) {
            return;
        }

        speed = speed * 0.6f + std::hypot(dx, dy) / dt * 0.4f;
        float gain = kGainMin + std::min(kGainMax, speed * kGainSpeed);
        float factor = gain / kReferenceWidth * settings.touch.cursor_speed / 100.0f;
        float aspect = static_cast<float>(screenGetWidth()) / std::max(screenGetHeight(), 1);
        muiPadCursorMove(dx * factor, dy * factor * aspect);
        driftX += dx;
        driftY += dy;
    }

    void TouchpadScreen::end(const MuiContext& ui, float x, float y)
    {
        tracking = false;
        Uint32 duration = ui.now - startTime;
        float scale = kLayoutWidth / std::max(ui.screenRect().w, 1.0f);
        bool still = !slopLeft && std::hypot(x - startX, y - startY) * scale < kSlop;
        if (held) {
            held = false;
            muiPadTopHold(false);
        } else if (two) {
            if (still && duration <= kTwoFingerTapMs && settings.touch.two_finger_click) {
                float cx;
                float cy;
                muiPadCursorGet(&cx, &cy);
                muiPadRightClickTop(cx, cy);
            }
        } else if (still) {
            // Held long enough but let go before it armed: the same as a tap
            // (SpongeBob arms then releases).
            if (duration <= kTapMs || duration >= kHoldMs) {
                clickLeft(ui.now);
            }
        }
    }

    void TouchpadScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        MuiRect screen = ui.screenRect();

        float headerHeight = ui.dp(60.0f);
        float margin = ui.dp(theme.padding);
        MuiRect padRect = { screen.x + margin, screen.y + headerHeight, screen.w - margin * 2.0f, screen.h - headerHeight - margin };

        // The pad's touches, one frame at a time.
        if (clickUpAt != 0 && static_cast<int>(ui.now - clickUpAt) >= 0) {
            clickUpAt = 0;
            muiPadTopHold(false);
        }
        if (ui.interactive()) {
            bool down = ui.pointerDown();
            float x = ui.pointerX();
            float y = ui.pointerY();
            if (down && !wasDown) {
                if (padRect.contains(x, y)) {
                    begin(ui, x, y);
                }
            } else if (down && tracking) {
                move(ui, x, y, ui.secondPointerDown());
            } else if (!down && wasDown && tracking) {
                end(ui, x, y);
            }
            wasDown = down;
            if (tracking && ui.secondPointerDown() && !slopLeft && !held) {
                two = true;
            }
        }

        // Drawing: the plate, the pad's well with a dot grid, the hint.
        muiFillRect(screen, theme.background);
        std::u32string title = muiDecodeGameText(muiText(1121, "TOUCHPAD"));
        muiDrawTextAligned(title, { screen.x + margin, screen.y, screen.w * 0.5f, headerHeight }, ui.dp(theme.titleSize), theme.accent, MuiAlign::Start, MuiAlign::Center);

        float backWidth = ui.dp(120.0f);
        MuiRect backRect = { screen.right() - margin - backWidth, screen.y + ui.dp(4.0f), backWidth, headerHeight - ui.dp(8.0f) };
        if (ui.button("touchpad.back", backRect, muiDecodeGameText(muiText(1122, "BACK")), MuiButtonStyle::Primary)) {
            close();
            return;
        }

        bool active = tracking && (slopLeft || held);
        muiFillRoundRect(padRect, ui.dp(theme.radius), theme.well);
        muiPushClip(padRect);
        float step = ui.dp(36.0f);
        float offsetX = std::fmod(driftX * (screen.w / kLayoutWidth), step);
        float offsetY = std::fmod(driftY * (screen.w / kLayoutWidth), step);
        if (offsetX < 0.0f) {
            offsetX += step;
        }
        if (offsetY < 0.0f) {
            offsetY += step;
        }
        MuiColor dot = theme.textDim.withAlpha(active ? 150 : 90);
        for (float py = padRect.y + offsetY; py < padRect.bottom(); py += step) {
            for (float px = padRect.x + offsetX; px < padRect.right(); px += step) {
                muiFillCircle(px, py, ui.dp(1.6f), dot);
            }
        }
        muiPopClip();
        muiStrokeRoundRect(padRect, ui.dp(theme.radius), ui.dp(held ? 4.0f : 2.0f), held ? theme.accent : theme.panelBorder);

        float centerY = padRect.centerY();
        if (!tracking) {
            float bodySize = ui.dp(theme.bodySize);
            // The middle dot if the font has it.
            const char* dot = muiFontHasGlyph(0xB7) ? " \xC2\xB7 " : " - ";
            std::string hintText = std::string("drag = move cursor") + dot + "tap = click" + dot + "hold then drag = carry" + dot + "two-finger tap = right click";
            std::u32string hint = muiDecodeUtf8(muiText(1123, hintText.c_str()));
            std::vector<std::u32string> lines = muiWrapText(hint, padRect.w - ui.dp(48.0f), bodySize);
            float lineHeight = muiLineHeight(bodySize);
            float y = centerY - lineHeight * lines.size() / 2.0f;
            for (const std::u32string& line : lines) {
                muiDrawTextAligned(line, { padRect.x, y, padRect.w, lineHeight }, bodySize, theme.textDim, MuiAlign::Center, MuiAlign::Center);
                y += lineHeight;
            }
        } else if (held) {
            muiDrawTextAligned(muiDecodeGameText(muiText(1124, "HOLDING")), { padRect.x, padRect.bottom() - ui.dp(56.0f), padRect.w, ui.dp(40.0f) }, ui.dp(theme.titleSize), theme.accent, MuiAlign::Center, MuiAlign::Center);
        }

        ui.mark("touchpad.pad", padRect);
        ui.mark("touchpad.back", backRect);
    }

} // namespace

void muiTouchpadInit()
{
    if (!gInitialized) {
        gInitialized = true;
        muiPush(&gScreen);
    }
}

void muiTouchpadFree()
{
    if (gInitialized) {
        gScreen.close();
        muiRemove(&gScreen);
        gInitialized = false;
    }
}

void muiTouchpadOpen()
{
    gScreen.open();
}

void muiTouchpadClose()
{
    gScreen.close();
}

bool muiTouchpadIsOpen()
{
    return gScreen.isOpen();
}

} // namespace fallout
