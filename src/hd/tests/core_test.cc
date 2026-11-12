// Synthetic CE state; tests exercise the real portable modules without game data.
#include "art.h"
#include "color.h"
#include "debug.h"
#include "hd/cycle_scan.h"
#include "hd/draw_list.h"
#include "hd/draw_recorder.h"
#include "hd/json_reader.h"
#include "hd/json_writer.h"
#include "hd/light_scene.h"
#include "hd/palette_fit.h"
#include "hd/sprite_shape.h"
#include "map.h"
#include "map_edge.h"
#include "object.h"
#include "proto.h"
#include "scripts.h"
#include "tile.h"
#include "worldmap.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>

#define CHECK(expr)                                         \
    do {                                                    \
        if (!(expr)) {                                      \
            std::cerr << __LINE__ << ": " << #expr << '\n'; \
            std::exit(1);                                   \
        }                                                   \
    } while (0)
namespace fallout {
unsigned char _cmap[COLOR_MAP_SIZE], _systemCmap[COLOR_MAP_SIZE];
int gElevation = 0, gCenterTile = 20100, gHexGridSize = HEX_GRID_SIZE;
Object* gEgg = nullptr;
TileData* _square[ELEVATION_COUNT];
MapHeader gMapHeader = {};
static std::vector<Object*> objects;
static size_t nextObject;
static Art art = {};
static std::vector<unsigned char> pixels(16, 238);
static bool hasArt = true, emitFloors = true, edgeEnabled = false;
static int originX = 0, originY = 0;
static int colorReads = 0;
int debugPrint(const char*, ...) { return 0; }
Object* objectFindFirstAtElevation(int)
{
    nextObject = 0;
    return objectFindNextAtElevation();
}
Object* objectFindNextAtElevation() { return nextObject < objects.size() ? objects[nextObject++] : nullptr; }
Art* artLock(const FrmId&, CacheEntry** handle)
{
    *handle = nullptr;
    colorReads++;
    return hasArt ? &art : nullptr;
}
int artUnlock(CacheEntry*) { return 0; }
int artGetWidth(Art*, int, Rotation) { return 4; }
int artGetHeight(Art*, int, Rotation) { return 4; }
int artGetFrameCount(Art*) { return 1; }
int artGetSize(Art*, int, Rotation, int* w, int* h)
{
    *w = *h = 4;
    return 0;
}
unsigned char* artGetFrameData(Art*, int, Rotation) { return pixels.data(); }
char* FrmId::buildPath(int, char* path)
{
    std::strcpy(path, "art/scenery/lamp.frm");
    return path;
}
int artCopyFileName(const FrmId&, char* out)
{
    std::strcpy(out, "lamp.frm");
    return 0;
}
int protoGetProto(const ProtoId&, Proto** out)
{
    static Proto proto = {};
    *out = &proto;
    return 0;
}
int tileToScreenXY(int tile, int* x, int* y)
{
    int a = 100 - tile % 200, b = tile / 200 - 100;
    *x = 24 * a + 16 * b + originX;
    *y = -6 * a + 12 * b + originY;
    return 0;
}
int tileFromScreenXY(int, int, bool) { return 20100; }
int squareTileToScreenXY(int, int* x, int* y, int)
{
    *x = *y = 0;
    return 0;
}
bool mapEdgeIsEnabled() { return edgeEnabled; }
int mapEdgeSelectZoneAndClamp(int tile, int) { return edgeEnabled ? -1 : tile; }
int lightGetAmbientIntensity() { return 32768; }
int lightGetTrueTileIntensity(int, int) { return 131072; }
int gameTimeGetHour() { return 2200; }
void tileWindowRefresh()
{
    if (emitFloors) hd::recordPass(hd::DrawPass::Floors);
}
int wmMapMaxCount() { return 0; }
int wmMapIdxToName(Map, char*, size_t) { return -1; }
int mapLoadByName(char*) { return -1; }
int mapSetElevation(int) { return 0; }
void compat_windows_path_to_native(char* path)
{
    for (; *path; ++path)
        if (*path == '\\') *path = '/';
}
FILE* compat_fopen(const char* path, const char* mode) { return std::fopen(path, mode); }
} // namespace fallout
using namespace fallout;
using namespace fallout::hd;
static void drawTest()
{
    Rect clip = { 1, 2, 3, 4 };
    drawClearMapDirty();
    recordFill(clip, 5);
    CHECK(drawMapDirty());
    CHECK(drawRecords().empty());
    drawRecorderBegin();
    CHECK(!drawEmitFullFrame());
    recordPass(DrawPass::Floors);
    recordFill(clip, 5);
    int vertices[10] = { 123 }, intensity[3] = { 1, 2, 3 };
    recordFloor(42, 7, 8, clip, 100, vertices, intensity, 3);
    unsigned char text[] = { 1, 2, 3, 4 };
    recordText(0, 0, 2, 2, text, clip);
    text[0] = 99;
    Object obj = {};
    obj.fid = FrmId(ItemFrameId(1)).fid();
    obj.flags = OBJECT_TRANS_GLASS;
    obj.id = 17;
    obj.sx = 20;
    recordObject(&obj, obj.fid, 4, 4, clip, 65536);
    recordObjectEgg(77, clip);
    recordOutline(&obj, obj.fid, 4, 4, clip);
    drawRecorderEnd();
    CHECK(drawListComplete());
    CHECK(drawRecords().size() == 6);
    CHECK(drawRecords()[2].vertexIntensities[0] == 123);
    CHECK(drawTextPixels(0)[0] == 1);
    CHECK(drawRecords()[4].blend == ObjectBlend::TransGlass);
    CHECK(drawRecords()[4].objectId == 17);
    CHECK(drawRecorderFids().count(77) == 1);
    CHECK(drawRecorderFids().count(42) == 1);
    JsonWriter w;
    w.beginObject();
    drawRecorderWrite(w);
    w.endObject();
    JsonValue j;
    CHECK(jsonParse(w.str().data(), w.str().size(), j));
    CHECK(j["draws"].size() == 6);
    CHECK(j["floor_intensity_map"]["count"].asInt() == 3);
    drawClearMapDirty();
    CHECK(drawEmitFullFrame());
    CHECK(!drawMapDirty());
    CHECK(!gDrawEmitOnly && !gDrawRecorderActive);
    emitFloors = false;
    CHECK(!drawEmitFullFrame());
    CHECK(!drawListComplete());
}
static void lightTest()
{
    Object lamp = {};
    lamp.tile = 20100;
    lamp.fid = FrmId(SceneryFrameId(1)).fid();
    lamp.lightDistance = 12;
    lamp.lightIntensity = 131072;
    Object critter = lamp;
    critter.fid = (OBJ_TYPE_CRITTER << 24) | 1;
    critter.id = 2;
    Object otherElevation = lamp;
    otherElevation.elevation = 1;
    objects = { &lamp, &critter, &otherElevation };
    float palette[1024] = {};
    hasArt = false;
    const auto& s = lightSceneGather(640, 480, 32, 160, palette);
    CHECK(s.ambient == .5f && s.timeOfDay == 2200 && s.elevation == 0);
    CHECK(s.lights.size() == 2);
    CHECK(!s.hexes.empty());
    CHECK(s.hexes[0].intensity == 2.f);
    CHECK(s.lights[0].kind == LightKind::Lamp);
    CHECK(s.lights[0].z == 42.f);
    CHECK(s.lights[0].radius == 8.f && s.lights[0].intensity == 1.f);
    CHECK(s.lights[1].kind == LightKind::Critter && s.lights[1].z == 30.f);
    int reads = colorReads;
    originX = 24;
    lightSceneGather(640, 480, 32, 160, palette);
    CHECK(colorReads == reads);
    CHECK(s.lights[0].x == 40.f);
    lightSceneReset();
    lightSceneGather(640, 480, 32, 160, palette);
    CHECK(colorReads > reads);
    hasArt = true;
    lightSceneReset();
    lightSceneGather(640, 480, 32, 160, palette);
    CHECK(s.lights[0].kind == LightKind::Fire);
}
static void shapeTest()
{
    const int w = 9, h = 9;
    unsigned char p[w * h], rgba[w * h * 4];
    std::memset(p, 1, sizeof(p));
    spriteShape(p, w, h, OBJ_TYPE_TILE, rgba);
    for (int i = 0; i < w * h; i++)
        CHECK(rgba[i * 4] == 128 && rgba[i * 4 + 1] == 255 && rgba[i * 4 + 2] == 0 && rgba[i * 4 + 3] == 0);
    spriteShape(p, w, h, OBJ_TYPE_CRITTER, rgba);
    CHECK(rgba[2] == 128 && rgba[(h - 1) * w * 4 + 2] == 0); // 8px * 16
    CHECK(rgba[(4 * w) * 4] < 128 && rgba[(4 * w + 8) * 4] > 128);
    p[0] = 0;
    spriteShape(p, w, h, OBJ_TYPE_WALL, rgba);
    CHECK(rgba[2] == 0 && rgba[3] == 0);
    spriteShape(p, w, h, OBJ_TYPE_SCENERY, rgba, 1.f);
    CHECK(rgba[(4 * w) * 4] < rgba[(4 * w + 8) * 4]);
}
static void cycleTest()
{
    static TileData data = {};
    _square[0] = &data;
    for (auto& fid : data.tileFid)
        fid = -1;
    Object obj = {};
    obj.fid = FrmId(SceneryFrameId(1)).fid();
    obj.tile = 20100;
    objects = { &obj };
    int n = 123;
    CHECK(cycleScanBestTile(0, "missing", &n) == -1 && n == 0);
    CHECK(cycleScanBestTile(0, "slow_fire", &n) == 20100 && n == 16);
    CHECK(cycleScanBestTile(0, "slime", &n) == -1 && n == 0);
    obj.flags = OBJECT_HIDDEN;
    CHECK(cycleScanBestTile(0, "slow_fire", &n) == -1);
    obj.flags = OBJECT_NONE;
    edgeEnabled = true;
    CHECK(cycleScanBestTile(0, "slow_fire", &n) == -1);
}
static void paletteTest()
{
    for (int i = 0; i < 768; i++)
        _cmap[i] = i % 64;
    std::memcpy(_systemCmap, _cmap, 768);
    CHECK(paletteFit().ok);
    for (int i = 0; i < 768; i++)
        _systemCmap[i] = _cmap[i] / 2;
    auto f = paletteFit();
    CHECK(f.ok && std::fabs(f.gain[0] - .5f) < .02f);
    for (int i = 0; i < 768; i++)
        _systemCmap[i] = 32 + _cmap[i] / 2;
    f = paletteFit();
    CHECK(f.ok && std::fabs(f.offset[0] - 32) < 1.f);
    _systemCmap[30] = 0;
    CHECK(!paletteFit().ok);
    std::memset(_cmap, 0, 768);
    std::memset(_systemCmap, 0, 768);
    CHECK(paletteFit().ok);
}
static void jsonTest()
{
    JsonWriter w;
    w.beginObject().member("int", -7).member("uint", 4294967295u).member("bool", true);
    const char bytes[] = "quote\"\\\n\t\r\001\200\377";
    w.member("string", bytes).member("null", static_cast<const char*>(nullptr));
    w.key("nested").beginArray().beginObject().member("x", 3).endObject().value(false).endArray();
    w.key("base64").base64("abcde", 5).endObject();
    JsonValue v;
    std::string error;
    CHECK(jsonParse(w.str().data(), w.str().size(), v, &error));
    CHECK(v["int"].asInt() == -7 && v["uint"].asUint() == 4294967295u && v["bool"].boolean);
    CHECK(v["string"].string == bytes && v["null"].type == JsonValue::Type::Null);
    CHECK(v["nested"][size_t(0)]["x"].asInt() == 3 && v["base64"].string == "YWJjZGU=");
    for (const char* bad : { "{", "[]x", "{\"x\":}", "[1,]" })
        CHECK(!jsonParse(bad, std::strlen(bad), v, &error));
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    std::string name = argv[1];
    if (name == "draw_recorder")
        drawTest();
    else if (name == "light_scene")
        lightTest();
    else if (name == "sprite_shape")
        shapeTest();
    else if (name == "cycle_scan")
        cycleTest();
    else if (name == "palette_fit")
        paletteTest();
    else if (name == "json_roundtrip")
        jsonTest();
    else
        CHECK(false);
}
