// Adapted from Dustlight map_renderer.cc.
R"glsl(layout(binding=5)uniform highp sampler2D gShape;
// Categories and flags must match map_renderer.h (MapLightCategory).
#define CAT_NONE 0
#define CAT_FLOOR 1
#define CAT_ROOF 2
#define CAT_WALL 3
#define CAT_CRITTER 5
#define LIGHT_FLAT 1
#define LIGHT_GLOW 2


uniform vec4 gCategory[9];
uniform float gDetailScale;
// Output pixels cover gK x gK target pixels (lower-resolution light): each is
// shaded at its block's middle texel and stored at `lo`, a gLoSize target.
uniform int gK;
uniform ivec2 gLoSize;
uniform ivec2 gOccluderOrigin,gOccluderSize;

struct GbufOut {
    vec4 albedo;
    vec4 baseNormal;
    vec4 detailNormal;
    vec4 surface;
    vec4 material;
    vec4 coverage;
};

vec4 encodeNormal(vec3 n, float a) { return vec4((n * 0.5 + 0.5) * a, a); }

GbufOut gbufUnlit(vec3 c, float a)
{
    GbufOut o;
    o.albedo = vec4(c * a, a);
    o.baseNormal = encodeNormal(vec3(0.0, 0.0, 1.0), a);
    o.detailNormal = o.baseNormal;
    o.surface = vec4(0.0, a, 0.0, a);
    o.material = vec4(a, 0.0, 0.0, a);
    o.coverage = vec4(a, 0.0, 0.0, a);
    return o;
}

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// HD albedo luminance at frame-local HD texel q, clamped to the HD texels
// the sprite has in the pool: the frame, or for a frame kept in tiles
// (addSprite) its tile, light.z (first) to light.w (one past the last),
// each x | y << 16.
float hdLuma(Sprite s, ivec2 q)
{
    ivec2 lo = ivec2(0, 0);
    ivec2 hi = s.size * gScale;
    if (s.light.w != 0) {
        lo = ivec2(s.light.z & 0xFFFF, (s.light.z >> 16) & 0xFFFF);
        hi = ivec2(s.light.w & 0xFFFF, (s.light.w >> 16) & 0xFFFF);
    }
    q = clamp(q, lo, hi - 1);
    return luma(texelFetch(gPool, (ivec4(s.hd.xy + q, s.hd.z, 0)).xyz,0).rgb);
}

float shapeHeight(vec4 raw) { return (round(raw.b * 255.0) + round(raw.a * 255.0) * 256.0) / 16.0; }

// The sprite's shape (sprite_shape.h) at a target pixel: the normal
// bilinear over the 1x texels; the height bilinear over the opaque ones
// (transparent texels hold 0), else 1 px height steps print 1x blocks into
// the light of nearby lamps.
void shapeAt(Sprite s, ivec2 pix, out vec3 n, out float height)
{
    vec2 f = (vec2(pix) + 0.5) / float(gScale) - 0.5 - vec2(s.origin);
    ivec2 i0 = ivec2(floor(f));
    vec2 t = f - vec2(i0);
    vec2 uz = vec2(0.0);
    float heights = 0.0;
    float weights = 0.0;
     for (int k = 0; k < 4; k++) {
        ivec2 e = clamp(i0 + ivec2(k & 1, k >> 1), ivec2(0, 0), s.size - 1);
        float w = ((k & 1) != 0 ? t.x : 1.0 - t.x) * ((k >> 1) != 0 ? t.y : 1.0 - t.y);
        vec4 raw = texelFetch(gShape, (ivec3(s.src + e, 0)).xy,0);
        uz += raw.rg * w;
        if (int(texelFetch(gAtlas, (ivec3(s.src + e, 0)).xy,0).r) != 0) {
            heights += shapeHeight(raw) * w;
            weights += w;
        }
    }
    uz = uz * 2.0 - 1.0;
    n = vec3(uz.x, sqrt(sat(1.0 - dot(uz, uz))), uz.y);
    ivec2 texel = clamp(pix / gScale - s.origin, ivec2(0, 0), s.size - 1);
    height = weights > 1e-3 ? heights / weights : shapeHeight(texelFetch(gShape, (ivec3(s.src + texel, 0)).xy,0));
}

