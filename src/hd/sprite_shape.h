#ifndef FALLOUT_HD_SPRITE_SHAPE_H_
#define FALLOUT_HD_SPRITE_SHAPE_H_

namespace fallout::hd {

// Shape of a sprite for the new map lighting (PLAN.md P5.1): a base normal
// and a height above the ground per 1x texel, from the silhouette alone.
// Grew out of the P1.7 heuristics (tools/pipeline/fo2hd/hdassets.py); the
// original art and the HD art get the same shape:
//
//   walls          vertical planes; one facing per run of the bottom edge, from
//                  the map's three directions (corner pieces get two faces), so
//                  pieces of a straight wall match and nothing changes per column
//   critters, items, misc, small scenery
//                  "pillow" billboards: the silhouette inflated, edges turned
//                  away from the camera
//   large, thin or solid scenery
//                  flat, facing the camera: no body to inflate
//   round scenery  (roundStrength > 0: trees, cacti, bushes, rocks, barrels;
//                  the caller knows them by name) one rounded body over the
//                  whole object, its gaps closed, roundStrength times a
//                  sphere-like tilt at the rim
//   height         walls/scenery: above the ground line (each column's lowest
//                  pixel, constrained to the iso slope, so overhangs inherit the
//                  ground of what holds them up); others: above the frame's
//                  bottom row (the feet)
//
// World axes: u = screen right, v = toward the viewer along the ground,
// z = up. Output per texel, RGBA8:
//   R, G  normal u and z as (n + 1) / 2 (v = sqrt(1 - u^2 - z^2), never < 0)
//   B, A  height in 1/16 screen px, low byte / high byte
//
// `objectType` is the art's OBJ_TYPE_*; tiles and interface art get no
// shape (flat, height 0).
void spriteShape(const unsigned char* pixels, int width, int height, int objectType, unsigned char* rgba, float roundStrength = 0.0f);

} // namespace fallout::hd

#endif /* FALLOUT_HD_SPRITE_SHAPE_H_ */
