#include "hd/capture.h"

#include <ctype.h>
#include <stdio.h>

#include <filesystem>
#include <set>
#include <vector>

#include "art.h"
#include "color.h"
#include "debug.h"
#include "draw.h"
#include "geometry.h"
#include "hd/draw_recorder.h"
#include "hd/json_writer.h"
#include "hd/paths.h"
#include "light.h"
#include "lodepng.h"
#include "map.h"
#include "object.h"
#include "platform/git_version.h"
#include "scripts.h"
#include "svga.h"
#include "tile.h"
#include "window_manager.h"

namespace fallout::hd {

namespace {

    // Capture file format version; bump when the JSON layout changes so the
    // Python side (tools/pipeline/fo2hd/capture.py) can reject stale files.
    constexpr int kCaptureFormatVersion = 1;

    std::string defaultStem()
    {
        std::string name = gMapHeader.name;
        size_t dot = name.find('.');
        if (dot != std::string::npos) {
            name.resize(dot);
        }
        for (char& ch : name) {
            ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
        }
        if (name.empty()) {
            name = "map";
        }

        char suffix[48];
        snprintf(suffix, sizeof(suffix), "_e%d_%u", gElevation, gameTimeGetTime());
        return name + suffix;
    }

    bool writeIndexedPng(const std::filesystem::path& path, const unsigned char* pixels, int width, int height)
    {
        // The palette the screen shows: system palette through the gamma
        // table, 6 bits per channel. Scaled like CE's own screenshots (<< 2).
        const unsigned char* palette = directDrawGetPalette();

        lodepng::State state;
        state.info_raw.colortype = LCT_PALETTE;
        state.info_raw.bitdepth = 8;
        state.info_png.color.colortype = LCT_PALETTE;
        state.info_png.color.bitdepth = 8;
        state.encoder.auto_convert = 0;
        for (int index = 0; index < 256; index++) {
            unsigned char r = palette[index * 3] << 2;
            unsigned char g = palette[index * 3 + 1] << 2;
            unsigned char b = palette[index * 3 + 2] << 2;
            lodepng_palette_add(&state.info_png.color, r, g, b, 255);
            lodepng_palette_add(&state.info_raw, r, g, b, 255);
        }

        std::vector<unsigned char> png;
        unsigned error = lodepng::encode(png, pixels, width, height, state);
        if (error != 0) {
            debugPrint("HD: PNG encode failed: %s\n", lodepng_error_text(error));
            return false;
        }

        FILE* out = openFile(path.string(), "wb");
        if (out == nullptr) {
            return false;
        }
        bool ok = fwrite(png.data(), 1, png.size(), out) == png.size();
        return fclose(out) == 0 && ok;
    }

    void writeTables(JsonWriter& json)
    {
        json.key("palette").beginObject();
        json.key("system").base64(_systemCmap, 768);
        json.key("gamma").base64(_currentGammaTable, 64);
        // 6-bit values like "system"; multiply by 4 for 8-bit RGB.
        json.key("screen").base64(directDrawGetPalette(), 768);
        json.endObject();

        json.key("tables").beginObject();
        json.key("intensity_color").base64(intensityColorTable, sizeof(intensityColorTable));
        json.key("mix_add").base64(colorMixAddTable, sizeof(colorMixAddTable));
        // RGB555 -> nearest palette index; resolves CE's COLOR_* constants.
        json.key("color_15bit").base64(_colorTable, sizeof(_colorTable));

        // The five translucency tables (object.cc _obj_blend_table_init),
        // 16 rows x 256 entries, indexed [gray(src) << 8 | dest].
        struct NamedBlend {
            const char* name;
            Color color;
        };
        const NamedBlend blends[] = {
            { "wall", COLOR_PALE_BLUE },
            { "glass", COLOR_CYAN },
            { "steam", COLOR_WHITE },
            { "energy", COLOR_YELLOW_GREEN },
            { "red", COLOR_RED },
        };
        json.key("blend").beginObject();
        for (const NamedBlend& blend : blends) {
            if (_blendTable[blend.color] != nullptr) {
                json.key(blend.name).base64(_blendTable[blend.color], 4096);
            }
        }
        json.endObject();

        // object.cc keeps the glass gray table private; rebuild it with the
        // same formula (the replay test proves it matches).
        unsigned char glassGray[COLOR_COUNT];
        for (int index = 0; index < COLOR_COUNT; index++) {
            int rgb = Color2RGB(static_cast<Color>(index & COLOR_LAST));
            int r = (rgb & 0x7C00) >> 10;
            int g = (rgb & 0x3E0) >> 5;
            int b = rgb & 0x1F;
            glassGray[index] = static_cast<unsigned char>(((r + 5 * g + 4 * b) / 10) >> 2);
        }
        glassGray[0] = 0;

        json.key("gray").beginObject();
        json.key("common").base64(_commonGrayTable, COLOR_COUNT);
        json.key("glass").base64(glassGray, COLOR_COUNT);
        json.endObject();
        json.endObject();
    }

