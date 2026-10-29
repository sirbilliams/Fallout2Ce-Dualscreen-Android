#ifndef FALLOUT_MUI_COMBAT_PANEL_H_
#define FALLOUT_MUI_COMBAT_PANEL_H_

#include "mui.h"

namespace fallout {

// Dual-screen devices (AYN Thor): the combat panel and the quick-use hotbar
// of the bottom screen (mui_combat_panel.cc). Both only call the game's own
// functions (hit chances, action points, attacks, item use); the commands
// they post run in the game loop (`GameCommandType::Panel`).

constexpr int kQuickUseSlotCount = 6;

// Settings `[touch] combat_panel` and `hotbar` are on.
bool muiCombatPanelEnabled();
bool muiQuickUseEnabled();

// Bottom screen, the dashboard's middle: the combat panel in dude's combat,
// over [rect] (the hotbar is under it, see `muiQuickUseBuild`).
bool muiCombatPanelActive();
// [modes] gets the rect the HUD draws the attack mode chips in.
void muiCombatPanelBuild(MuiContext& ui, const MuiRect& rect, MuiRect* modes);

// The hotbar's strip over [rect].
void muiQuickUseBuild(MuiContext& ui, const MuiRect& rect);
float muiQuickUseHeight(MuiContext& ui);

// Quick-use slots assigned by long-pressing an inventory item.
bool muiQuickUseCanAssign(Object* item);
// Slot [slot] (0-based) takes [item]'s kind, -1 clears the slot [item]'s kind
// is in.
void muiQuickUseAssign(int slot, Object* item);
void muiQuickUseClear(Object* item);
int muiQuickUseSlotOf(Object* item);

// Runs a posted `GameCommandType::Panel` command in the game loop.
void muiCombatPanelRun(int value);

} // namespace fallout

#endif /* FALLOUT_MUI_COMBAT_PANEL_H_ */
