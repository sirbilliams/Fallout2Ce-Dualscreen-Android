#include "mui_map_view.h"

#include <algorithm>
#include <cmath>

#include "map_hints.h"
#include "mui_gameskin.h"
#include "mui_notify.h"
#include "object.h"
#include "player_commands.h"
#include "settings.h"

namespace fallout {

namespace {

    const MuiColor kScenery = muiRgb(0x2E6A30);

    // Texture pixels per tile: steps the map is drawn at (the view scales
    // the nearest one), the texture size limit.
    const float kScales[] = { 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 24.0f };
    constexpr int kMaxTextureSize = 2048;

    // Explored part margin, tiles.
    constexpr int kMargin = 3;

    // Floor colors are darkened to this part of their art's.
    constexpr float kFloorShade = 0.42f;

    // Labels show from this many dp per tile on.
    constexpr float kLabelZoomDp = 3.2f;

    SDL_Vertex vertex(float x, float y, MuiColor color)
    {
        SDL_Vertex result;
        result.position = { x, y };
        result.color = { color.r, color.g, color.b, color.a };
        result.tex_coord = { 0.0f, 0.0f };
        return result;
    }

    void addQuad(std::vector<SDL_Vertex>& vertices, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_FPoint d, MuiColor color)
    {
        vertices.push_back(vertex(a.x, a.y, color));
        vertices.push_back(vertex(b.x, b.y, color));
        vertices.push_back(vertex(c.x, c.y, color));
        vertices.push_back(vertex(a.x, a.y, color));
        vertices.push_back(vertex(c.x, c.y, color));
        vertices.push_back(vertex(d.x, d.y, color));
    }

    void addSegment(std::vector<SDL_Vertex>& vertices, float x1, float y1, float x2, float y2, float thickness, MuiColor color)
    {
        float dx = x2 - x1;
        float dy = y2 - y1;
        float length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0.0f) {
            return;
        }

        float nx = -dy / length * thickness / 2.0f;
        float ny = dx / length * thickness / 2.0f;
        addQuad(vertices, { x1 + nx, y1 + ny }, { x2 + nx, y2 + ny }, { x2 - nx, y2 - ny }, { x1 - nx, y1 - ny }, color);
    }

    // Joint of segments (octagon).
    void addDot(std::vector<SDL_Vertex>& vertices, float x, float y, float radius, MuiColor color)
    {
        constexpr int kSides = 8;
        for (int side = 0; side < kSides; side++) {
            float a1 = side * 2.0f * static_cast<float>(M_PI) / kSides;
            float a2 = (side + 1) * 2.0f * static_cast<float>(M_PI) / kSides;
            vertices.push_back(vertex(x, y, color));
            vertices.push_back(vertex(x + std::cos(a1) * radius, y + std::sin(a1) * radius, color));
            vertices.push_back(vertex(x + std::cos(a2) * radius, y + std::sin(a2) * radius, color));
        }
    }

} // namespace

void muiFillDiamond(float x, float y, float radius, MuiColor color)
{
    muiFillConvex({ { x, y - radius }, { x + radius, y }, { x - radius, y } }, color);
    muiFillConvex({ { x + radius, y }, { x, y + radius }, { x - radius, y } }, color);
}

unsigned char MuiMapView::tileAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width || y >= height) {
        return MUI_MAP_TILE_EMPTY;
    }
    return tiles[static_cast<size_t>(y) * width + x];
}

void MuiMapView::setTiles(const std::string& newKey, const std::vector<unsigned char>& newTiles, int newWidth, int newHeight)
{
    bool anotherMap = newKey != key;
    if (!anotherMap && newTiles == tiles) {
        return;
    }

    if (anotherMap && !key.empty() && !needsFit) remembered[key] = view;
    key = newKey;
    tiles = newTiles;
    width = newWidth;
    height = newHeight;
    if (anotherMap) {
        floors.clear();
        floorsWidth = 0;
        floorsHeight = 0;
    }
    textureValid = false;
    updateBounds();

    if (anotherMap) {
        auto saved = remembered.find(key);
        needsFit = saved == remembered.end();
        if (!needsFit) view = saved->second;
        lastRect = {};
        hasCenter = false;
    }
}

