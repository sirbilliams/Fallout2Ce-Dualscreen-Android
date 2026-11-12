// Adapted from Dustlight map_renderer.cc; see docs/DUSTLIGHT-NOTICE.md.
R"glsl(#version 310 es
precision highp float;
precision highp int;
precision highp usampler2D;
precision highp sampler2DArray;
layout(local_size_x=8,local_size_y=8) in;
layout(binding=0) uniform usampler2D gAtlas;
layout(binding=1) uniform usampler2D gText;
layout(binding=2) uniform usampler2D gTables;
layout(binding=3) uniform usampler2D gFloorCells;
layout(binding=4) uniform sampler2DArray gPool;
layout(r32ui,binding=0) uniform highp uimage2D gTarget;
layout(binding=6) uniform highp sampler2D gHdRead;
layout(rgba16f,binding=1) writeonly uniform highp image2D gHdTarget;
uniform ivec2 gTargetSize;
uniform int gScale, gSpriteBase, gMode;
float sat(float x) { return clamp(x,0.0,1.0); }
// Must match map_renderer.cc.
#define OP_FILL 0
#define OP_LIT 1
#define OP_FLOOR 2
#define OP_COPY 3
#define OP_TEXT 4
#define OP_TRANS 5
#define OP_EGG 6

struct Sprite {
    ivec4 rect;   // logical pixels to draw, inclusive (already clipped)
    ivec2 origin; // logical position of source texel (0, 0)
    ivec2 src;    // source texel (0, 0) in the atlas or text texture
    ivec2 size;   // source width, height
    int op;
    int param;  // light level, fill color, floor vertex base, outline colors
    ivec4 egg;    // egg rect (OP_LIT: skip inside), or outline visible frame rect
    ivec2 eggSrc; // egg mask in the atlas
    int table;  // translucency: blend table | gray table << 8; outline: see below
    int tableColor; // translucency: the blend table's color (psHd)
    ivec4 hd;     // HD art (psHd): pool texel x, y of the frame, pool page, flags
    ivec4 light;  // new light (psGbuffer): category, flags
};

layout(std430,binding=0) readonly buffer Sprites { Sprite gSprites[]; };
layout(std430,binding=1) readonly buffer Vertices { int gVertexLight[]; };
layout(std430,binding=2) readonly buffer Palette { vec4 gBasePalette[]; };
// Outline pixels carry this above the palette index, so the compositor can
// show them from the exact pass in HD mode. Everything reading the target
// masks it off.
#define OUTLINE_TAG 0x100
#define HD_ART 1
#define HD_HARD_ALPHA 2
#define HD_EDGE_FILL 4
#define HD_EDGE_CLAMP 8
#define HARD_EDGE_SEARCH 8




int intensityColor(int color, int level) { return int(texelFetch(gTables, (ivec3(level, color, 0)).xy, 0).r); }
int mixAdd(int a, int b) { return int(texelFetch(gTables, (ivec3(b, 256 + int(a), 0)).xy, 0).r); }
int blendColor(int table, int gray, int dest) { return int(texelFetch(gTables, (ivec3(dest, 512 + int(table) * 16 + int(gray), 0)).xy, 0).r); }
int grayOf(int table, int color) { return int(texelFetch(gTables, (ivec3(color, 592 + int(table), 0)).xy, 0).r); }

bool inside(ivec4 r, ivec2 p) { return p.x >= r.x && p.x <= r.z && p.y >= r.y && p.y <= r.w; }

const ivec3 kRightsideUp[5] = ivec3[5]( ivec3(2, 3, 0), ivec3(3, 4, 1), ivec3(5, 6, 3), ivec3(6, 7, 4), ivec3(8, 9, 6) );
const ivec3 kUpsideDown[5] = ivec3[5]( ivec3(0, 3, 1), ivec3(2, 5, 3), ivec3(3, 6, 4), ivec3(5, 8, 6), ivec3(6, 9, 7) );

// C integer division (truncates toward zero).
int cdiv(int a, int b) { int q = abs(a) / b; return a < 0 ? -q : q; }

