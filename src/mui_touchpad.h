#ifndef FALLOUT_MUI_TOUCHPAD_H_
#define FALLOUT_MUI_TOUCHPAD_H_

namespace fallout {

// Dual-screen devices (AYN Thor): the bottom screen's touchpad page, opened by
// the dashboard's TOUCHPAD button. A laptop trackpad for the top screen's
// cursor (the way SpongeBob's touchpad works): drag moves the cursor
// relatively, faster drags go further; a tap clicks; hold still, then drag
// carries (the left button stays down: item drags); two fingers tapped
// together right click (the map's action menu). Back closes it.

void muiTouchpadInit();
void muiTouchpadFree();
void muiTouchpadOpen();
void muiTouchpadClose();
bool muiTouchpadIsOpen();

} // namespace fallout

#endif /* FALLOUT_MUI_TOUCHPAD_H_ */