GbufOut psGbuffer(Sprite s, ivec2 pix)
{
    ivec2 p = pix / gScale;
    ivec2 t = p - s.origin;
    int index;
    switch (s.op) {
    case OP_FILL:
        return gbufUnlit(baseColor(s.param), 1.0);
    case OP_TEXT:
        index = int(texelFetch(gText, (ivec3(s.src + t, 0)).xy,0).r);
        if (index == 0) {
            return gbufUnlit(vec3(0.0),0.0);
        }
        return gbufUnlit(baseColor(index), 1.0);
    case OP_COPY:
        index = int(texelFetch(gAtlas, (ivec3(s.src + t, 0)).xy,0).r);
        if (index == 0) {
            return gbufUnlit(vec3(0.0),0.0);
        }
        return gbufUnlit(baseColor(index), 1.0);
    }

    if (s.op == OP_LIT && s.egg.z >= s.egg.x && inside(s.egg, p)) {
        return gbufUnlit(vec3(0.0),0.0); // drawn by the OP_EGG sprite
    }
    index = int(texelFetch(gAtlas, (ivec3(s.src + t, 0)).xy,0).r);
    bool hd = (s.hd.w & HD_ART) != 0;
    vec4 art;
    float a;
    ivec2 toward = ivec2(0, 0);
    if (hd) {
        art = texelFetch(gPool, (ivec4(s.hd.xy + pix - s.origin * gScale, s.hd.z, 0)).xyz,0);
        a = (s.hd.w & HD_HARD_ALPHA) != 0 ? hardAlpha(s, pix, t, index, toward) : art.a;
        if ((s.hd.w & HD_EDGE_CLAMP) != 0 && a > 0.0) {
            art.rgb = edgeClamp(s, t + toward, art.rgb);
        }
    } else {
        art = vec4(baseColor(index), 1.0);
        a = index != 0 ? 1.0 : 0.0;
    }
    if (a <= 0.0) {
        return gbufUnlit(vec3(0.0),0.0);
    }

    int level = int(s.param);
    if (s.op == OP_FLOOR) {
        level = clamp(floorIntensity(s.param, t) >> 9, 0, 255);
    }

    if (s.op == OP_TRANS) {
        // psHd's translucency with the light left out: lerp(dest, table
        // color, gray), i.e. "over" with weight a * gray.
        vec3 c = art.rgb;
        float gray = (s.table >> 8) == 0 ? (c.b + 3.0 * c.r + 6.0 * c.g) / 10.0 : (c.r + 5.0 * c.g + 4.0 * c.b) / 10.0;
        float g = clamp(gray * 7.97 - 0.5, 0.0, 7.0) / 7.0;
        GbufOut o = gbufUnlit(vec3(0.0),0.0);
        o.albedo = vec4(baseColor(s.tableColor) * (g * a), g * a);
        o.coverage = vec4(a, 0.0, 0.0, a);
        return o;
    }
    if (s.op == OP_EGG) {
        a *= eggWeight(s, pix);
    }
    // Palette-cycle colors (P6.1, P6.2): HD art animated by cycleAt, original
    // art at its live color. All ranges but the shoreline glow: emissive
    // (never darker than the art in the light pass, and the bloom's source).
    // Shoreline water is lit like the floor it lies on.
    Cycle cycle;
    if (hd) {
        cycle = cycleAt(s, pix);
    } else {
        cycle = cycleExact(index);
    }
    art.rgb = max(art.rgb + cycle.delta, 0.0);

    int category = int(s.light.x);
    bool isFlat = category == CAT_FLOOR || category == CAT_ROOF || (s.light.y & LIGHT_FLAT) != 0;
    vec3 n = vec3(0.0, 0.0, 1.0);
    float height = 0.0;
    if (!isFlat) {
        shapeAt(s, pix + toward * gScale, n, height);
    }

    // Surface relief from the HD albedo's luminance read as a height field
    // (fo2hd.hdassets.add_detail): Sobel, in the 4x masters' units.
    vec4 k = gCategory[category];
    vec3 detail = n;
    if (hd && k.x > 0.0) {
        ivec2 q = pix - s.origin * gScale;
        float l00 = hdLuma(s, q + ivec2(-1, -1));
        float l10 = hdLuma(s, q + ivec2(0, -1));
        float l20 = hdLuma(s, q + ivec2(1, -1));
        float l01 = hdLuma(s, q + ivec2(-1, 0));
        float l21 = hdLuma(s, q + ivec2(1, 0));
        float l02 = hdLuma(s, q + ivec2(-1, 1));
        float l12 = hdLuma(s, q + ivec2(0, 1));
        float l22 = hdLuma(s, q + ivec2(1, 1));
        float dx = ((l20 + 2.0 * l21 + l22) - (l00 + 2.0 * l01 + l02)) * gDetailScale;
        float dy = ((l02 + 2.0 * l12 + l22) - (l00 + 2.0 * l10 + l20)) * gDetailScale;
        // Flat: screen y runs along v (foreshortened 2:1); upright: down = -z.
        vec3 tv = isFlat ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, -1.0);
        float dv = isFlat ? dy * 0.5 : dy;
        detail = normalize(n - k.x * (dx * vec3(1.0, 0.0, 0.0) + dv * tv));
    }

    GbufOut o;
    o.albedo = vec4(art.rgb * a, a);
    o.baseNormal = encodeNormal(n, a);
    o.detailNormal = encodeNormal(detail, a);
    o.surface = vec4(height * a, float(level) / 128.0 * a, k.z * a, a);
    // Flames and neon near a lamp or fire: bright, saturated art.
    float glow = 0.0;
    if ((s.light.y & LIGHT_GLOW) != 0) {
        float value = max(art.r, max(art.g, art.b));
        float chroma = (value - min(art.r, min(art.g, art.b))) / max(value, 1e-3);
        glow = smoothstep(0.55, 0.8, value) * smoothstep(0.35, 0.6, chroma);
    }
    glow = max(glow, cycle.glow);
    // g: 1 = roof (ambient only); up to 0.25 = the share of the sun's shadow this art
    // ignores, which isFlat art doesn't: it lies on the ground and takes shadows like it.
    float materialG = category == CAT_ROOF ? 1.0 : (isFlat ? 0.0 : k.w * 0.25);
    o.material = vec4(cycle.screens * a, materialG * a, k.y * a, a);
    o.coverage = vec4(a, glow * a, cycle.bloom * a, a);
    return o;
}

