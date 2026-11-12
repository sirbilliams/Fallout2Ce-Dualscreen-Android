#ifndef FALLOUT_HD_LIGHT_SCENE_H_
#define FALLOUT_HD_LIGHT_SCENE_H_

#include <vector>

namespace fallout::hd {

// Inputs of the new map lighting (PLAN.md P5.1), read from CE's state for
// every map build. CE stays the authority on how much light reaches a hex:
// its per-hex light (light.cc; walls already block it) is the field the new
// light starts from, and the light sources only decide direction, color and
// shadows. Same model as the P1.9 Python compositor (fo2hd.compose).
//
// Units: 1x pixels of the map window, ground foreshortened 2:1 (ground
// v = 2 * screen y), heights in screen pixels.

enum class LightKind {
    Lamp,
    Fire,
    Critter,
};

struct SceneLight {
    float x; // center of the light's hex
    float y;
    float z; // mount height above the ground
    float radius; // hexes (CE's light distance, at most 8)
    float intensity; // 0..1
    float color[3]; // max component 1 (P5.4)
    LightKind kind;
    int objectId;
};

// A hex inside the view (plus a margin): its center and CE's unclamped
// light there / 65536 (the 655 floor plus every light; not the ambient).
struct SceneHex {
    float x;
    float y;
    float intensity;
};

struct LightScene {
    float ambient = 1.0f; // 0.25..1
    int timeOfDay = 1200; // HHMM
    int elevation = 0;
    int worldX = 0; // screen position of hex 0: moves exactly as the view scrolls
    int worldY = 0;
    std::vector<SceneLight> lights; // those that can reach the view
    std::vector<SceneHex> hexes;
};

// Gathers the scene for a map window of `width` x `height`; hexes within
// `margin` px of it (more below, where tall objects' ground lies).
// `palette`: 256 x RGBA floats in base space, for light colors.
const LightScene& lightSceneGather(int width, int height, int margin, int marginBelow, const float* palette);

// Forgets cached light colors (map change, or the palette was replaced).
void lightSceneReset();

} // namespace fallout::hd

#endif /* FALLOUT_HD_LIGHT_SCENE_H_ */
