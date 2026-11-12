#include "hd/hd.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "color.h"
#include "config.h"
#include "critter.h"
#include "debug.h"
#include "game.h"
#include "game_config.h"
#include "game_mouse.h"
#include "hd/capture.h"
#include "hd/cycle_scan.h"
#include "hd/first_run.h"
#include "hd/map_layer.h"
#include "hd/map_lighting.h"
#include "hd/map_post.h"
#include "hd/release_info.h"
#include "input.h"
#include "kb.h"
#include "light.h"
#include "map.h"
#include "mouse.h"
#include "movie.h"
#include "object.h"
#include "palette.h"
#include "party_member.h"
#include "platform_compat.h"
#include "proto.h"
#include "scripts.h"
#include "svga.h"
#include "text_object.h"
#include "tile.h"

namespace fallout::hd {

namespace {

    constexpr char kSection[] = "hd";
    constexpr char kDebugSection[] = "hd_debug";

    // Game time runs in ticks of 1/10 s.
    constexpr unsigned int kTicksPerMinute = 600;
    constexpr unsigned int kTicksPerDay = 24 * 60 * kTicksPerMinute;

    struct ScriptedCapture {
        std::string map;
        std::string name;
        int elevation = -1;
        int tile = -1;
        int dudeTile = -1;
        int timeOfDay = -1;
        int settleMs = 2000;
        bool stress = false;
        std::string cycle; // capture_cycle: center on the reachable view with the most of this cycle range
    };

    // [hd_debug] inject_keys: key codes fed into the input queue at fixed
    // times after init, so tests can drive hotkeys without a desktop.
    struct InjectedKey {
        unsigned int atMs;
        int keyCode;
    };

    bool gInitialized = false;
    bool gEnabled = false;
    // [hd] developer=1: capture/self-test/stress hotkeys. Separate from [hd] enabled
    // (the user toggle), which Dustlight upstream also used for developer mode.
    bool gDeveloper = false;
    std::string gCaptureDir;
    ScriptedCapture gScriptedCapture;
    std::vector<InjectedKey> gInjectedKeys;
    size_t gNextInjectedKey = 0;
    unsigned int gInitTicks = 0;
    bool gKeysHeldForMovie = false;
    unsigned int gMovieStartTicks = 0;
    bool gUiWalk = false; // [hd_debug] ui_walk: Ctrl+F1 world map, Ctrl+F2 dialogue

    std::string configString(const char* section, const char* key, const char* defaultValue)
    {
        char* value = nullptr;
        configGetString(&gGameConfig, section, key, &value, defaultValue);
        return value != nullptr ? value : defaultValue;
    }

    int configInt(const char* section, const char* key, int defaultValue)
    {
        int value = defaultValue;
        configGetInt(&gGameConfig, section, key, &value, defaultValue);
        return value;
    }

    // Moves the clock forward (never back) to the next HHMM.
    void setTimeOfDay(int hhmm)
    {
        unsigned int minutes = (hhmm / 100) * 60 + hhmm % 100;
        unsigned int now = gameTimeGetTime();
        unsigned int time = (now / kTicksPerDay) * kTicksPerDay + minutes * kTicksPerMinute;
        if (time < now) {
            time += kTicksPerDay;
        }
        gameTimeSetTime(time);
    }

    // "8000:394,9000:390" -> (8000 ms, key 394), (9000 ms, key 390).
    std::vector<InjectedKey> parseInjectedKeys(const std::string& text)
    {
        std::vector<InjectedKey> keys;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t comma = text.find(',', pos);
            std::string item = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            unsigned int atMs;
            int keyCode;
            if (sscanf(item.c_str(), "%u:%d", &atMs, &keyCode) == 2) {
                keys.push_back({ atMs, keyCode });
            } else {
                debugPrint("HD: ignoring inject_keys item '%s'\n", item.c_str());
            }
            if (comma == std::string::npos) {
                break;
            }
            pos = comma + 1;
        }
        // The ticker injects in list order: a key listed after a later one
        // used to wait for it (the combat benchmark's turn keys came late).
        std::stable_sort(keys.begin(), keys.end(), [](const InjectedKey& a, const InjectedKey& b) { return a.atMs < b.atMs; });
        return keys;
    }