    void writeLights(JsonWriter& json)
    {
        json.key("lights").beginArray();
        for (Object* object = objectFindFirstAtElevation(gElevation); object != nullptr; object = objectFindNextAtElevation()) {
            if (object->lightDistance <= 0 || object->lightIntensity <= 0) {
                continue;
            }

            int screenX;
            int screenY;
            tileToScreenXY(object->tile, &screenX, &screenY);

            json.newline();
            json.beginObject();
            json.member("id", object->id);
            json.member("pid", object->pid);
            json.member("fid", object->fid);
            json.member("tile", object->tile);
            json.member("elevation", static_cast<int>(object->elevation));
            json.member("distance", object->lightDistance);
            json.member("intensity", object->lightIntensity);
            json.member("flags", static_cast<unsigned int>(object->flags));
            json.key("screen").beginArray().value(screenX).value(screenY).endArray();
            json.endObject();
        }
        json.newline();
        json.endArray();

        // Unclamped per-hex intensity for the current elevation; the renderer
        // uses min(value, 65536).
        std::vector<int> hexLight(HEX_GRID_SIZE);
        std::vector<int> hexScreen(HEX_GRID_SIZE * 2);
        for (int tile = 0; tile < HEX_GRID_SIZE; tile++) {
            hexLight[tile] = lightGetTrueTileIntensity(gElevation, tile);
            tileToScreenXY(tile, &(hexScreen[tile * 2]), &(hexScreen[tile * 2 + 1]));
        }

        json.key("hex_light").beginObject();
        json.member("width", HEX_GRID_WIDTH);
        json.member("height", HEX_GRID_HEIGHT);
        json.key("int32le").base64(hexLight.data(), hexLight.size() * sizeof(int));
        json.endObject();

        // Screen position (top-left of the 32x16 hex cell) of every hex.
        json.key("hex_screen").beginObject();
        json.key("int32le_xy").base64(hexScreen.data(), hexScreen.size() * sizeof(int));
        json.endObject();
    }

    void writeArtPaths(JsonWriter& json)
    {
        std::set<int> fids = drawRecorderFids();
        // The egg mask applies to roofs too, which don't name it per draw.
        fids.insert(gEgg->fid);

        json.key("art").beginObject();
        for (int fid : fids) {
            char key[16];
            snprintf(key, sizeof(key), "%d", fid);
            // filePath() may point into the FrmId, so keep it alive.
            FrmId frmId(fid);
            json.key(key).value(frmId.filePath());
        }
        json.endObject();
    }

} // namespace

bool captureMapView(const std::string& dir, const std::string& stem, std::string* jsonPathOut)
{
    if (gIsoWindow == -1) {
        debugPrint("HD: capture skipped, no map window\n");
        return false;
    }

    unsigned char* buffer = windowGetBuffer(gIsoWindow);
    int width = windowGetWidth(gIsoWindow);
    int height = windowGetHeight(gIsoWindow);
    if (buffer == nullptr || width <= 0 || height <= 0) {
        debugPrint("HD: capture skipped, map window has no buffer\n");
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(nativePath(dir), error);
    if (error) {
        debugPrint("HD: can't create capture folder %s: %s\n", dir.c_str(), error.message().c_str());
        return false;
    }

    std::string name = stem.empty() ? defaultStem() : stem;
    std::filesystem::path jsonPath = std::filesystem::path(nativePath(dir)) / (name + ".json");
    std::filesystem::path pngPath = std::filesystem::path(nativePath(dir)) / (name + ".png");

    // Full redraw from a cleared window. A normal refresh may skip the clear
    // inside the visible map area, which would keep pixels from earlier frames.
    drawRecorderBegin();
    Rect windowRect = { 0, 0, width - 1, height - 1 };
    bufferFill(buffer, width, height, width, COLOR_FIRST);
    recordFill(windowRect, COLOR_FIRST);
    tileWindowRefresh();
    drawRecorderEnd();

    int centerX;
    int centerY;
    tileToScreenXY(gCenterTile, &centerX, &centerY);

    JsonWriter json;
    json.beginObject();
    json.member("format", "fo2hd-capture");
    json.member("version", kCaptureFormatVersion);
    json.member("build", _BUILD_HASH);

    json.key("map").beginObject();
    json.member("name", gMapHeader.name);
    json.member("index", gMapHeader.index);
    json.member("elevation", static_cast<int>(gElevation));
    json.endObject();

    json.member("game_time", gameTimeGetTime());
    json.member("time_of_day", gameTimeGetHour());
    json.member("ambient", lightGetAmbientIntensity());

    json.key("window").beginObject();
    json.member("width", width);
    json.member("height", height);
    json.endObject();

    json.key("camera").beginObject();
    json.member("center_tile", gCenterTile);
    json.key("center_screen").beginArray().value(centerX).value(centerY).endArray();
    json.endObject();

    json.key("dude").beginObject();
    json.member("id", gDude->id);
    json.member("tile", gDude->tile);
    json.member("fid", gDude->fid);
    json.endObject();

    json.key("egg").beginObject();
    json.member("fid", gEgg->fid);
    json.member("tile", gEgg->tile);
    json.endObject();

    writeTables(json);
    writeLights(json);
    writeArtPaths(json);
    drawRecorderWrite(json);
    json.endObject();
    json.newline();

    FILE* stream = openFile(jsonPath.string(), "wb");
    bool written = false;
    if (stream != nullptr) {
        written = fwrite(json.str().data(), 1, json.str().size(), stream) == json.str().size();
        written = fclose(stream) == 0 && written;
    }
    if (!written) {
        debugPrint("HD: can't write %s\n", jsonPath.string().c_str());
        return false;
    }

    if (!writeIndexedPng(pngPath, buffer, width, height)) {
        debugPrint("HD: can't write %s\n", pngPath.string().c_str());
        return false;
    }

    debugPrint("HD: captured %s (%d bytes)\n", jsonPath.string().c_str(), static_cast<int>(json.str().size()));
    if (jsonPathOut != nullptr) {
        *jsonPathOut = jsonPath.string();
    }
    return true;
}

} // namespace fallout::hd