// tileRenderFloor's _intensity_map value for frame texel t. Cells the
// triangle fill never writes stay 0 in CE.
int floorIntensity(int base, ivec2 t)
{
    int index = 160 + 80 * t.y + t.x;
    if (index < 0 || index >= 3280) {
        return 0;
    }
    int cell = int(texelFetch(gFloorCells, (ivec3(index % 80, index / 80, 0)).xy, 0).r);
    if ((cell & 0x8000) == 0) {
        return 0;
    }
    int tri = (cell >> 11) & 7;
    int i = int((cell >> 6) & 31);
    int j = int(cell & 63);
    if ((cell & 0x4000) == 0) {
        ivec3 f = kRightsideUp[tri];
        int value = gVertexLight[int(base) + f.z];
        int stepX = cdiv(gVertexLight[int(base) + f.y] - gVertexLight[int(base) + f.x], 32);
        int stepY = cdiv(gVertexLight[int(base) + f.x] - value, 13);
        return value + i * stepY + j * stepX;
    } else {
        ivec3 f = kUpsideDown[tri];
        int value = gVertexLight[int(base) + f.x];
        int stepX = cdiv(gVertexLight[int(base) + f.z] - value, 32);
        int stepY = cdiv(gVertexLight[int(base) + f.y] - value, 13);
        return value + i * stepY + j * stepX;
    }
}

int psSprite(Sprite s, ivec2 pix)
{
    ivec2 p = pix / gScale;
    ivec2 t = p - s.origin;
    int color;
    switch (s.op) {
    case OP_FILL:
        return s.param;
    case OP_LIT:
        if (s.egg.z >= s.egg.x && inside(s.egg, p)) {
            return -1;
        }
        color = int(texelFetch(gAtlas, (ivec3(s.src + t, 0)).xy, 0).r);
        if (color == 0) {
            return -1;
        }
        return color < 0xE5 ? intensityColor(color, int(s.param)) : color;
    case OP_FLOOR:
        color = int(texelFetch(gAtlas, (ivec3(s.src + t, 0)).xy, 0).r);
        if (color == 0) {
            return -1;
        }
        return intensityColor(color, clamp(floorIntensity(s.param, t) >> 9, 0, 255));
    case OP_COPY:
        color = int(texelFetch(gAtlas, (ivec3(s.src + t, 0)).xy, 0).r);
        if (color == 0) {
            return -1;
        }
        return color;
    case OP_TEXT:
        color = int(texelFetch(gText, (ivec3(s.src + t, 0)).xy, 0).r);
        if (color == 0) {
            return -1;
        }
        return color;
    }
    return -1;
    return 0;
}

// Destination-reading ops, one thread per target pixel of the sprite rect.

void csPixelOp(uvec3 id)
{
    Sprite s = gSprites[gSpriteBase];
    ivec2 pix = s.rect.xy * gScale + ivec2(id.xy);
    if (any(greaterThanEqual(pix, (s.rect.zw + 1) * gScale)) || any(greaterThanEqual(pix, gTargetSize))) {
        return;
    }
    ivec2 p = pix / gScale;
    int color = int(texelFetch(gAtlas, (ivec3(s.src + (p - s.origin), 0)).xy, 0).r);
    if (color == 0) {
        return;
    }
    int dest = int(imageLoad(gTarget, pix).r) & 0xFF;
    if (s.op == OP_TRANS) {
        int gray = grayOf(s.table >> 8, color);
        imageStore(gTarget, pix, uvec4(intensityColor(blendColor(s.table & 0xFF, gray, dest), int(s.param))));
    } else {
        color = intensityColor(color, int(s.param));
        int mask = int(int(texelFetch(gAtlas, (ivec3(s.eggSrc + (p - s.egg.xy), 0)).xy, 0).r));
        if (mask != 0) {
            int under = intensityColor(dest, 128 - mask);
            int over = intensityColor(color, mask);
            color = mixAdd(over, under);
        }
        imageStore(gTarget, pix, uvec4(color));
    }
}