    void injectKeysTicker()
    {
        // A key during a movie only skips it: Arroyo village's vsuit.mve on
        // map entry swallowed the dialogue walk's Ctrl+F2. Keys wait for the
        // movie to end, and the ones still to come move later by its length.
        if (_moviePlaying()) {
            if (!gKeysHeldForMovie) {
                gKeysHeldForMovie = true;
                gMovieStartTicks = getTicks();
            }
            return;
        }
        if (gKeysHeldForMovie) {
            gKeysHeldForMovie = false;
            unsigned int held = getTicksSince(gMovieStartTicks);
            gInitTicks += held;
            debugPrint("HD: injected keys held %u ms for a movie\n", held);
        }

        unsigned int elapsed = getTicksSince(gInitTicks);
        while (gNextInjectedKey < gInjectedKeys.size() && gInjectedKeys[gNextInjectedKey].atMs <= elapsed) {
            debugPrint("HD: injecting key %d\n", gInjectedKeys[gNextInjectedKey].keyCode);
            enqueueInputEvent(gInjectedKeys[gNextInjectedKey].keyCode);
            gNextInjectedKey++;
        }
        if (gNextInjectedKey == gInjectedKeys.size()) {
            tickersRemove(injectKeysTicker);
        }
    }

    unsigned int gQuitAfterMs = 0;
    unsigned int gQuitStart = 0;

    // [hd_debug] quit_after_ms: a clean exit that long after init, so the
    // exit statistics (GPU timers, VRAM, HD text counts) reach the log;
    // tests used to kill the game instead. Modal screens (inventory,
    // dialogue) see the request when they return to the main loop. Kept
    // asserted: leaving a loaded game resets it, and the main menu only
    // exits the game on it.
    void quitTicker()
    {
        if (getTicksSince(gQuitStart) < gQuitAfterMs) {
            return;
        }
        if (_game_user_wants_to_quit != GAME_QUIT_REQUEST_EXIT) {
            debugPrint("HD: quit_after_ms reached, quitting\n");
            _game_user_wants_to_quit = GAME_QUIT_REQUEST_EXIT;
        }
    }

    // [hd_debug] ui_walk=1, Ctrl+F2: asks for a dialogue (the way a script
    // does) with the nearest living critter on this elevation, outside the
    // party, whose script has a talk procedure. For the UI walk (P7b), which
    // only ever loads a save.
    void requestNearestDialogue()
    {
        Object* best = nullptr;
        int bestDistance = 0;
        for (Object* object = objectFindFirstAtElevation(static_cast<MapElevation>(gElevation)); object != nullptr; object = objectFindNextAtElevation()) {
            if (object == gDude || ProtoId(object).objectType() != OBJ_TYPE_CRITTER || object->sid == -1 || object->tile == -1
                || critterIsDead(object) || objectIsPartyMember(object) || !scriptHasProc(object->sid, SCRIPT_PROC_TALK)) {
                continue;
            }
            int distance = tileDistanceBetween(gDude->tile, object->tile);
            if (best == nullptr || distance < bestDistance) {
                best = object;
                bestDistance = distance;
            }
        }
        if (best == nullptr) {
            debugPrint("HD: ui walk: no critter to talk to on this elevation\n");
            return;
        }
        debugPrint("HD: ui walk: dialogue with pid %d at tile %d (%d hexes away)\n", best->pid, best->tile, bestDistance);
        scriptsRequestDialog(best);
    }

    // [hd_debug] map_stress=1 (scripted capture mode, for the GPU map test):
    // puts every translucency mode and outline type on objects in view and
    // floats a text over the player, then keeps scrolling the map and moving
    // critters while the capture settles, so the self-tests see all draw
    // paths and CE's incremental redraws.
    struct Stress {
        std::vector<Object*> movers;
        unsigned int nextMs = 0;
        int step = 0;
    };

    Stress gStress;

