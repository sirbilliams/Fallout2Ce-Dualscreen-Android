// Off-screen preview of the dual-screen skin and detailed map (not part of
// the game): renders with SDL's software renderer to BMP files.
#include <SDL.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "automap.h"
#include "mui.h"
#include "mui_draw.h"
#include "mui_dual.h"
#include "mui_map_view.h"
#include "mui_skin.h"

using namespace fallout;

static MuiContext makeContext(int width, int height, float density)
{
    MuiContext ui;
    ui.pixelsPerDp = density;
    ui.screen = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) };
    ui.safe = ui.screen;
    ui.now = 1000;
    ui.surface = MuiSurface::Bottom;
    return ui;
}

static void save(SDL_Renderer* renderer, SDL_Surface* surface, const char* path)
{
    SDL_RenderPresent(renderer);
    SDL_SaveBMP(surface, path);
}

// Layout check without game data: every string of the engine's own texts
// (argv[6] = files/ce.dat/text) must only use characters the UI font has,
// else the screen shows '?'. Returns the number of missing characters.
static int checkGlyphs(const char* textDir)
{
    static const char* languages[] = { "english", "french", "german", "italian", "polish", "portuguese", "russian", "spanish" };
    int missing = 0;
    for (const char* language : languages) {
        std::string path = std::string(textDir) + "/" + language + "/game/ce.msg";
        std::ifstream file(path);
        std::string line;
        int lineNumber = 0;
        while (std::getline(file, line)) {
            lineNumber++;
            if (line.empty() || line[0] != '{') {
                continue;
            }
            // {id}{audio}{text}; the text is in the game charset.
            size_t third = line.find('{', line.find('}', line.find('}') + 1) + 1);
            if (third == std::string::npos) {
                continue;
            }
            std::u32string text = muiDecodeGameText(line.substr(third + 1).c_str());
            for (char32_t ch : text) {
                if (ch == U'}' || ch == U'\r') {
                    break;
                }
                if (ch >= 0x20 && !muiFontHasGlyph(ch)) {
                    printf("MISSING GLYPH U+%04X in %s:%d\n", static_cast<unsigned>(ch), path.c_str(), lineNumber);
                    missing++;
                }
            }
        }
    }
    return missing;
}

