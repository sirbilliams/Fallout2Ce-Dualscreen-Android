#ifndef FALLOUT_MUI_MAP_VIEW_H_
#define FALLOUT_MUI_MAP_VIEW_H_

#include <string>
#include <vector>

#include "mui.h"

namespace fallout {

// Explored map of the mobile UI (Pip-Boy maps, automap): walls as glowing
// lines, scenery as dim dots, markers on top; one finger drags it, two
// fingers pinch it, like the world view. The game's data is drawn as it is
// (the tiles it keeps for the automap), only the look is new.

// Tile codes of the map.
enum MuiMapTile : unsigned char {
    MUI_MAP_TILE_EMPTY = 0,
    MUI_MAP_TILE_WALL = 1,
    MUI_MAP_TILE_SCENERY = 2,
};

enum class MuiMapMarkerShape {
    Dot,
    Square,
    Diamond,
    Ring,
    Triangle,
};

// Marker in tile coordinates (tile centers are at x + 0.5, y + 0.5).
struct MuiMapMarker {
    float x;
    float y;
    MuiColor color;
    // dp.
    float radius;
    MuiMapMarkerShape shape = MuiMapMarkerShape::Dot;
};

// A name on the map at tile ([x], [y]), shown once zoomed in enough to read.
struct MuiMapLabel {
    float x;
    float y;
    std::u32string text;
    MuiColor color;
};

class MuiMapView {
public:
    // [tiles]: [width] x [height] codes, rows top down. A new [key] (another
    // map) shows the explored part whole; the same key keeps the view.
    void setTiles(const std::string& key, const std::vector<unsigned char>& tiles, int width, int height);

    // The explored floor under the walls: [floorWidth] x [floorHeight]
    // squares of 2 x 2 tiles, ARGB colors (0 - none), drawn darkened.
    void setFloors(const std::vector<unsigned int>& colors, int floorWidth, int floorHeight);

    bool isEmpty() const { return right < left; }

    // Shows the explored part whole.
    void fit() { needsFit = true; }

    // Centers the view on tile ([x], [y]) (e.g. the player).
    void centerOn(float x, float y);

    // Keeps tile ([x], [y]) at the center this frame (a live map following
    // the player); callers stop while the map is being dragged.
    void follow(float x, float y)
    {
        following = true;
        followX = x;
        followY = y;
    }

    // The zoom to use once, at [tileDp] dp per tile (within the map's
    // limits).
    void zoomTo(float tileDp) { pendingZoomDp = tileDp; }

    // When a finger last moved or pinched the map (SDL ticks, 0 - never).
    unsigned int lastTouchTime() const { return lastTouch; }

    // The tile at the view's center now.
    float viewCenterX() const { return view.centerX; }
    float viewCenterY() const { return view.centerY; }

    // Where the center of tile ([x], [y]) was drawn in the last `build`
    // (output pixels); false before any or outside the viewport.
    bool tileToScreen(float x, float y, float* screenX, float* screenY) const
    {
        if (lastRect.w <= 0.0f) {
            return false;
        }
        *screenX = lastRect.centerX() + (x + 0.5f - view.centerX) * view.zoom;
        *screenY = lastRect.centerY() + (y + 0.5f - view.centerY) * view.zoom;
        return lastRect.contains(*screenX, *screenY);
    }

    // Engine hex tile selected by the last build, or -1 (pan/pinch/no tap).
    int tappedTile = -1;

    void build(MuiContext& ui, const std::string& id, const MuiRect& rect, const std::vector<MuiMapMarker>& markers, const std::vector<MuiMapLabel>* labels = nullptr);

private:
    std::string key;
    std::vector<unsigned char> tiles;
    int width = 0;
    int height = 0;

    std::vector<unsigned int> floors;
    int floorsWidth = 0;
    int floorsHeight = 0;

    // Explored part (tiles), with a margin.
    int left = 0;
    int top = 0;
    int right = -1;
    int bottom = -1;

    MuiPanZoom view;
    bool needsFit = true;
    bool following = false;
    float followX = 0.0f;
    float followY = 0.0f;
    float pendingZoomDp = 0.0f;
    unsigned int lastTouch = 0;
    MuiRect lastRect;
    bool hasCenter = false;
    float centerX = 0.0f;
    float centerY = 0.0f;

    // The drawn map (texture pixels per tile, drawn again at another scale
    // after zooming).
    SDL_Texture* texture = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;
    float textureScale = 0.0f;
    unsigned int textureGeneration = 0;
    bool textureValid = false;

    unsigned char tileAt(int x, int y) const;
    // The explored part from the tiles and the floor.
    void updateBounds();
    void render(MuiContext& ui, float scale);
};

struct AutomapView;

// A diamond centered at ([x], [y]) reaching [radius] out (two triangles).
void muiFillDiamond(float x, float y, float radius, MuiColor color);

// The automap's markers and names (mui_automap.cc), the same on the map
// screen and the dual-screen dashboard's live map: exits and their
// destinations, doors, containers, stairs / ladders / elevators, party
// members, and with [critters] the motion sensor's critters. Dude is the
// caller's.
void muiAutomapDetail(const AutomapView& view, bool critters, std::vector<MuiMapMarker>* markers, std::vector<MuiMapLabel>* labels);

// Screen position of an engine tile on the open automap (automated tests).
bool muiAutomapTileCenter(int tile, float* x, float* y);

// What the markers are: a row of keys along the bottom of [area].
void muiAutomapLegend(MuiContext& ui, const MuiRect& area);

} // namespace fallout

#endif /* FALLOUT_MUI_MAP_VIEW_H_ */
