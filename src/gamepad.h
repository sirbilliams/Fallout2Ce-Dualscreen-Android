#ifndef FALLOUT_GAMEPAD_H_
#define FALLOUT_GAMEPAD_H_

#include <SDL.h>

namespace fallout {

// Game controllers (the AYN Thor's pad, any SDL game controller). Buttons post
// the same game commands as the touch HUD's buttons (game_commands.h), so
// the game's rules apply (combat AP, whose turn, what may open now);
// commands for the map only while the map is played, never into a screen.
//
// Layout (same conventions as sirbilliams/neutron2-android: B back, Start
// menu):
// - B: back (the showing screen's Back, the game's menu on the map).
// - Start: the game's menu; in a screen, back.
// - Y: inventory. X: skills. L1: character. R1: Pip-Boy. R3: map. L3: sneak.
// - L2: swap hands. R2: the active item (attack: then tap the target).
// - Select held: end turn (held so it can't happen by accident).
// - Right stick: moves the view. D-pad up / down: zoom in / out.

// Starts the game controller subsystem (after SDL_Init).
void gamepadInit();

// input.cc: controller events; true when taken.
bool gamepadHandleEvent(const SDL_Event* event);

// input.cc: once per input pass, the sticks and held buttons.
void gamepadTick();

} // namespace fallout

#endif /* FALLOUT_GAMEPAD_H_ */