    void stressSetUp()
    {
        const ObjectFlags translucency[] = { OBJECT_TRANS_GLASS, OBJECT_TRANS_STEAM, OBJECT_TRANS_ENERGY, OBJECT_TRANS_RED, OBJECT_TRANS_WALL };
        const int outlines[] = {
            OUTLINE_TYPE_HOSTILE,
            OUTLINE_TYPE_SAME_TEAM | OUTLINE_PALETTED,
            OUTLINE_TYPE_SAME_TEAM,
            OUTLINE_TYPE_BODY | OUTLINE_PALETTED,
            OUTLINE_TYPE_FRIENDLY,
            OUTLINE_TYPE_ITEM | OUTLINE_PALETTED,
            OUTLINE_TYPE_ITEM,
            OUTLINE_TYPE_BLOCKED,
        };
        Rect window = { 0, 0, screenGetWidth() - 1, screenGetVisibleHeight() - 1 };
        int transCount = 0;
        int outlineCount = 0;
        gStress = Stress();
        int seen = 0;
        int inView = 0;
        for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
            seen++;
            if (object == gDude || object->tile == -1 || (object->flags & OBJECT_HIDDEN) != OBJECT_NONE) {
                continue;
            }
            Rect rect;
            objectGetRect(object, &rect);
            if (rect.right < window.left || rect.left > window.right || rect.bottom < window.top || rect.top > window.bottom) {
                continue;
            }
            inView++;
            ObjectType type = ProtoId(object).objectType();
            if ((type == OBJ_TYPE_CRITTER || type == OBJ_TYPE_ITEM) && outlineCount < 8) {
                if (objectSetOutline(object, static_cast<OutlineType>(outlines[outlineCount]), nullptr) == 0) {
                    outlineCount++;
                }
                if (type == OBJ_TYPE_CRITTER && gStress.movers.size() < 3) {
                    gStress.movers.push_back(object);
                }
            } else if ((object->flags & OBJECT_FLAT) == OBJECT_NONE && transCount < 15) {
                // Replaces any OBJECT_TRANS_NONE (walls often carry it).
                object->flags = (object->flags & ~OBJECT_FLAG_0xFC000) | translucency[transCount % 5];
                transCount++;
            }
        }

        char text[] = "HD map test: floating text";
        Rect rect;
        textObjectAdd(gDude, text, 101, COLOR_LIGHT_YELLOW, COLOR_BLACK, &rect);
        gameMouseObjectsShow();
        tileWindowRefresh();
        debugPrint("HD: map stress: %d translucent objects, %d outlines, %zu moving critters, 1 floating text (%d objects on the elevation, %d in view)\n", transCount, outlineCount, gStress.movers.size(), seen, inView);
    }

    // One change every 200 ms: scroll a step (around a square), then move a
    // critter or the player one hex, alternating.
    void stressStep(unsigned int elapsedMs)
    {
        if (elapsedMs < gStress.nextMs) {
            return;
        }
        gStress.nextMs = elapsedMs + 200;
        int step = gStress.step++;
        if (step % 10 == 9) {
            // Floating text lasts a few seconds; keep one up.
            char text[32];
            snprintf(text, sizeof(text), "HD map test %d", step / 10);
            Rect rect;
            if (textObjectAdd(gDude, text, 101, COLOR_LIGHT_YELLOW, COLOR_BLACK, &rect) == 0) {
                tileWindowRefreshRect(&rect, gElevation);
            }
        }
        if (step % 2 == 0) {
            const int dx[8] = { 1, 1, 0, 0, -1, -1, 0, 0 };
            const int dy[8] = { 0, 0, 1, 1, 0, 0, -1, -1 };
            mapScroll(dx[(step / 2) % 8], dy[(step / 2) % 8]);
            return;
        }
        if (step % 4 == 1) {
            // game_mouse.cc rewrites the hex cursor's art in place (action
            // points, the "no path" X); do the same so the test sees whether
            // the GPU atlas notices. A box that changes size and color.
            CacheEntry* handle;
            Art* art = artLock(FrmId(gGameMouseHexCursor), &handle);
            if (art != nullptr) {
                int width = artGetWidth(art);
                int height = artGetHeight(art);
                unsigned char* pixels = artGetFrameData(art);
                int box = 2 + (step / 4) % 5;
                memset(pixels, 0, static_cast<size_t>(width) * height);
                for (int y = height / 2 - box; y < height / 2 + box; y++) {
                    for (int x = width / 2 - box; x < width / 2 + box; x++) {
                        if (x >= 0 && y >= 0 && x < width && y < height) {
                            pixels[y * width + x] = (step / 4) % 2 == 0 ? COLOR_LIGHT_YELLOW : COLOR_RED;
                        }
                    }
                }
                artUnlock(handle);
            }
            int tile = tileGetTileInDirection(gDude->tile, ROTATION_SE, 2 + (step / 4) % 2);
            Rect rect;
            if (tile != -1 && objectSetLocation(gGameMouseHexCursor, tile, gElevation, &rect) == 0) {
                tileWindowRefreshRect(&rect, gElevation);
            }
            objectGetRect(gGameMouseHexCursor, &rect);
            tileWindowRefreshRect(&rect, gElevation);
            return;
        }
        Object* object = gDude;
        if (!gStress.movers.empty() && (step / 2) % 3 != 0) {
            object = gStress.movers[(step / 2) % gStress.movers.size()];
        }
        // Back and forth between two neighboring hexes.
        int tile = tileGetTileInDirection(object->tile, static_cast<Rotation>((step / 2) % 2 == 0 ? ROTATION_E : ROTATION_W), 1);
        Rect rect;
        if (tile != -1 && objectSetLocation(object, tile, gElevation, &rect) == 0) {
            tileWindowRefreshRect(&rect, gElevation);
        }
    }

    void captureFromHotkey()
    {
        std::string path;
        if (captureMapView(gCaptureDir, std::string(), &path)) {
            debugPrint("HD: Ctrl+F12 capture written to %s\n", path.c_str());
        } else {
            debugPrint("HD: Ctrl+F12 capture failed\n");
        }
    }

} // namespace

