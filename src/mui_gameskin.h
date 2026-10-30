#ifndef FALLOUT_MUI_GAMESKIN_H_
#define FALLOUT_MUI_GAMESKIN_H_

#include "art.h"
#include "mui.h"
#include "mui_icons.h"

namespace fallout {

// The "Fallout" bottom-screen theme (`[touch] bottom_theme=0`): the game's own
// interface art (art/intrface, art/skilldex, art/inven) and its interface fonts
// (font0-4.aaf), read from the player's own game data at run time through the
// game's loaders, drawn with whole-number scaling. Nothing of the game's data
// is shipped. Where the game has no art for a thing it is drawn in the same
// style (pixel art, brass, worn metal), never as modern vector icons.
//
// Stage 2 and later screens use the "themed" primitives at the bottom of this
// header; they pick the Fallout or the classic look by themselves.

// The theme is on for the surface being drawn (the bottom screen, `bottom_theme`
// is 0).
bool muiSkinFallout();
bool muiSkinPipboy();
void muiPipboyOverlay(MuiContext& ui);

// Fonts of the game by role.
enum class MuiFontRole {
    // Font 1: small spaced capitals, readouts, numbers.
    Label,
    // Font 2: condensed text, the body of lists and dialogue.
    Body,
    // Font 3: heavy capitals, buttons and headings.
    Heading,
    // Font 4: the grunge title face.
    Title,
};

// Text by size (output pixels, as `muiDrawText`): the role is picked by size.
float muiGameTextWidth(const std::u32string& text, float size);
float muiGameLineHeight(float size);
void muiGameDrawText(const std::u32string& text, float x, float y, float size, MuiColor color);

// Text in a role at [scale] (the glyph pixels' multiple, 1 - the game's own
// size). The game's black drop shadow is part of it.
float muiGameRoleWidth(const std::u32string& text, MuiFontRole role, float scale);
float muiGameRoleHeight(MuiFontRole role, float scale);
void muiGameDrawRole(const std::u32string& text, float x, float y, MuiFontRole role, float scale, MuiColor color);
// Largest scale from [maxScale] down to 1 (steps of a quarter) the text fits
// [width] with.
float muiGameFitScale(const std::u32string& text, MuiFontRole role, float width, float maxScale);

// Art of the game by file name in its group (`art/intrface/<name>.frm`...).
enum class MuiArtGroup {
    Interface,
    Skilldex,
    Inventory,
};
bool muiGameArtSize(MuiArtGroup group, const char* name, int* width, int* height);
// Drawn into [dest], pixels scaled as they are.
bool muiGameDrawArt(MuiArtGroup group, const char* name, const MuiRect& dest, MuiColor tint = { 255, 255, 255, 255 });
// The part of the art that is ink (dark lines on the skilldex paper) in
// [tint].
bool muiGameDrawInk(MuiArtGroup group, const char* name, const MuiRect& dest, MuiColor tint);
// A plate stretched over [dest] keeping its [corner] source pixels at [scale].
bool muiGameDrawNineSlice(const char* name, const MuiRect& dest, int corner, float scale, MuiColor tint = { 255, 255, 255, 255 });

// Icons: the game's art where it has some, pixel art in its style elsewhere.
// Returns false if there is none (the classic icons are drawn).
bool muiGameDrawIcon(MuiIcon icon, float x, float y, float size, MuiColor color);

// The pieces `mui_skin.cc` draws with.
void muiGameBackdrop(MuiContext& ui, const MuiRect& rect);
void muiGamePlate(MuiContext& ui, const MuiRect& rect, bool rivets);
void muiGameWell(MuiContext& ui, const MuiRect& rect);
void muiGameButton(MuiContext& ui, const MuiRect& rect, bool pressed, bool primary);
void muiGameNamePlate(MuiContext& ui, float x, float y, const std::u32string& text);
// A button's label: Heading font fitted to [rect].
void muiGameButtonLabel(MuiContext& ui, const MuiRect& rect, const std::u32string& label, MuiColor color);
// A lamp (toggle): amber when [on].
void muiGameLamp(MuiContext& ui, const MuiRect& rect, bool on, bool pressed);
// A slider's knob centered at ([x], [y]).
void muiGameKnob(MuiContext& ui, float x, float y, bool pressed);
// Frees what belongs to the renderer.
void muiGameSkinReset();

// ---- Primitives for screens (the same in either theme) ----

// Raised metal plate.
inline void themedPanel(MuiContext& ui, const MuiRect& rect, bool rivets = true);
// Recessed readout.
inline void themedWell(MuiContext& ui, const MuiRect& rect);
// Metal button, pressed in while [pressed], red when [primary].
inline void themedButton(MuiContext& ui, const MuiRect& rect, bool pressed, bool primary = false);
// Text in the theme's face (game fonts in the Fallout theme).
inline void themedText(const std::u32string& text, float x, float y, float size, MuiColor color);
// Icon.
inline void themedIcon(MuiIcon icon, float x, float y, float size, float thickness, MuiColor color);

} // namespace fallout

#include "mui_skin.h"

namespace fallout {

inline void themedPanel(MuiContext& ui, const MuiRect& rect, bool rivets) { muiSkinPlate(ui, rect, rivets); }
inline void themedWell(MuiContext& ui, const MuiRect& rect) { muiSkinWell(ui, rect); }
inline void themedButton(MuiContext& ui, const MuiRect& rect, bool pressed, bool primary) { muiSkinButton(ui, rect, pressed, primary); }
inline void themedText(const std::u32string& text, float x, float y, float size, MuiColor color) { muiDrawText(text, x, y, size, color); }
inline void themedIcon(MuiIcon icon, float x, float y, float size, float thickness, MuiColor color) { muiDrawIcon(icon, x, y, size, thickness, color); }

} // namespace fallout

#endif /* FALLOUT_MUI_GAMESKIN_H_ */
