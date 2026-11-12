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

// Post effects on the HD map (PLAN.md P6.3). Bloom: the light pass's bloom
// source is filtered down a chain of half-size levels (13-tap downsample,
// Jimenez 2014) and back up (3x3 tent, each level adding the wider one
// below it), then added "screen"-style. Grade and vignette follow.
uniform ivec2 gSize;
uniform ivec2 gLevelSize;
uniform vec2 gSourceTexel;
uniform float gSpread;
uniform float gBloomStrength;
uniform vec3 gWhiteBalance;
uniform float gSaturation;
uniform vec3 gLift;
uniform float gVignette;
uniform vec3 gGamma;
uniform float gLutSize;
uniform vec3 gGain;
uniform int gGrade;

layout(binding=0) uniform sampler2D gSource;
layout(binding=1) uniform sampler2D gWide;   // csUp: the wider level, already up
layout(binding=2) uniform sampler2D gHd;
layout(binding=3) uniform sampler2D gBloom;  // level 0 after the way up
layout(binding=4) uniform sampler3D gLut;

layout(rgba16f, binding=0) writeonly uniform highp image2D gOut;

vec3 tap(vec2 uv, vec2 offset) { return textureLod(gSource, uv + offset * gSourceTexel, float(0)).rgb; }

void csDown(uvec3 id)
{
    if (any(greaterThanEqual(ivec2(id.xy), gLevelSize))) {
        return;
    }
    vec2 uv = (vec2(id.xy) + 0.5) / vec2(gLevelSize);
    vec3 e = tap(uv, vec2(0, 0));
    vec3 corners = tap(uv, vec2(-2, -2)) + tap(uv, vec2(2, -2)) + tap(uv, vec2(-2, 2)) + tap(uv, vec2(2, 2));
    vec3 edges = tap(uv, vec2(0, -2)) + tap(uv, vec2(-2, 0)) + tap(uv, vec2(2, 0)) + tap(uv, vec2(0, 2));
    vec3 inner = tap(uv, vec2(-1, -1)) + tap(uv, vec2(1, -1)) + tap(uv, vec2(-1, 1)) + tap(uv, vec2(1, 1));
    imageStore(gOut, ivec2(id.xy), vec4(vec4(e * 0.125 + corners * 0.03125 + edges * 0.0625 + inner * 0.125, 1.0)));
}

void csUp(uvec3 id)
{
    if (any(greaterThanEqual(ivec2(id.xy), gLevelSize))) {
        return;
    }
    vec2 uv = (vec2(id.xy) + 0.5) / vec2(gLevelSize);
    vec3 wide = vec3(0.0);
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float w = (x == 0 ? 2.0 : 1.0) * (y == 0 ? 2.0 : 1.0) / 16.0;
            wide += w * textureLod(gWide, uv + vec2(x, y) * gSourceTexel, float(0)).rgb;
        }
    }
    imageStore(gOut, ivec2(id.xy), vec4(vec4(texelFetch(gSource, (ivec3(id.xy, 0)).xy, 0).rgb + gSpread * wide, 1.0)));
}

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

vec3 grade(vec3 c)
{
    c *= gWhiteBalance;
    c = gGain * (c + gLift * (1.0 - c));
    c = pow(max(c, vec3(0.0)), 1.0 / gGamma);
    c = mix(vec3(luma(c)), c, gSaturation);
    if (gLutSize > 0.0) {
        vec3 p = saturate(c) * ((gLutSize - 1.0) / gLutSize) + 0.5 / gLutSize;
        c = textureLod(gLut, p, float(0)).rgb;
    }
    return max(c, 0.0);
}

void csPost(uvec3 id)
{
    if (any(greaterThanEqual(ivec2(id.xy), gSize))) {
        return;
    }
    vec4 hd = texelFetch(gHd, (ivec3(id.xy, 0)).xy, 0);
    vec3 c = hd.rgb;
    vec2 uv = (vec2(id.xy) + 0.5) / vec2(gSize);
    if (gBloomStrength > 0.0) {
        vec3 glow = gBloomStrength * textureLod(gBloom, uv, float(0)).rgb;
        c += glow * saturate(1.0 - c);
    }
    if (gGrade != 0) {
        c = grade(c);
    }
    if (gVignette > 0.0) {
        // Elliptical: 0 at the center, 1 in the corners of the map window.
        float d = length((uv - 0.5) * 2.0) * 0.70710678;
        c *= 1.0 - gVignette * smoothstep(0.45, 1.0, d);
    }
    imageStore(gOut, ivec2(id.xy), vec4(vec4(c, hd.a)));
}
