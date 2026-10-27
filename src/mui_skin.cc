#include "mui_skin.h"

#include <math.h>
#include <stdint.h>

#include <algorithm>
#include <vector>

namespace fallout {

namespace {

    // The metal texture: tiles seamlessly, grey (tinted when drawn).
    constexpr int kMetalSize = 128;
    // Drawn this many output pixels per texture pixel (coarse grain, as the
    // game's art at its scale).
    constexpr float kMetalScaleDp = 1.25f;

    SDL_Texture* gMetal = nullptr;
    unsigned int gMetalGeneration = 0;

    uint32_t hash(uint32_t x, uint32_t y, uint32_t seed)
    {
        uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }

    float random01(uint32_t x, uint32_t y, uint32_t seed)
    {
        return (hash(x, y, seed) & 0xFFFF) / 65535.0f;
    }

    // Value noise on a lattice of [cellsX] x [cellsY] cells over the texture,
    // wrapping at its edges (the texture tiles).
    float valueNoise(float x, float y, int cellsX, int cellsY, uint32_t seed)
    {
        float fx = x * cellsX / kMetalSize;
        float fy = y * cellsY / kMetalSize;
        int x0 = static_cast<int>(floorf(fx));
        int y0 = static_cast<int>(floorf(fy));
        float tx = fx - x0;
        float ty = fy - y0;
        tx = tx * tx * (3.0f - 2.0f * tx);
        ty = ty * ty * (3.0f - 2.0f * ty);
        auto at = [&](int cx, int cy) {
            return random01(static_cast<uint32_t>((cx % cellsX + cellsX) % cellsX), static_cast<uint32_t>((cy % cellsY + cellsY) % cellsY), seed);
        };
        float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * tx;
        float b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * tx;
        return a + (b - a) * ty;
    }

    // Worn metal: grain, brushed streaks along the plate, soft blotches of
    // wear, a few pits and scratches.
    SDL_Texture* metalTexture()
    {
        SDL_Renderer* renderer = muiDrawGetRenderer();
        if (renderer == nullptr) {
            return nullptr;
        }
        if (gMetal != nullptr && gMetalGeneration == muiDrawGeneration()) {
            return gMetal;
        }

        std::vector<uint32_t> pixels(kMetalSize * kMetalSize);
        for (int y = 0; y < kMetalSize; y++) {
            for (int x = 0; x < kMetalSize; x++) {
                float value = 0.80f;
                value += (random01(x, y, 1) - 0.5f) * 0.09f;
                // Faint brushing, mottled wear at two sizes.
                value += (valueNoise(static_cast<float>(x), static_cast<float>(y), 8, 32, 2) - 0.5f) * 0.05f;
                value += (valueNoise(static_cast<float>(x), static_cast<float>(y), 3, 3, 3) - 0.5f) * 0.20f;
                value += (valueNoise(static_cast<float>(x), static_cast<float>(y), 12, 12, 7) - 0.5f) * 0.10f;
                // Pits.
                if (random01(x / 2, y / 2, 4) > 0.996f) {
                    value -= 0.22f;
                }
                // Scratches: short diagonal light lines.
                if (((x + y * 3) % 97 == 0 && random01(x / 16, y / 16, 5) > 0.55f) || ((x * 2 - y + 400) % 131 == 0 && random01(x / 16, y / 16, 6) > 0.6f)) {
                    value += 0.12f;
                }
                int level = static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
                pixels[y * kMetalSize + x] = 0xFF000000u | (level << 16) | (level << 8) | level;
            }
        }

        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, kMetalSize, kMetalSize);
        if (texture == nullptr) {
            return nullptr;
        }
        SDL_UpdateTexture(texture, nullptr, pixels.data(), kMetalSize * 4);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);

        // The old one went with the old renderer's textures.
        gMetal = texture;
        gMetalGeneration = muiDrawGeneration();
        return gMetal;
    }

    // The metal over [rect], tinted [tint].
    void fillMetal(MuiContext& ui, const MuiRect& rect, MuiColor tint)
    {
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            return;
        }

        SDL_Texture* texture = metalTexture();
        if (texture == nullptr) {
            muiFillRect(rect, tint);
            return;
        }

        float tile = std::max(std::round(ui.dp(kMetalScaleDp) * kMetalSize), 32.0f);
        muiPushClip(rect);
        // Tiles from a grid fixed to the screen, so neighbouring plates line
        // up.
        float startX = floorf(rect.x / tile) * tile;
        float startY = floorf(rect.y / tile) * tile;
        for (float y = startY; y < rect.bottom(); y += tile) {
            for (float x = startX; x < rect.right(); x += tile) {
                muiDrawTexture(texture, { x, y, tile, tile }, tint);
            }
        }
        muiPopClip();
    }

} // namespace

void muiSkinBevel(const MuiRect& rect, float thickness, MuiColor light, MuiColor dark, bool inset)
{
    MuiColor topLeft = inset ? dark : light;
    MuiColor bottomRight = inset ? light : dark;
    muiFillRect({ rect.x, rect.y, rect.w, thickness }, topLeft);
    muiFillRect({ rect.x, rect.y + thickness, thickness, rect.h - thickness }, topLeft);
    muiFillRect({ rect.x + thickness, rect.bottom() - thickness, rect.w - thickness, thickness }, bottomRight);
    muiFillRect({ rect.right() - thickness, rect.y + thickness, thickness, rect.h - thickness * 2.0f }, bottomRight);
}

