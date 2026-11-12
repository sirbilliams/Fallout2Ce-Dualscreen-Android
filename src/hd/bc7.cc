#include "hd/bc7.h"
#include <cstring>
#include <utility>
namespace fallout::hd {
namespace {
    struct Bits {
        const unsigned char* bytes;
        int pos = 0;
        unsigned take(int n)
        {
            unsigned v = 0;
            for (int i = 0; i < n; i++, pos++)
                v |= ((bytes[pos / 8] >> (pos % 8)) & 1) << i;
            return v;
        }
    };
    // BC7's fixed partition and anchor tables, row-major. These are format
    // constants (not a decoder dependency). Generated/checked against Mesa's
    // hardware-format decode using the scratch bc7_tables helper.
    // 2 subsets: partition labels (two bits per pixel), fix-up anchors.
    static constexpr unsigned int kPartition2[64] = {
        0x50505050u,
        0x40404040u,
        0x54545454u,
        0x54505040u,
        0x50404000u,
        0x55545450u,
        0x55545040u,
        0x54504000u,
        0x50400000u,
        0x55555450u,
        0x55544000u,
        0x54400000u,
        0x55555440u,
        0x55550000u,
        0x55555500u,
        0x55000000u,
        0x55150100u,
        0x00004054u,
        0x15010000u,
        0x00405054u,
        0x00004050u,
        0x15050100u,
        0x05010000u,
        0x40505054u,
        0x00404050u,
        0x05010100u,
        0x14141414u,
        0x05141450u,
        0x01155440u,
        0x00555500u,
        0x15014054u,
        0x05414150u,
        0x44444444u,
        0x55005500u,
        0x11441144u,
        0x05055050u,
        0x05500550u,
        0x11114444u,
        0x41144114u,
        0x44111144u,
        0x15055054u,
        0x01055040u,
        0x05041050u,
        0x05455150u,
        0x14414114u,
        0x50050550u,
        0x41411414u,
        0x00141400u,
        0x00041504u,
        0x00105410u,
        0x10541000u,
        0x04150400u,
        0x50410514u,
        0x41051450u,
        0x05415014u,
        0x14054150u,
        0x41050514u,
        0x41505014u,
        0x40011554u,
        0x54150140u,
        0x50505500u,
        0x00555050u,
        0x15151010u,
        0x54540404u,
    };
    static constexpr unsigned char kAnchor2[64][3] = {
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 2, 0 },
        { 0, 8, 0 },
        { 0, 2, 0 },
        { 0, 2, 0 },
        { 0, 8, 0 },
        { 0, 8, 0 },
        { 0, 15, 0 },
        { 0, 2, 0 },
        { 0, 8, 0 },
        { 0, 2, 0 },
        { 0, 2, 0 },
        { 0, 8, 0 },
        { 0, 8, 0 },
        { 0, 2, 0 },
        { 0, 2, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 6, 0 },
        { 0, 8, 0 },
        { 0, 2, 0 },
        { 0, 8, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 2, 0 },
        { 0, 8, 0 },
        { 0, 2, 0 },
        { 0, 2, 0 },
        { 0, 2, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 6, 0 },
        { 0, 6, 0 },
        { 0, 2, 0 },
        { 0, 6, 0 },
        { 0, 8, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 2, 0 },
        { 0, 2, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 15, 0 },
        { 0, 2, 0 },
        { 0, 2, 0 },
        { 0, 15, 0 },
    };
    // 3 subsets: partition labels (two bits per pixel), fix-up anchors.
    static constexpr unsigned int kPartition3[64] = {
        0xaa685050u,
        0x6a5a5040u,
        0x5a5a4200u,
        0x5450a0a8u,
        0xa5a50000u,
        0xa0a05050u,
        0x5555a0a0u,
        0x5a5a5050u,
        0xaa550000u,
        0xaa555500u,
        0xaaaa5500u,
        0x90909090u,
        0x94949494u,
        0xa4a4a4a4u,
        0xa9a59450u,
        0x2a0a4250u,
        0xa5945040u,
        0x0a425054u,
        0xa5a5a500u,
        0x55a0a0a0u,
        0xa8a85454u,
        0x6a6a4040u,
        0xa4a45000u,
        0x1a1a0500u,
        0x0050a4a4u,
        0xaaa59090u,
        0x14696914u,
        0x69691400u,
        0xa08585a0u,
        0xaa821414u,
        0x50a4a450u,
        0x6a5a0200u,
        0xa9a58000u,
        0x5090a0a8u,
        0xa8a09050u,
        0x24242424u,
        0x00aa5500u,
        0x24924924u,
        0x24499224u,
        0x50a50a50u,
        0x500aa550u,
        0xaaaa4444u,
        0x66660000u,
        0xa5a0a5a0u,
        0x50a050a0u,
        0x69286928u,
        0x44aaaa44u,
        0x66666600u,
        0xaa444444u,
        0x54a854a8u,
        0x95809580u,
        0x96969600u,
        0xa85454a8u,
        0x80959580u,
        0xaa141414u,
        0x96960000u,
        0xaaaa1414u,
        0xa05050a0u,
        0xa0a5a5a0u,
        0x96000000u,
        0x40804080u,
        0xa9a8a9a8u,
        0xaaaaaa44u,
        0x2a4a5254u,
    };
    static constexpr unsigned char kAnchor3[64][3] = {
        { 0, 3, 15 },
        { 0, 3, 8 },
        { 0, 15, 8 },
        { 0, 15, 3 },
        { 0, 8, 15 },
        { 0, 3, 15 },
        { 0, 15, 3 },
        { 0, 15, 8 },
        { 0, 8, 15 },
        { 0, 8, 15 },
        { 0, 6, 15 },
        { 0, 6, 15 },
        { 0, 6, 15 },
        { 0, 5, 15 },
        { 0, 3, 15 },
        { 0, 3, 8 },
        { 0, 3, 15 },
        { 0, 3, 8 },
        { 0, 8, 15 },
        { 0, 15, 3 },
        { 0, 3, 15 },
        { 0, 3, 8 },
        { 0, 6, 15 },
        { 0, 10, 8 },
        { 0, 5, 3 },
        { 0, 8, 15 },
        { 0, 8, 6 },
        { 0, 6, 10 },
        { 0, 8, 15 },
        { 0, 5, 15 },
        { 0, 15, 10 },
        { 0, 15, 8 },
        { 0, 8, 15 },
        { 0, 15, 3 },
        { 0, 3, 15 },
        { 0, 5, 10 },
        { 0, 6, 10 },
        { 0, 10, 8 },
        { 0, 8, 9 },
        { 0, 15, 10 },
        { 0, 15, 6 },
        { 0, 3, 15 },
        { 0, 15, 8 },
        { 0, 5, 15 },
        { 0, 15, 3 },
        { 0, 15, 6 },
        { 0, 15, 6 },
        { 0, 15, 8 },
        { 0, 3, 15 },
        { 0, 15, 3 },
        { 0, 5, 15 },
        { 0, 5, 15 },
        { 0, 5, 15 },
        { 0, 8, 15 },
        { 0, 5, 15 },
        { 0, 10, 15 },
        { 0, 5, 15 },
        { 0, 10, 15 },
        { 0, 8, 15 },
        { 0, 13, 15 },
        { 0, 15, 3 },
        { 0, 12, 15 },
        { 0, 3, 15 },
        { 0, 3, 8 },
    };
    constexpr int weights[3][16] = { { 0, 21, 43, 64 }, { 0, 9, 18, 27, 37, 46, 55, 64 }, { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 } };
    int expand(int value, int bits) { return (value << (8 - bits)) | (value >> (2 * bits - 8)); }
} // namespace
void bc7Decode(const unsigned char block[16], unsigned char rgba[64])
{
    Bits b { block };
    int mode = 0;
    while (mode < 8 && !b.take(1))
        mode++;
    if (mode == 8) {
        std::memset(rgba, 0, 64);
        return;
    }
    constexpr int subsetsByMode[] = { 3, 2, 3, 2, 1, 1, 1, 2 };
    constexpr int rgbBits[] = { 4, 6, 5, 7, 5, 7, 7, 5 };
    constexpr int alphaBits[] = { 0, 0, 0, 0, 6, 8, 7, 5 };
    constexpr int indexBits[] = { 3, 3, 2, 2, 2, 2, 4, 2 };
    int subsets = subsetsByMode[mode], part = 0, rotation = 0, selection = 0;
    if (subsets > 1) part = b.take(mode == 0 ? 4 : 6);
    if (mode == 4 || mode == 5) {
        rotation = b.take(2);
        if (mode == 4) selection = b.take(1);
    }
    int e[6][4] = {};
    for (int ch = 0; ch < 3; ch++)
        for (int k = 0; k < subsets * 2; k++)
            e[k][ch] = b.take(rgbBits[mode]);
    if (alphaBits[mode])
        for (int k = 0; k < subsets * 2; k++)
            e[k][3] = b.take(alphaBits[mode]);
    bool pbits = mode == 0 || mode == 1 || mode == 3 || mode == 6 || mode == 7;
    if (pbits) {
        for (int k = 0; k < subsets * 2;) {
            int p = b.take(1);
            int count = mode == 1 ? 2 : 1;
            for (int i = 0; i < count; i++, k++)
                for (int ch = 0; ch < (alphaBits[mode] ? 4 : 3); ch++)
                    e[k][ch] = (e[k][ch] << 1) | p;
        }
    }
    for (int k = 0; k < subsets * 2; k++) {
        for (int ch = 0; ch < 3; ch++)
            e[k][ch] = expand(e[k][ch], rgbBits[mode] + pbits);
        e[k][3] = alphaBits[mode] ? expand(e[k][3], alphaBits[mode] + pbits) : 255;
    }
    unsigned partition = subsets == 1 ? 0 : (subsets == 2 ? kPartition2[part] : kPartition3[part]);
    int idx[16], idx2[16] = {};
    for (int pix = 0; pix < 16; pix++) {
        int subset = (partition >> (pix * 2)) & 3;
        int anchor = subsets == 1 ? 0 : (subsets == 2 ? kAnchor2[part][subset] : kAnchor3[part][subset]);
        idx[pix] = b.take(indexBits[mode] - (pix == anchor));
    }
    int secondary = mode == 4 ? 3 : (mode == 5 ? 2 : 0);
    if (secondary)
        for (int pix = 0; pix < 16; pix++)
            idx2[pix] = b.take(secondary - (pix == 0));
    for (int pix = 0; pix < 16; pix++) {
        int subset = (partition >> (pix * 2)) & 3;
        for (int ch = 0; ch < 4; ch++) {
            bool second = secondary && ((ch == 3) != bool(selection));
            int bits = second ? secondary : indexBits[mode];
            int w = weights[bits - 2][second ? idx2[pix] : idx[pix]];
            rgba[pix * 4 + ch] = ((64 - w) * e[subset * 2][ch] + w * e[subset * 2 + 1][ch] + 32) >> 6;
        }
        if (rotation) std::swap(rgba[pix * 4 + 3], rgba[pix * 4 + rotation - 1]);
    }
}
} // namespace fallout::hd