// objectDrawOutline. Sprite: rect.xy = unclipped object position, size =
// frame size, egg = visible frame rect, param = color | count << 8 |
// band << 16, table = 0 or (paletted: 1 << 16 | blend table << 8 | gray).
// One thread per frame row (row pass), then per column (column pass); both
// write only their own row or column, as in CE's loops.
void writeLogical(ivec2 p, int value)
{
    for (int dy = 0; dy < gScale; dy++) {
        for (int dx = 0; dx < gScale; dx++) {
            ivec2 pix = p * gScale + ivec2(dx, dy);
            if (all(greaterThanEqual(pix, ivec2(0))) && all(lessThan(pix, gTargetSize))) {
                imageStore(gTarget, pix, uvec4((value & 0xFF) | OUTLINE_TAG));
            }
        }
    }
}

int outlineValue(Sprite s, int color, ivec2 p)
{
    if ((s.table & 0x10000) == 0) {
        return color & 0xFF;
    }
    int gray = grayOf(s.table & 0xFF, color);
    return blendColor((s.table >> 8) & 0xFF, gray, int(imageLoad(gTarget, p * gScale).r) & 0xFF);
}

int advanceBand(int outlineColor, int y, int color, int count, int band)
{
    if (band != 0) {
        if (y % band == 0) {
            outlineColor = (outlineColor + 1) & 0xFF;
        }
        if (int(outlineColor) > count + int(color) - 1) {
            outlineColor = color;
        }
    }
    return outlineColor;
}


void csOutlineRows(uvec3 id)
{
    Sprite s = gSprites[gSpriteBase];
    int y = int(id.x);
    int w = s.size.x;
    if (y >= s.size.y) {
        return;
    }
    int color = s.param & 0xFF;
    int count = int((s.param >> 8) & 0xFF);
    int band = int(s.param >> 16);
    int outlineColor = color;
    for (int yy = 0; yy <= y; yy++) {
        outlineColor = advanceBand(outlineColor, yy, color, count, band);
    }

    ivec4 vis = s.egg;
    bool yVisible = y >= vis.y && y <= vis.w;
    int mapWidth = gTargetSize.x / gScale;
    int destY = s.rect.y + y;
    bool cycle = true;
    for (int x = 0; x < w; x++) {
        int pixel = int(texelFetch(gAtlas, (ivec3(s.src + ivec2(x, y), 0)).xy, 0).r);
        int destX = s.rect.x + x;
        if (pixel != 0 && cycle) {
            if (yVisible && x >= vis.x && x <= vis.z && destX >= 1) {
                writeLogical(ivec2(destX - 1, destY), outlineValue(s, outlineColor, ivec2(destX - 1, destY)));
            }
            cycle = false;
        } else if (pixel == 0 && !cycle) {
            if (yVisible && x >= vis.x && x <= vis.z) {
                writeLogical(ivec2(destX, destY), outlineValue(s, outlineColor, ivec2(destX, destY)) & 0xFF);
            }
            cycle = true;
        }
    }
    if (int(texelFetch(gAtlas, (ivec3(s.src + ivec2(w - 1, y), 0)).xy, 0).r) != 0) {
        int destX = s.rect.x + w;
        if (yVisible && w - 1 >= vis.x && w - 1 <= vis.z && destX < mapWidth) {
            writeLogical(ivec2(destX, destY), outlineValue(s, outlineColor, ivec2(destX, destY)));
        }
    }
}


