#ifndef FALLOUT_HD_PALETTE_FIT_H_
#define FALLOUT_HD_PALETTE_FIT_H_

namespace fallout::hd {

// HD colors (the HD map, the HD UI) are in the loaded palette's space
// (_cmap, color.pal); the screen shows CE's live palette (_systemCmap).
// Fades (palette.cc paletteFadeTo) step linearly from one palette to
// another: toward black that's a per-channel gain, toward white (quitting
// to the menu, the new-game transition) a gain plus an offset. While the
// live palette is such a fade of the loaded one, HD colors can be shown
// through the same transform (P6.4).
struct PaletteFit {
    bool ok = false; // live palette = gain * loaded + offset, within 1.5 steps
    float gain[3] = { 1.0f, 1.0f, 1.0f };
    float offset[3] = { 0.0f, 0.0f, 0.0f }; // 6-bit units
};

// Least squares per channel over the non-cycling colors (1..228).
PaletteFit paletteFit();

} // namespace fallout::hd

#endif /* FALLOUT_HD_PALETTE_FIT_H_ */
