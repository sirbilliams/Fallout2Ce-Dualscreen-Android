#include "mui_dual.h"
#include "mui_skin.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

#include "critter.h"
#include "game.h"
#include "main.h"
#include "map.h"
#include "object.h"
#include "scripts.h"
#include "stat.h"

namespace fallout {

namespace {

    // Texts in `game\ce.msg`, English fallbacks for data without them.
    constexpr int kTextPaused = 902;
    constexpr int kTextInConversation = 903;
    constexpr int kTextTraveling = 904;
    constexpr int kTextLevel = 905;
    constexpr int kTextWasteland = 906;
    constexpr int kTextDualScreen = 907;
    constexpr int kTextStatusPlate = 917;
    constexpr int kTextRads = 919;

    const char* const kMonths[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

    const MuiColor kFadeColor = muiRgb(0x17120C);
    const MuiColor kWarningRed = muiRgb(0xFF3B30);
    const MuiColor kHpGood = muiRgb(0x3CE03C);
    const MuiColor kHpMid = muiRgb(0xF0D030);
    const MuiColor kHpLow = muiRgb(0xE84030);

    // A screen opening on the bottom screen fades in this long.
    constexpr unsigned int kFadeMs = 180;
    // The top screen's dim comes in this long.
    constexpr unsigned int kDimMs = 220;
    // Low HP: at or under this part of the maximum, one pulse this long.
    constexpr float kLowHitPoints = 0.25f;
    constexpr float kPulseMs = 1200.0f;

    // The bottom screen switches on as a CRT: a line, opening up, a flash.
    constexpr unsigned int kPowerOnMs = 700;
    const MuiColor kPhosphor = muiRgb(0xEFFFE0);

    // Radiation zones of the Geiger gauge (rads up to each end): the game's
    // radiation levels (critter.cc).
    struct RadZone {
        float end;
        MuiColor color;
    };
    const RadZone kRadZones[] = {
        { 100.0f, muiRgb(0x3CE03C) },
        { 200.0f, muiRgb(0xF0D030) },
        { 400.0f, muiRgb(0xFF9A2E) },
        { 600.0f, muiRgb(0xFF5A3C) },
        { 1000.0f, muiRgb(0xD81E1E) },
    };
    constexpr float kRadMax = 1000.0f;

    // Where [rads] sit on the dial, 0..1: a square root scale, as many Geiger
    // counters have, so the low levels that matter most get room.
    float radPosition(float rads)
    {
        return sqrtf(std::clamp(rads, 0.0f, kRadMax) / kRadMax);
    }

    MuiColor darkened(MuiColor color, float factor)
    {
        return { static_cast<Uint8>(color.r * factor), static_cast<Uint8>(color.g * factor), static_cast<Uint8>(color.b * factor), color.a };
    }

    // The bottom screen came on: the power-on starts at its next frame.
    bool gPowerOnPending = true;
    unsigned int gPowerOnStart = 0;

    MuiScreen* gLastTop = nullptr;
    unsigned int gTopChangeTime = 0;

    bool gTopDimming = false;
    unsigned int gTopDimStart = 0;

    std::u32string text(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }

    // A game is being played (not the main menu, not starting or loading
    // one from there): the game's main loop runs. The map loaded behind the
    // main menu doesn't count, nor dude before he is made.
    bool inGame()
    {
        return gDude != nullptr && mainIsPlaying();
    }

    // A point on the arc of [radiusX] x [radiusY] around ([centerX],
    // [centerY]) at [angle] (0 right, pi left, upwards).
    SDL_FPoint arcPoint(float centerX, float centerY, float radiusX, float radiusY, float angle)
    {
        return { centerX + radiusX * cosf(angle), centerY - radiusY * sinf(angle) };
    }

    // The CRT switching on over the whole bottom screen, [progress] 0..1.
    void drawPowerOn(MuiContext& ui, float progress)
    {
        MuiRect screen = ui.screenRect();
        MuiColor black = muiRgb(0x000000);
        if (progress < 0.25f) {
            // A dot stretching into a line across the middle.
            float stretch = progress / 0.25f;
            stretch = 1.0f - (1.0f - stretch) * (1.0f - stretch);
            muiFillRect(screen, black);
            float width = std::max(screen.w * stretch, ui.dp(6.0f));
            float thickness = ui.dp(3.0f);
            MuiRect line = { screen.centerX() - width / 2.0f, screen.centerY() - thickness / 2.0f, width, thickness };
            muiFillRect(line.inset(0.0f, -ui.dp(5.0f)), kPhosphor.withAlpha(60));
            muiFillRect(line, kPhosphor);
            return;
        }

        if (progress < 0.7f) {
            // The line opens up into the picture.
            float open = (progress - 0.25f) / 0.45f;
            open = 1.0f - (1.0f - open) * (1.0f - open) * (1.0f - open);
            float height = std::max(screen.h * open, ui.dp(3.0f));
            float top = screen.centerY() - height / 2.0f;
            muiFillRect({ screen.x, screen.y, screen.w, top - screen.y }, black);
            muiFillRect({ screen.x, top + height, screen.w, screen.bottom() - top - height }, black);
            MuiColor edge = kPhosphor.withAlpha(static_cast<Uint8>(200.0f * (1.0f - open)));
            muiFillRect({ screen.x, top, screen.w, ui.dp(2.0f) }, edge);
            muiFillRect({ screen.x, top + height - ui.dp(2.0f), screen.w, ui.dp(2.0f) }, edge);
            muiFillRect(screen, kPhosphor.withAlpha(static_cast<Uint8>(70.0f * (1.0f - open))));
            return;
        }

        // A last flash fading out.
        float fade = (progress - 0.7f) / 0.3f;
        muiFillRect(screen, kPhosphor.withAlpha(static_cast<Uint8>(55.0f * (1.0f - fade))));
    }

    // The sun by day (6:00 to 18:00) and the moon by night on an arc over the
    // horizon of [rect], where the game's clock has them.
    void drawDayArcAt(MuiContext& ui, const MuiRect& rect, int hour)
    {
        const MuiTheme& theme = muiTheme();
        float minutes = static_cast<float>((hour / 100) * 60 + hour % 100);
        bool day = minutes >= 360.0f && minutes < 1080.0f;
        float progress = day ? (minutes - 360.0f) / 720.0f : fmodf(minutes - 1080.0f + 1440.0f, 1440.0f) / 720.0f;

        float centerX = rect.centerX();
        float baseY = rect.bottom() - ui.dp(4.0f);
        float radiusX = rect.w / 2.0f - ui.dp(10.0f);
        float radiusY = rect.h - ui.dp(14.0f);

        // The horizon and the path, dotted.
        muiFillRect({ rect.x, baseY, rect.w, std::max(ui.dp(1.0f), 1.0f) }, theme.textDim.withAlpha(110));
        constexpr int kDots = 23;
        for (int index = 0; index <= kDots; index++) {
            float angle = 3.14159265f * index / kDots;
            SDL_FPoint point = arcPoint(centerX, baseY, radiusX, radiusY, angle);
            muiFillCircle(point.x, point.y, ui.dp(1.1f), theme.textDim.withAlpha(90));
        }

        SDL_FPoint body = arcPoint(centerX, baseY, radiusX, radiusY, 3.14159265f * (1.0f - progress));
        if (day) {
            MuiColor sun = theme.accent;
            for (int ray = 0; ray < 8; ray++) {
                float angle = ray * 3.14159265f / 4.0f;
                float inner = ui.dp(9.5f);
                float outer = ui.dp(13.0f);
                muiDrawLine(body.x + cosf(angle) * inner, body.y + sinf(angle) * inner, body.x + cosf(angle) * outer, body.y + sinf(angle) * outer, ui.dp(1.6f), sun);
            }
            muiFillCircle(body.x, body.y, ui.dp(11.0f), sun.withAlpha(50));
            muiFillCircle(body.x, body.y, ui.dp(7.0f), sun);
        } else {
            // A crescent: the moon, the sky's dark over most of it.
            muiFillCircle(body.x, body.y, ui.dp(10.0f), muiRgb(0xDCE8FF, 30));
            muiFillCircle(body.x, body.y, ui.dp(7.0f), muiRgb(0xDCE8FF));
            muiFillCircle(body.x + ui.dp(3.2f), body.y - ui.dp(1.8f), ui.dp(6.2f), theme.well.withAlpha(255));
        }
    }

    // A Geiger counter's gauge: a half dial from 0 to 1000 rads in the game's
    // radiation levels' colors, its needle twitching while dude is radiated.
    void drawRadGaugeAt(MuiContext& ui, float centerX, float baseY, float radius, float rads)
    {
        const MuiTheme& theme = muiTheme();

        // The zones, as thick arcs.
        float start = 0.0f;
        for (const RadZone& zone : kRadZones) {
            std::vector<SDL_FPoint> points;
            constexpr int kSteps = 12;
            for (int step = 0; step <= kSteps; step++) {
                float value = start + (zone.end - start) * step / kSteps;
                points.push_back(arcPoint(centerX, baseY, radius, radius, 3.14159265f * (1.0f - radPosition(value))));
            }
            // Opaque (no darker joints), zones not reached dimmed.
            muiDrawPolyline(points, ui.dp(6.0f), rads >= start ? zone.color : darkened(zone.color, 0.42f));
            start = zone.end;
        }

        // Ticks every 100 rads.
        for (int tick = 0; tick <= 10; tick++) {
            float angle = 3.14159265f * (1.0f - radPosition(tick * 100.0f));
            SDL_FPoint inner = arcPoint(centerX, baseY, radius - ui.dp(9.0f), radius - ui.dp(9.0f), angle);
            SDL_FPoint outer = arcPoint(centerX, baseY, radius - ui.dp(5.0f), radius - ui.dp(5.0f), angle);
            muiDrawLine(inner.x, inner.y, outer.x, outer.y, ui.dp(tick % 5 == 0 ? 1.8f : 1.0f), theme.textDim);
        }

        // The needle: twitches with the counts while there are rads.
        float value = std::clamp(rads, 0.0f, kRadMax);
        if (rads > 0.0f) {
            float twitch = sinf(ui.now * 0.031f) * sinf(ui.now * 0.017f + 1.3f) + 0.4f * sinf(ui.now * 0.11f);
            value = std::clamp(value + twitch * 9.0f, 0.0f, kRadMax);
        }
        SDL_FPoint tip = arcPoint(centerX, baseY, radius - ui.dp(3.0f), radius - ui.dp(3.0f), 3.14159265f * (1.0f - radPosition(value)));
        muiDrawLine(centerX + ui.dp(0.8f), baseY + ui.dp(1.2f), tip.x + ui.dp(0.8f), tip.y + ui.dp(1.2f), ui.dp(2.6f), muiRgb(0x000000, 140));
        muiDrawLine(centerX, baseY, tip.x, tip.y, ui.dp(2.2f), muiRgb(0xF2DDA6));
        muiFillCircle(centerX, baseY, ui.dp(5.0f), muiRgb(0x6B5A42));
        muiFillCircle(centerX - ui.dp(1.2f), baseY - ui.dp(1.2f), ui.dp(2.2f), muiRgb(0xE8D5A8, 160));

        // "RADS 42" under the hub.
        char label[32];
        snprintf(label, sizeof(label), "%s %d", muiText(kTextRads, "RADS"), static_cast<int>(rads));
        MuiColor color = theme.textDim;
        for (const RadZone& zone : kRadZones) {
            if (rads < zone.end) {
                color = rads > 0.0f ? zone.color : theme.textDim;
                break;
            }
        }
        if (rads >= kRadMax) {
            color = kRadZones[4].color;
        }
        MuiRect labelRect = { centerX - radius, baseY + ui.dp(6.0f), radius * 2.0f, ui.dp(18.0f) };
        muiDrawTextAligned(muiDecodeGameText(label), labelRect, ui.dp(13.0f), color, MuiAlign::Center, MuiAlign::Center);
    }

    MuiColor hitPointsColor(float fraction)
    {
        if (fraction <= kLowHitPoints) {
            return kHpLow;
        }
        if (fraction <= 0.5f) {
            return kHpMid;
        }
        return kHpGood;
    }

} // namespace

void muiDualHitPointsBar(MuiContext& ui, const MuiRect& rect)
{
    const MuiTheme& theme = muiTheme();
    float fraction = muiDualHitPointsFraction();
    float radius = rect.h / 2.0f;
    muiFillRoundRect(rect, radius, muiRgb(0x2A2218, 230));
    if (fraction > 0.0f) {
        MuiRect fill = { rect.x, rect.y, std::max(rect.w * fraction, rect.h), rect.h };
        muiFillRoundRect(fill, radius, hitPointsColor(fraction).withAlpha(210));
    }
    muiStrokeRoundRect(rect, radius, ui.dp(1.0f), theme.panelBorder);

    char label[48];
    snprintf(label, sizeof(label), "HP %d/%d", critterGetHitPoints(gDude), critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS));
    muiDrawTextAligned(muiDecodeUtf8(label), rect, rect.h * 0.62f, muiRgb(0x041006), MuiAlign::Center, MuiAlign::Center);
}

void muiDualDayArc(MuiContext& ui, const MuiRect& rect, int hour)
{
    drawDayArcAt(ui, rect, hour);
}

void muiDualRadGauge(MuiContext& ui, float centerX, float baseY, float radius, float rads)
{
    drawRadGaugeAt(ui, centerX, baseY, radius, rads);
}

void muiDualPowerOnFrame(MuiContext& ui, float progress)
{
    drawPowerOn(ui, progress);
}

void muiDualPowerOn()
{
    gPowerOnPending = true;
}

float muiDualHitPointsFraction()
{
    if (!inGame()) {
        return 1.0f;
    }
    int maximum = critterGetStat(gDude, STAT_MAXIMUM_HIT_POINTS);
    if (maximum <= 0) {
        return 1.0f;
    }
    return std::clamp(static_cast<float>(critterGetHitPoints(gDude)) / maximum, 0.0f, 1.0f);
}

void muiDualBackdrop(MuiContext& ui)
{
    // The bottom screen is a metal console (mui_skin.h), bolted at the
    // corners.
    MuiRect screen = ui.screenRect();
    muiSkinBackdrop(ui, screen);
    float inset = ui.dp(9.0f);
    muiSkinRivet(ui, screen.x + inset, screen.y + inset);
    muiSkinRivet(ui, screen.right() - inset, screen.y + inset);
    muiSkinRivet(ui, screen.x + inset, screen.bottom() - inset);
    muiSkinRivet(ui, screen.right() - inset, screen.bottom() - inset);
}

void muiDualCompanion(MuiContext& ui)
{
    const MuiTheme& theme = muiTheme();
    MuiRect screen = ui.screenRect();
    float width = std::min(screen.w - ui.dp(88.0f), ui.dp(400.0f));
    float x = screen.centerX() - width / 2.0f;

    // A monitor in a riveted bezel, the readouts in it.
    float bezel = ui.dp(16.0f);
    float padding = ui.dp(14.0f);
    auto monitor = [&](float contentHeight, bool label) {
        float height = contentHeight + (bezel + padding) * 2.0f;
        MuiRect plate = { x - bezel - padding, screen.centerY() - height / 2.0f, width + (bezel + padding) * 2.0f, height };
        muiSkinPlate(ui, plate, true);
        muiSkinWell(ui, plate.inset(bezel));
        if (label) {
            muiSkinLabel(ui, plate.centerX(), plate.y + bezel / 2.0f, text(kTextStatusPlate, "STATUS"));
        }
        return plate.y + bezel + padding;
    };

    if (!inGame()) {
        // Outside a game: the title.
        float y = monitor(ui.dp(84.0f), false);
        MuiRect title = { x, y, width, ui.dp(56.0f) };
        muiDrawTextAligned(muiDecodeUtf8("FALLOUT 2"), title, ui.dp(44.0f), theme.accent, MuiAlign::Center, MuiAlign::Center);
        MuiRect subtitle = { x, title.bottom(), width, ui.dp(28.0f) };
        muiDrawTextAligned(text(kTextDualScreen, "Dual screen"), subtitle, ui.dp(16.0f), theme.textDim, MuiAlign::Center, MuiAlign::Center);
        return;
    }

    // What is going on up there.
    std::u32string mode;
    int gameMode = GameMode::getCurrentGameMode();
    if ((gameMode & (GameMode::kDialog | GameMode::kBarter)) != 0) {
        mode = text(kTextInConversation, "In conversation");
    } else if ((gameMode & GameMode::kWorldmap) != 0) {
        mode = text(kTextTraveling, "Traveling");
    }

    // Place, the day's arc, clock, date, rule, who, HP, the Geiger gauge.
    float arcHeight = ui.dp(46.0f);
    float gaugeRadius = ui.dp(48.0f);
    float contentHeight = ui.dp(24.0f + 4.0f) + arcHeight + ui.dp(64.0f + 26.0f + 12.0f + 1.0f + 12.0f + 24.0f + 10.0f + 22.0f + 14.0f) + gaugeRadius + ui.dp(26.0f);
    if (!mode.empty()) {
        contentHeight += ui.dp(14.0f + 30.0f);
    }
    float y = monitor(contentHeight, true);

    // Where.
    std::u32string place;
    Map map = mapGetCurrentMap();
    if ((gameMode & GameMode::kWorldmap) == 0 && mapIsValid(map)) {
        place = muiDecodeGameText(mapGetCityName(map));
        std::u32string name = muiDecodeGameText(mapGetName(map, gElevation));
        if (!name.empty() && name != place) {
            place += place.empty() ? name : U" \u00B7 " + name;
        }
    }
    if (place.empty()) {
        place = text(kTextWasteland, "Wasteland");
    }
    MuiRect placeRect = { x, y, width, ui.dp(24.0f) };
    float placeSize = muiFitTextSize(place, width, ui.dp(16.0f), ui.dp(11.0f));
    muiDrawTextAligned(place, placeRect, placeSize, theme.accent, MuiAlign::Center, MuiAlign::Center);
    y = placeRect.bottom() + ui.dp(4.0f);

    // The sun or the moon on their way across the sky.
    muiDualDayArc(ui, { x + width * 0.12f, y, width * 0.76f, arcHeight }, gameTimeGetHour());
    y += arcHeight;

    // The game's clock, large, and its date: green readouts.
    int hour = gameTimeGetHour();
    char clock[16];
    snprintf(clock, sizeof(clock), "%02d:%02d", hour / 100, hour % 100);
    MuiRect clockRect = { x, y, width, ui.dp(64.0f) };
    muiDrawTextAligned(muiDecodeUtf8(clock), clockRect, ui.dp(64.0f), theme.text, MuiAlign::Center, MuiAlign::Center);
    y = clockRect.bottom();

    int month;
    int day;
    int year;
    gameTimeGetDate(&month, &day, &year);
    char date[32];
    snprintf(date, sizeof(date), "%d %s %d", day, kMonths[std::clamp(month, 1, 12) - 1], year);
    MuiRect dateRect = { x, y, width, ui.dp(26.0f) };
    muiDrawTextAligned(muiDecodeUtf8(date), dateRect, ui.dp(18.0f), theme.textDim, MuiAlign::Center, MuiAlign::Center);
    y = dateRect.bottom() + ui.dp(12.0f);

    muiFillRect({ x + width * 0.1f, y, width * 0.8f, std::max(ui.dp(1.0f), 1.0f) }, theme.textDim.withAlpha(120));
    y += ui.dp(1.0f) + ui.dp(12.0f);

    // Who: name and level, then HP.
    char level[32];
    snprintf(level, sizeof(level), muiText(kTextLevel, "Level %d"), pcGetStat(PC_STAT_LEVEL));
    std::u32string who = muiDecodeGameText(critterGetName(gDude)) + U" \u00B7 " + muiDecodeGameText(level);
    MuiRect whoRect = { x, y, width, ui.dp(24.0f) };
    muiDrawTextAligned(who, whoRect, muiFitTextSize(who, width, ui.dp(17.0f), ui.dp(12.0f)), theme.text, MuiAlign::Center, MuiAlign::Center);
    y = whoRect.bottom() + ui.dp(10.0f);

    muiDualHitPointsBar(ui, { x + width * 0.1f, y, width * 0.8f, ui.dp(22.0f) });
    y += ui.dp(22.0f) + ui.dp(14.0f);

    // The Geiger counter.
    muiDualRadGauge(ui, x + width / 2.0f, y + gaugeRadius, gaugeRadius, static_cast<float>(critterGetRadiation(gDude)));
    y += gaugeRadius + ui.dp(26.0f) + ui.dp(14.0f);

    if (!mode.empty()) {
        float size = ui.dp(15.0f);
        float chipWidth = muiTextWidth(mode, size) + ui.dp(28.0f);
        MuiRect chip = { screen.centerX() - chipWidth / 2.0f, y, chipWidth, ui.dp(30.0f) };
        muiFillRoundRect(chip, chip.h / 2.0f, muiRgb(0x2A2218, 230));
        muiStrokeRoundRect(chip, chip.h / 2.0f, ui.dp(1.0f), theme.accent.withAlpha(160));
        muiDrawTextAligned(mode, chip, size, theme.accent, MuiAlign::Center, MuiAlign::Center);
    }
}

void muiDualBottomOverlay(MuiContext& ui, MuiScreen* top)
{
    MuiRect screen = ui.screenRect();

    // The screen came on (`muiDualPowerOn`): it switches on from now.
    if (gPowerOnPending) {
        gPowerOnPending = false;
        gPowerOnStart = ui.now;
    }

    // Another screen came up: it fades in from the backdrop.
    if (top != gLastTop) {
        gLastTop = top;
        gTopChangeTime = ui.now;
    }
    if (gTopChangeTime != 0 && ui.now - gTopChangeTime < kFadeMs) {
        float progress = static_cast<float>(ui.now - gTopChangeTime) / kFadeMs;
        muiFillRect(screen, kFadeColor.withAlpha(static_cast<Uint8>((1.0f - progress) * 255.0f)));
    }

    // Low HP: the edges pulse red.
    float fraction = muiDualHitPointsFraction();
    if (inGame() && fraction > 0.0f && fraction <= kLowHitPoints) {
        float phase = fmodf(static_cast<float>(ui.now), kPulseMs) / kPulseMs;
        float strength = 0.5f + 0.5f * sinf(phase * 6.2831853f);
        MuiColor edgeColor = kWarningRed.withAlpha(static_cast<Uint8>(40.0f + 110.0f * strength));
        MuiColor clear = kWarningRed.withAlpha(0);
        float edge = ui.dp(28.0f);
        muiFillGradient({ screen.x, screen.y, edge, screen.h }, edgeColor, clear, clear, edgeColor);
        muiFillGradient({ screen.right() - edge, screen.y, edge, screen.h }, clear, edgeColor, edgeColor, clear);
        muiFillGradient({ screen.x, screen.y, screen.w, edge }, edgeColor, edgeColor, clear, clear);
        muiFillGradient({ screen.x, screen.bottom() - edge, screen.w, edge }, clear, clear, edgeColor, edgeColor);
    }

    // Over everything: the CRT switching on.
    if (ui.now - gPowerOnStart < kPowerOnMs) {
        drawPowerOn(ui, static_cast<float>(ui.now - gPowerOnStart) / kPowerOnMs);
    }
}

void muiDualTopDim(MuiContext& ui)
{
    const MuiTheme& theme = muiTheme();
    bool holding = muiBottomHoldsGame();
    if (!holding) {
        gTopDimming = false;
        return;
    }
    if (!gTopDimming) {
        gTopDimming = true;
        gTopDimStart = ui.now;
    }

    float progress = std::min(static_cast<float>(ui.now - gTopDimStart) / kDimMs, 1.0f);
    MuiRect screen = ui.screenRect();
    muiFillRect(screen, muiRgb(0x000000, static_cast<Uint8>(progress * 170.0f)));

    // Points down to where the game goes on: a chip at the bottom edge,
    // bobbing gently.
    std::u32string label = text(kTextPaused, "Game paused: continue on the bottom screen");
    float size = ui.dp(15.0f);
    float arrow = ui.dp(10.0f);
    float chipWidth = muiTextWidth(label, size) + ui.dp(36.0f) + arrow * 2.0f;
    float bob = sinf(static_cast<float>(ui.now) / 280.0f) * ui.dp(3.0f);
    MuiRect safe = ui.safeRect();
    MuiRect chip = { screen.centerX() - chipWidth / 2.0f, safe.bottom() - ui.dp(56.0f) + bob, chipWidth, ui.dp(36.0f) };
    Uint8 alpha = static_cast<Uint8>(progress * 255.0f);
    muiFillRoundRect(chip, chip.h / 2.0f, muiRgb(0x0C0F09, static_cast<Uint8>(progress * 235.0f)));
    muiStrokeRoundRect(chip, chip.h / 2.0f, ui.dp(1.5f), theme.accent.withAlpha(alpha));

    float arrowX = chip.x + ui.dp(18.0f) + arrow;
    float arrowY = chip.centerY();
    muiFillConvex({ { arrowX - arrow, arrowY - arrow * 0.5f }, { arrowX + arrow, arrowY - arrow * 0.5f }, { arrowX, arrowY + arrow * 0.6f } }, theme.accent.withAlpha(alpha));

    MuiRect textRect = { arrowX + arrow + ui.dp(10.0f), chip.y, chip.right() - (arrowX + arrow + ui.dp(10.0f)) - ui.dp(16.0f), chip.h };
    muiDrawTextAligned(label, textRect, size, theme.text.withAlpha(alpha), MuiAlign::Start, MuiAlign::Center);
}

} // namespace fallout
