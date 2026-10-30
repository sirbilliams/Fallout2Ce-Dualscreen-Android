#include "mui.h"
#include "mui_icons.h"
#include "mui_map_view.h"
#include "mui_notify.h"
#include "mui_screens.h"
#include "mui_skin.h"

#include <algorithm>
#include <string>
#include <vector>

#include "animation.h"
#include "automap.h"
#include "dev_autotest.h"
#include "fps_limiter.h"
#include "game.h"
#include "game_commands.h"
#include "game_sound.h"
#include "input.h"
#include "kb.h"
#include "map.h"
#include "object.h"
#include "pipboy.h"
#include "player_commands.h"
#include "scripts.h"
#include "svga.h"

namespace fallout {

namespace {

    const MuiColor kBackground = muiRgb(0x070A06);
    const MuiColor kListBackground = muiRgb(0x0C0F09);
    const MuiColor kDude = muiRgb(0xFF5A4A);
    const MuiColor kExit = muiRgb(0xFFB000);
    const MuiColor kCritter = muiRgb(0xFF3B30);
    const MuiColor kDoor = muiRgb(0xD8B26A);
    const MuiColor kContainer = muiRgb(0x8CCBFF);
    const MuiColor kLevelChange = muiRgb(0xF4F0E0);
    const MuiColor kParty = muiRgb(0x6CFF6C);

    // Texts of the legend in `game\ce.msg`.
    constexpr int kTextLegendDoor = 912;
    constexpr int kTextLegendContainer = 913;
    constexpr int kTextLegendLevel = 914;
    constexpr int kTextLegendParty = 915;
    constexpr int kTextLegendExit = 916;

    // Texts in `game\ce.msg`.
    constexpr int kTextDetails = 212;
    constexpr int kTextMotionSensor = 213;

    // Map: the automap of the current map as the game's window shows it
    // (seen walls, scenery with details, the player, exit grids, critters
    // with the motion sensor), dragged and pinched; the game screens' tabs at
    // the edge.
    class AutomapScreen : public MuiScreen {
    public:
        // Dual-screen devices: the bottom screen, the world stays on top.
        MuiSurface surface() override { return muiGameScreenSurface(); }
        AutomapScreen()
        {
            modal = true;
        }

        bool coversScreen() override { return true; }

        bool tileCenter(int tile, float* x, float* y) const { return mapView.tileToScreen(HEX_GRID_WIDTH - 1 - tile % HEX_GRID_WIDTH, tile / HEX_GRID_WIDTH, x, y); }
        void reset();
        void build(MuiContext& ui) override;
        void back() override { finished = true; }

    private:
        MuiMapView mapView;
        bool opened = false;
        int elevation = 0;

        // What the automap shows, taken again when it changes (the game is
        // paused under the screen).
        AutomapView view;
        bool viewDirty = true;

        bool toggle(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& label, bool on, bool enabled);
    };

    AutomapScreen gAutomapScreen;

    void AutomapScreen::reset()
    {
        finished = false;
        opened = false;
        elevation = gElevation;
        viewDirty = true;
    }

    bool AutomapScreen::toggle(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::u32string& label, bool on, bool enabled)
    {
        const MuiTheme& theme = muiTheme();
        bool pressed;
        bool tapped = ui.touchable(id, rect, &pressed);
        MuiColor fill = pressed ? theme.buttonPressed : (on ? theme.buttonPrimary : theme.button);
        MuiColor text = on && !pressed ? theme.buttonPrimaryText : theme.buttonText;
        if (!enabled && !on) {
            text = text.withAlpha(90);
        }
        muiFillRoundRect(rect, ui.dp(6.0f), fill);
        muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(1.0f), on || pressed ? theme.accent : theme.buttonBorder);
        float size = muiFitTextSize(label, rect.w - ui.dp(12.0f), ui.dp(14.0f), ui.dp(10.0f));
        muiDrawTextAligned(label, rect, size, text, MuiAlign::Center, MuiAlign::Center);
        return tapped;
    }

