#ifndef FALLOUT_HD_MAP_PRESENT_H
#define FALLOUT_HD_MAP_PRESENT_H
#include <SDL.h>
namespace fallout::hd {
// Private SDL presentation glue; contracts in map_layer.h stay unchanged.
void mapLayerRenderWorld(SDL_Renderer* renderer, const SDL_Rect& source, const SDL_FRect& destination);
} // namespace fallout::hd
#endif
