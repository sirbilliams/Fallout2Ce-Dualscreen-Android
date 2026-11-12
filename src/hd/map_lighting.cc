#include "hd/map_lighting.h"
#ifdef FALLOUT_HD
#include "config.h"
#include "game_config.h"
#include "hd/compute.h"
#include "hd/light_scene.h"
#include "hd/map_renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace fallout::hd {
namespace {
    constexpr int kGroundMargin = 64, kGroundMarginBelow = 288, kGroundAlign = 32;
    constexpr int kShadowWidth = 320, kShadowHeight = 144, kMaxLights = 64;
#include "map_lighting_settings.inc"
#include "map_lighting_shader.h"
#include "map_lighting_uniforms.inc"
    struct State {
        Settings settings;
        GLuint programs[8] = {}, hexes = 0, lights = 0;
        compute::Texture gbuffer[6], occluders, occluderMax, field, sunAo, shadows, giScene, cascades[4], fluence, bloom;
        bool bloomValid = false;
        ~State()
        {
            glDeleteBuffers(1, &hexes);
            glDeleteBuffers(1, &lights);
            for (auto p : programs)
                glDeleteProgram(p);
        }
    };
    State* gState = nullptr;
    unsigned features()
    {
        const auto& s = gState->settings;
        unsigned allowed = s.quality == 0 ? ~(kShadows | kSun | kAo | kGi) : s.quality == 1 ? ~kGi
                                                                                            : ~0u;
        if (s.effectOff >= 0) allowed &= ~kEffects[s.effectOff].bit;
        return s.quality < 0 ? 0 : s.switches & allowed;
    }
    bool ensureTargets(int width, int height, int scale)
    {
        auto& t = *gState;
        int w = width * scale, h = height * scale, gw = width + 160, gh = height + 384, fw = (gw + 1) / 2, fh = (gh + 1) / 2;
        for (int i = 0; i < 6; i++)
            if (!t.gbuffer[i].ensure(w, h, (i == 0 || i == 3) ? GL_RGBA16F : GL_RGBA8)) return false;
        int cw = (fw + 15) / 16 * 16, ch = (fh + 15) / 16 * 16;
        for (auto& c : t.cascades)
            if (!c.ensure(cw, ch)) return false;
        return t.bloom.ensure(w, h) && t.occluders.ensure(gw, gh, GL_R32UI) && t.occluderMax.ensure(fw, fh, GL_R32F) && t.field.ensure(fw, fh) && t.sunAo.ensure(fw, fh) && t.shadows.ensure(320, 144, GL_RGBA16F, 64) && t.giScene.ensure(fw, fh) && t.fluence.ensure(cw / 2, ch / 2);
    }
    // Replacing additive Gaussian splats with a gather avoids requiring floating
    // point blend support on GLES. Same finite 3-sigma footprint and weights.
    const char kFieldShader[] = R"glsl(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x=8,local_size_y=8) in;
struct Hex {vec2 pos;float intensity;float pad;};
layout(std430,binding=0) readonly buffer Hexes {Hex hexes[];};
layout(rgba16f,binding=0) writeonly uniform highp image2D outputField;
uniform ivec2 size,origin;
uniform float sigma;
uniform int count;
void csField(uvec3 id){
 if(any(greaterThanEqual(ivec2(id.xy),size)))return;
 vec2 p=(vec2(id.xy)+0.5)*2.0-vec2(origin);vec2 sum=vec2(0.0);
 for(int i=0;i<count;i++){
  vec2 d=(p-hexes[i].pos)*vec2(1.0,2.0);
  if(any(greaterThan(abs(d),vec2(3.0*sigma))))continue;
  float w=exp(-dot(d,d)/(2.0*sigma*sigma));sum+=vec2(w*hexes[i].intensity,w);
 }
 imageStore(outputField,ivec2(id.xy),vec4(sum,0.0,0.0));
})glsl";
    void pass(int index, const LightingConstants& c, int w, int h, int z = 1)
    {
        GLuint p = gState->programs[index];
        glUseProgram(p);
        uniforms(p, c);
        compute::dispatch(w, h, z);
    }
} // namespace
// namespace
bool mapLightingInit()
{
    mapLightingShutdown();
    gState = new State;
    loadSettings(gState->settings);
    const char* names[] = {
        "csOccluderMax", "csShadowMaps", "csSunAo", "csGiScene", "csGiCascade", "csGiResolve", "csLight"
    };
    for (int i = 0; i < 7; i++)
        if (!(gState->programs[i] = compute::program(kShaderSource, names[i]))) {
            mapLightingShutdown();
            return false;
        }
    if (!(gState->programs[7] = compute::program(kFieldShader, "csField"))) {
        mapLightingShutdown();
        return false;
    }
    mapRendererWantShapes(mapLightingActive());
    return true;
}
void mapLightingShutdown()
{
    delete gState;
    gState = nullptr;
}
bool mapLightingActive()
{
    return gState && gState->settings.newLight && gState->settings.quality >= 0;
}
const char* mapLightingToggle()
{
    if (!gState) return "lighting unavailable";
    gState->settings.newLight = !gState->settings.newLight;
    mapRendererWantShapes(mapLightingActive());
    return mapLightingActive() ? "new light" : "original light";
}
const char* mapLightingCycleEffect()
{
    if (!gState) return "lighting unavailable";
    auto& off = gState->settings.effectOff;
    off = off + 1 >= kEffectCount ? -1 : off + 1;
    static char name[32];
    snprintf(name, sizeof(name), off < 0 ? "all effects" : "no %s", off < 0 ? "" : kEffects[off].name);
    return name;
}
bool mapLightingDraw(int mapWidth, int mapHeight, int scale, const float* palette)
{
    if (!gState || mapWidth <= 0 || mapHeight <= 0 || scale < 1 || scale > 4) return false;
    gState->bloomValid = false;
    if (!mapLightingActive()) return mapRendererDrawHd(scale, palette);
    GLuint output = mapRendererHdUav(scale);
    if (!output || !ensureTargets(mapWidth, mapHeight, scale)) return false;
    const Settings& s = gState->settings;
    unsigned enabled = features();
    // Inputs from CE.
    const LightScene& scene = lightSceneGather(mapWidth, mapHeight, kGroundMargin, kGroundMarginBelow, palette);
    // Ground (0, 0) = grid texel origin; texel edges stay put in the world.
    int origin[2] = {
        kGroundMargin + ((-scene.worldX) % kGroundAlign + kGroundAlign) % kGroundAlign,
        kGroundMargin + ((-scene.worldY) % kGroundAlign + kGroundAlign) % kGroundAlign,
    };
    std::vector<GpuHex> hexes;
    hexes.reserve(scene.hexes.size());
    for (const SceneHex& hex : scene.hexes) {
        hexes.push_back({ { hex.x, hex.y }, hex.intensity, 0.0f });
    }
    std::vector<GpuLight> lights;
    for (const SceneLight& light : scene.lights) {
        if (static_cast<int>(lights.size()) >= kMaxLights) {
            break;
        }
        GpuLight g = {};
        g.pos[0] = light.x;
        g.pos[1] = light.y;
        g.z = light.z;
        g.radius = light.radius;
        g.intensity = light.intensity;
        g.step = (light.intensity - 655.0f / 65536.0f) / (light.radius + 1.0f);
        g.slice = static_cast<int>(lights.size());
        g.pool = light.kind == LightKind::Critter ? 0.0f : 1.0f;
        memcpy(g.color, light.color, sizeof(g.color));
        lights.push_back(g);
    }
    compute::buffer(gState->hexes, 0, hexes.empty() ? nullptr : hexes.data(), hexes.size() * sizeof(GpuHex));
    compute::buffer(gState->lights, 3, lights.empty() ? nullptr : lights.data(), lights.size() * sizeof(GpuLight));
    float night = (enabled & kNight) != 0 ? nightAmount(scene.timeOfDay, scene.ambient) : 0.0f;
    float daylight = dayAmount(scene.timeOfDay, scene.ambient);
    float day = (enabled & kSun) != 0 ? daylight : 0.0f;
    LightingConstants constants = {};
    constants.targetSize[0] = mapWidth * scale;
    constants.targetSize[1] = mapHeight * scale;
    constants.scale = scale;
    constants.lightCount = static_cast<int>(lights.size());
    constants.groundOrigin[0] = origin[0];
    constants.groundOrigin[1] = origin[1];
    constants.groundSize[0] = gState->occluders.width;
    constants.groundSize[1] = gState->occluders.height;
    constants.ambient = scene.ambient;
    for (int k = 0; k < 3; k++) {
        constants.ambientColor[k] = 1.0f + night * (s.nightAmbient * s.nightTint[k] - 1.0f);
    }
    constants.poolGain = s.poolGain;
    constants.shadowStrength = s.shadowStrength;
    constants.sunStrength = s.sunStrength * day;
    constants.aoStrength = s.aoStrength;
    float sun[3];
    sunDirection(s, scene.timeOfDay, sun);
    float sunGround = std::max(sqrtf(sun[0] * sun[0] + sun[1] * sun[1]), 1e-3f);
    constants.sun[0] = sun[0];
    constants.sun[1] = sun[1];
    constants.sun[2] = sun[2];
    constants.sunStep[0] = sun[0] / sunGround;
    constants.sunStep[1] = sun[1] / sunGround * 0.5f;
    constants.sunRise = sun[2] / sunGround * 0.8660254f;
    // Long enough for a 200 px tall blocker's whole shadow: 64 steps (192
    // ground px) at noon as in 0.9, at most 160 (480 px) when the sun is low.
    constants.sunSteps = std::min(std::max(static_cast<int>(ceilf(200.0f / (3.0f * std::max(constants.sunRise, 1e-3f)))), 64), 160);
    constants.features = enabled;
    constants.mix = s.originalMix;
    constants.fieldSigma = s.fieldSigma;
    constants.shadowRange = s.shadowRange;
    constants.fieldMax = s.fieldMax;
    constants.fieldSize[0] = gState->field.width;
    constants.fieldSize[1] = gState->field.height;
    constants.shadowSize[0] = kShadowWidth;
    constants.shadowSize[1] = kShadowHeight;
    constants.knee = s.knee;
    constants.tonemapWhite = s.tonemapWhite;
    constants.tonemapCurve = s.tonemapCurve;
    constants.agxExposure = s.agxExposure;
    constants.bloomGlow = s.bloomGlow;
    constants.bloomHot = s.bloomHot;
    constants.lightScale = enabled ? 1.0f - daylight * (1.0f - s.dayLights) : 1.0f;
    if (!enabled) constants.fieldMax = 0.0f;
    constants.sunGain = s.sunGain;
    constants.giStrength = s.giStrength;
    constants.giGround = s.giGround;
    constants.giWall = s.giWall;
    constants.cascadeSize[0] = gState->cascades[0].width;
    constants.cascadeSize[1] = gState->cascades[0].height;
    // Clear R32UI without relying on glClearTexImage (not core ES 3.1).
    auto& t = *gState;
    std::vector<unsigned> zero(size_t(t.occluders.width) * t.occluders.height, 0);
    glBindTexture(GL_TEXTURE_2D, t.occluders.id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t.occluders.width, t.occluders.height, GL_RED_INTEGER, GL_UNSIGNED_INT, zero.data());
    MapGbufferDraw draw;
    for (int i = 0; i < 6; i++)
        draw.targets[i] = t.gbuffer[i].id;
    draw.occluders = t.occluders.id;
    std::copy(origin, origin + 2, draw.occluderOrigin);
    draw.occluderSize[0] = t.occluders.width;
    draw.occluderSize[1] = t.occluders.height;
    memcpy(draw.category, s.category, sizeof(draw.category));
    draw.detailScale = scale / 4.0f;
    if (!mapRendererDrawGbuffer(scale, palette, draw)) return false;
    glMemoryBarrier(GL_ALL_BARRIER_BITS);
    GLuint p = t.programs[7];
    glUseProgram(p);
    glUniform2i(glGetUniformLocation(p, "size"), t.field.width, t.field.height);
    glUniform2i(glGetUniformLocation(p, "origin"), origin[0], origin[1]);
    glUniform1f(glGetUniformLocation(p, "sigma"), std::max(s.fieldSigma, 0.01f));
    glUniform1i(glGetUniformLocation(p, "count"), hexes.size());
    compute::image(0, t.field);
    compute::dispatch(t.field.width, t.field.height);
    compute::sample(1, t.occluders.id);
    compute::image(0, t.occluderMax);
    pass(0, constants, t.occluderMax.width, t.occluderMax.height);
    compute::sample(2, t.occluderMax.id);
    if ((enabled & kShadows) && !lights.empty()) {
        compute::image(0, t.shadows);
        pass(1, constants, 320, 144, lights.size());
    }
    compute::image(0, t.sunAo);
    pass(2, constants, t.sunAo.width, t.sunAo.height);
    for (int i = 0; i < 6; i++)
        compute::sample(4 + i, t.gbuffer[i].id);
    compute::sample(10, t.field.id);
    compute::sample(11, t.shadows.id, GL_TEXTURE_2D_ARRAY);
    compute::sample(12, t.sunAo.id);
    if (enabled & kGi) {
        compute::image(0, t.giScene);
        pass(3, constants, t.giScene.width, t.giScene.height);
        compute::sample(14, t.giScene.id);
        for (int i = 3; i >= 0; i--) {
            constants.cascade = i;
            compute::sample(13, i < 3 ? t.cascades[i + 1].id : 0);
            compute::image(0, t.cascades[i]);
            pass(4, constants, t.cascades[i].width, t.cascades[i].height);
        }
        compute::sample(13, t.cascades[0].id);
        compute::image(0, t.fluence);
        pass(5, constants, t.fluence.width, t.fluence.height);
    }
    compute::sample(15, t.fluence.id);
    glBindImageTexture(0, output, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    compute::image(1, t.bloom);
    pass(6, constants, mapWidth * scale, mapHeight * scale);
    for (int i = 0; i < 2; i++)
        glBindImageTexture(i, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
    t.bloomValid = glGetError() == GL_NO_ERROR;
    return t.bloomValid;
}
GLuint mapLightingBloomSource()
{
    return mapLightingActive() && gState->bloomValid && gState->settings.quality > 0 ? gState->bloom.id : 0;
}
void mapLightingDrawOverlay()
{
}
// No ImGui dependency in CE's mobile UI.
} // namespace fallout::hd
// namespace fallout::hd
#endif