    void AutomapScreen::build(MuiContext& ui)
    {
        const MuiTheme& theme = muiTheme();
        muiSkinBackdrop(ui, ui.screenRect());

        MuiRect content;
        MuiRect tabs;
        muiDialogLayout(ui, &content, &tabs);

        MuiGameScreenTab tab = muiGameScreenTabs(ui, tabs, "automap", MuiGameScreenTab::Map, true);
        if (tab == MuiGameScreenTab::Back) {
            back();
        } else if (tab != MuiGameScreenTab::None && muiSwitchGameScreen(ui, tab)) {
            finished = true;
        }

        // Header: where (the game's names), details and motion sensor at the
        // right.
        float headerHeight = ui.dp(32.0f);
        MuiRect header = { content.x, content.y, content.w, headerHeight };
        float size = ui.dp(14.0f);
        float padding = ui.dp(9.0f);

        float buttonWidth = ui.dp(170.0f);
        MuiRect sensor = { header.right() - buttonWidth, header.y, buttonWidth, header.h };
        MuiRect details = { sensor.x - ui.dp(6.0f) - ui.dp(120.0f), header.y, ui.dp(120.0f), header.h };

        Map map = mapGetCurrentMap();
        float x = header.x;
        MuiRect names = { header.x, header.y, std::max(details.x - header.x - ui.dp(6.0f), 0.0f), header.h };
        if (ui.surface == MuiSurface::Bottom) {
            muiPushClip(names);
        }
        if (mapIsValid(map)) {
            std::u32string city = muiDecodeGameText(mapGetCityName(map));
            std::u32string name = muiDecodeGameText(mapGetName(map, gElevation));
            for (const auto& chip : { std::make_pair(city, theme.accent), std::make_pair(name, theme.textDim) }) {
                if (chip.first.empty()) {
                    continue;
                }
                float width = muiTextWidth(chip.first, size) + padding * 2.0f;
                if (ui.surface == MuiSurface::Bottom) {
                    width = std::max(std::min(width, names.w / 2.0f - ui.dp(3.0f)), 0.0f);
                }
                MuiRect rect = { x, header.y, width, header.h };
                muiFillRoundRect(rect, ui.dp(6.0f), kListBackground);
                muiStrokeRoundRect(rect, ui.dp(6.0f), ui.dp(1.0f), theme.panelBorder);
                muiDrawTextAligned(chip.first, rect, muiFitTextSize(chip.first, std::max(width - padding * 2.0f, 0.0f), size, ui.dp(1.0f)), chip.second, MuiAlign::Center, MuiAlign::Center);
                x += width + ui.dp(6.0f);
            }
        }

        if (ui.surface == MuiSurface::Bottom) {
            muiPopClip();
        }

        // High details: scenery too (the game's switch, remembered).
        if (toggle(ui, "automap.details", details, muiDecodeGameText(muiText(kTextDetails, "Details")), automapGetHighDetails(), true)) {
            _gsound_toggle_butt_press_(-1, 0);
            automapSetHighDetails(!automapGetHighDetails());
            viewDirty = true;
        }

        // Motion sensor in the hands: critters for this look (a charge).
        bool scanner = automapIsScannerActive();
        if (toggle(ui, "automap.sensor", sensor, muiDecodeGameText(muiText(kTextMotionSensor, "Motion sensor")), scanner, !scanner) && !scanner) {
            const char* error = automapActivateScanner();
            if (error != nullptr) {
                soundPlayFile("iisxxxx1");
                muiNotify(error, MuiNoticeKind::Warning);
            } else {
                _gsound_red_butt_press(-1, 0);
                viewDirty = true;
            }
        }

        MuiRect body = { content.x, header.bottom() + ui.dp(8.0f), content.w, content.bottom() - header.bottom() - ui.dp(8.0f) };
        ui.panel(body);

        MuiRect controls = { body.x + ui.dp(6), body.y + ui.dp(5), ui.dp(100), ui.dp(32) };
        if (ui.button("automap.elevation", controls, U"LEVEL " + muiDecodeUtf8(std::to_string(elevation + 1).c_str()))) {
            // Only elevations actually saved by the engine are browsable.
            std::vector<int> available { gElevation };
            for (const auto& location : pipboyGetAutomaps())
                for (const auto& entry : location.maps)
                    if (entry.map == map && std::find(available.begin(), available.end(), entry.elevation) == available.end()) available.push_back(entry.elevation);
            std::sort(available.begin(), available.end());
            auto next = std::find(available.begin(), available.end(), elevation);
            elevation = next == available.end() || ++next == available.end() ? available.front() : *next;
            viewDirty = true;
        }
        muiDrawText(muiDecodeGameText(gameTimeGetTimeString()), controls.right() + ui.dp(10), controls.y + ui.dp(8), ui.dp(12), theme.text);
        if (viewDirty) {
            viewDirty = false;
            if (elevation == gElevation)
                automapGetView(gElevation, &view);
            else {
                view = {};
                view.dudeX = -1;
                automapGetPipboyTiles(map, elevation, view.tiles);
            }
        }
        mapView.setTiles("automap." + std::to_string(map) + "." + std::to_string(elevation), view.tiles, HEX_GRID_WIDTH, HEX_GRID_HEIGHT);
        if (!opened) {
            opened = true;
            // setTiles restores the remembered view for this map/elevation.
        }

        mapView.setFloors(view.floors, kAutomapFloorWidth, kAutomapFloorHeight);

        std::vector<MuiMapMarker> markers;
        std::vector<MuiMapLabel> labels;
        muiAutomapDetail(view, true, &markers, &labels);
        if (view.dudeX != -1) {
            markers.push_back({ static_cast<float>(view.dudeX), static_cast<float>(view.dudeY), kDude, 5.0f, MuiMapMarkerShape::Arrow });
        }

        MuiRect mapRect = body.inset(ui.dp(4.0f));
        mapRect.y += ui.dp(40);
        mapRect.h -= ui.dp(40);
        mapView.allowWalk = elevation == gElevation;
        mapView.build(ui, "automap.map", mapRect, markers, &labels);
        if (elevation == gElevation && mapView.tappedTile >= 0 && gDude != nullptr && playerCanMoveToExactTile(mapView.tappedTile)) {
            gameCommandPost(GameCommandType::Hud, 10000 + mapView.tappedTile);
            finished = true;
        }
    }

} // namespace

