#include "hd/map_layer.h"
#ifdef FALLOUT_HD
#include "color.h"
#include "debug.h"
#include "game_config.h"
#include "hd/art_config.h"
#include "hd/draw_list.h"
#include "hd/draw_recorder.h"
#include "hd/gl_private.h"
#include "hd/hd_art.h"
#include "hd/map_present.h"
#include "hd/map_renderer.h"
#include "hd/palette_fit.h"
#ifdef FALLOUT_HD_LIGHTING
#include "hd/map_lighting.h"
#include "hd/map_post.h"
#endif
#include "map.h"
#include "svga.h"
#include "tile.h"
#include "world_view.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace fallout::hd {
namespace {
    struct State {
        bool enabled = true, tint = false, valid = false;
        int width = 0, height = 0, tier = 0, scale = 0;
        GLuint mask = 0, cpu = 0, program = 0, vao = 0;
        int selfTestInterval = 0;
        Uint32 lastSelfTest = 0;
        GLuint presented = 0;
        float palette[1024] = {};
        unsigned char cycles[78] = {};
    }* g = nullptr;
    const char* vertex = R"glsl(#version 310 es
precision highp float;
uniform vec4 destination;uniform vec2 outputSize;
out vec2 uv;
void main(){vec2 q=vec2((gl_VertexID==1||gl_VertexID==4||gl_VertexID==5)?1.:0.,gl_VertexID>=2&&gl_VertexID!=4?1.:0.);uv=q;vec2 p=destination.xy+q*destination.zw;gl_Position=vec4(p.x/outputSize.x*2.-1.,1.-p.y/outputSize.y*2.,0.,1.);}
)glsl";
    const char* fragment = R"glsl(#version 310 es
