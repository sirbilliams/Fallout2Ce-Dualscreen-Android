#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include "color.h"
#include "text_font.h"

namespace fallout {

extern FontManager gModernFontManager;

int interfaceFontsInit();
void interfaceFontsExit();
// Scaled interface font drawing supports plain color text plus DRAW_TEXT_FLAG_SHADOWED.
// Unsupported flags like DRAW_TEXT_FLAG_MONOSPACED and DRAW_TEXT_FLAG_UNDERLINED are ignored with a debug log.
void interfaceFontDrawTextScaled2D(const Buffer2D& dest, int x, int y, const char* string, ColorWithFlags color, float scale);
int interfaceFontGetStringWidthScaled(const char* string, ColorWithFlags color, float scale);

// CE: How many interface fonts there can be (`font0.aaf`...).
int interfaceFontCount();
// CE: The picture of [ch] in interface font [font]: its size and pixels (a
// byte each, row by row); false - no such font or an empty picture.
// CE: Metrics of interface font [font] (its pixels): height of a line,
// space after each letter, width of a space; false - no such font.
bool interfaceFontGetMetrics(int font, int* maxHeight, int* letterSpacing, int* wordSpacing);
bool interfaceFontGetGlyph(int font, unsigned char ch, int* width, int* height, const unsigned char** pixels);

} // namespace fallout

#endif /* FONT_MANAGER_H */
