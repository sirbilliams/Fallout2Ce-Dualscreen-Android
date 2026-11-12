#version 310 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp usampler2D;
precision highp sampler2DArray;
precision highp sampler3D;
layout(local_size_x=8, local_size_y=8) in;
float saturate(float x) { return clamp(x,0.0,1.0); }
vec2 saturate(vec2 x) { return clamp(x,0.0,1.0); }
vec3 saturate(vec3 x) { return clamp(x,0.0,1.0); }

#define F_NORMALS 1
#define F_COLORS 2
#define F_SHADOWS 4
#define F_POOL 8
#define F_TONEMAP 16
#define F_SUN 32
#define F_AO 64
#define F_GI 256

uniform ivec2 gTargetSize;
uniform int gScale;
uniform int gLightCount;
uniform ivec2 gGroundOrigin;
uniform ivec2 gGroundSize;
uniform float gAmbient;
uniform vec3 gAmbientColor;
uniform float gPoolGain;
uniform float gShadowStrength;
uniform float gSunStrength;
uniform float gAoStrength;
uniform vec4 gSun;
uniform vec2 gSunStep;
uniform float gSunRise;
uniform int gFeatures;
uniform float gMix;
uniform float gFieldSigma;
uniform float gShadowRange;
uniform float gFieldMax;
uniform ivec2 gFieldSize;
uniform ivec2 gShadowSize;
uniform float gKnee;
uniform float gLightScale;
uniform float gSunGain;
uniform float gTonemapWhite;
uniform float gGiStrength;
uniform float gGiGround;
uniform float gGiWall;
uniform int gCascade;
uniform ivec2 gCascadeSize;
uniform int gTonemapCurve;
uniform float gAgxExposure;
uniform float gBloomGlow;
uniform float gBloomHot;
uniform int gSunSteps;
uniform float gPad4;

struct Hex {
    vec2 pos;
    float intensity;
    float pad;
};

struct Light {
    vec2 pos;   // 1x px, center of the light's hex
    float z;      // mount height, screen px
    float radius;
    float intensity;
    float step;   // CE's falloff per hex
    int slice;    // shadow map, -1 = none
    float pool;   // 1 = its share gets the pool gain (lamps, fires; not critters)
    vec3 color;
    float pad2;
};

layout(std430, binding=0) readonly buffer BgHexes { Hex gHexes[]; };
layout(binding=1) uniform usampler2D gOccluders;
layout(binding=2) uniform sampler2D gOccluderMax;
layout(std430, binding=3) readonly buffer BgLights { Light gLights[]; };
layout(binding=4) uniform sampler2D gAlbedo;
layout(binding=5) uniform sampler2D gBaseNormal;
layout(binding=6) uniform sampler2D gDetailNormal;
layout(binding=7) uniform sampler2D gSurface;
layout(binding=8) uniform sampler2D gMaterial;
layout(binding=9) uniform sampler2D gCoverage; // coverage, glow, cycle-color bloom weight (both * coverage)
layout(binding=10) uniform sampler2D gField;
layout(binding=11) uniform sampler2DArray gShadows;
layout(binding=12) uniform sampler2D gSunAo;
layout(binding=13) uniform sampler2D gCascadeUpper;
layout(binding=14) uniform sampler2D gGiScene;
layout(binding=15) uniform sampler2D gFluence;


layout(r32f, binding=0) writeonly uniform highp image2D gOccluderMaxOut;
layout(rgba16f, binding=0) writeonly uniform highp image2DArray gShadowsOut;
layout(rgba16f, binding=0) writeonly uniform highp image2D gSunAoOut;
layout(rgba16f, binding=0) writeonly uniform highp image2D gOut;
layout(rgba16f, binding=0) writeonly uniform highp image2D gGiOut;
layout(rgba16f, binding=1) writeonly uniform highp image2D gBloomOut; // bloom source (P6.3), coverage-weighted

const float kCos30 = 0.8660254;

// --- CE's per-hex light, splatted onto a half-resolution ground grid ------
// Gaussian weights in ground units (v = 2 * screen y), so the field is
// round on the ground; the light pass reads sum(w * I) / sum(w).