void init(bool isMapper)
{
    if (gInitialized) {
        exit();
    }
    gEnabled = !isMapper && configInt(kSection, "enabled", 0) != 0;
    gDeveloper = gEnabled && configInt(kSection, "developer", 0) != 0;
    gCaptureDir = configString(kSection, "capture_dir", "hd_captures");

    if (!isMapper) {
        gScriptedCapture.map = configString(kDebugSection, "capture_map", "");
        gScriptedCapture.name = configString(kDebugSection, "capture_name", "");
        gScriptedCapture.elevation = configInt(kDebugSection, "capture_elevation", -1);
        gScriptedCapture.tile = configInt(kDebugSection, "capture_tile", -1);
        gScriptedCapture.dudeTile = configInt(kDebugSection, "capture_dude_tile", -1);
        gScriptedCapture.timeOfDay = configInt(kDebugSection, "capture_time", -1);
        gScriptedCapture.settleMs = configInt(kDebugSection, "capture_settle_ms", 2000);
        gScriptedCapture.stress = configInt(kDebugSection, "map_stress", 0) != 0;
        gScriptedCapture.cycle = configString(kDebugSection, "capture_cycle", "");

        gUiWalk = configInt(kDebugSection, "ui_walk", 0) != 0;
        gInjectedKeys = parseInjectedKeys(configString(kDebugSection, "inject_keys", ""));
        gNextInjectedKey = 0;
        if (!gInjectedKeys.empty()) {
            gInitTicks = getTicks();
            tickersAdd(injectKeysTicker);
        }
        gQuitAfterMs = static_cast<unsigned int>(std::max(configInt(kDebugSection, "quit_after_ms", 0), 0));
        if (gQuitAfterMs > 0) {
            gQuitStart = getTicks();
            tickersAdd(quitTicker);
        }
    }

#ifdef HD_HAVE_MAP_LAYER
    if (!isMapper) {
        mapLayerStartSelfTests();
    }
#endif
    // HD UI recording/skin is deliberately excluded from this port.

    gInitialized = true;
    debugPrint("HD: %s %s\n", HD_RELEASE_NAME, HD_RELEASE_VERSION);
    debugPrint("HD: module %s, captures go to %s\n", gEnabled ? "enabled" : "disabled", gCaptureDir.c_str());
    if (firstRunReport()[0] != '\0') {
        debugPrint("%s\n", firstRunReport());
    }
}