void MuiMapView::centerOn(float x, float y)
{
    centerX = view.centerX = x;
    centerY = view.centerY = y;
    hasCenter = true;
}

void MuiMapView::setFloors(const std::vector<unsigned int>& colors, int floorWidth, int floorHeight)
{
    if (colors == floors && floorWidth == floorsWidth && floorHeight == floorsHeight) {
        return;
    }

    floors = colors;
    floorsWidth = floorWidth;
    floorsHeight = floorHeight;
    textureValid = false;
    updateBounds();
}

void MuiMapView::updateBounds()
{
    left = width;
    top = height;
    right = -1;
    bottom = -1;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (tileAt(x, y) != MUI_MAP_TILE_EMPTY) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
        }
    }

    // A floor square covers 2 x 2 tiles.
    for (int y = 0; y < floorsHeight; y++) {
        for (int x = 0; x < floorsWidth; x++) {
            if (floors[static_cast<size_t>(y) * floorsWidth + x] != 0) {
                left = std::min(left, x * 2);
                right = std::max(right, std::min(x * 2 + 1, width - 1));
                top = std::min(top, y * 2);
                bottom = std::max(bottom, std::min(y * 2 + 1, height - 1));
            }
        }
    }

    if (right >= left) {
        left = std::max(left - kMargin, 0);
        top = std::max(top - kMargin, 0);
        right = std::min(right + kMargin, width - 1);
        bottom = std::min(bottom + kMargin, height - 1);
    }
}