// --- Occluders: the 1x heightfield max-pooled to half resolution and
// dilated a little, so marches with ~3 texel steps can't miss a 1 px wall.

void csOccluderMax(uvec3 id)
{
    if (any(greaterThanEqual(id.xy, uvec2(gFieldSize)))) {
        return;
    }
    ivec2 base = ivec2(id.xy) * 2;
    float h = 0.0;
    for (int y = -2; y <= 3; y++) {
        for (int x = -2; x <= 3; x++) {
            ivec2 q = base + ivec2(x, y);
            if (all(greaterThanEqual(q, ivec2(0))) && all(lessThan(q, gGroundSize))) {
                h = max(h, uintBitsToFloat(texelFetch(gOccluders, (ivec3(q, 0)).xy, 0).r));
            }
        }
    }
    imageStore(gOccluderMaxOut, ivec2(id.xy), vec4(h));
}

float occluderAt(vec2 screen)
{
    ivec2 q = ivec2(floor((screen + vec2(gGroundOrigin)) / 2.0));
    return all(greaterThanEqual(q, ivec2(0))) && all(lessThan(q, gFieldSize)) ? texelFetch(gOccluderMax, (ivec3(q, 0)).xy, 0).r : 0.0;
}

// --- Shadows from lights: per light, a map of the lowest height a point
// needs to see the light, on a half-resolution grid around the light.
// Marches at most gShadowRange toward the light and stops short of it
// (whatever holds the light doesn't block it), like the prototype: longer
// blocking is already in CE's light field.

void csShadowMaps(uvec3 id)
{
    if (any(greaterThanEqual(id.xy, uvec2(gShadowSize)))) {
        return;
    }
    Light light = gLights[id.z];
    vec2 p = light.pos + (vec2(id.xy) + 0.5 - vec2(gShadowSize) * 0.5) * 2.0;
    vec2 delta = light.pos - p;
    float dist = length(vec2(delta.x, 2.0 * delta.y));
    float visible = -8.0;
    if (dist > 1.0) {
        float tMax = min(min(gShadowRange / dist, 1.0), max(1.0 - 24.0 / dist, 0.0));
        for (int k = 1; k <= 32; k++) {
            float t = tMax * float(k) / 32.0;
            if (dist * t < 4.0) {
                continue; // the point's own object
            }
            float blocker = occluderAt(p + delta * t);
            // Blocked below this height: blocker > h + (z - h) t + 2.
            visible = max(visible, (blocker - light.z * t - 2.0) / (1.0 - t));
        }
    }
    imageStore(gShadowsOut, ivec3(uvec3(id.xy, id.z)), vec4(min(visible, 4000.0)));
}

// --- Sun shadow and ambient occlusion on the half-resolution ground grid:
// r = the lowest height that sees the sun, g = how much sky the ground
// sees (horizon angles of the heightfield in 8 directions).

void csSunAo(uvec3 id)
{
    if (any(greaterThanEqual(id.xy, uvec2(gFieldSize)))) {
        return;
    }
    vec2 p = (vec2(id.xy) + 0.5) * 2.0 - vec2(gGroundOrigin);
    float sun = -8.0;
    if ((gFeatures & F_SUN) != 0) {
        // Steps of 3 ground px (the grid is 2 px): coarser steps print the
        // occluder's footprint once per step, as stairs along the edges.
        // 64 at noon (192 ground px); a low sun casts longer shadows.
        for (int k = 1; k <= gSunSteps; k++) {
            float s = 3.0 * float(k); // ground px
            float blocker = occluderAt(p + gSunStep * s);
            sun = max(sun, blocker - s * gSunRise - 2.0);
        }
    }
    float ao = 1.0;
    if ((gFeatures & F_AO) != 0) {
        const float kSteps[4] = float[4]( 3.0, 8.0, 16.0, 30.0 );
        float occlusion = 0.0;
        for (int d = 0; d < 8; d++) {
            float angle = float(d) * 0.7853982;
            vec2 dir = vec2(cos(angle), sin(angle) * 0.5); // ground -> screen
            float horizon = 0.0;
            for (int k = 0; k < 4; k++) {
                float blocker = occluderAt(p + dir * kSteps[k]) / kCos30;
                horizon = max(horizon, atan(blocker, kSteps[k]));
            }
            occlusion += horizon / 1.5707963;
        }
        ao = 1.0 - occlusion / 8.0;
    }
    imageStore(gSunAoOut, ivec2(id.xy), vec4(min(sun, 4000.0), ao, 0.0, 0.0));
}

