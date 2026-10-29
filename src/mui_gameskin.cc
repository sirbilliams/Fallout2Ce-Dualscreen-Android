#include "mui_gameskin.h"

#include <math.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

#include "color.h"
#include "font_manager.h"
#include "game_charset.h"
#include "settings.h"
#include "svga.h"

namespace fallout {

namespace {

    // ---------------------------------------------------------------- fonts

    constexpr int kFontCount = 5;

    struct GameFont {
        SDL_Texture* texture = nullptr;
        unsigned int generation = 0;
        bool failed = false;
        int maxHeight = 0;
        int letterSpacing = 0;
        int wordSpacing = 0;
        int cellWidth = 0;
        int cellHeight = 0;
        int width[256] = {};
        int height[256] = {};
        SDL_Rect ink[256] = {};
        int digitStroke = 1;
    };

    GameFont gFonts[kFontCount];

    int fontIndexOf(MuiFontRole role)
    {
        switch (role) {
        case MuiFontRole::Label:
            return 1;
        case MuiFontRole::Body:
            return 2;
        case MuiFontRole::Heading:
            return 3;
        case MuiFontRole::Title:
            return 4;
        }
        return 2;
    }

    // The font's glyphs as one white texture whose alpha is the font's
    // anti-aliasing (16 x 16 cells).
    GameFont* loadFont(int index)
    {
        GameFont& font = gFonts[index];
        if (font.texture != nullptr && font.generation == muiDrawGeneration()) {
            return &font;
        }
        if (font.failed && font.generation == muiDrawGeneration()) {
            return nullptr;
        }

        SDL_Renderer* renderer = muiDrawGetRenderer();
        if (renderer == nullptr) {
            return nullptr;
        }

        font.failed = true;
        font.generation = muiDrawGeneration();
        font.texture = nullptr;
        if (!interfaceFontGetMetrics(index, &font.maxHeight, &font.letterSpacing, &font.wordSpacing)) {
            return nullptr;
        }

        int maxWidth = 1;
        for (int ch = 0; ch < 256; ch++) {
            int width;
            int height;
            const unsigned char* pixels;
            font.width[ch] = 0;
            font.height[ch] = 0;
            font.ink[ch] = {};
            if (interfaceFontGetGlyph(index, static_cast<unsigned char>(ch), &width, &height, &pixels)) {
                font.width[ch] = width;
                font.height[ch] = height;
                maxWidth = std::max(maxWidth, width);
            }
        }

        font.cellWidth = maxWidth + 1;
        font.cellHeight = font.maxHeight + 1;
        int atlasWidth = font.cellWidth * 16;
        int atlasHeight = font.cellHeight * 16;
        std::vector<Uint32> atlas(static_cast<size_t>(atlasWidth) * atlasHeight, 0);
        for (int ch = 0; ch < 256; ch++) {
            int width;
            int height;
            const unsigned char* pixels;
            if (!interfaceFontGetGlyph(index, static_cast<unsigned char>(ch), &width, &height, &pixels)) {
                continue;
            }
            int left = width, right = -1, first = height, last = -1;
            for (int row = 0; row < height; row++) {
                int run = 0;
                for (int col = 0; col <= width; col++) {
                    if (col < width && pixels[row * width + col] > 0) {
                        left = std::min(left, col);
                        right = std::max(right, col);
                        first = std::min(first, row);
                        last = std::max(last, row);
                        run++;
                    } else if (run > 0) {
                        if (ch == '0' && row == height / 2) font.digitStroke = run;
                        run = 0;
                    }
                }
            }
            font.ink[ch] = { left, first, std::max(0, right - left + 1), std::max(0, last - first + 1) };
            int originX = (ch % 16) * font.cellWidth;
            int originY = (ch / 16) * font.cellHeight;
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int level = std::min(static_cast<int>(pixels[y * width + x]), 7);
                    Uint32 alpha = static_cast<Uint32>(level * 255 / 7);
                    atlas[static_cast<size_t>(originY + y) * atlasWidth + originX + x] = (alpha << 24) | 0x00FFFFFF;
                }
            }
        }

