#ifndef MAIN_H
#define MAIN_H

namespace fallout {

int falloutMain(int argc, char** argv);

// CE: A game is being played: the game's main loop runs (the map, and the
// screens opened from it). False on the main menu, while starting a new
// game or loading one from there, and on the death screen.
bool mainIsPlaying();

} // namespace fallout

#endif /* MAIN_H */