void csOutlineColumns(uvec3 id)
{
    Sprite s = gSprites[gSpriteBase];
    int x = int(id.x);
    int h = s.size.y;
    ivec4 vis = s.egg;
    if (x >= s.size.x || x < vis.x || x > vis.z) {
        return;
    }
    int color = s.param & 0xFF;
    int count = int((s.param >> 8) & 0xFF);
    int band = int(s.param >> 16);
    int mapHeight = gTargetSize.y / gScale;
    int destX = s.rect.x + x;
    bool cycle = true;
    int outlineColor = color;
    for (int y = 0; y < h; y++) {
        outlineColor = advanceBand(outlineColor, y, color, count, band);
        bool yVisible = y >= vis.y && y <= vis.w;
        int pixel = int(texelFetch(gAtlas, (ivec3(s.src + ivec2(x, y), 0)).xy, 0).r);
        int destY = s.rect.y + y;
        if (pixel != 0 && cycle) {
            if (yVisible && destY - 1 >= 0) {
                writeLogical(ivec2(destX, destY - 1), outlineValue(s, outlineColor, ivec2(destX, destY - 1)));
            }
            cycle = false;
        } else if (pixel == 0 && !cycle) {
            if (yVisible) {
                writeLogical(ivec2(destX, destY), outlineValue(s, outlineColor, ivec2(destX, destY)));
            }
            cycle = true;
        }
    }
    if (int(texelFetch(gAtlas, (ivec3(s.src + ivec2(x, h - 1), 0)).xy, 0).r) != 0) {
        int destY = s.rect.y + h;
        if (h - 1 >= vis.y && h - 1 <= vis.w && destY < mapHeight) {
            writeLogical(ivec2(destX, destY), outlineValue(s, outlineColor, ivec2(destX, destY)));
        }
    }
}

// HD color pass (PLAN.md P4.2): the same sprites into an RGBA16F target,
// colors in "base" palette space (color.pal before fades and gamma; the
// compositor applies those). Continuous versions of CE's rules:
//   light        c * level/128, CE's "lighter" half above 128
//   translucency light * mix(dest, table color, gray(src)/7)  (_buildBlendTable)
//   egg          lit color weighted by the egg mask, filtered at N x
//   cycle colors original art: the exact pass's result from the live palette;
//                HD art: animated by cycleAt (P6.1), lit only on floors
// Every op ends in one dual-source blend: dest = o0 + dest * o1, so o0 is the
// premultiplied color and o1 the factor left for what's below. The alpha
// channel accumulates coverage (holes test).

struct HdOut {
    vec4 color;
    vec4 factor;
};

vec3 baseColor(int index) { return gBasePalette[index & 0xFF].rgb; }

vec3 lit(vec3 c, int level)
{
    return level <= 128 ? c * (float(level) / 128.0) : c + (1.0 - c) * (float(level - 128) / 128.0);
}

HdOut over(vec3 c, float a)
{
    HdOut o;
    o.color = vec4(c * a, a);
    o.factor = vec4(vec3(1.0 - a), 1.0 - a);
    return o;
}

// Tiling art (floors, roofs, walls) is cut with its 1x mask, so against sharp HD
// art its edge reads as saw teeth. Each stair of the mask gets filled in up to the
// straight line through the stair's corners. Only ever past the mask, never inside
// it, so pieces that join can't open a gap between them, and only at real stairs:
// air on the far side, a riser one cell tall, so holes and checkerboards stay.
bool maskOpaque(Sprite s, ivec2 e)
{
    if (any(lessThan(e, ivec2(0))) || any(greaterThanEqual(e, s.size))) {
        return false;
    }
    return int(texelFetch(gAtlas, (ivec3(s.src + e, 0)).xy, 0).r) != 0;
}

// Cells walked from cell e along `along` to the end of the run on `floor` side
// `toward`; rise = the run ends at a stair's riser.
int stairRun(Sprite s, ivec2 e, ivec2 along, ivec2 toward, out bool rise)
{
    rise = false;
     for (int k = 1; k <= HARD_EDGE_SEARCH; k++) {
        ivec2 q = e + along * k;
        if (maskOpaque(s, q)) {
            rise = !maskOpaque(s, q - toward);
            return k - 1;
        }
        if (!maskOpaque(s, q + toward)) {
            return k - 1;
        }
    }
    return HARD_EDGE_SEARCH;
}