void exit()
{
    tickersRemove(injectKeysTicker);
    tickersRemove(quitTicker);
    // D3D swap-chain destruction is dropped; the GL lanes own GPU teardown.
    gScriptedCapture = ScriptedCapture();
    gInjectedKeys.clear();
    gUiWalk = false;
    gInitialized = false;
    gEnabled = false;
    gDeveloper = false;
}

bool enabled()
{
    return gEnabled;
}

bool uiWalk()
{
    return gUiWalk;
}

bool handleKey(int keyCode)
{
    // Every player gets the comparison keys; the rest are developer keys.
#ifdef HD_HAVE_MAP_LAYER
    if (keyCode == KEY_CTRL_F8) {
        debugPrint("HD: Ctrl+F8 pressed, %s\n", mapLayerToggleHd());
        tileWindowRefresh();
        return true;
    }
#endif
#ifdef HD_HAVE_MAP_LIGHTING
    if (keyCode == KEY_CTRL_F7) {
        debugPrint("HD: Ctrl+F7 pressed, %s\n", mapLightingToggle());
        tileWindowRefresh();
        return true;
    }
#endif
    if (!gEnabled || !gDeveloper) {
        return false;
    }

    switch (keyCode) {
    case KEY_CTRL_F12:
        debugPrint("HD: Ctrl+F12 pressed\n");
        captureFromHotkey();
        return true;
    case KEY_CTRL_F1:
        if (!gUiWalk) {
            return false;
        }
        debugPrint("HD: ui walk: world map\n");
        scriptsRequestWorldMap();
        return true;
    case KEY_CTRL_F2:
        if (!gUiWalk) {
            return false;
        }
        requestNearestDialogue();
        return true;
        // Ctrl+F11's D3D/ImGui overlay is dropped (no HD UI skin).
#ifdef HD_HAVE_MAP_LAYER
    case KEY_CTRL_F10:
        debugPrint("HD: Ctrl+F10 pressed, map self-test\n");
        mapLayerSelfTest("hotkey");
        return true;
    case KEY_CTRL_F9:
        debugPrint("HD: Ctrl+F9 pressed, GPU map %s\n", mapLayerCycleMode());
        tileWindowRefresh();
        return true;
#endif
#ifdef HD_HAVE_MAP_LIGHTING
    case KEY_CTRL_F6:
        debugPrint("HD: Ctrl+F6 pressed, light: %s\n", mapLightingCycleEffect());
        tileWindowRefresh();
        return true;
#endif
#ifdef HD_HAVE_MAP_POST
    case KEY_CTRL_F5:
        debugPrint("HD: Ctrl+F5 pressed, %s\n", mapPostCycle());
        tileWindowRefresh();
        return true;
#endif
    case KEY_CTRL_F3:
        // [hd_debug] fade_test=1: the game's own fades over the map, to
        // white and back, to black and back (P6.4).
        if (configInt(kDebugSection, "fade_test", 0) != 0) {
            // D3D frame-sequence readback is dropped; retain CE fade testing.
            debugPrint("HD: Ctrl+F3 pressed, fade test\n");
            paletteFadeTo(gPaletteWhite);
            paletteFadeTo(_cmap);
            paletteFadeTo(gPaletteBlack);
            paletteFadeTo(_cmap);
            return true;
        }
        return false;
        // Ctrl+F4 D3D frame-sequence capture has no GL contract and is dropped.
    }

    return false;
}

bool scriptedCaptureRequested()
{
    return gInitialized && !gScriptedCapture.map.empty();
}

bool scriptedPlayRequested()
{
    return scriptedCaptureRequested() && configInt(kDebugSection, "capture_play", 0) != 0;
}

namespace {

