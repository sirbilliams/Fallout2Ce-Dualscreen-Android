#include "hd/palette_fit.h"

#include <math.h>

#include "color.h"

namespace fallout::hd {

PaletteFit paletteFit()
{
    PaletteFit fit;
    for (int c = 0; c < 3; c++) {
        double n = 0.0;
        double sx = 0.0;
        double sy = 0.0;
        double sxx = 0.0;
        double sxy = 0.0;
        for (int i = 1; i < 229; i++) {
            double x = _cmap[i * 3 + c];
            double y = _systemCmap[i * 3 + c];
            n += 1.0;
            sx += x;
            sy += y;
            sxx += x * x;
            sxy += x * y;
        }
        double den = n * sxx - sx * sx;
        double gain = den > 0.0 ? (n * sxy - sx * sy) / den : 1.0;
        double offset = (sy - gain * sx) / n;
        if (fabs(offset) < 0.5) {
            // A plain gain (fades to black): fit it through zero.
            gain = sxx > 0.0 ? sxy / sxx : 1.0;
            offset = 0.0;
        }
        fit.gain[c] = static_cast<float>(gain);
        fit.offset[c] = static_cast<float>(offset);
        for (int i = 1; i < 229; i++) {
            if (fabs(_systemCmap[i * 3 + c] - (gain * _cmap[i * 3 + c] + offset)) > 1.5) {
                return fit;
            }
        }
    }
    fit.ok = true;
    return fit;
}

} // namespace fallout::hd