// Coverage of a pixel in transparent cell e, at f (0..1) within the cell; `toward`
// is the direction of the mask it fills up to, for the light.
float hardEdgeFill(Sprite s, ivec2 e, vec2 f, out ivec2 toward)
{
    const ivec2 kToward[4] = ivec2[4]( ivec2(0, 1), ivec2(0, -1), ivec2(1, 0), ivec2(-1, 0) );
    float best = 0.0;
    toward = ivec2(0, 0);
     for (int d = 0; d < 4; d++) {
        ivec2 n = kToward[d];
        if (!maskOpaque(s, e + n) || maskOpaque(s, e - n)) {
            continue;
        }
        ivec2 u = n.y != 0 ? ivec2(1, 0) : ivec2(0, 1);
        bool riseBack;
        bool riseOn;
        int back = stairRun(s, e, -u, n, riseBack);
        int on = stairRun(s, e, u, n, riseOn);
        if (!riseBack && !riseOn) {
            continue;
        }
        float along = n.y != 0 ? f.x : f.y;
        float across = n.y > 0 ? 1.0 - f.y : (n.y < 0 ? f.y : (n.x > 0 ? 1.0 - f.x : f.x));
        float run = float(back + on + 1) * (riseBack && riseOn ? 0.5 : 1.0);
        float reach = 0.0;
        if (riseBack) {
            reach = max(reach, 1.0 - (float(back) + along) / run);
        }
        if (riseOn) {
            reach = max(reach, 1.0 - (float(on) + 1.0 - along) / run);
        }
        float cover = reach > 0.0 ? sat((reach - across) * float(gScale) + 0.5) : 0.0;
        if (cover > best) {
            best = cover;
            toward = n;
        }
    }
    return best;
}


// [hd] edge_clamp: an HD color in cell t (opaque, with a transparent 4-neighbor) or in the
// stair fill toward t, kept within the phase-0 palette colors of the opaque cells around t.
vec3 edgeClamp(Sprite s, ivec2 t, vec3 c)
{
    if (!maskOpaque(s, t)) {
        return c;
    }
    if (maskOpaque(s, t + ivec2(1, 0)) && maskOpaque(s, t - ivec2(1, 0)) && maskOpaque(s, t + ivec2(0, 1)) && maskOpaque(s, t - ivec2(0, 1))) {
        return c;
    }
    vec3 lo = vec3(1e9, 1e9, 1e9);
    vec3 hi = vec3(-1e9, -1e9, -1e9);
     for (int k = 0; k < 9; k++) {
        ivec2 e = t + ivec2(k % 3 - 1, k / 3 - 1);
        if (maskOpaque(s, e)) {
            // HD art holds the cycle colors at phase 0 (gBasePalette[256 + i], filled
            // for 229-254 only); every other color is the live entry.
            int i = int(texelFetch(gAtlas, (ivec3(s.src + e, 0)).xy, 0).r) & 0xFF;
            vec3 p = gBasePalette[i >= 229 && i <= 254 ? 256 + i : i].rgb;
            lo = min(lo, p);
            hi = max(hi, p);
        }
    }
    return clamp(c, lo, hi);
}

// Tiling HD art's coverage at target pixel `pix`, in cell t of the frame.
float hardAlpha(Sprite s, ivec2 pix, ivec2 t, int index, out ivec2 toward)
{
    toward = ivec2(0, 0);
    if (index != 0) {
        return 1.0;
    }
    if ((s.hd.w & HD_EDGE_FILL) == 0 || s.op == OP_TRANS) {
        return 0.0;
    }
    vec2 f = (vec2(pix - (pix / gScale) * gScale) + 0.5) / float(gScale);
    return hardEdgeFill(s, t, f, toward);
}

// Egg weight at a target pixel: 1 where the mask is 0 (no cut), else
// mask/128, bilinear over the 1x mask at the pixel's logical position.
float eggWeight(Sprite s, ivec2 pix)
{
    vec2 f = (vec2(pix) + 0.5) / float(gScale) - 0.5 - vec2(s.egg.xy);
    ivec2 i0 = ivec2(floor(f));
    vec2 t = f - vec2(i0);
    ivec2 size = s.egg.zw - s.egg.xy + 1;
    float w[4];
     for (int k = 0; k < 4; k++) {
        ivec2 e = i0 + ivec2(k & 1, k >> 1);
        int m = all(greaterThanEqual(e, ivec2(0))) && all(lessThan(e, size)) ? int(texelFetch(gAtlas, (ivec3(s.eggSrc + e, 0)).xy, 0).r) : 0;
        w[k] = m == 0 ? 1.0 : float(m) / 128.0;
    }
    return mix(mix(w[0], w[1], t.x), mix(w[2], w[3], t.x), t.y);
}