precision highp float;precision highp int;precision highp usampler2D;
in vec2 uv;out vec4 color;
layout(binding=0)uniform usampler2D exactMap;
layout(binding=1)uniform usampler2D cpuMap;
layout(binding=2)uniform sampler2D hdMap;
uniform vec4 source;uniform int tier,hasHd,tint;
uniform vec3 gain,offset;uniform float grain,phase;uniform float gammaTable[64];uniform vec4 livePalette[256];
float gammaValue(float v){float x=clamp(v*63.75,0.,63.);int i=int(x);return mix(gammaTable[i],gammaTable[min(i+1,63)],fract(x))*4./255.;}
void main(){vec2 p=source.xy+uv*source.zw;ivec2 q=ivec2(p);uint index=texelFetch(exactMap,q,0).r;
if((index&255u)!=texelFetch(cpuMap,q,0).r)discard;
if(hasHd==0||(index&256u)!=0u){color=livePalette[index&255u];}
else{vec4 c=texelFetch(hdMap,ivec2(p*float(tier)),0);if(c.a<0.999)discard;vec3 v=c.rgb*gain+offset/63.75;color=vec4(gammaValue(v.r),gammaValue(v.g),gammaValue(v.b),1.);}
if(hasHd!=0&&(index&256u)==0u&&grain>0.)color.rgb+=grain*(fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233))+phase)*43758.5453)-.5);
if(tint!=0)color.rgb=mix(color.rgb,vec3(0.,1.,1.),.25);
}
)glsl";
    bool gFitOk = true; // last palette fit, for the art-state log line
    bool gDrawOk = true; // last HD draw
    void palette(MapComposite& c)
    {
        PaletteFit fit = paletteFit();
        for (int k = 0; k < 3; k++) {
            c.gain[k] = fit.gain[k];
            c.offset[k] = fit.offset[k];
        }
        for (int i = 0; i < 64; i++)
            c.gamma[i] = _currentGammaTable[i];
        for (int i = 0; i < 256; i++)
            for (int k = 0; k < 4; k++) {
                float v = k == 3 ? 1.f : _cmap[i * 3 + k] * 4 / 255.f;
                if (k < 3 && i >= 229 && i <= 254) {
                    v = _systemCmap[i * 3 + k] * 4 / 255.f;
                    if (fit.gain[k] > .05f) v = std::clamp((v - fit.offset[k] / 63.75f) / fit.gain[k], 0.f, 1.f);
                }
                g->palette[i * 4 + k] = v;
            }
        gFitOk = fit.ok;
        if (!fit.ok) c.hd = 0; // unrelated palettes: the exact CE colors are authoritative
    }
} // namespace
bool mapLayerRequested()
{
    int enabled = 0;
    configGetInt(&gGameConfig, "hd", "enabled", &enabled, 0);
    int map = 1;
    configGetInt(&gGameConfig, "hd", "map_gpu", &map, 1);
    return enabled && map;
}
bool mapLayerInit()
{
    mapLayerShutdown();
    if (!mapLayerRequested() || !hdGlLoad()) return false;
    GlState saved;
    if (!mapRendererInit()) {
        mapRendererShutdown();
        return false;
    }
    g = new State;
    g->program = glProgram(vertex, fragment);
    if (!g->program) {
        mapLayerShutdown();
        return false;
    }
    glGenVertexArrays(1, &g->vao);
    hdArtInit();
#ifdef FALLOUT_HD_LIGHTING
    mapLightingInit();
    mapPostInit();
#endif
    return g->program != 0;
}
void mapLayerShutdown()
{
    if (!g) return;
    glDeleteTextures(1, &g->mask);
    glDeleteTextures(1, &g->cpu);
    glDeleteProgram(g->program);
    glDeleteVertexArrays(1, &g->vao);
#ifdef FALLOUT_HD_LIGHTING
    mapPostShutdown();
    mapLightingShutdown();
#endif
    hdArtShutdown();
    mapRendererShutdown();
    delete g;
    g = nullptr;
}
void mapLayerStartSelfTests()
{
    if (g) {
        configGetInt(&gGameConfig, "hd_debug", "map_selftest_ms", &g->selfTestInterval, 0);
        g->lastSelfTest = SDL_GetTicks();
    }
}
MapComposite mapLayerPrepare(int scale, bool selfTest)
{
    MapComposite c;
    if (!g || !g->enabled || !worldViewIsEnabled()) return c;
    GlState saved;
    int w = worldViewGetWidth(), h = worldViewGetHeight();
    if (w <= 0 || h <= 0) return c;
    scale = std::clamp(scale, 1, 4);
    int tier = hdArtConfiguredTier();
    bool arrived = hdArtPoll();
    bool rebuild = !g->valid || drawMapDirty() || arrived || g->tier != tier || g->width != w || g->height != h;
    if (rebuild) {
        g->valid = false;
        if (!drawEmitFullFrame() || !drawListComplete()) return c;
        if (!mapRendererBuild(w, h, tier) || !mapRendererDraw(1)) return c;
        drawClearMapDirty();
        g->valid = true;
        g->tier = tier;
        g->scale = 0;
    }
    if (g->width != w || g->height != h) {
        glDeleteTextures(1, &g->cpu);
        glDeleteTextures(1, &g->mask);
        g->cpu = glTexture(w, h, GL_R8UI, GL_UNSIGNED_BYTE);
        std::vector<unsigned char> ones(size_t(w) * h, 1);
        g->mask = glTexture(w, h, GL_R8UI, GL_UNSIGNED_BYTE, ones.data());
        g->width = w;
        g->height = h;
    }
    glUpload(g->cpu, 0, 0, w, h, GL_RED_INTEGER, GL_UNSIGNED_BYTE, worldViewGetBuffer());
    c.active = true;
    c.isoWidth = w;
    c.isoHeight = h;
    c.scale = scale;
    c.exact = mapRendererView(1);
    c.mask = g->mask;
    c.tint = g->tint;
    if (scale == 1)
        c.scaled = c.exact;
    else {
        if (!mapRendererDraw(scale)) return {};
        c.scaled = mapRendererView(scale);
    }
    c.hd = g->presented;
    palette(c);
    bool cycle = std::memcmp(g->cycles, &_systemCmap[229 * 3], 78) != 0;
    if (hdArtActive() && (rebuild || g->scale != tier || cycle)) {
        bool drawn = false;
#ifdef FALLOUT_HD_LIGHTING
        if (mapLightingActive())
            drawn = mapLightingDraw(w, h, tier, g->palette);
        else
#endif
            drawn = mapRendererDrawHd(tier, g->palette);
        gDrawOk = drawn;
        if (drawn) {
            g->scale = tier;
            std::memcpy(g->cycles, &_systemCmap[229 * 3], 78);
            g->presented = mapRendererHdView(tier);
#ifdef FALLOUT_HD_LIGHTING
            g->presented = mapPostDraw(g->presented, mapLightingActive() ? mapLightingBloomSource() : 0, w * tier, h * tier);
            c.grain = mapPostGrain();
#endif
            c.hd = g->presented;
            palette(c);
        } else
            c.hd = 0;
    }
    if (!hdArtActive() || hdArtStats().buildHd == 0) c.hd = 0;
    {
        // One line whenever the map switches between HD and original art.
        static int logged = -1;
        int state = c.hd ? 0 : !hdArtActive() ? 1
            : hdArtStats().buildHd == 0       ? 2
            : !gDrawOk                        ? 3
            : !gFitOk                         ? 4
                                              : 5;
        if (state != logged) {
            logged = state;
            static const char* const kWhy[] = { "HD art", "original art (store off)", "original art (no HD frames)", "original art (HD draw failed)", "original art (palette fit failed)", "original art (no HD image yet)" };
            debugPrint("HD: map shows %s, tier %d, view %dx%d\n", kWhy[state], tier, w, h);
        }
    }
    if (c.hd) {
        c.scale = tier;
        c.scaled = mapRendererView(1);
    }
#ifdef FALLOUT_HD_LIGHTING
    if (c.hd) c.grain = mapPostGrain();
#endif
    (void)selfTest;
    return c;
}
const char* mapLayerToggleHd()
{
    if (g) g->valid = false;
    return hdArtToggle() ? "HD art" : "original art";
}
const char* mapLayerCycleMode()
{
    if (!g) return "off";
    if (!g->enabled) {
        g->enabled = true;
        g->tint = false;
    } else if (!g->tint)
        g->tint = true;
    else
        g->enabled = false;
    return !g->enabled ? "off" : g->tint ? "tinted"
                                         : "on";
}
int mapLayerSelfTest(const char* label)
{
    if (!g) return -1;
    tileWindowRefresh();
    auto c = mapLayerPrepare(1, true);
    if (!c.active) return -1;
    std::vector<unsigned char> pixels;
    int w, h;
    if (!mapRendererReadback(1, pixels, &w, &h)) return 1;
    int failures = std::memcmp(pixels.data(), worldViewGetBuffer(), size_t(w) * h) != 0;
    for (int tier = 2; tier <= 4; tier++) {
        std::vector<unsigned char> scaled;
        if (!mapRendererDraw(tier) || !mapRendererReadback(tier, scaled, nullptr, nullptr)) {
            failures++;
            continue;
        }
        for (int yy = 0; yy < h * tier; yy++)
            for (int xx = 0; xx < w * tier; xx++)
                if (scaled[size_t(yy) * w * tier + xx] != pixels[size_t(yy / tier) * w + xx / tier]) failures++;
    }
    debugPrint("HD: %s exact/scaled self-test: %d failures\n", label ? label : "map", failures);
    return failures;
}
bool mapLayerSnapshot(int x, int y, int w, int h, std::vector<unsigned char>& rgba, std::vector<unsigned char>& owned, int* scale)
{
    auto c = mapLayerPrepare(1);
    if (!c.active || !c.hd || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > c.isoWidth || y + h > c.isoHeight) return false;
    std::vector<float> hd;
    std::vector<unsigned char> exact, tags;
    int tw, th;
    if (!mapRendererHdReadback(c.scale, hd, &tw, &th) || !mapRendererReadback(1, exact, nullptr, nullptr, &tags)) return false;
    rgba.resize(size_t(w) * h * c.scale * c.scale * 4);
    owned.resize(size_t(w) * h);
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            size_t i = size_t(y + yy) * c.isoWidth + x + xx;
            owned[size_t(yy) * w + xx] = !tags[i] && exact[i] == worldViewGetBuffer()[i];
        }
    for (int yy = 0; yy < h * c.scale; yy++)
        for (int xx = 0; xx < w * c.scale; xx++)
            for (int k = 0; k < 4; k++)
                rgba[(size_t(yy) * w * c.scale + xx) * 4 + k] = std::clamp(int(hd[(size_t(y * c.scale + yy) * tw + x * c.scale + xx) * 4 + k] * 255.f + .5f), 0, 255);
    if (scale) *scale = c.scale;
    return true;
}
void mapLayerDrawOverlay() { }
void mapLayerRenderWorld(SDL_Renderer* renderer, const SDL_Rect& src, const SDL_FRect& dst)
{
    if (!g || !g->enabled) return;
    if (g->selfTestInterval > 0 && SDL_GetTicks() - g->lastSelfTest >= static_cast<Uint32>(g->selfTestInterval)) {
        // Tests refresh CPU pixels; flush queued SDL work before traversal.
        SDL_RenderFlush(renderer);
        g->lastSelfTest = SDL_GetTicks();
        mapLayerSelfTest("scheduled");
    }
    // Flush before taking the state snapshot: SDL's queued copy must finish
    // below this layer. Our FBOs and state never escape this scope.
    if (SDL_RenderFlush(renderer) != 0) return;
    GlState saved;
    MapComposite c = mapLayerPrepare(1);
    if (!c.active) return;
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    float sx, sy;
    SDL_RenderGetScale(renderer, &sx, &sy);
    glUseProgram(g->program);
    glBindVertexArray(g->vao);
    GLuint textures[] = { c.exact, g->cpu, c.hd };
    for (int i = 0; i < 3; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glBindSampler(i, 0);
    }
    glUniform4f(glGetUniformLocation(g->program, "source"), src.x, src.y, src.w, src.h);
    glUniform4f(glGetUniformLocation(g->program, "destination"), dst.x * sx, dst.y * sy, dst.w * sx, dst.h * sy);
    glUniform2f(glGetUniformLocation(g->program, "outputSize"), viewport[2], viewport[3]);
    glUniform1i(glGetUniformLocation(g->program, "tier"), c.scale);
    glUniform1i(glGetUniformLocation(g->program, "hasHd"), c.hd != 0);
    glUniform1i(glGetUniformLocation(g->program, "tint"), c.tint);
    glUniform1f(glGetUniformLocation(g->program, "grain"), c.grain);
    glUniform1f(glGetUniformLocation(g->program, "phase"), SDL_GetTicks() * 0.01f);
    glUniform3fv(glGetUniformLocation(g->program, "gain"), 1, c.gain);
    glUniform3fv(glGetUniformLocation(g->program, "offset"), 1, c.offset);
    glUniform1fv(glGetUniformLocation(g->program, "gammaTable"), 64, c.gamma);
    float live[1024];
    for (int i = 0; i < 256; i++) {
        for (int k = 0; k < 3; k++)
            live[i * 4 + k] = _currentGammaTable[std::min<int>(_systemCmap[i * 3 + k], 63)] * 4 / 255.f;
        live[i * 4 + 3] = 1;
    }
    glUniform4fv(glGetUniformLocation(g->program, "livePalette"), 256, live);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
} // namespace fallout::hd
#endif
