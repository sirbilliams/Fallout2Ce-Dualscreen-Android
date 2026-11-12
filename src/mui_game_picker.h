#ifndef FALLOUT_MUI_GAME_PICKER_H_
#define FALLOUT_MUI_GAME_PICKER_H_
namespace fallout {
enum class MuiSurface;
bool muiGamePickerRun(); // false: quit / restart requested
// Consumed only while picker owns input; directions -1 / +1.
bool muiGamePickerMove(int direction);
bool muiGamePickerAccept(MuiSurface surface);
// X on a pad: plays the shown game's intro movies, when it has some.
bool muiGamePickerIntro();
} // namespace fallout
#endif