// Palette cycling on HD art (PLAN.md P6.1). cycle.cc rotates the colors of
// six ranges in place (229-232 slime, 233-237 monitors, 238-242 slow fire,
// 243-247 fast fire, 248-253 shoreline, 254 alarm); the HD art was upscaled
// with them at phase 0 (gBasePalette[256 + index]). Each HD pixel gets the
// change (live - phase 0) of the 1x pixels around it, blended with bilinear
// weights sharpened toward the nearest one: the original animates pixel by
// pixel (monitor screens are 1-pixel dot patterns), and the HD art keeps its
// detail.
// How each range glows in the new light (P6.2), as shares of the pixel:
//   screens  monitors and the alarm: unlit, as CE draws them (they emit)
//   glow     slime and fire: never darker than the art, but lit like the
//            static pixels of the same goo or flame around them (unlit,
//            they speckled lamp-lit goo with dark dots)
//   bloom    bloom source weight: all of them, slime at half
//   the shoreline is plain water, lit like its floor.
struct Cycle {
    float weight; // share of cycle colors in the pixel (0 = none)
    float screens;
    float glow;
    float bloom;
    vec3 delta; // live - phase 0, blended
};

bool isCycle(int index) { return index >= 229 && index <= 254; }

void cycleShares(int index, float w, inout Cycle c)
{
    bool slime = index <= 232;
    bool screen = (index >= 233 && index <= 237) || index == 254;
    bool fire = index >= 238 && index <= 247;
    c.weight += w;
    c.screens += screen ? w : 0.0;
    c.glow += slime || fire ? w : 0.0;
    c.bloom += screen || fire ? w : (slime ? 0.5 * w : 0.0);
}

// Original art: the pixel shows its live color already.
Cycle cycleExact(int index)
{
    Cycle c = Cycle(0.0,0.0,0.0,0.0,vec3(0.0));
    if (isCycle(index)) {
        cycleShares(index, 1.0, c);
    }
    return c;
}

Cycle cycleAt(Sprite s, ivec2 pix)
{
    Cycle c = Cycle(0.0,0.0,0.0,0.0,vec3(0.0));
    vec2 f = (vec2(pix) + 0.5) / float(gScale) - 0.5 - vec2(s.origin);
    ivec2 i0 = ivec2(floor(f));
    vec2 t = smoothstep(0.15, 0.85, f - vec2(i0));
     for (int k = 0; k < 4; k++) {
        ivec2 e = i0 + ivec2(k & 1, k >> 1);
        int index = all(greaterThanEqual(e, ivec2(0))) && all(lessThan(e, s.size)) ? int(texelFetch(gAtlas, (ivec3(s.src + e, 0)).xy, 0).r) : 0;
        if (!isCycle(index)) {
            continue;
        }
        float w = ((k & 1) != 0 ? t.x : 1.0 - t.x) * ((k >> 1) != 0 ? t.y : 1.0 - t.y);
        cycleShares(index, w, c);
        c.delta += w * (gBasePalette[index].rgb - gBasePalette[256 + index].rgb);
    }
    return c;
}