void muiAutomapDetail(const AutomapView& view, bool critters, std::vector<MuiMapMarker>* markers, std::vector<MuiMapLabel>* labels)
{
    auto add = [&](const std::vector<std::pair<int, int>>& places, MuiColor color, float radius, MuiMapMarkerShape shape) {
        for (const auto& place : places) {
            markers->push_back({ static_cast<float>(place.first), static_cast<float>(place.second), color, radius, shape });
        }
    };

    add(view.doors, kDoor, 2.2f, MuiMapMarkerShape::Square);
    add(view.containers, kContainer, 2.6f, MuiMapMarkerShape::Diamond);
    add(view.exits, kExit, 2.5f, MuiMapMarkerShape::Dot);
    add(view.levelChanges, kLevelChange, 3.2f, MuiMapMarkerShape::Triangle);
    add(view.party, kParty, 3.2f, MuiMapMarkerShape::Ring);
    if (critters) {
        add(view.critters, kCritter, 3.0f, MuiMapMarkerShape::Dot);
    }

    if (labels != nullptr) {
        for (const AutomapLabel& label : view.exitLabels) {
            labels->push_back({ label.x, label.y, muiDecodeGameText(label.text.c_str()), kExit });
        }
    }
}

void muiAutomapLegend(MuiContext& ui, const MuiRect& area)
{
    struct Key {
        MuiMapMarkerShape shape;
        MuiColor color;
        int text;
        const char* fallback;
    };
    const Key keys[] = {
        { MuiMapMarkerShape::Square, kDoor, kTextLegendDoor, "Door" },
        { MuiMapMarkerShape::Diamond, kContainer, kTextLegendContainer, "Container" },
        { MuiMapMarkerShape::Triangle, kLevelChange, kTextLegendLevel, "Stairs" },
        { MuiMapMarkerShape::Ring, kParty, kTextLegendParty, "Party" },
        { MuiMapMarkerShape::Dot, kExit, kTextLegendExit, "Exit" },
    };

    float size = ui.dp(11.0f);
    float height = ui.dp(22.0f);
    float gap = ui.dp(6.0f);
    float x = area.x + ui.dp(6.0f);
    float y = area.bottom() - height - ui.dp(6.0f);
    for (const Key& key : keys) {
        std::u32string name = muiDecodeGameText(muiText(key.text, key.fallback));
        float width = height + muiTextWidth(name, size) + ui.dp(8.0f);
        if (x + width > area.right() - ui.dp(6.0f)) {
            break;
        }
        MuiRect pill = { x, y, width, height };
        muiFillRoundRect(pill, height / 2.0f, muiRgb(0x000000, 170));

        // The marker as drawn on the map (a one-marker map line).
        float cx = pill.x + height / 2.0f;
        float cy = pill.centerY();
        float radius = ui.dp(3.5f);
        switch (key.shape) {
        case MuiMapMarkerShape::Square:
            muiFillRect({ cx - radius, cy - radius, radius * 2.0f, radius * 2.0f }, key.color);
            break;
        case MuiMapMarkerShape::Diamond:
            muiFillDiamond(cx, cy, radius, key.color);
            break;
        case MuiMapMarkerShape::Arrow:
        case MuiMapMarkerShape::Triangle:
            muiFillConvex({ { cx, cy - radius * 1.15f }, { cx + radius, cy + radius * 0.8f }, { cx - radius, cy + radius * 0.8f } }, key.color);
            break;
        case MuiMapMarkerShape::Ring:
            muiStrokeCircle(cx, cy, radius, ui.dp(1.6f), key.color);
            break;
        case MuiMapMarkerShape::Dot:
            muiFillCircle(cx, cy, radius, key.color);
            break;
        }

        MuiRect label = { pill.x + height, pill.y, width - height - ui.dp(4.0f), height };
        muiDrawTextAligned(name, label, size, muiTheme().text, MuiAlign::Start, MuiAlign::Center);
        x += width + gap;
    }
}

bool muiAutomapTileCenter(int tile, float* x, float* y)
{
    return gAutomapScreen.tileCenter(tile, x, y);
}

void muiAutomapScreenRun()
{
    AutomapScreen& screen = gAutomapScreen;
    screen.reset();
    muiPush(&screen);

    while (!screen.finished) {
        sharedFpsLimiter.mark();

        int keyCode = inputGetInput();
        switch (keyCode) {
        // The game's keys closing the automap.
        case KEY_ESCAPE:
        case KEY_TAB:
        case KEY_UPPERCASE_A:
        case KEY_LOWERCASE_A:
            screen.back();
            break;
        }

        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_NONE) {
            screen.finished = true;
        }

        devAutotestTick();

        renderPresent();
        sharedFpsLimiter.throttle();
    }

    muiRemove(&screen);
}

} // namespace fallout
