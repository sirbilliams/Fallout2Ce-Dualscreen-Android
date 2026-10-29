#ifndef FALLOUT_GAMEPAD_H_
#define FALLOUT_GAMEPAD_H_

#include <SDL.h>

namespace fallout {

// Game controllers (the AYN Thor's pad, any SDL game controller). Buttons post
// the same game commands as the touch HUD's buttons (game_commands.h), so
// the game's rules apply (combat AP, whose turn, what may open now);
// commands for the map only while the map is played, never into a screen.
//
// Stage 3 (docs/gamepad-stage3.md): in screens the d-pad / left stick moves a
// focus ring (mui_pad.h), A taps, X long presses, B goes back, the right stick
// scrolls lists and pans maps. On the map the right stick moves a cursor
// (A click / walk, X long press = context menu), the left stick pans the
// view. The map buttons are the `[touch] gamepad_bindings` setting.

// Starts the game controller subsystem (after SDL_Init).
void gamepadInit();

// input.cc: controller events; true when taken.
bool gamepadHandleEvent(const SDL_Event* event);

// Automated tests: a button press (SDL_GameControllerButton, or 100 / 101 for the
// left / right trigger) and a virtual right stick for some frames.
void gamepadDevPress(int button, bool down);
void gamepadDevStick(float x, float y, int frames);

// input.cc: once per input pass, the sticks and held buttons.
void gamepadTick();

} // namespace fallout

#endif /* FALLOUT_GAMEPAD_H_ */