HdOut psHd(Sprite s, ivec2 pix)
{
    ivec2 p = pix / gScale;
    ivec2 t = p - s.origin;
    int index;
    switch (s.op) {
    case OP_FILL:
        return over(baseColor(s.param), 1.0);
    case OP_TEXT:
        index = int(texelFetch(gText, (ivec3(s.src + t, 0)).xy, 0).r);
        if (index == 0) {
            return over(vec3(0.0),0.0);
        }
        return over(baseColor(index), 1.0);
    case OP_COPY:
        index = int(texelFetch(gAtlas, (ivec3(s.src + t, 0)).xy, 0).r);
        if (index == 0) {
            return over(vec3(0.0),0.0);
        }
        return over(baseColor(index), 1.0);
    }

    // Lit art: OP_LIT, OP_FLOOR, OP_EGG, OP_TRANS.
    if (s.op == OP_LIT && s.egg.z >= s.egg.x && inside(s.egg, p)) {
        return over(vec3(0.0),0.0); // drawn by the OP_EGG sprite
    }
    index = int(texelFetch(gAtlas, (ivec3(s.src + t, 0)).xy, 0).r);
    vec4 art;
    float a;
    if ((s.hd.w & HD_ART) != 0) {
        art = texelFetch(gPool, (ivec4(s.hd.xy + pix - s.origin * gScale, s.hd.z, 0)).xyz, 0);
        ivec2 toward = ivec2(0, 0);
        a = (s.hd.w & HD_HARD_ALPHA) != 0 ? hardAlpha(s, pix, t, index, toward) : art.a;
        if ((s.hd.w & HD_EDGE_CLAMP) != 0 && a > 0.0) {
            art.rgb = edgeClamp(s, t + toward, art.rgb);
        }
    } else {
        art = vec4(baseColor(index), 1.0);
        a = index != 0 ? 1.0 : 0.0;
    }
    if (a <= 0.0) {
        return over(vec3(0.0),0.0);
    }

    int level = int(int(s.param));
    if (s.op == OP_FLOOR) {
        level = clamp(floorIntensity(s.param, t) >> 9, 0, 255);
    } else if (s.op == OP_TRANS) {
        level = min(level, 128);
    }

    if (s.op == OP_TRANS) {
        // _dark_translucent_trans_buf_to_buf: gray of the source picks the
        // blend row, the table's color is mixed in, then the light.
        vec3 c = art.rgb;
        float gray = (s.table >> 8) == 0 ? (c.b + 3.0 * c.r + 6.0 * c.g) / 10.0 : (c.r + 5.0 * c.g + 4.0 * c.b) / 10.0;
        // CE: 5-bit gray >> 2, i.e. floor(gray * 7.97) in 0..7. Kept
        // continuous for HD art, minus the half step the floor loses on
        // average (else translucency comes out brighter than CE's).
        float g = clamp(gray * 7.97 - 0.5, 0.0, 7.0) / 7.0;
        float light = float(level) / 128.0;
        HdOut o;
        o.color = vec4(baseColor(s.tableColor) * (g * light * a), a);
        o.factor = vec4(vec3(1.0 - a + a * light * (1.0 - g)), 1.0 - a);
        return o;
    }

    vec3 color;
    if ((s.hd.w & HD_ART) != 0) {
        Cycle cycle = cycleAt(s, pix);
        vec3 c = max(art.rgb + cycle.delta, 0.0);
        // CE lights cycle colors on floors (and the egg), not on other art.
        color = mix(lit(c, level), c, s.op == OP_FLOOR || s.op == OP_EGG ? 0.0 : cycle.weight);
    } else if (index >= 0xE5 && (s.op == OP_FLOOR || s.op == OP_EGG)) {
        color = baseColor(intensityColor(index, level)); // CE lights cycle colors here
    } else if (index >= 0xE5) {
        color = baseColor(index); // _dark_trans_buf_to_buf leaves them unlit
    } else {
        color = lit(art.rgb, level);
    }
    if (s.op == OP_EGG) {
        a *= eggWeight(s, pix);
    }
    return over(color, a);
}


void main() {
    uvec3 id=gl_GlobalInvocationID;
    if (gMode==2) { if(id.y==0u) csOutlineRows(id); return; }
    if (gMode==3) { if(id.y==0u) csOutlineColumns(id); return; }
    Sprite s=gSprites[gSpriteBase];
    ivec2 pix=s.rect.xy*gScale+ivec2(id.xy);
    if(any(lessThan(pix,ivec2(0))) || any(greaterThanEqual(pix,gTargetSize)) || any(greaterThanEqual(pix,(s.rect.zw+1)*gScale)))return;
    if(gMode==1) {
        HdOut o=psHd(s,pix);
        // Same dual-source equation as Dustlight. One invocation owns a
        // pixel; a barrier between sprites orders destination reads.
        imageStore(gHdTarget,pix,o.color+texelFetch(gHdRead,pix,0)*o.factor);
    } else if(s.op==OP_TRANS || s.op==OP_EGG) csPixelOp(id);
    else { int v=psSprite(s,pix); if(v!=-1)imageStore(gTarget,pix,uvec4(v)); }
}
)glsl"