// --- Light at a point (light pass and bounce light) -------------------------

// How much of a point at `height` sees past the blockers of a "lowest
// visible height" map: four bilinear taps half a texel apart, for soft edges.
float softVisible(sampler2D map, vec2 uv, vec2 texel, float height)
{
    float v = 0.0;
    for (int k = 0; k < 4; k++) {
        vec2 o = (vec2(k & 1, k >> 1) - 0.5) * texel;
        v += 1.0 - smoothstep(-2.0, 4.0, textureLod(map, uv + o, float(0)).r - height);
    }
    return v * 0.25;
}

float softVisibleSlice(vec3 uvw, vec2 texel, float height)
{
    float v = 0.0;
    for (int k = 0; k < 4; k++) {
        vec2 o = (vec2(k & 1, k >> 1) - 0.5) * texel;
        v += 1.0 - smoothstep(-2.0, 4.0, textureLod(gShadows, vec3(uvw.xy + o, uvw.z), float(0)).r - height);
    }
    return v * 0.25;
}

vec3 decodeNormal(vec4 v)
{
    return normalize(v.rgb / max(v.a, 1e-4) * 2.0 - 1.0);
}

float shoulder(float x)
{
    return x <= gKnee ? x : gKnee + (1.0 - gKnee) * (1.0 - exp(-(x - gKnee) / (1.0 - gKnee)));
}

// AgX (Troy Sobotka), with Benjamin Wrensch's polynomial fit of the default
// contrast curve. Our colors are display-referred palette values, so they
// are linearized (2.2) first; the curve's output is display-referred again.
// The matrices are the GLSL reference's (column-major), hence mul(v, M).
vec3 agxContrast(vec3 x)
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

vec3 agx(vec3 c)
{
    const mat3 inset = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 outset = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;
    vec3 v = inset * pow(max(c, vec3(0.0)), vec3(2.2)) * gAgxExposure;
    v = (clamp(log2(max(v, 1e-10)), minEv, maxEv) - minEv) / (maxEv - minEv);
    return saturate(outset * agxContrast(v));
}

// CE's light, continuous (psHd): darker below 128, lighter above.
vec3 litOriginal(vec3 c, float level)
{
    return level <= 128.0 ? c * (level / 128.0) : c + (1.0 - c) * ((level - 128.0) / 128.0);
}

// light.cc's falloff: linear per hex out to the light's radius.
float falloff(Light light, vec2 ground)
{
    float k = length((ground - light.pos) / vec2(32.0, 13.856));
    return max(light.intensity - max(k - 0.5, 0.0) * light.step, 0.0);
}

// How a surface turns toward a light (fo2hd.compose): half-Lambert of the
// shape normal, plus relief from the detail normal that averages out to 1.
float shapeTerm(vec3 n, vec3 nRef, vec3 toLight, float reliefWeight, float shapeWeight)
{
    float wrap = saturate(dot(n, toLight) * 0.5 + 0.5);
    float wrapRef = saturate(dot(nRef, toLight) * 0.5 + 0.5);
    float relief = clamp((wrap + 0.15) / (wrapRef + 0.15), 0.6, 1.5);
    return (1.0 + reliefWeight * (relief - 1.0)) * (1.0 + shapeWeight * (2.0 * wrapRef - 1.0));
}