void muiSkinRivet(MuiContext& ui, float x, float y)
{
    float radius = ui.dp(3.2f);
    muiFillCircle(x + ui.dp(0.8f), y + ui.dp(0.8f), radius, muiRgb(0x000000, 150));
    muiFillCircle(x, y, radius, muiRgb(0x6B5A42));
    muiFillCircle(x - radius * 0.3f, y - radius * 0.3f, radius * 0.55f, muiRgb(0xC9B48A, 200));
    muiStrokeCircle(x, y, radius, ui.dp(0.8f), muiRgb(0x1A140D, 220));
}

void muiSkinPlate(MuiContext& ui, const MuiRect& rect, bool rivets)
{
    const MuiTheme& theme = muiTheme();
    fillMetal(ui, rect, theme.metal);
    float bevel = std::max(ui.dp(1.5f), 1.0f);
    muiSkinBevel(rect, bevel, muiRgb(0xE8D5A8, 70), muiRgb(0x000000, 130), false);
    muiSkinBevel({ rect.x - 1.0f, rect.y - 1.0f, rect.w + 2.0f, rect.h + 2.0f }, 1.0f, muiRgb(0x000000, 160), muiRgb(0x000000, 160), false);

    float inset = ui.dp(7.0f);
    if (rivets && rect.w > inset * 5.0f && rect.h > inset * 5.0f) {
        muiSkinRivet(ui, rect.x + inset, rect.y + inset);
        muiSkinRivet(ui, rect.right() - inset, rect.y + inset);
        muiSkinRivet(ui, rect.x + inset, rect.bottom() - inset);
        muiSkinRivet(ui, rect.right() - inset, rect.bottom() - inset);
    }
}

void muiSkinBackdrop(MuiContext& ui, const MuiRect& rect)
{
    const MuiTheme& theme = muiTheme();
    muiFillRect(rect, theme.background);
    fillMetal(ui, rect, theme.metal.withAlpha(235));

    // Seams: the backdrop is bolted together from plates.
    float seamGap = std::max(rect.h / 3.0f, ui.dp(140.0f));
    for (float y = rect.y + seamGap; y < rect.bottom() - ui.dp(40.0f); y += seamGap) {
        muiFillRect({ rect.x, y, rect.w, std::max(ui.dp(1.5f), 1.0f) }, muiRgb(0x000000, 120));
        muiFillRect({ rect.x, y + std::max(ui.dp(1.5f), 1.0f), rect.w, 1.0f }, muiRgb(0xE8D5A8, 40));
    }

    // Darker towards the edges.
    float edge = ui.dp(60.0f);
    MuiColor shade = muiRgb(0x000000, 120);
    MuiColor clear = shade.withAlpha(0);
    muiFillGradient({ rect.x, rect.y, edge, rect.h }, shade, clear, clear, shade);
    muiFillGradient({ rect.right() - edge, rect.y, edge, rect.h }, clear, shade, shade, clear);
    muiFillGradient({ rect.x, rect.y, rect.w, edge }, shade, shade, clear, clear);
    muiFillGradient({ rect.x, rect.bottom() - edge, rect.w, edge }, clear, clear, shade, shade);
}

void muiSkinWell(MuiContext& ui, const MuiRect& rect)
{
    const MuiTheme& theme = muiTheme();
    muiFillRect(rect, theme.well);

    // The rim's shadow on the glass.
    float shadow = ui.dp(7.0f);
    MuiColor dark = muiRgb(0x000000, 150);
    MuiColor clear = dark.withAlpha(0);
    muiFillGradient({ rect.x, rect.y, rect.w, shadow }, dark, dark, clear, clear);
    muiFillGradient({ rect.x, rect.y, shadow, rect.h }, dark, clear, clear, dark);

    // A faint glow of the readouts.
    MuiColor glow = theme.text.withAlpha(14);
    muiFillGradient({ rect.x, rect.y, rect.w, rect.h }, glow, glow, clear, clear);

    float bevel = std::max(ui.dp(1.5f), 1.0f);
    muiSkinBevel(rect, bevel, muiRgb(0xE8D5A8, 60), muiRgb(0x000000, 190), true);
}

void muiSkinButton(MuiContext& ui, const MuiRect& rect, bool pressed, bool primary)
{
    const MuiTheme& theme = muiTheme();
    MuiColor tint = primary ? theme.buttonPrimary : theme.button;
    if (pressed) {
        tint = { static_cast<Uint8>(tint.r * 0.6f), static_cast<Uint8>(tint.g * 0.6f), static_cast<Uint8>(tint.b * 0.6f), tint.a };
    }
    fillMetal(ui, rect, tint);

    float bevel = std::max(ui.dp(2.0f), 1.0f);
    muiSkinBevel(rect, bevel, muiRgb(0xF2E2BC, pressed ? 40 : 95), muiRgb(0x000000, pressed ? 90 : 150), pressed);
    muiSkinBevel({ rect.x - 1.0f, rect.y - 1.0f, rect.w + 2.0f, rect.h + 2.0f }, 1.0f, theme.buttonBorder, theme.buttonBorder, false);
}

} // namespace fallout