    // The capture's map, elevation, dude tile, time of day and view.
    // Returns 0, or 1 if the map can't be loaded.
    int setUpCaptureMap(int (*loadNewMap)(char* mapFileName))
    {
        const ScriptedCapture& capture = gScriptedCapture;
        // Set the clock before loading, so map_enter_p_proc lights the map for
        // the requested time, and again after (loading may advance it).
        if (capture.timeOfDay >= 0) {
            setTimeOfDay(capture.timeOfDay);
        }

        char mapName[COMPAT_MAX_PATH];
        snprintf(mapName, sizeof(mapName), "%s", capture.map.c_str());
        if (loadNewMap(mapName) != 0) {
            debugPrint("HD: can't load %s\n", mapName);
            return 1;
        }
        gGameLoaded = true;

        if (capture.elevation >= 0 && capture.elevation != gElevation) {
            mapSetElevation(static_cast<MapElevation>(capture.elevation));
        }

        if (capture.dudeTile >= 0) {
            objectSetLocation(gDude, capture.dudeTile, gElevation, nullptr);
        }

        scriptsEnable();
        if (capture.timeOfDay >= 0) {
            setTimeOfDay(capture.timeOfDay);
            // Day/night lighting is set by map scripts in map_update_p_proc.
            scriptsExecMapUpdateProc();
        }

        int centerTile = capture.tile >= 0 ? capture.tile : gDude->tile;
        if (!capture.cycle.empty()) {
            int pixels = 0;
            int tile = cycleScanBestTile(gElevation, capture.cycle.c_str(), &pixels);
            debugPrint("HD: capture_cycle %s: tile %d (%d px of it in view)%s\n", capture.cycle.c_str(), tile, pixels, tile < 0 ? ", none on this elevation" : "");
            if (tile >= 0) {
                centerTile = tile;
            }
        }
        tileSetCenter(centerTile, TILE_SET_CENTER_REFRESH_WINDOW | TILE_SET_CENTER_FLAG_IGNORE_SCROLL_RESTRICTIONS);
        return 0;
    }

} // namespace

int prepareScriptedPlay(int (*loadNewMap)(char* mapFileName))
{
    debugPrint("HD: scripted play on %s\n", gScriptedCapture.map.c_str());
    if (setUpCaptureMap(loadNewMap) != 0) {
        return 1;
    }
    // mainLoop enables the scripts again.
    scriptsDisable();
    // gameHandleKey drops every key while the cursor scrolls the map (it
    // sits on a screen edge, as it did on the locked desktop): the injected
    // keys of the dialogue walk and the combat benchmark went nowhere.
    _mouse_set_position(screenGetWidth() / 2, screenGetHeight() / 2);
    return 0;
}

int runScriptedCapture(int (*loadNewMap)(char* mapFileName))
{
    const ScriptedCapture& capture = gScriptedCapture;
    if (configInt(kDebugSection, "cycle_scan", 0) != 0) {
        return runCycleScan(loadNewMap, capture.map, gCaptureDir);
    }
    debugPrint("HD: scripted capture of %s\n", capture.map.c_str());
    if (setUpCaptureMap(loadNewMap) != 0) {
        return 1;
    }
    // Store page stills and clips: no hex cursor in the picture (release_clip.ps1,
    // media_views.json). gameMouseRefresh, a ticker since the map opened, would
    // show it again on the next tick.
    if (configInt(kDebugSection, "capture_no_cursor", 0) != 0) {
        tickersRemove(gameMouseRefresh);
        gameMouseObjectsHide();
    }
    // Let tickers run (animations, palette cycling, fades) like the main
    // loop does, without dispatching input or script requests.
    if (capture.stress) {
        stressSetUp();
    }

    unsigned int start = getTicks();
    while (getTicksSince(start) < static_cast<unsigned int>(std::max(capture.settleMs, 0))) {
        sharedFpsLimiter.mark();
        // Only the HD hotkeys (injected by tests) are acted on.
        int keyCode = inputGetInput();
        if (keyCode != -1) {
            handleKey(keyCode);
        }
        if (capture.stress) {
            stressStep(getTicksSince(start));
        }
        renderPresent();
        sharedFpsLimiter.throttle();
    }

    // The hex cursor follows the mouse; keep it out of regression captures.
    gameMouseObjectsHide();

    std::string path;
    bool ok = captureMapView(gCaptureDir, capture.name, &path);
    scriptsDisable();

    if (ok) {
        debugPrint("HD: scripted capture written to %s\n", path.c_str());
    }
    return ok ? 0 : 1;
}

} // namespace fallout::hd