void MuiMapView::render(MuiContext& ui, float scale)
{
    SDL_Renderer* renderer = muiDrawGetRenderer();
    if (renderer == nullptr) {
        return;
    }

    int newWidth = static_cast<int>(std::ceil((right - left + 1) * scale));
    int newHeight = static_cast<int>(std::ceil((bottom - top + 1) * scale));

    if (texture != nullptr && (textureGeneration != muiDrawGeneration() || newWidth != textureWidth || newHeight != textureHeight)) {
        if (textureGeneration == muiDrawGeneration()) {
            SDL_DestroyTexture(texture);
        }
        texture = nullptr;
    }

    if (texture == nullptr) {
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, newWidth, newHeight);
        if (texture == nullptr) {
            return;
        }
        SDL_SetTextureBlendMode(texture, muiPremultipliedBlendMode());
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
        textureWidth = newWidth;
        textureHeight = newHeight;
        textureGeneration = muiDrawGeneration();
    }

    const MuiTheme& theme = muiTheme();
    // Walls in the readouts' green, as the game's automap.
    MuiColor glow = theme.text.withAlpha(60);
    MuiColor core = theme.text;
    float thickness = std::max(scale * 0.34f, 1.5f);

    std::vector<SDL_Vertex> floorVertices;
    std::vector<SDL_Vertex> scenery;
    std::vector<SDL_Vertex> glowVertices;
    std::vector<SDL_Vertex> coreVertices;

    // The explored floor in its art's colors, darkened so walls and markers
    // stand out on it.
    for (int y = 0; y < floorsHeight; y++) {
        for (int x = 0; x < floorsWidth; x++) {
            unsigned int color = floors[static_cast<size_t>(y) * floorsWidth + x];
            if (color == 0 || x * 2 + 1 < left || x * 2 > right || y * 2 + 1 < top || y * 2 > bottom) {
                continue;
            }
            MuiColor fill = {
                static_cast<Uint8>(((color >> 16) & 0xFF) * kFloorShade),
                static_cast<Uint8>(((color >> 8) & 0xFF) * kFloorShade),
                static_cast<Uint8>((color & 0xFF) * kFloorShade),
                255,
            };
            if (muiSkinPipboy()) {
                Uint8 level = static_cast<Uint8>((fill.r + fill.g + fill.b) / 3);
                fill = { static_cast<Uint8>(level / 5), static_cast<Uint8>(level / 2), static_cast<Uint8>(level / 4), 255 };
            }
            float x1 = (x * 2 - left) * scale;
            float y1 = (y * 2 - top) * scale;
            float x2 = x1 + 2.0f * scale;
            float y2 = y1 + 2.0f * scale;
            addQuad(floorVertices, { x1, y1 }, { x2, y1 }, { x2, y2 }, { x1, y2 }, fill);
        }
    }

    auto center = [&](int x, int y) {
        return SDL_FPoint { (x - left + 0.5f) * scale, (y - top + 0.5f) * scale };
    };

    for (int y = top; y <= bottom; y++) {
        for (int x = left; x <= right; x++) {
            unsigned char tile = tileAt(x, y);
            SDL_FPoint p = center(x, y);

            if (tile == MUI_MAP_TILE_SCENERY) {
                addDot(scenery, p.x, p.y, scale * 0.32f, kScenery.withAlpha(200));
                continue;
            }

            if (tile != MUI_MAP_TILE_WALL) {
                continue;
            }

            addDot(glowVertices, p.x, p.y, thickness * 1.6f, glow);
            addDot(coreVertices, p.x, p.y, thickness / 2.0f, core);

            // Lines to walls right, below and diagonal (diagonals only where
            // no straight line joins them already).
            bool rightWall = tileAt(x + 1, y) == MUI_MAP_TILE_WALL;
            bool belowWall = tileAt(x, y + 1) == MUI_MAP_TILE_WALL;
            bool leftWall = tileAt(x - 1, y) == MUI_MAP_TILE_WALL;

            auto link = [&](int toX, int toY) {
                SDL_FPoint q = center(toX, toY);
                addSegment(glowVertices, p.x, p.y, q.x, q.y, thickness * 3.2f, glow);
                addSegment(coreVertices, p.x, p.y, q.x, q.y, thickness, core);
            };

            if (rightWall) {
                link(x + 1, y);
            }
            if (belowWall) {
                link(x, y + 1);
            }
            if (tileAt(x + 1, y + 1) == MUI_MAP_TILE_WALL && !rightWall && !belowWall) {
                link(x + 1, y + 1);
            }
            if (tileAt(x - 1, y + 1) == MUI_MAP_TILE_WALL && !leftWall && !belowWall) {
                link(x - 1, y + 1);
            }
        }
    }

    SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, texture) != 0) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    // Colors with alpha blended over transparent black are premultiplied.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (const std::vector<SDL_Vertex>* layer : { &floorVertices, &scenery, &glowVertices, &coreVertices }) {
        if (!layer->empty()) {
            SDL_RenderGeometry(renderer, nullptr, layer->data(), static_cast<int>(layer->size()), nullptr, 0);
        }
    }

    SDL_SetRenderTarget(renderer, previousTarget);

    textureScale = scale;
    textureValid = true;
}