// CE's light above ambient at a ground point (uv on the half-resolution
// grid). May exceed 1 near lights (HDR). In daylight CE shows no light pools
// (its light is clamped at full), so most of it fades out by day.
float aboveAt(vec2 uv)
{
    vec2 field = textureLod(gField, uv, float(0)).rg;
    float total = max(field.g > 1e-3 ? field.r / field.g : 0.0, gAmbient);
    // CE clamps each hex at full light; the sum is kept for HDR pools, but
    // where many lights stack (Klamath's toxic caves) it rolls off toward
    // gFieldMax instead of growing without bound.
    if (gFieldMax > 1.0 && total > 1.0) {
        total = 1.0 + (gFieldMax - 1.0) * (1.0 - exp(-(total - 1.0) / (gFieldMax - 1.0)));
    } else if (gFieldMax > 0.0) {
        total = min(total, gFieldMax); // 1 or less: a hard limit (was: none at all)
    }
    return (total - gAmbient) * gLightScale;
}

// The HDR pool boost at a ground point (uv as for aboveAt): strong light
// that stands out from its surroundings (the mean on a ring kPoolRing ground
// px away) is boosted, as under a street lamp at night; even light isn't (a
// cave lit by dozens of lights everywhere stayed 2x the original's
// brightness when every lit point counted as a pool, P6).
const float kPoolRing = 192.0; // ground px, 6 hexes

float poolBoost(vec2 uv, float above)
{
    if ((gFeatures & F_POOL) == 0 || above <= 1e-3) {
        return 1.0;
    }
    float wide = 0.0;
    for (int k = 0; k < 8; k++) {
        float angle = float(k) * 0.78539816;
        wide += aboveAt(uv + vec2(cos(angle), 0.5 * sin(angle)) * kPoolRing / 2.0 / vec2(gFieldSize));
    }
    float pool = saturate(2.0 * (above - wide / 8.0) / above);
    return 1.0 + gPoolGain * above * pool;
}

// The light above ambient, `above`, shared among the lights that reach the
// ground point by CE's falloff, each share shaped by the normal, shadowed
// and colored by its light. Lamp and fire shares get the pool boost
// (poolBoost); a critter's own light doesn't, or the player glows.
// Shadows are looked up at `front`: the ground point moved off the surface
// along its normal (frontOf), so a wall face doesn't shadow itself.
vec3 lampLight(vec2 ground, vec2 front, float height, float above, float boost, vec3 n, vec3 nRef, float reliefWeight, float shapeWeight)
{
    vec3 sum = vec3(0.0);
    float weights = 0.0;
    for (int i = 0; i < gLightCount; i++) {
        Light source = gLights[i];
        float w = falloff(source, ground);
        if (w <= 0.0) {
            continue;
        }
        vec3 toLight = normalize(vec3(source.pos.x - ground.x, 2.0 * (source.pos.y - ground.y), (source.z - height) / kCos30));
        float shape = shapeTerm(n, nRef, toLight, reliefWeight, shapeWeight);
        float shadow = 1.0;
        if ((gFeatures & F_SHADOWS) != 0 && source.slice >= 0) {
            vec2 suv = ((front - source.pos) / 2.0 + vec2(gShadowSize) * 0.5) / vec2(gShadowSize);
            if (all(greaterThanEqual(suv, vec2(0.0))) && all(lessThanEqual(suv, vec2(1.0)))) {
                float visible = softVisibleSlice(vec3(suv, source.slice), 1.0 / vec2(gShadowSize), height);
                shadow = 1.0 - gShadowStrength * (1.0 - visible);
            }
        }
        vec3 color = (gFeatures & F_COLORS) != 0 ? source.color : vec3(1.0, 1.0, 1.0);
        sum += w * mix(1.0, boost, source.pool) * shape * shadow * color;
        weights += w;
    }
    return above * (weights > 0.0 ? sum / weights : vec3(boost, boost, boost));
}

// --- Bounce light (P5.7): 2D radiance cascades on the half-resolution
// ground grid. Only the light above ambient bounces: the original art
// already has its ambient bounce painted in, and by day the lamps fade, so
// day scenes keep their brightness. Emitters are ground and walls lit by
// the lamps (their albedo times lampLight); walls (occluders taller than
// kGiWallHeight) also block. Rays run in ground units (v = 2 * screen y),
// so the light spreads round on the ground.
//
// Cascade i: probes every 2^(i+1) grid texels, 4^(i+1) directions stored as
// a 2^(i+1) square block per probe (so every cascade is the same size),
// rays over [r0 (4^i - 1) / 3, r0 (4^(i+1) - 1) / 3] ground px. Built from
// the top down; each ray that gets through takes the four upper directions
// it covers, interpolated between the four nearest upper probes.

