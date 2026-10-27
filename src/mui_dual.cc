#include "mui_dual.h"
#include "mui_skin.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

#include "critter.h"
#include "game.h"
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

    MuiScreen* gLastTop = nullptr;
    unsigned int gTopChangeTime = 0;

    bool gTopDimming = false;
    unsigned int gTopDimStart = 0;

    std::u32string text(int id, const char* fallback)
    {
        return muiDecodeGameText(muiText(id, fallback));
    }

    // A game is being played (not the main menu, not loading one).
    bool inGame()
    {
        if (gDude == nullptr) {
            return false;
        }
        if ((GameMode::getCurrentGameMode() & GameMode::kWorldmap) != 0) {
            return true;
        }
        return mapIsValid(mapGetCurrentMap());
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
    auto monitor = [&](float contentHeight) {
        float height = contentHeight + (bezel + padding) * 2.0f;
        MuiRect plate = { x - bezel - padding, screen.centerY() - height / 2.0f, width + (bezel + padding) * 2.0f, height };
        muiSkinPlate(ui, plate, true);
        muiSkinWell(ui, plate.inset(bezel));
        return plate.y + bezel + padding;
    };

    if (!inGame()) {
        // Outside a game: the title.
        float y = monitor(ui.dp(84.0f));
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

    float contentHeight = ui.dp(24.0f + 8.0f + 72.0f + 26.0f + 14.0f + 1.0f + 14.0f + 24.0f + 10.0f + 22.0f);
    if (!mode.empty()) {
        contentHeight += ui.dp(16.0f + 30.0f);
    }
    float y = monitor(contentHeight);

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
    y = placeRect.bottom() + ui.dp(8.0f);

    // The game's clock, large, and its date: green readouts.
    int hour = gameTimeGetHour();
    char clock[16];
    snprintf(clock, sizeof(clock), "%02d:%02d", hour / 100, hour % 100);
    MuiRect clockRect = { x, y, width, ui.dp(72.0f) };
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
    y = dateRect.bottom() + ui.dp(14.0f);

    muiFillRect({ x + width * 0.1f, y, width * 0.8f, std::max(ui.dp(1.0f), 1.0f) }, theme.textDim.withAlpha(120));
    y += ui.dp(1.0f) + ui.dp(14.0f);

    // Who: name and level, then HP.
    char level[32];
    snprintf(level, sizeof(level), muiText(kTextLevel, "Level %d"), pcGetStat(PC_STAT_LEVEL));
    std::u32string who = muiDecodeGameText(critterGetName(gDude)) + U" \u00B7 " + muiDecodeGameText(level);
    MuiRect whoRect = { x, y, width, ui.dp(24.0f) };
    muiDrawTextAligned(who, whoRect, muiFitTextSize(who, width, ui.dp(17.0f), ui.dp(12.0f)), theme.text, MuiAlign::Center, MuiAlign::Center);
    y = whoRect.bottom() + ui.dp(10.0f);

    muiDualHitPointsBar(ui, { x + width * 0.1f, y, width * 0.8f, ui.dp(22.0f) });
    y += ui.dp(22.0f) + ui.dp(16.0f);

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
