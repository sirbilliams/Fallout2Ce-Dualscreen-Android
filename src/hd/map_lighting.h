#ifndef FALLOUT_HD_MAP_LIGHTING_H_
#define FALLOUT_HD_MAP_LIGHTING_H_

#ifdef FALLOUT_HD

#include "hd/gl.h"

namespace fallout::hd {

// New map lighting (PLAN.md P5), on the HD pass. The model is the P1.9
// Python compositor's (tools/pipeline/fo2hd/compose.py, ADR-0012):
//
//   light = ambient (tinted at night, shaded by sun and ambient occlusion)
//         + sum over lights of  E * share * shape * shadow * color
//         + bounce (2D radiance cascades over ground and walls lit by the
//           lights; the ambient part isn't bounced, the art has it painted in)
//
// where E is the light above ambient in CE's own per-hex light field
// (light.cc, walls already block it), smoothed and sampled at each pixel's
// ground point, boosted near strong sources; share splits E among the
// lights that reach the point by CE's falloff; shape comes from the
// pixel's normal; shadow from an occluder heightfield. With every effect
// off this is CE's light field itself, and `mode=original` is the P4 pass
// (CE's per-object light, exactly).
//
//   [hd_light]
//   mode=new                  new | original (the P4 look); Ctrl+F7 toggles
//   quality=high              off (original light) | low (no shadows, sun, AO,
//                             bounce, bloom) | medium (no
//                             bounce) | high (everything); Android default off
//   normals=1 colors=1 shadows=1 sun=1 ao=1 pool=1 tonemap=1 night=1 gi=1
//                             one switch per effect (quality caps them)
//   pool_gain=0.6             E * (1 + pool_gain * E) near strong lights
//   shadow_strength=0.5       how dark shadows from lights get
//   shadow_range=64           how far (1x px) shadows reach; longer blocking
//                             is already in CE's light field
//   sun_strength=0.3          share of daylight that comes from the sun
//   sun_gain=1.18             sunlit open ground vs. the original (makes up
//                             for the light shadows take away)
//   sun_dir=-1,-0.6,1.5       toward the sun (u right, v toward the viewer, z up)
//   day_lights=0.15           share of the lights' pools kept in full daylight
//                             (CE shows none: its light is clamped at full)
//   ao_strength=0.4           ambient occlusion
//   night_ambient=0.92        ambient scale at night ...
//   night_tint=0.82,0.88,1.0  ... and its color
//   field_sigma=14            smoothing of CE's per-hex light (ground px)
//   field_max=1.5             soft limit on CE's summed light (1 = full);
//                             0 = none. Where many lights stack it rolls off
//                             toward this instead of growing without bound
//   original_mix=0            blend toward the original light (0..1)
//   gi_strength=1             bounce light: overall strength ...
//   gi_ground=0.25            ... lit ground's emission (per 64 px of ray) ...
//   gi_wall=0.5               ... lit walls' emission
//   tonemap_curve=shoulder    shoulder (P5: knee, tonemap_white) | agx
//   agx_exposure=1            AgX: scale of the linearized light before the curve
//   bloom_glow=0.8            bloom source (P6.3): share of the glow heuristic's
//   bloom_hot=0               flames and neon; share of light above full white
//   floor=1.2,0.8,0.3,0       per category: detail, relief, shape weights, and
//                             the share of the sun's shadow the art ignores
//   (also roof, wall, scenery, critter, item, misc; wall and scenery ignore all of it)

bool mapLightingInit();
void mapLightingShutdown();

// The new light is on (mode=new) and its GPU parts are up.
bool mapLightingActive();

// Ctrl+F7: new light <-> original light. Returns a name for the new state.
const char* mapLightingToggle();

// Ctrl+F6 (developer): switches one effect off at a time, in turn
// (all on -> no normals -> no colors -> ... -> all on), for side-by-sides.
// Returns a name for the new state ("all effects", "no shadows", ...).
const char* mapLightingCycleEffect();

// Lights the built draw list (mapRendererBuild with an HD tier) into the
// HD target at `scale`: G-buffer, light inputs, light. `palette` as for
// mapRendererDrawHd.
bool mapLightingDraw(int mapWidth, int mapHeight, int scale, const float* palette);

// The last draw's bloom source (P6.3, hd/map_post.h): palette-cycle
// emissives, glow pixels and (optionally) light above white, at the HD
// target's size. Null with the original light or quality low.
GLuint mapLightingBloomSource();

// Lines for the developer overlay.
void mapLightingDrawOverlay();

} // namespace fallout::hd

#endif // FALLOUT_HD

#endif /* FALLOUT_HD_MAP_LIGHTING_H_ */