const int kCascades = 4;
const float kGiRay0 = 4.0;        // cascade 0 ray length, ground px
const float kGiStep = 2.0;        // march step, ground px
const float kGiWallHeight = 16.0; // occluders at least this tall block
const vec3 kGiDefaultAlbedo = vec3(0.3, 0.27, 0.24); // off screen

void csGiScene(uvec3 id)
{
    if (any(greaterThanEqual(id.xy, uvec2(gFieldSize)))) {
        return;
    }
    vec2 ground = (vec2(id.xy) + 0.5) * 2.0 - vec2(gGroundOrigin);
    vec2 uv = (vec2(id.xy) + 0.5) / vec2(gFieldSize);
    float wall = texelFetch(gOccluderMax, (ivec3(id.xy, 0)).xy, 0).r;
    bool opaque = wall >= kGiWallHeight;
    float above = aboveAt(uv);
    vec3 emission = vec3(0.0);
    if (above > 1e-3) {
        // Walls: lit at half their height (at most 24 px up).
        float height = opaque ? min(wall, 48.0) * 0.5 : 0.0;
        vec3 up = vec3(0.0, 0.0, 1.0);
        vec3 light = lampLight(ground, ground, height, above, poolBoost(uv, above), up, up, 0.0, 0.0);
        vec2 screen = vec2(ground.x, ground.y - height) * float(gScale);
        vec3 albedo = kGiDefaultAlbedo;
        if (all(greaterThanEqual(screen, vec2(0.0))) && all(lessThan(screen, vec2(gTargetSize)))) {
            vec4 a = textureLod(gAlbedo, screen / vec2(gTargetSize), float(0));
            if (a.a > 0.5) {
                albedo = a.rgb / a.a;
            }
        }
        emission = albedo * light * (opaque ? gGiWall : gGiGround / 64.0);
    }
    imageStore(gGiOut, ivec2(id.xy), vec4(vec4(emission, opaque ? 1.0 : 0.0)));
}

void csGiCascade(uvec3 id)
{
    if (any(greaterThanEqual(id.xy, uvec2(gCascadeSize)))) {
        return;
    }
    int side = 2 << gCascade; // block side = probe spacing in grid texels
    ivec2 probe = ivec2(id.xy) / side;
    ivec2 sub = ivec2(id.xy) - probe * side;
    int dir = sub.y * side + sub.x;
    float angle = (float(dir) + 0.5) * 6.2831853 / float(side * side);
    vec2 d = vec2(cos(angle), sin(angle));
    vec2 perGround = vec2(d.x * 0.5, d.y * 0.25); // grid texels per ground px
    vec2 center = (vec2(probe) + 0.5) * float(side);
    float start = kGiRay0 * float((1 << (2 * gCascade)) - 1) / 3.0;
    float end = kGiRay0 * float((1 << (2 * gCascade + 2)) - 1) / 3.0;
    // A probe inside a wall (walls are a few texels thick) ignores that
    // wall: its own ground point is the wall's base.
    ivec2 home = ivec2(floor(center));
    bool inWall = all(lessThan(home, gFieldSize)) && texelFetch(gGiScene, (ivec3(home, 0)).xy, 0).a > 0.5;

    vec3 radiance = vec3(0.0);
    float transmit = 1.0;
    for (float r = start + kGiStep * 0.5; r < end; r += kGiStep) {
        ivec2 q = ivec2(floor(center + perGround * r));
        if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, gFieldSize))) {
            break;
        }
        vec4 s = texelFetch(gGiScene, (ivec3(q, 0)).xy, 0);
        if (s.a > 0.5) {
            if (inWall && r < 16.0) {
                continue;
            }
            radiance += s.rgb;
            transmit = 0.0;
            break;
        }
        inWall = false;
        radiance += s.rgb * kGiStep;
    }

    if (transmit > 0.0 && gCascade < kCascades - 1) {
        int upperSide = side * 2;
        ivec2 upperCount = gCascadeSize / upperSide;
        vec2 f = center / float(upperSide) - 0.5;
        ivec2 q0 = ivec2(floor(f));
        vec2 t = f - vec2(q0);
        vec3 upper = vec3(0.0);
        for (int k = 0; k < 4; k++) {
            ivec2 qp = clamp(q0 + ivec2(k & 1, k >> 1), ivec2(0, 0), upperCount - 1);
            float w = ((k & 1) != 0 ? t.x : 1.0 - t.x) * ((k >> 1) != 0 ? t.y : 1.0 - t.y);
            vec3 acc = vec3(0.0);
            for (int j = 0; j < 4; j++) {
                int e = dir * 4 + j;
                acc += texelFetch(gCascadeUpper, (ivec3(qp * upperSide + ivec2(e % upperSide, e / upperSide), 0)).xy, 0).rgb;
            }
            upper += w * acc * 0.25;
        }
        radiance += transmit * upper;
    }
    imageStore(gGiOut, ivec2(id.xy), vec4(vec4(radiance, transmit)));
}