layout(std430,binding=3) buffer Heightfield { uint heights[]; };
layout(r32ui,binding=6)writeonly uniform highp uimage2D gOccluders;

// Occluder heightfield (P5.5): every opaque texel of an upright object
// (walls, scenery, items, misc; not isFlat ones) puts its height at its ground
// point, 1x, highest wins. Whole frames, also the parts outside the view. One
// thread group per sprite. Critters stay out: their shadows are painted in.


void csOccluders(uvec3 group, uvec3 thread)
{
    Sprite s = gSprites[gSpriteBase + int(group.x)];
    if (s.op != OP_LIT || int(s.light.x) < CAT_WALL || int(s.light.x) == CAT_CRITTER || (s.light.y & LIGHT_FLAT) != 0) {
        return;
    }
    int count = s.size.x * s.size.y;
    for (int k = int(thread.x); k < count; k += 64) {
        ivec2 e = ivec2(k % s.size.x, k / s.size.x);
        if (int(texelFetch(gAtlas, (ivec3(s.src + e, 0)).xy,0).r) == 0) {
            continue;
        }
        float height = shapeHeight(texelFetch(gShape, (ivec3(s.src + e, 0)).xy,0));
        ivec2 ground = s.origin + e + ivec2(0, int(round(height))) + gOccluderOrigin;
        if (height > 3.0 && all(greaterThanEqual(ground,ivec2(0))) && all(lessThan(ground,gOccluderSize))) {
            atomicMax(heights[ground.y*gOccluderSize.x+ground.x],floatBitsToUint(height));
        }
    }
}


layout(binding=6)uniform highp sampler2D previous0;
layout(rgba16f,binding=0)writeonly uniform highp image2D output0;
layout(binding=7)uniform highp sampler2D previous1;
layout(rgba8,binding=1)writeonly uniform highp image2D output1;
layout(binding=8)uniform highp sampler2D previous2;
layout(rgba8,binding=2)writeonly uniform highp image2D output2;
layout(binding=9)uniform highp sampler2D previous3;
layout(rgba16f,binding=3)writeonly uniform highp image2D output3;
layout(binding=10)uniform highp sampler2D previous4;
layout(rgba8,binding=4)writeonly uniform highp image2D output4;
layout(binding=11)uniform highp sampler2D previous5;
layout(rgba8,binding=5)writeonly uniform highp image2D output5;

void main(){
    if(gMode==5){ivec2 q=ivec2(gl_GlobalInvocationID.xy);if(all(lessThan(q,gOccluderSize)))imageStore(gOccluders,q,uvec4(heights[q.y*gOccluderSize.x+q.x]));return;}
    Sprite s=gSprites[gSpriteBase];
    if(gMode==4){if(gl_GlobalInvocationID.y!=0u)return;csOccluders(uvec3(0),uvec3(gl_GlobalInvocationID.x,0,0));return;}
    ivec2 lo=ivec2(floor(vec2(s.rect.xy*gScale)/float(gK)))+ivec2(gl_GlobalInvocationID.xy);
    ivec2 pix=min(lo*gK+gK/2,gTargetSize-1);
    if(any(lessThan(lo,ivec2(0)))||any(greaterThanEqual(lo,gLoSize))||any(lessThan(pix,s.rect.xy*gScale))||any(greaterThanEqual(pix,(s.rect.zw+1)*gScale)))return;
    GbufOut o=psGbuffer(s,pix);
#ifdef GBUF_HD_ALBEDO
    imageStore(output0,lo,o.albedo+texelFetch(previous0,lo,0)*(1.0-o.albedo.a));
    imageStore(output5,lo,o.coverage+texelFetch(previous5,lo,0)*(1.0-o.coverage.a));
#else
    imageStore(output0,lo,o.albedo+texelFetch(previous0,lo,0)*(1.0-o.albedo.a));
    imageStore(output1,lo,o.baseNormal+texelFetch(previous1,lo,0)*(1.0-o.baseNormal.a));
    imageStore(output2,lo,o.detailNormal+texelFetch(previous2,lo,0)*(1.0-o.detailNormal.a));
    imageStore(output3,lo,o.surface+texelFetch(previous3,lo,0)*(1.0-o.surface.a));
    imageStore(output4,lo,o.material+texelFetch(previous4,lo,0)*(1.0-o.material.a));
    imageStore(output5,lo,o.coverage+texelFetch(previous5,lo,0)*(1.0-o.coverage.a));
#endif
}
)glsl"
