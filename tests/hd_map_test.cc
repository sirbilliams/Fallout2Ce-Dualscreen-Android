// Lane-local recorder/art fixtures; no L1/L2 hooks or game data required.
#include "art.h"
#include "color.h"
#include "game_config.h"
#include "hd/bc7.h"
#include "hd/draw_list.h"
#include "hd/gl_private.h"
#include "hd/hd_art.h"
#include "hd/map_layer.h"
#include "hd/map_present.h"
#include "hd/map_renderer.h"
#include "map.h"
#include "object.h"
#include "tile.h"
#include <SDL.h>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>
using namespace fallout;
using namespace fallout::hd;
namespace fallout {
Config gGameConfig {};
int gElevation = 0;
int gHexGridSize = 40000;
Color intensityColorTable[256][256], colorMixAddTable[256][256], _commonGrayTable[256], *_blendTable[256], _colorTable[32768];
unsigned char _cmap[768], _systemCmap[768], _currentGammaTable[64];
static std::string root;
static int cpuFallback = 0;
int debugPrint(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    int n = vfprintf(stderr, fmt, a);
    va_end(a);
    return n;
}
bool configGetInt(Config*, const char*, const char* key, int* out, int def)
{
    *out = !strcmp(key, "enabled") ? 1 : !strcmp(key, "vram_budget_mb") ? 16
        : !strcmp(key, "bc7_cpu")                                       ? cpuFallback
                                                                        : def;
    return true;
}
bool configGetInt(Config* c, const char* s, const char* k, int* out) { return configGetInt(c, s, k, out, *out); }
bool configGetString(Config*, const char*, const char*, char** out, const char*)
{
    *out = root.data();
    return true;
}
bool configGetDouble(Config*, const char*, const char*, double*) { return false; }
static unsigned char artPixels[16] = { 1, 2, 3, 4, 5, 0, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static Art fakeArt;
Art* artLock(const FrmId&, CacheEntry** h)
{
    *h = nullptr;
    return &fakeArt;
}
int artUnlock(CacheEntry*) { return 0; }
int artGetSize(Art*, int, Rotation, int* w, int* h)
{
    *w = *h = 4;
    return 0;
}
unsigned char* artGetFrameData(Art*, int, Rotation) { return artPixels; }
int artCopyFileName(const FrmId&, char* out)
{
    strcpy(out, "fixture.frm");
    return 0;
}
char* FrmId::buildPath(int, char* path)
{
    strcpy(path, "art\\scenery\\fixture.frm");
    return path;
}
int Color2RGB(Color c) { return (c & 31) * 1057; }
Object* objectFindFirstAtLocation(int, int) { return nullptr; }
Object* objectFindNextAtLocation() { return nullptr; }
int tileToScreenXY(int, int*, int*) { return -1; }
static unsigned char world[64];
bool worldViewIsEnabled() { return true; }
int worldViewGetWidth() { return 8; }
int worldViewGetHeight() { return 8; }
unsigned char* worldViewGetBuffer() { return world; }
void tileWindowRefresh() { }

} // namespace fallout
namespace fallout::hd {
// L4 glue compiled in this fixture; the lane's real lighting/post shaders
// have separate tests. Keep original lighting and identity post here.
bool mapLightingInit() { return true; }
void mapLightingShutdown() { }
bool mapLightingActive() { return false; }
bool mapLightingDraw(int, int, int, const float*) { return false; }
GLuint mapLightingBloomSource() { return 0; }
void mapLightingTimingMark(const char*) { }
void mapLightingTimingEndFrame() { }
bool mapPostInit() { return true; }
void mapPostShutdown() { }
GLuint mapPostDraw(GLuint hd, GLuint, int, int) { return hd; }
float mapPostGrain() { return 0; }
static std::vector<DrawRecord> records;
static std::vector<unsigned char> text = { 8, 0, 9, 10 };
const std::vector<DrawRecord>& drawRecords() { return records; }
const std::vector<unsigned char>& drawTextPixels(int) { return text; }
bool drawMapDirty() { return true; }
void drawClearMapDirty() { }
bool drawEmitFullFrame() { return true; }
bool drawListComplete() { return true; }

} // namespace fallout::hd
static void require(bool ok, const char* what)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s (GL %x, SDL %s)\n", what, glGetError(), SDL_GetError());
        exit(1);
    }
}
static void put(std::vector<unsigned char>& b, int& bit, unsigned v, int count)
{
    for (int i = 0; i < count; i++, bit++)
        b[bit / 8] |= ((v >> i) & 1) << (bit % 8);
}
static std::vector<unsigned char> solidBc7()
{
    std::vector<unsigned char> b(16);
    int bit = 0;
    put(b, bit, 64, 7);
    for (int c = 0; c < 4; c++)
        for (int e = 0; e < 2; e++)
            put(b, bit, c == 0 || c == 3 ? 127 : 0, 7);
    put(b, bit, 1, 1);
    put(b, bit, 1, 1);
    return b;
}
static void u32(std::vector<unsigned char>& d, int at, unsigned v)
{
    for (int i = 0; i < 4; i++)
        d[at + i] = v >> (8 * i);
}
static void pack(unsigned hash)
{
    auto dir = std::filesystem::path(fallout::root) / "art/scenery/fixture";
    std::filesystem::create_directories(dir);
    std::ofstream json(dir / "pack.json");
    json << "{\"version\":1,\"hard_alpha\":true,\"layout\":[4,4],\"rotations\":[{\"frames\":[[0,0,4,4," << hash << "]]},{\"same_as\":0},{\"same_as\":0},{\"same_as\":0},{\"same_as\":0},{\"same_as\":0}]}";
    json.close();
    for (int tier = 2; tier <= 4; tier++) {
        int size = 4 * tier;
        std::vector<unsigned char> dds(148 + size * size);
        memcpy(dds.data(), "DDS ", 4);
        u32(dds, 4, 124);
        u32(dds, 12, size);
        u32(dds, 16, size);
        u32(dds, 76, 32);
        memcpy(dds.data() + 84, "DX10", 4);
        u32(dds, 128, 98);
        u32(dds, 132, 3);
        u32(dds, 140, 1);
        auto block = solidBc7();
        for (size_t i = 148; i < dds.size(); i += 16)
            memcpy(dds.data() + i, block.data(), 16);
        std::ofstream f(dir / ("albedo_" + std::to_string(tier) + "x.dds"), std::ios::binary);
        f.write((char*)dds.data(), dds.size());
    }
}
static unsigned hashPixels()
{
    unsigned h = 2166136261u;
    for (auto c : fallout::artPixels)
        h = (h ^ c) * 16777619u;
    return h;
}
static void waitPack()
{
    for (int i = 0; i < 200 && hdArtLoading(); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        hdArtPoll();
    }
    require(!hdArtLoading(), "loader finished");
}
static void decoderTest()
{
    require(hdGlHasBc7(), "Mesa BC7 oracle available");
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexStorage2D(GL_TEXTURE_2D, 1, 0x8e8c, 4, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GLuint output = glTexture(4, 4, GL_RGBA8, GL_UNSIGNED_BYTE), f;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    GLuint p = glCompute("#version 310 es\nlayout(local_size_x=4,local_size_y=4)in;layout(binding=0)uniform highp sampler2D src;layout(rgba8,binding=0)writeonly uniform highp image2D dst;void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);imageStore(dst,p,texelFetch(src,p,0));}");
    require(p, "oracle shader");
    glUseProgram(p);
    glBindImageTexture(0, output, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t);
    std::mt19937 rng(3127);
    for (int mode = 0; mode < 8; mode++)
        for (int i = 0; i < 256; i++) {
            unsigned char b[16], cpu[64], gpu[64];
            for (auto& x : b)
                x = rng();
            b[0] = (b[0] & ~((1 << (mode + 1)) - 1)) | (1 << mode);
            bc7Decode(b, cpu);
            glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, 0x8e8c, 16, b);
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_ALL_BARRIER_BITS);
            glReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, gpu);
            if (memcmp(cpu, gpu, 64)) {
                fprintf(stderr, "mode %d block %d\n", mode, i);
                for (int k = 0; k < 64; k++)
                    if (cpu[k] != gpu[k]) fprintf(stderr, "%d: %d != %d\n", k, cpu[k], gpu[k]);
                require(false, "BC7 CPU vs Mesa");
            }
        }
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
    glDeleteTextures(1, &t);
    glDeleteTextures(1, &output);
    glUseProgram(0);
    glDeleteProgram(p);
    glDeleteFramebuffers(1, &f);
    puts("BC7: 2048 randomized blocks, all eight modes byte-exact");
}
static void ruleTests()
{
    auto saved = records;
    DrawRecord fill = saved[0], o = saved[1];
    const ObjectBlend modes[] = { ObjectBlend::TransRed, ObjectBlend::TransWall, ObjectBlend::TransGlass, ObjectBlend::TransSteam, ObjectBlend::TransEnergy };
    const Color colors[] = { COLOR_RED, COLOR_PALE_BLUE, COLOR_CYAN, COLOR_WHITE, COLOR_YELLOW_GREEN };
    for (int mode = 0; mode < 5; mode++) {
        o.blend = modes[mode];
        o.light = 32768;
        records = { fill, o };
        require(mapRendererBuild(8, 8), "translucency build");
        require(mapRendererDraw(1), "translucency draw");
        std::vector<unsigned char> p;
        require(mapRendererReadback(1, p, nullptr, nullptr), "translucency readback");
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++) {
                int src = artPixels[y * 4 + x], gray = _commonGrayTable[src];
                if (mode == 2) {
                    int rgb = Color2RGB(static_cast<Color>(src));
                    gray = ((((rgb >> 10) & 31) + 5 * ((rgb >> 5) & 31) + 4 * (rgb & 31)) / 10) >> 2;
                }
                int want = src ? intensityColorTable[_blendTable[colors[mode]][gray * 256 + 31]][mode == 1 ? 128 : 64] : 31;
                require(p[(y + 2) * 8 + x + 2] == want, "CE translucency table/light");
            }
    }
    o = saved[1];
    o.light = 32768;
    o.egg = true;
    o.eggRect = { 3, 3, 4, 4 };
    o.eggFid = o.fid;
    records = { fill, o };
    require(mapRendererBuild(8, 8) && mapRendererDraw(1), "egg replay");
    std::vector<unsigned char> p;
    require(mapRendererReadback(1, p, nullptr, nullptr), "egg readback");
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            int src = artPixels[y * 4 + x], want = src ? intensityColorTable[src][64] : 31;
            if (src && x >= 1 && x <= 2 && y >= 1 && y <= 2) {
                int mask = artPixels[(y - 1) * 4 + x - 1];
                int under = intensityColorTable[31][128 - mask], over = intensityColorTable[want][mask];
                if (mask) want = colorMixAddTable[over][under];
            }
            require(p[(y + 2) * 8 + x + 2] == want, "CE egg mask/mix-add");
        }
    DrawRecord textOp {};
    textOp.op = DrawOp::Text;
    textOp.x = 0;
    textOp.y = 0;
    textOp.width = 2;
    textOp.height = 2;
    textOp.clip = { 0, 0, 7, 7 };
    textOp.textIndex = 0;
    records = { fill, textOp };
    require(mapRendererBuild(8, 8) && mapRendererDraw(1) && mapRendererReadback(1, p, nullptr, nullptr), "text replay");
    require(p[0] == 8 && p[1] == 31 && p[8] == 9 && p[9] == 10, "transparent text pixels");
    DrawRecord outline = saved[1];
    outline.op = DrawOp::Outline;
    outline.outline = OUTLINE_TYPE_BLOCKED;
    records = { fill, outline };
    std::vector<unsigned char> tags;
    require(mapRendererBuild(8, 8) && mapRendererDraw(1) && mapRendererReadback(1, p, nullptr, nullptr, &tags), "outline replay");
    require(p[2 * 8 + 1] == 61 && tags[2 * 8 + 1] == 1, "tagged outline");
    auto old = artPixels[0];
    artPixels[0] = 229;
    o = saved[1];
    o.fid = 0x06000000;
    o.light = 32768;
    records = { fill, o };
    require(mapRendererBuild(8, 8) && mapRendererDraw(1) && mapRendererReadback(1, p, nullptr, nullptr), "cycle replay");
    require(p[2 * 8 + 2] == 229 && mapRendererHasCycle(), "CE cycle colors bypass object light");
    artPixels[0] = old;
    records = saved;
    puts("PASS: CE five translucency tables, egg, text, outline tags, cycle light rules");
}
int main(int argc, char** argv)
{
    require(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL init");
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    auto* win = SDL_CreateWindow("HD test", 0, 0, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    require(win, "window");
    auto* r = SDL_CreateRenderer(win, -1, SDL_RENDERER_TARGETTEXTURE);
    require(r, "SDL ES renderer");
    SDL_RenderClear(r);
    SDL_RenderFlush(r);
    require(hdGlLoad(), "ES 3.1 context");
    printf("GL: %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    decoderTest();
    std::string testRoot = argc > 1 ? argv[1] : "hd-map-test-pack";
    fallout::root = testRoot + "/absent";
    for (int c = 0; c < 256; c++) {
        _commonGrayTable[c] = static_cast<Color>(c % 8);
        for (int l = 0; l < 256; l++) {
            intensityColorTable[c][l] = static_cast<Color>((c * l / 128) & 255);
            colorMixAddTable[c][l] = static_cast<Color>((c + l) & 255);
        }
    }
    for (int i = 0; i < 32768; i++)
        _colorTable[i] = static_cast<Color>(i % 256);
    static Color blend[256][16 * 256];
    for (int i = 0; i < 256; i++) {
        _blendTable[i] = blend[i];
        for (int j = 0; j < 16 * 256; j++)
            blend[i][j] = static_cast<Color>((j % 256 + (j / 256) * 3 + i) % 256);
    }
    require(mapRendererInit(), "renderer init");
    DrawRecord fill {};
    fill.op = DrawOp::Fill;
    fill.clip = { 0, 0, 7, 7 };
    fill.color = 31;
    records.push_back(fill);
    DrawRecord o {};
    o.op = DrawOp::Object;
    o.fid = 0x02000000;
    o.x = 2;
    o.y = 2;
    o.width = 4;
    o.height = 4;
    o.light = 65536;
    o.clip = { 0, 0, 7, 7 };
    records.push_back(o);
    require(hdArtInit(), "art init, absent pack");
    require(mapRendererBuild(8, 8, 2), "build missing");
    require(mapRendererDraw(1), "exact draw");
    std::vector<unsigned char> exact;
    require(mapRendererReadback(1, exact, nullptr, nullptr), "exact readback");
    std::vector<unsigned char> expected(64, 31);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            if (artPixels[y * 4 + x]) expected[(y + 2) * 8 + x + 2] = artPixels[y * 4 + x];
    require(exact == expected, "absent pack == 8-bit frame");
    for (int scale = 2; scale <= 4; scale++) {
        require(mapRendererDraw(scale), "scale draw");
        std::vector<unsigned char> p;
        require(mapRendererReadback(scale, p, nullptr, nullptr), "scale readback");
        for (int y = 0; y < 8 * scale; y++)
            for (int x = 0; x < 8 * scale; x++)
                require(p[y * 8 * scale + x] == expected[(y / scale) * 8 + x / scale], "nearest exact invariant");
    }
    float palette[1024];
    for (int i = 0; i < 256; i++) {
        palette[i * 4] = palette[i * 4 + 1] = palette[i * 4 + 2] = i / 255.f;
        palette[i * 4 + 3] = 1;
    }
    require(mapRendererDrawHd(2, palette), "HD original draw");
    std::vector<float> rgba;
    require(mapRendererHdReadback(2, rgba, nullptr, nullptr), "HD readback");
    for (size_t i = 0; i < rgba.size(); i += 4)
        require(rgba[i + 3] > .999, "coverage");
    ruleTests();
    waitPack();
    hdArtShutdown();
    fallout::root = testRoot + "/valid";
    pack(hashPixels());
    for (int cpu = 0; cpu < 2; cpu++) {
        fallout::cpuFallback = cpu;
        require(hdArtInit(), "synthetic store init");
        for (int tier = 2; tier <= 4; tier++) {
            require(mapRendererBuild(8, 8, tier), "request synthetic");
            waitPack();
            require(mapRendererBuild(8, 8, tier), "resident build");
            require(hdArtStats().buildHd == 1, "HD frame selected");
            require(mapRendererDrawHd(tier, palette), "HD synthetic draw");
            require(mapRendererHdReadback(tier, rgba, nullptr, nullptr), "synthetic readback");
            int i = ((2 * tier) * (8 * tier) + 2 * tier) * 4;
            require(rgba[i] > .99 && rgba[i + 1] < .01 && rgba[i + 2] < .01, "BC7 replaces art");
            if (tier == 2) {
                mapRendererWantShapes(true);
                require(mapRendererBuild(8, 8, tier), "shape build");
                MapGbufferDraw gb;
                gb.occluderSize[0] = gb.occluderSize[1] = 8;
                gb.occluders = glTexture(8, 8, GL_R32UI, GL_UNSIGNED_INT);
                glActiveTexture(GL_TEXTURE0);
                for (int k = 0; k < 6; k++)
                    gb.targets[k] = glTexture(16, 16, k == 0 || k == 3 ? GL_RGBA16F : GL_RGBA8, GL_UNSIGNED_BYTE);
                require(mapRendererDrawGbuffer(2, palette, gb), "G-buffer replay");
                GLuint f;
                glGenFramebuffers(1, &f);
                glBindFramebuffer(GL_FRAMEBUFFER, f);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gb.targets[0], 0);
                float c[4];
                glReadPixels(4, 4, 1, 1, GL_RGBA, GL_FLOAT, c);
                require(c[0] > .99 && c[1] < .01, "G-buffer HD albedo");
                glDeleteFramebuffers(1, &f);
                glDeleteTextures(1, &gb.targets[5]);
                gb.targets[5] = glTexture(16, 16, GL_R8, GL_UNSIGNED_BYTE);
                require(mapRendererDrawGbuffer(2, palette, gb), "R8 coverage G-buffer variant");
                glDeleteTextures(6, gb.targets);
                glDeleteTextures(1, &gb.occluders);
            }
        }
        hdArtShutdown();
    }
    fallout::root = testRoot + "/mismatch";
    pack(hashPixels() ^ 1);
    require(hdArtInit(), "mismatch store");
    require(mapRendererBuild(8, 8, 2), "mismatch request");
    waitPack();
    require(mapRendererBuild(8, 8, 2), "mismatch build");
    require(hdArtStats().buildHd == 0 && hdArtStats().mismatches > 0, "hash mismatch falls back");
    require(mapRendererDraw(1), "mismatch exact");
    require(mapRendererReadback(1, exact, nullptr, nullptr) && exact == expected, "mismatch preserves 8-bit");
    hdArtShutdown();
    mapRendererShutdown();
    // Exercise the SDL flush -> raw GLES -> SDL overlay boundary using the
    // same world-space source rect as world_view.cc.
    fallout::root = testRoot + "/absent";
    fallout::cpuFallback = 0;
    std::memcpy(fallout::world, expected.data(), 64);
    for (int i = 0; i < 64; i++)
        _currentGammaTable[i] = i;
    for (int i = 0; i < 768; i++)
        _cmap[i] = _systemCmap[i] = (i / 3) % 64;
    require(mapLayerInit(), "layer init");
    SDL_RenderSetLogicalSize(r, 8, 8);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_RenderFlush(r);
    GLint oldProgram, oldFbo, oldActive;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldFbo);
    glActiveTexture(GL_TEXTURE9);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(GL_PACK_ROW_LENGTH, 77);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 55);
    mapLayerRenderWorld(r, SDL_Rect { 0, 0, 8, 8 }, SDL_FRect { 0, 0, 8, 8 });
    GLint v;
    glGetIntegerv(GL_CURRENT_PROGRAM, &v);
    require(v == oldProgram, "program restored");
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &v);
    require(v == oldFbo, "FBO restored");
    glGetIntegerv(GL_ACTIVE_TEXTURE, &v);
    require(v == oldActive, "texture unit restored");
    glGetIntegerv(GL_PACK_ROW_LENGTH, &v);
    require(v == 77, "pack state restored");
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &v);
    require(v == 55, "unpack state restored");
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    std::vector<unsigned char> screen(64 * 64 * 4);
    require(SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, screen.data(), 64 * 4) == 0, "composited readback");
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int index = expected[(y / 8) * 8 + x / 8];
            int at = (y * 64 + x) * 4;
            require(screen[at] == _systemCmap[index * 3] * 4 && screen[at + 1] == _systemCmap[index * 3 + 1] * 4 && screen[at + 2] == _systemCmap[index * 3 + 2] * 4, "absent pack composite == CE palette frame");
        }
    SDL_SetRenderDrawColor(r, 0, 255, 0, 255);
    SDL_Rect overlay { 0, 0, 2, 2 };
    SDL_RenderFillRect(r, &overlay);
    require(SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, screen.data(), 64 * 4) == 0, "SDL overlay readback");
    require(screen[0] == 0 && screen[1] == 255 && screen[2] == 0, "SDL overlay stays on top");
    require(mapLayerSelfTest("fixture") == 0, "layer exact/scaled self-test");
    mapLayerShutdown();
    fallout::root = testRoot + "/valid";
    require(mapLayerInit(), "synthetic layer init");
    mapLayerRenderWorld(r, SDL_Rect { 0, 0, 8, 8 }, SDL_FRect { 0, 0, 8, 8 });
    waitPack();
    mapLayerRenderWorld(r, SDL_Rect { 0, 0, 8, 8 }, SDL_FRect { 0, 0, 8, 8 });
    require(SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, screen.data(), 64 * 4) == 0, "synthetic composite readback");
    int redPixel = (16 * 64 + 16) * 4;
    require(screen[redPixel] > 248 && screen[redPixel + 1] < 3 && screen[redPixel + 2] < 3, "HD art replaces composite");
    for (int i = 0; i < 768; i++)
        _systemCmap[i] = _cmap[i] / 2;
    mapLayerRenderWorld(r, SDL_Rect { 0, 0, 8, 8 }, SDL_FRect { 0, 0, 8, 8 });
    require(SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, screen.data(), 64 * 4) == 0, "fade readback");
    require(screen[redPixel] >= 120 && screen[redPixel] <= 132, "HD follows CE fade gain");
    for (int i = 0; i < 768; i++)
        _systemCmap[i] = _cmap[i];
    for (int i = 0; i < 64; i++)
        _currentGammaTable[i] = i / 2;
    mapLayerRenderWorld(r, SDL_Rect { 0, 0, 8, 8 }, SDL_FRect { 0, 0, 8, 8 });
    require(SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, screen.data(), 64 * 4) == 0, "gamma readback");
    require(screen[redPixel] >= 120 && screen[redPixel] <= 128, "HD follows CE gamma");
    std::vector<unsigned char> snap, owned;
    int snapScale = 0;
    require(mapLayerSnapshot(2, 2, 1, 1, snap, owned, &snapScale) && owned[0] && snapScale == 2 && snap[0] > 248, "HD snapshot and ownership");
    fallout::world[2 * 8 + 2] = 17;
    SDL_SetRenderDrawColor(r, 255, 0, 255, 255);
    SDL_RenderClear(r);
    mapLayerRenderWorld(r, SDL_Rect { 0, 0, 8, 8 }, SDL_FRect { 0, 0, 8, 8 });
    require(SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_RGBA32, screen.data(), 64 * 4) == 0, "ownership mismatch readback");
    require(screen[redPixel] == 255 && screen[redPixel + 1] == 0 && screen[redPixel + 2] == 255, "unrecorded CE pixel stays underneath");
    mapLayerShutdown();
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(win);
    SDL_Quit();
    puts("PASS: missing pack, 1x/2x/3x/4x exact, BC7 GPU+CPU tier replacement, hash mismatch");
    return 0;
}