        font.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, atlasWidth, atlasHeight);
        if (font.texture == nullptr) {
            return nullptr;
        }
        SDL_SetTextureBlendMode(font.texture, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(font.texture, nullptr, atlas.data(), atlasWidth * 4);
        font.failed = false;
        return &font;
    }

    struct TextGlyph {
        GameFont* font;
        unsigned char ch;
        bool pixelDollar = false;
        bool fittedDollar = false;
    };

    bool hasGlyph(const GameFont& font, unsigned char ch)
    {
        return ch != 0 && (ch == ' ' || (font.width[ch] > 0 && font.height[ch] > 0));
    }

    // Resolve once for both measurement and drawing. Only substitute when the
    // active charset and the actual AAF glyph table cannot represent the text.
    void appendGlyph(std::vector<TextGlyph>& out, char32_t cp, GameFont* font)
    {
        unsigned char ch = gameCharsetEncode(cp);
        if (cp == U'$') {
            GameFont* best = nullptr;
            for (int index = 0; index < kFontCount; index++) {
                GameFont* other = loadFont(index);
                if (other != nullptr && other->ink['$'].h >= 5 && other->ink['$'].w >= 3
                    && (best == nullptr || other->ink['$'].h > best->ink['$'].h)) {
                    best = other;
                }
            }
            out.push_back({ best != nullptr ? best : font, '$', best == nullptr, true });
            return;
        }
        if (hasGlyph(*font, ch)) {
            out.push_back({ font, ch });
            return;
        }
        const char32_t* replacement = nullptr;
        switch (cp) {
        case U'\u2212':
        case U'\u2010':
        case U'\u2011':
        case U'\u2012':
        case U'\u2013':
        case U'\u2014':
        case U'\u00ad':
            replacement = U"-";
            break;
        case U'\u2018':
        case U'\u2019':
        case U'\u201a':
        case U'\u2032':
            replacement = U"'";
            break;
        case U'\u201c':
        case U'\u201d':
        case U'\u201e':
        case U'\u2033':
            replacement = U"\"";
            break;
        case U'\u2026':
            replacement = U"...";
            break;
        case U'\u00b7':
        case U'\u2022':
        case U'\u2219':
            replacement = U"-";
            break;
        case U'\u00a0':
        case U'\u2007':
        case U'\u202f':
            replacement = U" ";
            break;
        case U'\u2039':
        case U'\u00ab':
        case U'\u2190':
            replacement = U"<";
            break;
        case U'\u203a':
        case U'\u00bb':
        case U'\u2192':
            replacement = U">";
            break;
        case U'\u00d7':
            replacement = U"x";
            break;
        default:
            break;
        }
        if (replacement != nullptr) {
            for (; *replacement != 0; replacement++) {
                appendGlyph(out, *replacement, font);
            }
            return;
        }
        if (cp >= U'a' && cp <= U'z' && hasGlyph(*font, cp - 32)) {
            ch = static_cast<unsigned char>(cp - 32);
        } else if (cp >= U'A' && cp <= U'Z' && hasGlyph(*font, cp + 32)) {
            ch = static_cast<unsigned char>(cp + 32);
        } else {
            ch = hasGlyph(*font, '?') ? '?' : ' ';
        }
        out.push_back({ font, ch });
    }

    std::vector<TextGlyph> resolveText(const std::u32string& text, GameFont* font)
    {
        std::vector<TextGlyph> glyphs;
        for (char32_t cp : text) {
            appendGlyph(glyphs, cp, font);
        }
        return glyphs;
    }

    float advanceOf(unsigned char ch, const GameFont& font)
    {
        if (ch == ' ') {
            return static_cast<float>(font.wordSpacing + font.letterSpacing);
        }
        return static_cast<float>(font.width[ch] + font.letterSpacing);
    }

    SDL_Rect digitInk(const GameFont& font)
    {
        SDL_Rect ink = font.ink['0'];
        if (ink.h == 0) ink = font.ink['S'];
        if (ink.h == 0) ink = { 0, 0, std::max(5, font.maxHeight / 2), font.maxHeight };
        return ink;
    }

    float glyphAdvance(const TextGlyph& glyph, const GameFont& font)
    {
        return glyph.fittedDollar ? (font.width['0'] > 0 ? font.width['0'] : digitInk(font).w) + font.letterSpacing : advanceOf(glyph.ch, *glyph.font);
    }

    struct Pick {
        int font;
        float scale;
    };

    // The face and scale for a size (`muiDrawText`'s output pixels): small
    // sizes - the spaced font 1, the rest - the condensed font 2.
    float bottomTextScale() { return muiCurrentSurface() == MuiSurface::Bottom ? settings.touch.bottom_text_size / 100.0f : 1.0f; }

    Pick pickForSize(float size)
    {
        float height = size * 1.05f;
        if (size < 26.0f) {
            return { 1, std::clamp(floorf(height / 9.0f + 0.3f), 2.0f, 3.0f) };
        }
        return { 2, std::clamp(roundf(height / 24.0f * 4.0f) / 4.0f, 1.0f, 4.0f) };
    }

    float textWidth(const std::u32string& text, int fontIndex, float scale)
    {
        GameFont* font = loadFont(fontIndex);
        if (font == nullptr) {
            return 0.0f;
        }
        float width = 0.0f;
        for (const TextGlyph& glyph : resolveText(text, font)) {
            width += glyphAdvance(glyph, *font);
        }
        return width * scale;
    }

    void drawText(const std::u32string& text, float x, float y, int fontIndex, float scale, MuiColor color, bool shadow)
    {
        GameFont* font = loadFont(fontIndex);
        SDL_Renderer* renderer = muiDrawGetRenderer();
        if (font == nullptr || renderer == nullptr || text.empty()) {
            return;
        }

        const auto glyphs = resolveText(text, font);
        float offset = std::max(1.0f, roundf(scale * 0.8f));
        for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
            MuiColor ink = pass == 0 ? MuiColor { 0, 0, 0, static_cast<Uint8>(color.a * 0.8f) } : color;
            float pen = roundf(x) + (pass == 0 ? offset : 0.0f);
            float top = roundf(y) + (pass == 0 ? offset : 0.0f);
            for (const TextGlyph& glyph : glyphs) {
                GameFont& face = *glyph.font;
                unsigned char ch = glyph.ch;
                if (glyph.fittedDollar) {
                    SDL_Rect digit = digitInk(*font);
                    SDL_FRect dest = { roundf(pen),
                        roundf(top + (font->maxHeight - font->height['0'] + digit.y) * scale),
                        roundf(digit.w * scale), roundf(digit.h * scale) };
                    unsigned char shape = glyph.pixelDollar ? 'S' : '$';
                    SDL_Rect source = face.ink[shape];
                    source.x += (shape % 16) * face.cellWidth;
                    source.y += (shape / 16) * face.cellHeight;
                    SDL_SetTextureScaleMode(face.texture, SDL_ScaleModeNearest);
                    SDL_SetTextureColorMod(face.texture, ink.r, ink.g, ink.b);
                    SDL_SetTextureAlphaMod(face.texture, ink.a);
                    SDL_RenderCopyF(renderer, face.texture, &source, &dest);
                    if (glyph.pixelDollar) {
                        // Reuse the face's S and digit stroke weight at the digit baseline.
                        float stroke = std::max(1.0f, roundf(font->digitStroke * scale));
                        SDL_FRect bar = { roundf(dest.x + (dest.w - stroke) / 2), dest.y, stroke, dest.h };
                        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                        SDL_SetRenderDrawColor(renderer, ink.r, ink.g, ink.b, ink.a);
                        SDL_RenderFillRectF(renderer, &bar);
                    }
                } else if (face.width[ch] > 0 && ch != ' ') {
                    SDL_SetTextureScaleMode(face.texture, scale == floorf(scale) ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
                    SDL_SetTextureColorMod(face.texture, ink.r, ink.g, ink.b);
                    SDL_SetTextureAlphaMod(face.texture, ink.a);
                    SDL_Rect source = { (ch % 16) * face.cellWidth, (ch / 16) * face.cellHeight, face.width[ch], face.height[ch] };
                    SDL_FRect dest = {
                        roundf(pen),
                        roundf(top + (font->maxHeight - face.height[ch]) * scale),
                        roundf(face.width[ch] * scale),
                        roundf(face.height[ch] * scale),
                    };
                    SDL_RenderCopyF(renderer, face.texture, &source, &dest);
                }
                pen += glyphAdvance(glyph, *font) * scale;
            }
        }
    }

    // --------------------------------------------------------------- art

    struct ArtEntry {
        std::unique_ptr<FrmImage> image;
        bool missing = false;
    };

    std::map<std::string, ArtEntry> gArt;

    struct InkEntry {
        SDL_Texture* texture = nullptr;
        unsigned int generation = 0;
        int width = 0;
        int height = 0;
        bool missing = false;
    };

    std::map<std::string, InkEntry> gInk;

    ObjectType objectTypeOf(MuiArtGroup group)
    {
        switch (group) {
        case MuiArtGroup::Interface:
            return OBJ_TYPE_INTERFACE;
        case MuiArtGroup::Skilldex:
            return OBJ_TYPE_SKILLDEX;
        case MuiArtGroup::Inventory:
            return OBJ_TYPE_INVENTORY;
        }
        return OBJ_TYPE_INTERFACE;
    }

    std::string artKey(MuiArtGroup group, const char* name)
    {
        return std::to_string(static_cast<int>(group)) + "/" + name;
    }

    FrmImage* artImage(MuiArtGroup group, const char* name)
    {
        ArtEntry& entry = gArt[artKey(group, name)];
        if (entry.missing) {
            return nullptr;
        }
        if (entry.image == nullptr) {
            entry.image = std::make_unique<FrmImage>();
            std::string file = std::string(name) + ".frm";
            if (!entry.image->lock(objectTypeOf(group), file.c_str()) || entry.image->getData() == nullptr) {
                entry.image.reset();
                entry.missing = true;
                return nullptr;
            }
        }
        return entry.image.get();
    }

    SDL_Texture* artTexture(MuiArtGroup group, const char* name, int* width, int* height)
    {
        FrmImage* image = artImage(group, name);
        if (image == nullptr) {
            return nullptr;
        }
        *width = image->getWidth();
        *height = image->getHeight();
        return muiIndexedTexture("skin." + artKey(group, name), image->getData(), *width, *height, 0);
    }

    // ------------------------------------------------------------- metal

    // The game's worn metal (the character editor's background), a quarter of
    // it mirrored on both axes so that it tiles without seams.
    constexpr int kMetalSourceWidth = 256;
    constexpr int kMetalSourceHeight = 200;

    SDL_Texture* metalTexture(int* width, int* height)
    {
        static std::vector<unsigned char> pixels;
        FrmImage* image = artImage(MuiArtGroup::Interface, "ceditbkg");
        if (image == nullptr || image->getWidth() < kMetalSourceWidth || image->getHeight() < kMetalSourceHeight) {
            return nullptr;
        }

        int outWidth = kMetalSourceWidth * 2;
        int outHeight = kMetalSourceHeight * 2;
        if (pixels.empty()) {
            pixels.resize(static_cast<size_t>(outWidth) * outHeight);
            for (int y = 0; y < outHeight; y++) {
                int sourceY = y < kMetalSourceHeight ? y : outHeight - 1 - y;
                for (int x = 0; x < outWidth; x++) {
                    int sourceX = x < kMetalSourceWidth ? x : outWidth - 1 - x;
                    unsigned char value = image->getData()[sourceY * image->getWidth() + sourceX];
                    pixels[static_cast<size_t>(y) * outWidth + x] = value == 0 ? 1 : value;
                }
            }
        }

        *width = outWidth;
        *height = outHeight;
        return muiIndexedTexture("skin.metal", pixels.data(), outWidth, outHeight, 0);
    }

    void fillMetal(const MuiRect& rect, MuiColor tint, float scale)
    {
        if (rect.w <= 0.0f || rect.h <= 0.0f) {
            return;
        }

        int width;
        int height;
        SDL_Texture* texture = metalTexture(&width, &height);
        if (texture == nullptr) {
            muiFillRect(rect, tint);
            return;
        }

        float tileW = width * scale;
        float tileH = height * scale;
        muiPushClip(rect);
        float startX = floorf(rect.x / tileW) * tileW;
        float startY = floorf(rect.y / tileH) * tileH;
        for (float y = startY; y < rect.bottom(); y += tileH) {
            for (float x = startX; x < rect.right(); x += tileW) {
                muiDrawTexture(texture, { x, y, tileW, tileH }, tint);
            }
        }
        muiPopClip();
    }

    // Chunky pixels of the game's scale (one source pixel of art is this
    // many output pixels).
    float artScale(MuiContext& ui)
    {
        (void)ui;
        return 2.0f;
    }

    // ------------------------------------------------------- pixel icons

    struct PixelIcon {
        const char* rows[12];
    };

    // '#' - set pixel. Pixel art in the style of the game's own little
    // symbols, for the things the game has no art of.
    const PixelIcon kPixelMenu = { { "#########", "#########", ".........", "#########", "#########", ".........", "#########", "#########", nullptr } };
    const PixelIcon kPixelBack = { { "....#....", "...##....", "..###....", ".########", "#########", ".########", "..###....", "...##....", "....#...." } };
    const PixelIcon kPixelPlay = { { "#........", "###......", "#####....", "#######..", "#########", "#######..", "#####....", "###......", "#........" } };
    const PixelIcon kPixelPause = { { "###...###", "###...###", "###...###", "###...###", "###...###", "###...###", "###...###", "###...###", nullptr } };
    const PixelIcon kPixelStop = { { "#########", "#########", "#########", "#########", "#########", "#########", "#########", "#########", "#########" } };
    const PixelIcon kPixelExpand = { { "####...####", "##.......##", "#.#.....#.#", "....#.#....", ".....#.....", "....#.#....", "#.#.....#.#", "##.......##", "####...####", nullptr } };
    const PixelIcon kPixelCollapse = { { "....###....", "....###....", "#....#....#", ".#########.", "...........", ".#########.", "#....#....#", "....###....", "....###....", nullptr } };
    const PixelIcon kPixelCancel = { { "##.....##", ".##...##.", "..##.##..", "...###...", "....#....", "...###...", "..##.##..", ".##...##.", "##.....##" } };
    const PixelIcon kPixelEdit = { { "......###", ".....####", "....###.#", "...###.#.", "..###.#..", ".###.#...", "####.....", "###......", "##.......", nullptr } };
    const PixelIcon kPixelEye = { { "...#####...", ".##.....##.", "#...###...#", "#..#####..#", "#...###...#", ".##.....##.", "...#####...", nullptr } };
    const PixelIcon kPixelSwap = { { "......#....", ".......#...", "###########", ".......#...", "......#....", "...........", "....#......", "...#.......", "###########", "...#.......", "....#......" } };
    const PixelIcon kPixelSpread = { { "##.....##", "##.....##", ".........", "....#....", "...###...", "....#....", ".........", "##.....##", "##.....##" } };
    const PixelIcon kPixelRegroup = { { ".........", "..#...#..", "...#.#...", "....#....", "...#.#...", "..#...#..", ".........", nullptr } };
    const PixelIcon kPixelTarget = { { "....#....", "..#####..", ".#..#..#.", "#...#...#", "#########", "#...#...#", ".#..#..#.", "..#####..", "....#...." } };
    const PixelIcon kPixelTiles = { { "###.###.###", "###.###.###", "###.###.###", nullptr } };

    void drawPixelIcon(const PixelIcon& icon, float centerX, float centerY, float size, MuiColor color)
    {
        int rowCount = 0;
        int columnCount = 0;
        while (rowCount < 12 && icon.rows[rowCount] != nullptr) {
            columnCount = std::max(columnCount, static_cast<int>(strlen(icon.rows[rowCount])));
            rowCount++;
        }
        if (rowCount == 0) {
            return;
        }

        float pixel = std::max(1.0f, floorf(size * 0.8f / std::max(columnCount, rowCount)));
        float left = roundf(centerX - columnCount * pixel / 2.0f);
        float top = roundf(centerY - rowCount * pixel / 2.0f);
        // Black under it, as every symbol of the game has.
        for (int pass = 0; pass < 2; pass++) {
            float shift = pass == 0 ? std::max(1.0f, floorf(pixel / 2.0f)) : 0.0f;
            MuiColor tint = pass == 0 ? muiRgb(0x000000, static_cast<Uint8>(color.a * 0.75f)) : color;
            for (int y = 0; y < rowCount; y++) {
                for (int x = 0; icon.rows[y][x] != '\0'; x++) {
                    if (icon.rows[y][x] == '#') {
                        muiFillRect({ left + x * pixel + shift, top + y * pixel + shift, pixel, pixel }, tint);
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------- icons

    enum class IconKind {
        None,
        // Game art: [name] (up), [lit] (pressed or chosen).
        Interface,
        Ink,
        Item,
        Pixel,
        // Lettering in the game's heavy capitals ([name] is the word).
        Text,
    };

    struct IconSpec {
        IconKind kind = IconKind::None;
        const char* name = nullptr;
        const char* lit = nullptr;
        const PixelIcon* pixel = nullptr;
    };

    IconSpec specOf(MuiIcon icon)
    {
        auto iface = [](const char* name, const char* lit = nullptr) { return IconSpec { IconKind::Interface, name, lit, nullptr }; };
        auto ink = [](const char* name) { return IconSpec { IconKind::Ink, name, nullptr, nullptr }; };
        auto item = [](const char* name) { return IconSpec { IconKind::Item, name, nullptr, nullptr }; };
        auto pixel = [](const PixelIcon* pixel) { return IconSpec { IconKind::Pixel, nullptr, nullptr, pixel }; };
        auto word = [](const char* name) { return IconSpec { IconKind::Text, name, nullptr, nullptr }; };

        switch (icon) {
        case MuiIcon::Menu:
            return word("MENU");
        case MuiIcon::QuickSave:
            return word("SAVE");
        case MuiIcon::QuickLoad:
            return word("LOAD");
        case MuiIcon::Inventory:
            return iface("invbutup", "invbutdn");
        case MuiIcon::Character:
            return iface("chaup", "chadn");
        case MuiIcon::Pipboy:
            return iface("pipup", "pipdn");
        case MuiIcon::Map:
            return iface("mapup", "mapdn");
        case MuiIcon::Skills:
            return ink("skills");
        case MuiIcon::Highlight:
            return pixel(&kPixelEye);
        case MuiIcon::Sneak:
            return ink("sneak");
        case MuiIcon::Log:
            return item("clipbrd");
        case MuiIcon::EndTurn:
            return iface("endturnu", "endturnd");
        case MuiIcon::EndCombat:
            return iface("endcmbtu", "endcmbtd");
        case MuiIcon::SwapHands:
            return pixel(&kPixelSwap);
        case MuiIcon::ActionPoints:
            return ink("actionpt");
        case MuiIcon::HitPoints:
            return ink("hitpoint");
        case MuiIcon::Aim:
            return pixel(&kPixelTarget);
        case MuiIcon::Reload:
            return item("10mmap");
        case MuiIcon::Fist:
            return ink("unarmed");
        case MuiIcon::Weight:
            return ink("carryamt");
        case MuiIcon::Back:
            return pixel(&kPixelBack);
        case MuiIcon::Turn:
            return iface("rotaten", "rotateh");
        case MuiIcon::PartyOrders:
            return ink("frienfoe");
        case MuiIcon::Holster:
            return item("pistol");
        case MuiIcon::Regroup:
            return pixel(&kPixelRegroup);
        case MuiIcon::Spread:
            return pixel(&kPixelSpread);
        case MuiIcon::Burst:
            return item("bullet");
        case MuiIcon::AutoLoot:
            return item("bag");
        case MuiIcon::Lockpick:
            return ink("lockpick");
        case MuiIcon::Steal:
            return ink("steal");
        case MuiIcon::Traps:
            return ink("traps");
        case MuiIcon::Doctor:
            return ink("doctor");
        case MuiIcon::Science:
            return ink("science");
        case MuiIcon::Repair:
            return ink("repair");
        case MuiIcon::Target:
            return pixel(&kPixelTarget);
        case MuiIcon::Talk:
            return ink("speech");
        case MuiIcon::Barter:
            return ink("barter");
        case MuiIcon::Review:
            return item("clipbrd");
        case MuiIcon::FilterAll:
            return item("pack2c");
        case MuiIcon::FilterWeapons:
            return item("pistol");
        case MuiIcon::FilterWeaponsAndAmmo:
            return item("magnum");
        case MuiIcon::FilterArmor:
            return item("ltharmor");
        case MuiIcon::FilterDrugs:
            return item("hypo01");
        case MuiIcon::FilterAmmo:
            return item("10mmap");
        case MuiIcon::FilterMisc:
            return item("key");
        case MuiIcon::Cancel:
            return iface("canceln", "cancelh");
        case MuiIcon::Drop:
            return iface("dropn", "droph");
        case MuiIcon::Look:
            return iface("lookn", "lookh");
        case MuiIcon::Rotate:
            return iface("rotaten", "rotateh");
        case MuiIcon::Use:
            return iface("usegetn", "usegeth");
        case MuiIcon::Unload:
            return iface("unloadn", "unloadh");
        case MuiIcon::Push:
            return iface("pushn", "pushh");
        case MuiIcon::Plus:
            return iface("splsoff", "splson");
        case MuiIcon::Minus:
            return iface("snegoff", "snegon");
        case MuiIcon::Edit:
            return pixel(&kPixelEdit);
        case MuiIcon::Play:
            return pixel(&kPixelPlay);
        case MuiIcon::Pause:
            return pixel(&kPixelPause);
        case MuiIcon::Stop:
            return pixel(&kPixelStop);
        case MuiIcon::Expand:
            return pixel(&kPixelExpand);
        case MuiIcon::Collapse:
            return pixel(&kPixelCollapse);
        case MuiIcon::TacticalView:
            return pixel(&kPixelTiles);
        default:
            break;
        }
        return {};
    }

    // Amber (lit) icon colors choose the pressed art.
    bool isLit(MuiColor color)
    {
        return color.r >= 220 && color.g >= 150 && color.b <= 140;
    }

    // Art as it is, dimmed when the icon's color says it is not available.
    MuiColor iconTint(MuiColor color)
    {
        if (color.r < 0x60 && color.g < 0x60) {
            return muiRgb(0x9A9A9A, color.a);
        }
        return muiRgb(0xFFFFFF, color.a);
    }

    // [width] x [height] art centered at ([x], [y]) at the largest whole
    // scale that stays within [limit] of the box (at least 1).
    MuiRect fitArt(float x, float y, int width, int height, float box, float limit)
    {
        float scale = std::max(1.0f, floorf(std::min(box * limit / width, box * limit / height) + 0.5f));
        float w = width * scale;
        float h = height * scale;
        return { roundf(x - w / 2.0f), roundf(y - h / 2.0f), w, h };
    }

} // namespace

bool muiSkinFallout()
{
    return muiBottomActive() && muiCurrentSurface() == MuiSurface::Bottom && settings.touch.bottom_theme == 0;
}

float muiGameTextWidth(const std::u32string& text, float size)
{
    Pick pick = pickForSize(size);
    pick.scale *= bottomTextScale();
    return textWidth(text, pick.font, pick.scale);
}

float muiGameLineHeight(float size)
{
    Pick pick = pickForSize(size);
    pick.scale *= bottomTextScale();
    GameFont* font = loadFont(pick.font);
    return font != nullptr ? (font->maxHeight + 2) * pick.scale : size * 1.2f;
}

void muiGameDrawText(const std::u32string& text, float x, float y, float size, MuiColor color)
{
    Pick pick = pickForSize(size);
    pick.scale *= bottomTextScale();
    drawText(text, x, y, pick.font, pick.scale, color, true);
}

float muiGameRoleWidth(const std::u32string& text, MuiFontRole role, float scale)
{
    return textWidth(text, fontIndexOf(role), scale * bottomTextScale());
}

float muiGameRoleHeight(MuiFontRole role, float scale)
{
    GameFont* font = loadFont(fontIndexOf(role));
    return font != nullptr ? font->maxHeight * scale * bottomTextScale() : 0.0f;
}

void muiGameDrawRole(const std::u32string& text, float x, float y, MuiFontRole role, float scale, MuiColor color)
{
    drawText(text, x, y, fontIndexOf(role), scale * bottomTextScale(), color, true);
}

float muiGameFitScale(const std::u32string& text, MuiFontRole role, float width, float maxScale)
{
    float scale = maxScale;
    while (scale > 1.0f && muiGameRoleWidth(text, role, scale) > width) {
        scale -= 0.25f;
    }
    return std::max(scale, 1.0f);
}

bool muiGameArtSize(MuiArtGroup group, const char* name, int* width, int* height)
{
    FrmImage* image = artImage(group, name);
    if (image == nullptr) {
        return false;
    }
    *width = image->getWidth();
    *height = image->getHeight();
    return true;
}

bool muiGameDrawArt(MuiArtGroup group, const char* name, const MuiRect& dest, MuiColor tint)
{
    int width;
    int height;
    SDL_Texture* texture = artTexture(group, name, &width, &height);
    if (texture == nullptr) {
        return false;
    }
    bool whole = dest.w == floorf(dest.w) && std::fmod(dest.w, static_cast<float>(width)) == 0.0f;
    SDL_SetTextureScaleMode(texture, whole ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
    muiDrawTexture(texture, dest, tint);
    return true;
}

bool muiGameDrawInk(MuiArtGroup group, const char* name, const MuiRect& dest, MuiColor tint)
{
    std::string key = artKey(group, name);
    InkEntry& entry = gInk[key];
    if (entry.missing && entry.generation == muiDrawGeneration()) {
        return false;
    }

    SDL_Renderer* renderer = muiDrawGetRenderer();
    if (renderer == nullptr) {
        return false;
    }

    if (entry.texture == nullptr || entry.generation != muiDrawGeneration()) {
        entry.texture = nullptr;
        entry.generation = muiDrawGeneration();
        FrmImage* image = artImage(group, name);
        if (image == nullptr) {
            entry.missing = true;
            return false;
        }

        // Ink is what is much darker than the paper, by the game's color
        // table.
        constexpr float kInk = 27.0f;
        constexpr float kPaper = 72.0f;
        Uint32 colors[256];
        colors[0] = 0;
        for (int index = 1; index < 256; index++) {
            float r = _cmap[index * 3] * 4.0f;
            float g = _cmap[index * 3 + 1] * 4.0f;
            float b = _cmap[index * 3 + 2] * 4.0f;
            float luminance = 0.299f * r + 0.587f * g + 0.114f * b;
            float alpha = std::clamp((kPaper - luminance) / (kPaper - kInk), 0.0f, 1.0f);
            colors[index] = (static_cast<Uint32>(alpha * 255.0f + 0.5f) << 24) | 0x00FFFFFF;
        }

        int width = image->getWidth();
        int height = image->getHeight();
        std::vector<Uint32> pixels(static_cast<size_t>(width) * height);
        for (size_t index = 0; index < pixels.size(); index++) {
            pixels[index] = colors[image->getData()[index]];
        }
        entry.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, width, height);
        if (entry.texture == nullptr) {
            entry.missing = true;
            return false;
        }
        SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(entry.texture, SDL_ScaleModeLinear);
        SDL_UpdateTexture(entry.texture, nullptr, pixels.data(), width * 4);
        entry.width = width;
        entry.height = height;
        entry.missing = false;
    }

    muiDrawTexture(entry.texture, dest, tint);
    return true;
}

bool muiGameDrawNineSlice(const char* name, const MuiRect& dest, int corner, float scale, MuiColor tint)
{
    int width;
    int height;
    SDL_Texture* texture = artTexture(MuiArtGroup::Interface, name, &width, &height);
    SDL_Renderer* renderer = muiDrawGetRenderer();
    if (texture == nullptr || renderer == nullptr || dest.w <= 0.0f || dest.h <= 0.0f) {
        return false;
    }

    // Small buttons: smaller corners rather than overlapping ones.
    float edge = corner * scale;
    while (scale > 1.0f && (edge * 2.0f > dest.w || edge * 2.0f > dest.h)) {
        scale -= 1.0f;
        edge = corner * scale;
    }
    edge = std::min({ edge, dest.w / 2.0f, dest.h / 2.0f });

    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    SDL_SetTextureColorMod(texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texture, tint.a);

    float xs[4] = { dest.x, dest.x + edge, dest.right() - edge, dest.right() };
    float ys[4] = { dest.y, dest.y + edge, dest.bottom() - edge, dest.bottom() };
    int sx[4] = { 0, corner, width - corner, width };
    int sy[4] = { 0, corner, height - corner, height };
    for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
            SDL_Rect source = { sx[column], sy[row], sx[column + 1] - sx[column], sy[row + 1] - sy[row] };
            SDL_FRect target = { xs[column], ys[row], xs[column + 1] - xs[column], ys[row + 1] - ys[row] };
            if (source.w > 0 && source.h > 0 && target.w > 0.0f && target.h > 0.0f) {
                SDL_RenderCopyF(renderer, texture, &source, &target);
            }
        }
    }
    return true;
}

bool muiGameDrawIcon(MuiIcon icon, float x, float y, float size, MuiColor color)
{
    IconSpec spec = specOf(icon);
    switch (spec.kind) {
    case IconKind::Interface: {
        const char* name = spec.lit != nullptr && isLit(color) ? spec.lit : spec.name;
        int width;
        int height;
        if (!muiGameArtSize(MuiArtGroup::Interface, name, &width, &height)) {
            return false;
        }
        MuiRect rect = fitArt(x, y, width, height, size, 1.35f);
        return muiGameDrawArt(MuiArtGroup::Interface, name, rect, iconTint(color));
    }
    case IconKind::Ink: {
        // The vault boy drawings, on their paper, 140 x 117.
        float width = size * 1.5f;
        float height = width * 117.0f / 140.0f;
        MuiRect rect = { roundf(x - width / 2.0f), roundf(y - height / 2.0f), roundf(width), roundf(height) };
        bool drawn = muiGameDrawArt(MuiArtGroup::Skilldex, spec.name, rect, iconTint(color));
        if (drawn) {
            muiSkinBevel(rect, 2.0f, muiRgb(0x000000, 140), muiRgb(0x000000, 140), false);
        }
        return drawn;
    }
    case IconKind::Item: {
        int width;
        int height;
        if (!muiGameArtSize(MuiArtGroup::Inventory, spec.name, &width, &height)) {
            return false;
        }
        float fit = std::min(size * 1.9f / width, size * 1.9f / height);
        float w = width * fit;
        float h = height * fit;
        return muiGameDrawArt(MuiArtGroup::Inventory, spec.name, { roundf(x - w / 2.0f), roundf(y - h / 2.0f), roundf(w), roundf(h) }, iconTint(color));
    }
    case IconKind::Text: {
        std::u32string word;
        for (const char* ch = spec.name; *ch != '\0'; ch++) {
            word.push_back(static_cast<char32_t>(*ch));
        }
        float scale = muiGameFitScale(word, MuiFontRole::Heading, size * 1.7f, 2.0f);
        float w = muiGameRoleWidth(word, MuiFontRole::Heading, scale);
        float h = muiGameRoleHeight(MuiFontRole::Heading, scale);
        MuiColor lettering = isLit(color) ? muiRgb(0xFFE890) : muiRgb(0xD8BE4A, color.a);
        muiGameDrawRole(word, x - w / 2.0f, y - h / 2.0f, MuiFontRole::Heading, scale, lettering);
        return true;
    }
    case IconKind::Pixel:
        drawPixelIcon(*spec.pixel, x, y, size, color);
        return true;
    case IconKind::None:
        break;
    }
    return false;
}

// -------------------------------------------------------------- surfaces

namespace {
    void pixelBevel(const MuiRect& rect, float t, MuiColor light, MuiColor dark, bool inset)
    {
        muiSkinBevel(rect, t, light, dark, inset);
    }
} // namespace

void muiGameBackdrop(MuiContext& ui, const MuiRect& rect)
{
    const MuiTheme& theme = muiTheme();
    muiFillRect(rect, theme.background);
    fillMetal(rect, muiRgb(0xE6DFD0), artScale(ui));

    float seam = 2.0f * artScale(ui);
    float seamGap = std::max(rect.h / 3.0f, ui.dp(140.0f));
    for (float y = rect.y + seamGap; y < rect.bottom() - ui.dp(40.0f); y += seamGap) {
        muiFillRect({ rect.x, y, rect.w, seam }, muiRgb(0x000000, 150));
        muiFillRect({ rect.x, y + seam, rect.w, 2.0f }, muiRgb(0xE8D5A8, 36));
    }

    float edge = ui.dp(60.0f);
    MuiColor shade = muiRgb(0x000000, 130);
    MuiColor clear = shade.withAlpha(0);
    muiFillGradient({ rect.x, rect.y, edge, rect.h }, shade, clear, clear, shade);
    muiFillGradient({ rect.right() - edge, rect.y, edge, rect.h }, clear, shade, shade, clear);
    muiFillGradient({ rect.x, rect.y, rect.w, edge }, shade, shade, clear, clear);
    muiFillGradient({ rect.x, rect.bottom() - edge, rect.w, edge }, clear, clear, shade, shade);
}

void muiGamePlate(MuiContext& ui, const MuiRect& rect, bool rivets)
{
    float pixel = artScale(ui);
    fillMetal(rect, muiRgb(0xFFF8E8), pixel);
    // The game's plates: a black outline, a lit edge top and left, a dark
    // one at the bottom and right.
    pixelBevel(rect, pixel, muiRgb(0xE8D5A8, 70), muiRgb(0x000000, 150), false);
    pixelBevel({ rect.x - pixel, rect.y - pixel, rect.w + pixel * 2.0f, rect.h + pixel * 2.0f }, pixel, muiRgb(0x000000, 200), muiRgb(0x000000, 200), false);

    float inset = pixel * 5.0f;
    if (rivets && rect.w > inset * 5.0f && rect.h > inset * 5.0f) {
        for (float rx : { rect.x + inset, rect.right() - inset }) {
            for (float ry : { rect.y + inset, rect.bottom() - inset }) {
                // A square-pixel rivet: dark seat, brass head, a bright pixel.
                muiFillRect({ rx - pixel * 2.0f, ry - pixel * 1.0f, pixel * 4.0f, pixel * 3.0f }, muiRgb(0x000000, 170));
                muiFillRect({ rx - pixel * 2.0f, ry - pixel * 2.0f, pixel * 4.0f, pixel * 3.0f }, muiRgb(0x7A6A48));
                muiFillRect({ rx - pixel * 1.0f, ry - pixel * 2.0f, pixel * 2.0f, pixel }, muiRgb(0xD8C690));
            }
        }
    }
}

void muiGameWell(MuiContext& ui, const MuiRect& rect)
{
    const MuiTheme& theme = muiTheme();
    float pixel = artScale(ui);
    muiFillRect(rect, theme.well);

    if (rect.w >= ui.dp(80.0f) && rect.h >= ui.dp(60.0f)) {
        // The CRT: scanlines and a soft glow of the readouts.
        muiPushClip(rect);
        MuiColor glow = theme.text.withAlpha(12);
        muiFillGradient(rect, glow, glow, glow.withAlpha(0), glow.withAlpha(0));
        MuiColor line = muiRgb(0x000000, 46);
        for (float y = rect.y; y < rect.bottom(); y += pixel * 2.0f) {
            muiFillRect({ rect.x, y, rect.w, pixel / 2.0f }, line);
        }
        float shadow = pixel * 6.0f;
        MuiColor dark = muiRgb(0x000000, 150);
        MuiColor clear = dark.withAlpha(0);
        muiFillGradient({ rect.x, rect.y, rect.w, shadow }, dark, dark, clear, clear);
        muiFillGradient({ rect.x, rect.y, shadow, rect.h }, dark, clear, clear, dark);
        muiPopClip();
    }

    // A rim of dark metal with a lit lip at the bottom and right.
    pixelBevel(rect, pixel, muiRgb(0xE8D5A8, 70), muiRgb(0x000000, 220), true);
    pixelBevel({ rect.x - pixel, rect.y - pixel, rect.w + pixel * 2.0f, rect.h + pixel * 2.0f }, pixel, muiRgb(0x000000, 190), muiRgb(0x000000, 190), false);
}

void muiGameButton(MuiContext& ui, const MuiRect& rect, bool pressed, bool primary)
{
    MuiColor tint = primary ? muiRgb(0xFF9A86) : muiRgb(0xFFFFFF);
    if (!muiGameDrawNineSlice(pressed ? "skldxon" : "skldxoff", rect, 5, artScale(ui), tint)) {
        muiFillRect(rect, primary ? muiTheme().buttonPrimary : muiTheme().button);
        return;
    }
    if (pressed) {
        muiFillRect(rect.inset(artScale(ui) * 2.0f), muiRgb(0x000000, 60));
    }
}

void muiGameNamePlate(MuiContext& ui, float x, float y, const std::u32string& text)
{
    float scale = 1.0f;
    float pixel = artScale(ui);
    float textW = muiGameRoleWidth(text, MuiFontRole::Label, 2.0f);
    float height = ui.dp(19.0f);
    float width = textW + pixel * 12.0f;
    MuiRect plate = { x - width / 2.0f, y - height / 2.0f, width, height };
    muiGameDrawNineSlice("skldxoff", plate, 5, scale * pixel);
    muiGameDrawRole(text, plate.x + (plate.w - textW) / 2.0f, plate.y + (plate.h - muiGameRoleHeight(MuiFontRole::Label, 2.0f)) / 2.0f, MuiFontRole::Label, 2.0f, muiRgb(0xC9A94A));
}

void muiGameButtonLabel(MuiContext& ui, const MuiRect& rect, const std::u32string& label, MuiColor color)
{
    float room = rect.w - ui.dp(14.0f);
    float maxScale = std::clamp(floorf((rect.h * 0.55f) / 16.0f * 4.0f) / 4.0f, 1.0f, 2.5f);
    float scale = muiGameFitScale(label, MuiFontRole::Heading, room, maxScale);
    float width = muiGameRoleWidth(label, MuiFontRole::Heading, scale);
    float height = muiGameRoleHeight(MuiFontRole::Heading, scale);
    muiGameDrawRole(label, rect.x + (rect.w - width) / 2.0f, rect.y + (rect.h - height) / 2.0f, MuiFontRole::Heading, scale, color);
}

void muiGameLamp(MuiContext& ui, const MuiRect& rect, bool on, bool pressed)
{
    (void)pressed;
    float size = std::min(rect.h, ui.dp(34.0f));
    float scale = std::max(1.0f, floorf(size / 34.0f * 1.0f + 0.5f));
    scale = std::max(scale, 1.0f);
    MuiRect lamp = { roundf(rect.right() - 34.0f * 2.0f), roundf(rect.centerY() - 34.0f), 68.0f, 68.0f };
    (void)scale;
    muiGameDrawArt(MuiArtGroup::Interface, on ? "optidn" : "optiup", lamp);
}

void muiGameKnob(MuiContext& ui, float x, float y, bool pressed)
{
    (void)ui;
    int width;
    int height;
    if (!muiGameArtSize(MuiArtGroup::Interface, "slider", &width, &height)) {
        return;
    }
    MuiRect knob = { roundf(x - width), roundf(y - height), width * 2.0f, height * 2.0f };
    muiGameDrawArt(MuiArtGroup::Interface, "slider", knob, pressed ? muiRgb(0xFFE0A0) : muiRgb(0xFFFFFF));
}

void muiGameSkinReset()
{
    for (GameFont& font : gFonts) {
        font.texture = nullptr;
        font.generation = 0;
        font.failed = false;
    }
    gInk.clear();
}

} // namespace fallout