void MuiMapView::build(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::vector<MuiMapMarker>& markers, const std::vector<MuiMapLabel>* labels)
{
    tappedTile = -1;
    if (renderedTheme != settings.touch.bottom_theme) {
        renderedTheme = settings.touch.bottom_theme;
        textureValid = false;
    }
    if (isEmpty()) {
        return;
    }

    float mapWidth = static_cast<float>(right - left + 1);
    float mapHeight = static_cast<float>(bottom - top + 1);

    // Whole explored part fits; zoom in up to a few dp per tile.
    float fitZoom = std::min(rect.w / mapWidth, rect.h / mapHeight);
    view.minZoom = fitZoom * 0.8f;
    view.maxZoom = std::max(ui.dp(28.0f), fitZoom * 1.5f);

    if (needsFit) {
        needsFit = false;
        view.zoom = fitZoom;
        view.centerX = left + mapWidth / 2.0f;
        view.centerY = top + mapHeight / 2.0f;
        if (hasCenter) {
            view.centerX = centerX;
            view.centerY = centerY;
        }
    }
    hasCenter = false;

    if (pendingZoomDp > 0.0f) {
        view.zoom = ui.dp(pendingZoomDp);
        pendingZoomDp = 0.0f;
    }

    if (following) {
        following = false;
        view.centerX = followX;
        view.centerY = followY;
    }

    bool tapped = false;
    bool touched = ui.panZoom(id, rect, &view, &tapped);
    if (tapped && allowWalk && settings.touch.map_tap_move && (id == "hud.livemap" || id == "automap.map")) {
        // Pick against the image the finger saw, before follow/clamping changes it.
        const MuiPanZoom& pickView = lastRect.w > 0 ? lastView : view;
        const MuiRect& pickRect = lastRect.w > 0 ? lastRect : rect;
        int x = static_cast<int>(std::floor(pickView.centerX + (ui.pointerX() - pickRect.centerX()) / pickView.zoom));
        int y = static_cast<int>(std::floor(pickView.centerY + (ui.pointerY() - pickRect.centerY()) / pickView.zoom));
        if (rect.contains(ui.pointerX(), ui.pointerY()) && x >= 0 && y >= 0 && x < width && y < height) {
            int tile = y * width + (width - 1 - x);
            if (tileAt(x, y) != MUI_MAP_TILE_WALL && playerCanMoveToExactTile(tile)) {
                tappedTile = tile;
            } else {
                muiNotify("Cannot walk there.", MuiNoticeKind::Warning);
            }
        }
    }
    if (tapped && (!allowWalk || !settings.touch.map_tap_move || id == "pipboy.map")) {
        const MuiPanZoom& pick = lastRect.w > 0 ? lastView : view;
        view.centerX = pick.centerX + (ui.pointerX() - rect.centerX()) / pick.zoom;
        view.centerY = pick.centerY + (ui.pointerY() - rect.centerY()) / pick.zoom;
    }
    if (touched) {
        lastTouch = ui.now != 0 ? ui.now : 1;
    }
    view.zoom = std::clamp(view.zoom, view.minZoom, view.maxZoom);
    view.centerX = std::clamp(view.centerX, static_cast<float>(left), static_cast<float>(right + 1));
    view.centerY = std::clamp(view.centerY, static_cast<float>(top), static_cast<float>(bottom + 1));

    // Texture pixels per tile for this zoom (drawn again after a pinch, not
    // during it).
    float limit = std::min(kMaxTextureSize / mapWidth, kMaxTextureSize / mapHeight);
    float wanted = kScales[0];
    for (float scale : kScales) {
        if (scale <= view.zoom * 1.25f) {
            wanted = scale;
        }
    }
    wanted = std::min(wanted, limit);

    if (!textureValid || textureGeneration != muiDrawGeneration() || (!touched && std::fabs(wanted - textureScale) > 0.01f)) {
        render(ui, wanted);
    }

    lastRect = rect;
    lastView = view;
    muiPushClip(rect);

    if (texture != nullptr && textureValid) {
        MuiRect dest = {
            rect.centerX() + (left - view.centerX) * view.zoom,
            rect.centerY() + (top - view.centerY) * view.zoom,
            textureWidth / textureScale * view.zoom,
            textureHeight / textureScale * view.zoom,
        };
        muiDrawTexture(texture, dest);
    }

    auto allMarkers = markers;
    int destination = mapHintsGetDestination();
    if (destination >= 0 && gDude != nullptr && (id == "hud.livemap" || id == "automap.map"))
        allMarkers.push_back({ static_cast<float>(width - 1 - destination % width), static_cast<float>(destination / width), muiRgb(0xFFD060), 6.0f, MuiMapMarkerShape::Ring });
    for (const MuiMapMarker& marker : allMarkers) {
        float x = rect.centerX() + (marker.x + 0.5f - view.centerX) * view.zoom;
        float y = rect.centerY() + (marker.y + 0.5f - view.centerY) * view.zoom;
        float radius = ui.dp(marker.radius);
        MuiColor outline = muiRgb(0x000000, 170);
        switch (marker.shape) {
        case MuiMapMarkerShape::Arrow: {
            float angle = gDude != nullptr ? static_cast<int>(gDude->rotation) * float(M_PI) / 3 : 0;
            auto p = [&](float a, float b) { return SDL_FPoint { x + a * std::cos(angle) - b * std::sin(angle), y + a * std::sin(angle) + b * std::cos(angle) }; };
            auto a = p(0, -radius * 1.8f), b = p(radius, radius), c = p(-radius, radius);
            muiFillConvex({ a, b, c }, muiTheme().text);
            break;
        }
        case MuiMapMarkerShape::Dot:
            muiFillCircle(x, y, radius * 2.0f, marker.color.withAlpha(50));
            muiFillCircle(x, y, radius, marker.color);
            break;
        case MuiMapMarkerShape::Square:
            muiFillRect({ x - radius - 1.0f, y - radius - 1.0f, radius * 2.0f + 2.0f, radius * 2.0f + 2.0f }, outline);
            muiFillRect({ x - radius, y - radius, radius * 2.0f, radius * 2.0f }, marker.color);
            break;
        case MuiMapMarkerShape::Diamond:
            muiFillDiamond(x, y, radius + 1.5f, outline);
            muiFillDiamond(x, y, radius, marker.color);
            break;
        case MuiMapMarkerShape::Ring:
            muiStrokeCircle(x, y, radius, std::max(radius * 0.45f, 1.5f), marker.color);
            break;
        case MuiMapMarkerShape::Triangle:
            muiFillConvex({ { x, y - radius * 1.15f - 1.5f }, { x + radius + 1.5f, y + radius * 0.8f + 1.0f }, { x - radius - 1.5f, y + radius * 0.8f + 1.0f } }, outline);
            muiFillConvex({ { x, y - radius * 1.15f }, { x + radius, y + radius * 0.8f }, { x - radius, y + radius * 0.8f } }, marker.color);
            break;
        }
    }

    // Names, once they can be read at this zoom.
    if (labels != nullptr && view.zoom >= ui.dp(kLabelZoomDp)) {
        float size = ui.dp(11.0f);
        for (const MuiMapLabel& label : *labels) {
            float x = rect.centerX() + (label.x + 0.5f - view.centerX) * view.zoom;
            float y = rect.centerY() + (label.y + 0.5f - view.centerY) * view.zoom - ui.dp(14.0f);
            float width = muiTextWidth(label.text, size) + ui.dp(10.0f);
            MuiRect plate = { x - width / 2.0f, y - ui.dp(9.0f), width, ui.dp(18.0f) };
            muiFillRoundRect(plate, ui.dp(4.0f), muiRgb(0x000000, 190));
            muiStrokeRoundRect(plate, ui.dp(4.0f), ui.dp(1.0f), label.color.withAlpha(150));
            muiDrawTextAligned(label.text, plate, size, label.color, MuiAlign::Center, MuiAlign::Center);
        }
    }

    muiPopClip();
    bool compact = rect.h < ui.dp(150);
    float h = ui.dp(compact ? 24 : 30), w = ui.dp(compact ? 66 : 72);
    float right = rect.right() - (compact ? reservedRight : 0.0f);
    MuiRect center = { right - w - ui.dp(6), compact ? rect.bottom() - h - ui.dp(4) : rect.y + ui.dp(48), w, h };
    if (ui.button(id + ".center", center, U"CENTER")) {
        if (id != "pipboy.map" && gDude != nullptr)
            centerOn(width - 1 - gDude->tile % width + 0.5f, gDude->tile / width + 0.5f);
        else
            fit();
        lastTouch = ui.now;
    }
    MuiRect legend = { compact ? center.x - w - ui.dp(4) : center.x, compact ? center.y : center.bottom() + ui.dp(4), w, h };
    if (ui.button(id + ".legend", legend, U"LEGEND")) showLegend = !showLegend;
    if (showLegend) muiAutomapLegend(ui, rect);
}

} // namespace fallout