int main(int argc, char** argv)
{
    SDL_Init(0);
    const int width = 1240;
    const int height = 1080;
    const float density = 2.6f;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // 1. The bottom screen: backdrop, a framed group of buttons, a monitor
    // with a detailed map, the legend.
    {
        MuiContext ui = makeContext(width, height, density);
        muiDrawBegin(renderer);
        muiDualBackdrop(ui);

        // A row of HUD buttons, one pressed, one active.
        float button = ui.dp(52.0f);
        for (int index = 0; index < 4; index++) {
            MuiRect rect = { ui.dp(10.0f) + index * (button + ui.dp(8.0f)), ui.dp(10.0f), button, button };
            muiSkinButton(ui, rect, index == 1, false);
            muiFillCircle(rect.centerX(), rect.centerY(), ui.dp(8.0f), muiTheme().accent);
        }
        MuiRect primary = { width - ui.dp(180.0f), ui.dp(10.0f), ui.dp(170.0f), button };
        muiSkinButton(ui, primary, false, true);

        // The weapon group's plate.
        MuiRect weapon = { width - ui.dp(250.0f), height - ui.dp(160.0f), ui.dp(240.0f), ui.dp(152.0f) };
        muiSkinPlate(ui, weapon, false);
        muiSkinWell(ui, weapon.inset(ui.dp(10.0f)));

        // The live map: bezel, monitor, a made-up building.
        MuiRect mapRect = { ui.dp(10.0f), ui.dp(80.0f), width - ui.dp(20.0f), height - ui.dp(80.0f) - ui.dp(170.0f) };
        muiSkinPlate(ui, mapRect, true);
        float bezel = ui.dp(13.0f);
        muiSkinWell(ui, mapRect.inset(bezel));
        MuiRect inner = mapRect.inset(bezel + ui.dp(3.0f));

        std::vector<unsigned char> tiles(200 * 200, 0);
        auto wall = [&](int x, int y) { tiles[y * 200 + x] = 1; };
        for (int x = 90; x <= 120; x++) {
            wall(x, 90);
            wall(x, 115);
        }
        for (int y = 90; y <= 115; y++) {
            wall(90, y);
            wall(120, y);
            if (y < 100 || y > 104) {
                wall(105, y);
            }
        }
        for (int i = 0; i < 40; i++) {
            tiles[(80 + (i * 7) % 50) * 200 + 70 + (i * 13) % 70] = 2;
        }

        std::vector<unsigned int> floors(100 * 100, 0);
        for (int y = 38; y < 64; y++) {
            for (int x = 33; x < 66; x++) {
                bool inside = x >= 45 && x <= 59 && y >= 45 && y <= 57;
                floors[y * 100 + x] = inside ? ((x + y) % 2 ? 0xFF8C8A80u : 0xFF85837Au) : ((x * 7 + y * 3) % 5 ? 0xFFC8AE78u : 0xFFBFA470u);
            }
        }

        static MuiMapView view;
        view.setTiles("preview", tiles, 200, 200);
        view.setFloors(floors, 100, 100);
        view.zoomTo(4.0f);
        view.follow(105.0f, 103.0f);

        std::vector<MuiMapMarker> markers = {
            { 105.0f, 102.0f, muiRgb(0xD8B26A), 2.2f, MuiMapMarkerShape::Square },
            { 95.0f, 95.0f, muiRgb(0x8CCBFF), 2.6f, MuiMapMarkerShape::Diamond },
            { 116.0f, 112.0f, muiRgb(0xF4F0E0), 3.2f, MuiMapMarkerShape::Triangle },
            { 100.0f, 108.0f, muiRgb(0x6CFF6C), 3.2f, MuiMapMarkerShape::Ring },
            { 125.0f, 103.0f, muiRgb(0xFFB000), 2.5f, MuiMapMarkerShape::Dot },
        };
        std::vector<MuiMapLabel> labels = { { 125.0f, 103.0f, U"Klamath", muiRgb(0xFFB000) } };
        view.build(ui, "preview.map", inner, markers, &labels);
        muiAutomapLegend(ui, inner);

        muiDrawEnd();
        save(renderer, surface, argc > 1 ? argv[1] : "bottom.bmp");
    }

    // 2. The companion panel, outside a game (the title).
    {
        MuiContext ui = makeContext(width, height, density);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        muiDrawBegin(renderer);
        muiDualBackdrop(ui);
        muiDualCompanion(ui);
        muiDrawEnd();
        save(renderer, surface, argc > 2 ? argv[2] : "companion.bmp");
    }

    // 3. Widgets: buttons, toggles, a segmented control, a slider, a list.
    {
        MuiContext ui = makeContext(width, height, density);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        muiDrawBegin(renderer);
        muiSkinBackdrop(ui, ui.screenRect());
        const MuiTheme& theme = muiTheme();

        float x = ui.dp(20.0f);
        float y = ui.dp(20.0f);
        ui.button("b1", { x, y, ui.dp(180.0f), ui.dp(52.0f) }, U"DONE");
        ui.button("b2", { x + ui.dp(200.0f), y, ui.dp(180.0f), ui.dp(52.0f) }, U"CANCEL", MuiButtonStyle::Primary);
        y += ui.dp(72.0f);

        bool on = true;
        bool off = false;
        ui.toggle("t1", { x, y, ui.dp(60.0f), ui.dp(30.0f) }, &on);
        ui.toggle("t2", { x + ui.dp(80.0f), y, ui.dp(60.0f), ui.dp(30.0f) }, &off);
        y += ui.dp(50.0f);

        int selected = 1;
        ui.segmented("s1", { x, y, ui.dp(380.0f), ui.dp(40.0f) }, { U"Brief", U"Normal", U"Verbose" }, &selected);
        y += ui.dp(60.0f);

        ui.slider("sl", { x, y, ui.dp(380.0f), ui.dp(30.0f) }, 0.6f, 0.0f, 1.0f);
        y += ui.dp(50.0f);

        MuiRect list = { x, y, ui.dp(440.0f), ui.dp(150.0f) };
        ui.panel(list);
        const char32_t* rows[] = { U"Stimpak        x3", U"10mm Pistol    x1", U"Rope           x1", U"Healing Powder x5" };
        for (int index = 0; index < 4; index++) {
            MuiRect row = { list.x + ui.dp(8.0f), list.y + ui.dp(8.0f) + index * ui.dp(34.0f), list.w - ui.dp(16.0f), ui.dp(32.0f) };
            if (index == 1) {
                muiFillRect(row, theme.selected);
            }
            muiDrawTextAligned(rows[index], row.inset(ui.dp(8.0f), 0.0f), ui.dp(16.0f), index == 1 ? theme.accent : theme.text, MuiAlign::Start, MuiAlign::Center);
        }
        muiDrawEnd();
        save(renderer, surface, argc > 3 ? argv[3] : "widgets.bmp");
    }

    // 4. The new pieces: stamped plates, the day arc by day and night, the
    // Geiger gauge at several rad counts, the glass on a big monitor.
    {
        MuiContext ui = makeContext(width, height, density);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        muiDrawBegin(renderer);
        muiDualBackdrop(ui);

        float bezel = ui.dp(16.0f);
        MuiRect monitor = { ui.dp(20.0f), ui.dp(30.0f), width - ui.dp(40.0f), ui.dp(200.0f) };
        muiSkinPlate(ui, monitor, true);
        muiSkinWell(ui, monitor.inset(bezel));
        muiSkinLabel(ui, monitor.centerX(), monitor.y + bezel / 2.0f, U"STATUS");
        MuiRect inner = monitor.inset(bezel + ui.dp(12.0f));
        muiDualDayArc(ui, { inner.x, inner.y, inner.w / 2.0f - ui.dp(10.0f), ui.dp(60.0f) }, 1230);
        muiDualDayArc(ui, { inner.x + inner.w / 2.0f + ui.dp(10.0f), inner.y, inner.w / 2.0f - ui.dp(10.0f), ui.dp(60.0f) }, 2215);

        MuiRect gauges = { ui.dp(20.0f), monitor.bottom() + ui.dp(20.0f), width - ui.dp(40.0f), ui.dp(140.0f) };
        muiSkinPlate(ui, gauges, true);
        muiSkinWell(ui, gauges.inset(bezel));
        const float rads[] = { 0.0f, 150.0f, 450.0f, 900.0f };
        for (int index = 0; index < 4; index++) {
            float centerX = gauges.x + gauges.w * (index + 0.5f) / 4.0f;
            muiDualRadGauge(ui, centerX, gauges.y + ui.dp(78.0f), ui.dp(48.0f), rads[index]);
        }
        muiDrawEnd();
        save(renderer, surface, argc > 4 ? argv[4] : "pieces.bmp");
    }

    // 5. The power-on at three moments, side by side.
    {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        const float moments[] = { 0.12f, 0.45f, 0.8f };
        for (int index = 0; index < 3; index++) {
            MuiContext ui = makeContext(width / 3, height / 2, density / 2.0f);
            ui.screen.x = static_cast<float>(index * (width / 3));
            ui.safe = ui.screen;
            muiDrawBegin(renderer);
            muiPushClip(ui.screen);
            muiDualBackdrop(ui);
            muiDualPowerOnFrame(ui, moments[index]);
            muiPopClip();
            muiDrawEnd();
        }
        save(renderer, surface, argc > 5 ? argv[5] : "poweron.bmp");
    }

    if (argc > 6) {
        int missing = checkGlyphs(argv[6]);
        if (missing != 0) {
            printf("%d missing glyph(s)\n", missing);
            return 2;
        }
    }
    return 0;
}

namespace fallout {
bool gProgramIsActive = false;
}