// Fluence per cascade-0 probe: the mean over its 4 directions.
void csGiResolve(uvec3 id)
{
    if (any(greaterThanEqual(id.xy, uvec2(gCascadeSize / 2)))) {
        return;
    }
    vec3 sum = vec3(0.0);
    for (int k = 0; k < 4; k++) {
        sum += texelFetch(gCascadeUpper, (ivec3(ivec2(id.xy) * 2 + ivec2(k & 1, k >> 1), 0)).xy, 0).rgb;
    }
    imageStore(gGiOut, ivec2(id.xy), vec4(vec4(sum * 0.25, 1.0)));
}

// --- The light pass ---------------------------------------------------------

// The shadow, sun and AO maps hold one value per ground point, and a wall's
// face shares its ground point with the wall (dilated by csOccluderMax): read
// there, every face would be in its own shadow. Faces read the maps a few px
// in front of themselves along the base normal (ground v = 2 * screen y).
const float kFrontOffset = 5.0;

vec2 frontOf(vec2 ground, vec3 nRef)
{
    return ground + vec2(nRef.x, nRef.y * 0.5) * kFrontOffset;
}

void csLight(uvec3 id)
{
    ivec2 pix = ivec2(id.xy);
    if (any(greaterThanEqual(pix, gTargetSize))) {
        return;
    }
    vec4 cover = texelFetch(gCoverage, pix, 0);
    float coverage = cover.r;
    vec4 albedo = texelFetch(gAlbedo, pix, 0);
    if (albedo.a <= 0.0) {
        imageStore(gOut, ivec2(pix), vec4(vec4(0.0, 0.0, 0.0, coverage)));
        imageStore(gBloomOut, ivec2(pix), vec4(0.0));
        return;
    }
    vec3 c = albedo.rgb / albedo.a;
    vec4 material = texelFetch(gMaterial, pix, 0);
    vec4 surface = texelFetch(gSurface, pix, 0);
    float ma = max(material.a, 1e-4);
    float sa = max(surface.a, 1e-4);
    float unlit = material.a > 0.0 ? material.r / ma : 1.0;
    float ambientOnly = material.g / ma;
    float glow = saturate(cover.g / max(cover.r, 1e-4));
    float emissive = saturate(cover.b / max(cover.r, 1e-4)); // bloom weight of cycle colors
    float reliefWeight = material.b / ma;
    float height = surface.r / sa;
    float level = surface.g / sa * 128.0;
    float shapeWeight = surface.b / sa;
    vec3 nRef = decodeNormal(texelFetch(gBaseNormal, pix, 0));
    vec3 n = decodeNormal(texelFetch(gDetailNormal, pix, 0));
    if ((gFeatures & F_NORMALS) == 0) {
        reliefWeight = 0.0;
        shapeWeight = 0.0;
    }

    // CE's light at the pixel's ground point.
    vec2 p = (vec2(pix) + 0.5) / float(gScale);
    vec2 ground = vec2(p.x, p.y + height);
    vec2 uv = (ground + vec2(gGroundOrigin)) / 2.0 / vec2(gFieldSize);
    float above = aboveAt(uv);
    vec2 front = frontOf(ground, nRef);
    vec2 frontUv = (front + vec2(gGroundOrigin)) / 2.0 / vec2(gFieldSize);

    vec3 light;
    if (ambientOnly >= 0.5) {
        light = gAmbient * gAmbientColor; // roofs: ambient only, as in CE
    } else {
        float sky = 1.0;
        vec2 sunAo = textureLod(gSunAo, frontUv, float(0)).rg;
        if ((gFeatures & F_AO) != 0) {
            sky *= mix(1.0, mix(sunAo.g, 1.0, saturate(height / 24.0)), gAoStrength);
        }
        if ((gFeatures & F_SUN) != 0 && gSunStrength > 0.0) {
            float sunLit = softVisible(gSunAo, frontUv, 1.0 / vec2(gFieldSize), height);
            // Art that ignores the shadow loses no light to it, so it gets no gain to make up for it.
            float noShadow = saturate(ambientOnly * 4.0);
            sunLit = mix(sunLit, 1.0, noShadow);
            // Energy kept where the surface faces the viewer (ADR-0004): the
            // reference is the sun's shape on a surface facing up (floors)
            // or toward the camera (upright art), blended by the normal.
            float up = saturate(nRef.z);
            vec3 facing = normalize(vec3(0.0, 1.0 - up, up));
            float reference = 1.0 + shapeWeight * (2.0 * saturate(dot(facing, gSun.xyz) * 0.5 + 0.5) - 1.0);
            float shape = shapeTerm(n, nRef, gSun.xyz, reliefWeight, shapeWeight) / reference;
            sky *= (1.0 - gSunStrength) + gSunStrength * mix(gSunGain, 1.0, noShadow) * shape * sunLit;
        }
        light = gAmbient * gAmbientColor * sky;
        light += lampLight(ground, front, height, above, poolBoost(uv, above), n, nRef, reliefWeight, shapeWeight);
        if ((gFeatures & F_GI) != 0) {
            // Fluence texel j sits at grid texel 2j + 1.
            vec2 g = (ground + vec2(gGroundOrigin)) / 2.0;
            light += gGiStrength * textureLod(gFluence, g / vec2(gCascadeSize), float(0)).rgb;
        }
    }

    // Flames and neon near a light (the P5 heuristic) never go darker than
    // the art; they also feed the bloom.
    light = mix(light, max(light, vec3(1.0, 1.0, 1.0)), glow);

    vec3 result = c * light;
    vec3 hot = max(result - 1.0, 0.0);
    if ((gFeatures & F_TONEMAP) != 0 && gTonemapCurve == 1) {
        result = agx(result);
    } else if ((gFeatures & F_TONEMAP) != 0) {
        // The shoulder per channel turns bright reds orange (green catches
        // up); on the largest channel it keeps the hue. A share of the
        // per-channel result lets very bright pools still whiten.
        float peak = max(result.r, max(result.g, result.b));
        vec3 hue = peak > gKnee ? result * (shoulder(peak) / peak) : result;
        vec3 white = vec3(shoulder(result.r), shoulder(result.g), shoulder(result.b));
        result = mix(hue, white, gTonemapWhite);
    }
    result = mix(result, litOriginal(c, level), gMix);
    // Unlit: interface, text, and screens and alarm lights (P6.2).
    result = mix(result, c, saturate(unlit));
    imageStore(gOut, ivec2(pix), vec4(vec4(result * albedo.a, coverage)));
    // Bloom source (P6.3): what glows. Palette-cycle colors at their own
    // color (screens, alarms, fire; slime at half), the glow heuristic's
    // flames and neon as lit, light above full white (hot pools) if asked for.
    vec3 bloom = c * emissive + result * glow * gBloomGlow + hot * gBloomHot;
    imageStore(gBloomOut, ivec2(pix), vec4(vec4(bloom * albedo.a, 0.0)));
}
