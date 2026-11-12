#include "hd_art_cpu.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace fallout::hd {

namespace {

    // Partition tables and anchor indices of BC7 (D3D11 functional spec).
static const uint8_t kPart2[64][16] = {
    {0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1},
    {0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1},
    {0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1},
    {0,0,0,1,0,0,1,1,0,0,1,1,0,1,1,1},
    {0,0,0,0,0,0,0,1,0,0,0,1,0,0,1,1},
    {0,0,1,1,0,1,1,1,0,1,1,1,1,1,1,1},
    {0,0,0,1,0,0,1,1,0,1,1,1,1,1,1,1},
    {0,0,0,0,0,0,0,1,0,0,1,1,0,1,1,1},
    {0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,1},
    {0,0,1,1,0,1,1,1,1,1,1,1,1,1,1,1},
    {0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1},
    {0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1},
    {0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1},
    {0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1},
    {0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1},
    {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1},
    {0,0,0,0,1,0,0,0,1,1,1,0,1,1,1,1},
    {0,1,1,1,0,0,0,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,1,0,0,0,1,1,1,0},
    {0,1,1,1,0,0,1,1,0,0,0,1,0,0,0,0},
    {0,0,1,1,0,0,0,1,0,0,0,0,0,0,0,0},
    {0,0,0,0,1,0,0,0,1,1,0,0,1,1,1,0},
    {0,0,0,0,0,0,0,0,1,0,0,0,1,1,0,0},
    {0,1,1,1,0,0,1,1,0,0,1,1,0,0,0,1},
    {0,0,1,1,0,0,0,1,0,0,0,1,0,0,0,0},
    {0,0,0,0,1,0,0,0,1,0,0,0,1,1,0,0},
    {0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0},
    {0,0,1,1,0,1,1,0,0,1,1,0,1,1,0,0},
    {0,0,0,1,0,1,1,1,1,1,1,0,1,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,1,1,1,0,0,0,1,1,0,0,0,1,1,1,0},
    {0,0,1,1,1,0,0,1,1,0,0,1,1,1,0,0},
    {0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1},
    {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
    {0,1,0,1,1,0,1,0,0,1,0,1,1,0,1,0},
    {0,0,1,1,0,0,1,1,1,1,0,0,1,1,0,0},
    {0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0},
    {0,1,0,1,0,1,0,1,1,0,1,0,1,0,1,0},
    {0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1},
    {0,1,0,1,1,0,1,0,1,0,1,0,0,1,0,1},
    {0,1,1,1,0,0,1,1,1,1,0,0,1,1,1,0},
    {0,0,0,1,0,0,1,1,1,1,0,0,1,0,0,0},
    {0,0,1,1,0,0,1,0,0,1,0,0,1,1,0,0},
    {0,0,1,1,1,0,1,1,1,1,0,1,1,1,0,0},
    {0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0},
    {0,0,1,1,1,1,0,0,1,1,0,0,0,0,1,1},
    {0,1,1,0,0,1,1,0,1,0,0,1,1,0,0,1},
    {0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0},
    {0,1,0,0,1,1,1,0,0,1,0,0,0,0,0,0},
    {0,0,1,0,0,1,1,1,0,0,1,0,0,0,0,0},
    {0,0,0,0,0,0,1,0,0,1,1,1,0,0,1,0},
    {0,0,0,0,0,1,0,0,1,1,1,0,0,1,0,0},
    {0,1,1,0,1,1,0,0,1,0,0,1,0,0,1,1},
    {0,0,1,1,0,1,1,0,1,1,0,0,1,0,0,1},
    {0,1,1,0,0,0,1,1,1,0,0,1,1,1,0,0},
    {0,0,1,1,1,0,0,1,1,1,0,0,0,1,1,0},
    {0,1,1,0,1,1,0,0,1,1,0,0,1,0,0,1},
    {0,1,1,0,0,0,1,1,0,0,1,1,1,0,0,1},
    {0,1,1,1,1,1,1,0,1,0,0,0,0,0,0,1},
    {0,0,0,1,1,0,0,0,1,1,1,0,0,1,1,1},
    {0,0,0,0,1,1,1,1,0,0,1,1,0,0,1,1},
    {0,0,1,1,0,0,1,1,1,1,1,1,0,0,0,0},
    {0,0,1,0,0,0,1,0,1,1,1,0,1,1,1,0},
    {0,1,0,0,0,1,0,0,0,1,1,1,0,1,1,1}
};
static const uint8_t kPart3[64][16] = {
    {0,0,1,1,0,0,1,1,0,2,2,1,2,2,2,2},
    {0,0,0,1,0,0,1,1,2,2,1,1,2,2,2,1},
    {0,0,0,0,2,0,0,1,2,2,1,1,2,2,1,1},
    {0,2,2,2,0,0,2,2,0,0,1,1,0,1,1,1},
    {0,0,0,0,0,0,0,0,1,1,2,2,1,1,2,2},
    {0,0,1,1,0,0,1,1,0,0,2,2,0,0,2,2},
    {0,0,2,2,0,0,2,2,1,1,1,1,1,1,1,1},
    {0,0,1,1,0,0,1,1,2,2,1,1,2,2,1,1},
    {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2},
    {0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2},
    {0,0,0,0,1,1,1,1,2,2,2,2,2,2,2,2},
    {0,0,1,2,0,0,1,2,0,0,1,2,0,0,1,2},
    {0,1,1,2,0,1,1,2,0,1,1,2,0,1,1,2},
    {0,1,2,2,0,1,2,2,0,1,2,2,0,1,2,2},
    {0,0,1,1,0,1,1,2,1,1,2,2,1,2,2,2},
    {0,0,1,1,2,0,0,1,2,2,0,0,2,2,2,0},
    {0,0,0,1,0,0,1,1,0,1,1,2,1,1,2,2},
    {0,1,1,1,0,0,1,1,2,0,0,1,2,2,0,0},
    {0,0,0,0,1,1,2,2,1,1,2,2,1,1,2,2},
    {0,0,2,2,0,0,2,2,0,0,2,2,1,1,1,1},
    {0,1,1,1,0,1,1,1,0,2,2,2,0,2,2,2},
    {0,0,0,1,0,0,0,1,2,2,2,1,2,2,2,1},
    {0,0,0,0,0,0,1,1,0,1,2,2,0,1,2,2},
    {0,0,0,0,1,1,0,0,2,2,1,0,2,2,1,0},
    {0,1,2,2,0,1,2,2,0,0,1,1,0,0,0,0},
    {0,0,1,2,0,0,1,2,1,1,2,2,2,2,2,2},
    {0,1,1,0,1,2,2,1,1,2,2,1,0,1,1,0},
    {0,0,0,0,0,1,1,0,1,2,2,1,1,2,2,1},
    {0,0,2,2,1,1,0,2,1,1,0,2,0,0,2,2},
    {0,1,1,0,0,1,1,0,2,0,0,2,2,2,2,2},
    {0,0,1,1,0,1,2,2,0,1,2,2,0,0,1,1},
    {0,0,0,0,2,0,0,0,2,2,1,1,2,2,2,1},
    {0,0,0,0,0,0,0,2,1,1,2,2,1,2,2,2},
    {0,2,2,2,0,0,2,2,0,0,1,2,0,0,1,1},
    {0,0,1,1,0,0,1,2,0,0,2,2,0,2,2,2},
    {0,1,2,0,0,1,2,0,0,1,2,0,0,1,2,0},
    {0,0,0,0,1,1,1,1,2,2,2,2,0,0,0,0},
    {0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0},
    {0,1,2,0,2,0,1,2,1,2,0,1,0,1,2,0},
    {0,0,1,1,2,2,0,0,1,1,2,2,0,0,1,1},
    {0,0,1,1,1,1,2,2,2,2,0,0,0,0,1,1},
    {0,1,0,1,0,1,0,1,2,2,2,2,2,2,2,2},
    {0,0,0,0,0,0,0,0,2,1,2,1,2,1,2,1},
    {0,0,2,2,1,1,2,2,0,0,2,2,1,1,2,2},
    {0,0,2,2,0,0,1,1,0,0,2,2,0,0,1,1},
    {0,2,2,0,1,2,2,1,0,2,2,0,1,2,2,1},
    {0,1,0,1,2,2,2,2,2,2,2,2,0,1,0,1},
    {0,0,0,0,2,1,2,1,2,1,2,1,2,1,2,1},
    {0,1,0,1,0,1,0,1,0,1,0,1,2,2,2,2},
    {0,2,2,2,0,1,1,1,0,2,2,2,0,1,1,1},
    {0,0,0,2,1,1,1,2,0,0,0,2,1,1,1,2},
    {0,0,0,0,2,1,1,2,2,1,1,2,2,1,1,2},
    {0,2,2,2,0,1,1,1,0,1,1,1,0,2,2,2},
    {0,0,0,2,1,1,1,2,1,1,1,2,0,0,0,2},
    {0,1,1,0,0,1,1,0,0,1,1,0,2,2,2,2},
    {0,0,0,0,0,0,0,0,2,1,1,2,2,1,1,2},
    {0,1,1,0,0,1,1,0,2,2,2,2,2,2,2,2},
    {0,0,2,2,0,0,1,1,0,0,1,1,0,0,2,2},
    {0,0,2,2,1,1,2,2,1,1,2,2,0,0,2,2},
    {0,0,0,0,0,0,0,0,0,0,0,0,2,1,1,2},
    {0,0,0,2,0,0,0,1,0,0,0,2,0,0,0,1},
    {0,2,2,2,1,2,2,2,0,2,2,2,1,2,2,2},
    {0,1,0,1,2,2,2,2,2,2,2,2,2,2,2,2},
    {0,1,1,1,2,0,1,1,2,2,0,1,2,2,2,0}
};
static const uint8_t kAnchor2[64] = {15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,2,8,2,2,8,8,15,2,8,2,2,8,8,2,2,15,15,6,8,2,8,15,15,2,8,2,2,2,15,15,6,6,2,6,8,15,15,2,2,15,15,15,15,15,2,2,15};
static const uint8_t kAnchor3a[64] = {3,3,15,15,8,3,15,15,8,8,6,6,6,5,3,3,3,3,8,15,3,3,6,10,5,8,8,6,8,5,15,15,8,15,3,5,6,10,8,15,15,3,15,5,15,15,15,15,3,15,5,5,5,8,5,10,5,10,8,13,15,12,3,3};
static const uint8_t kAnchor3b[64] = {15,8,8,3,15,15,3,8,15,15,15,15,15,15,15,8,15,8,15,3,15,8,15,8,3,15,6,10,15,15,10,8,15,3,15,10,10,8,9,10,6,15,8,15,3,6,6,8,15,3,15,15,15,15,15,15,15,15,15,15,3,15,15,8};

    struct ModeInfo {
        int ns, pb, rb, isb, cb, ab, epb, spb, ib, ib2;
    };
    const ModeInfo kModes[8] = {
        { 3, 4, 0, 0, 4, 0, 1, 0, 3, 0 },
        { 2, 6, 0, 0, 6, 0, 0, 1, 3, 0 },
        { 3, 6, 0, 0, 5, 0, 0, 0, 2, 0 },
        { 2, 6, 0, 0, 7, 0, 1, 0, 2, 0 },
        { 1, 0, 2, 1, 5, 6, 0, 0, 2, 3 },
        { 1, 0, 2, 0, 7, 8, 0, 0, 2, 2 },
        { 1, 0, 0, 0, 7, 7, 1, 0, 4, 0 },
        { 2, 6, 0, 0, 5, 5, 1, 0, 2, 0 },
    };

    const int kWeight2[4] = { 0, 21, 43, 64 };
    const int kWeight3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
    const int kWeight4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

    int weightOf(int bits, int index)
    {
        return bits == 2 ? kWeight2[index & 3] : (bits == 3 ? kWeight3[index & 7] : kWeight4[index & 15]);
    }

    struct BitReader {
        const uint8_t* data;
        int pos = 0;
        unsigned read(int n)
        {
            unsigned v = 0;
            for (int i = 0; i < n; i++, pos++) {
                v |= static_cast<unsigned>((data[pos >> 3] >> (pos & 7)) & 1) << i;
            }
            return v;
        }
    };

    int expand8(int v, int n)
    {
        return n >= 8 ? v : (v << (8 - n)) | (v >> (2 * n - 8));
    }

    int lerp(int a, int b, int w)
    {
        return (a * (64 - w) + b * w + 32) >> 6;
    }

} // namespace

void bc7DecodeBlock(const uint8_t* block, uint8_t out[16][4])
{
    int mode = 0;
    while (mode < 8 && !((block[0] >> mode) & 1)) {
        mode++;
    }
    if (mode >= 8) {
        std::memset(out, 0, 16 * 4); // reserved
        return;
    }
    const ModeInfo& m = kModes[mode];
    BitReader br { block };
    br.pos = mode + 1;
    int partition = m.pb ? static_cast<int>(br.read(m.pb)) : 0;
    int rotation = m.rb ? static_cast<int>(br.read(m.rb)) : 0;
    int isb = m.isb ? static_cast<int>(br.read(1)) : 0;

    int ep[6][4];
    for (int ch = 0; ch < 3; ch++) {
        for (int e = 0; e < m.ns * 2; e++) {
            ep[e][ch] = static_cast<int>(br.read(m.cb));
        }
    }
    for (int e = 0; e < m.ns * 2; e++) {
        ep[e][3] = m.ab ? static_cast<int>(br.read(m.ab)) : 255;
    }
    int pbit[6] = { 0, 0, 0, 0, 0, 0 };
    bool hasP = m.epb || m.spb;
    if (m.epb) {
        for (int e = 0; e < m.ns * 2; e++) {
            pbit[e] = static_cast<int>(br.read(1));
        }
    } else if (m.spb) {
        for (int s = 0; s < m.ns; s++) {
            pbit[2 * s] = pbit[2 * s + 1] = static_cast<int>(br.read(1));
        }
    }
    for (int e = 0; e < m.ns * 2; e++) {
        for (int ch = 0; ch < 3; ch++) {
            int v = ep[e][ch];
            int n = m.cb;
            if (hasP) {
                v = (v << 1) | pbit[e];
                n++;
            }
            ep[e][ch] = expand8(v, n);
        }
        if (m.ab) {
            int v = ep[e][3];
            int n = m.ab;
            if (hasP) {
                v = (v << 1) | pbit[e];
                n++;
            }
            ep[e][3] = expand8(v, n);
        }
    }

    int subset[16];
    bool anchor[16];
    for (int i = 0; i < 16; i++) {
        subset[i] = m.ns == 1 ? 0 : (m.ns == 2 ? kPart2[partition][i] : kPart3[partition][i]);
        anchor[i] = i == 0 || (m.ns == 2 && i == kAnchor2[partition])
            || (m.ns == 3 && (i == kAnchor3a[partition] || i == kAnchor3b[partition]));
    }

    int idxA[16];
    int idxB[16];
    for (int i = 0; i < 16; i++) {
        idxA[i] = static_cast<int>(br.read(anchor[i] ? m.ib - 1 : m.ib));
    }
    if (m.ib2) {
        for (int i = 0; i < 16; i++) {
            idxB[i] = static_cast<int>(br.read(anchor[i] ? m.ib2 - 1 : m.ib2));
        }
    }

    for (int i = 0; i < 16; i++) {
        int s = subset[i];
        int colorBits = m.ib;
        int alphaBits = m.ib;
        int ci = idxA[i];
        int ai = idxA[i];
        if (m.ib2) {
            if (isb) {
                ci = idxB[i];
                colorBits = m.ib2;
                ai = idxA[i];
                alphaBits = m.ib;
            } else {
                ci = idxA[i];
                colorBits = m.ib;
                ai = idxB[i];
                alphaBits = m.ib2;
            }
        }
        int wc = weightOf(colorBits, ci);
        int wa = weightOf(alphaBits, ai);
        int px[4];
        for (int ch = 0; ch < 3; ch++) {
            px[ch] = lerp(ep[2 * s][ch], ep[2 * s + 1][ch], wc);
        }
        px[3] = lerp(ep[2 * s][3], ep[2 * s + 1][3], wa);
        if (rotation == 1) {
            std::swap(px[0], px[3]);
        } else if (rotation == 2) {
            std::swap(px[1], px[3]);
        } else if (rotation == 3) {
            std::swap(px[2], px[3]);
        }
        for (int ch = 0; ch < 4; ch++) {
            out[i][ch] = static_cast<uint8_t>(px[ch]);
        }
    }
}

namespace {
    uint32_t rd32(const uint8_t* p)
    {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
} // namespace

bool ddsParseBc7(const std::vector<uint8_t>& file, DdsBc7& out)
{
    if (file.size() < 148 || std::memcmp(file.data(), "DDS ", 4) != 0 || rd32(&file[4]) != 124) {
        return false;
    }
    if (std::memcmp(&file[84], "DX10", 4) != 0) {
        return false;
    }
    uint32_t dxgi = rd32(&file[128]);
    if (dxgi != 98 && dxgi != 99) {
        return false;
    }
    int h = static_cast<int>(rd32(&file[12]));
    int w = static_cast<int>(rd32(&file[16]));
    if (w <= 0 || h <= 0 || w > 65536 || h > 65536) {
        return false;
    }
    size_t need = static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * 16;
    if (file.size() - 148 < need) {
        return false;
    }
    out.width = w;
    out.height = h;
    out.blocks = file.data() + 148;
    out.blockBytes = need;
    return true;
}

bool ddsDecodeRect(const DdsBc7& dds, int x, int y, int w, int h, HdRgba& out)
{
    if (dds.blocks == nullptr || x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > dds.width || y + h > dds.height) {
        return false;
    }
    int blocksPerRow = (dds.width + 3) / 4;
    out.width = w;
    out.height = h;
    out.pixels.assign(static_cast<size_t>(w) * h * 4, 0);
    for (int by = y / 4; by <= (y + h - 1) / 4; by++) {
        for (int bx = x / 4; bx <= (x + w - 1) / 4; bx++) {
            uint8_t px[16][4];
            bc7DecodeBlock(dds.blocks + (static_cast<size_t>(by) * blocksPerRow + bx) * 16, px);
            for (int i = 0; i < 16; i++) {
                int gx = bx * 4 + (i & 3);
                int gy = by * 4 + (i >> 2);
                if (gx < x || gx >= x + w || gy < y || gy >= y + h) {
                    continue;
                }
                std::memcpy(&out.pixels[(static_cast<size_t>(gy - y) * w + (gx - x)) * 4], px[i], 4);
            }
        }
    }
    return true;
}

uint32_t hdFnv1a(const uint8_t* data, size_t size)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < size; i++) {
        h = (h ^ data[i]) * 16777619u;
    }
    return h;
}

bool hdPackKey(const char* path, std::string& key)
{
    if (path == nullptr || *path == '\0') {
        return false;
    }
    std::string s(path);
    for (char& c : s) {
        c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    size_t art = s.find("art/");
    if (art == std::string::npos) {
        return false;
    }
    s = s.substr(art);
    size_t dot = s.rfind('.');
    if (dot == std::string::npos || s.find('/', dot) != std::string::npos) {
        return false;
    }
    std::string ext = s.substr(dot + 1);
    s.resize(dot);
    if (ext != "frm") {
        s += "_" + ext;
    }
    key = s;
    return true;
}

// ------------------------------------------------------------------ JSON
// Just enough JSON for pack.json: only arrays of numbers inside
// "rotations" matter, everything else is skipped.

namespace {

    struct JsonCursor {
        const char* p;
        const char* end;
        void ws()
        {
            while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) {
                p++;
            }
        }
        bool eat(char c)
        {
            ws();
            if (p < end && *p == c) {
                p++;
                return true;
            }
            return false;
        }
        bool str(std::string& s)
        {
            ws();
            if (p >= end || *p != '"') {
                return false;
            }
            p++;
            s.clear();
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) {
                    p++;
                }
                s += *p++;
            }
            if (p >= end) {
                return false;
            }
            p++;
            return true;
        }
        bool number(double& v)
        {
            ws();
            char* e = nullptr;
            v = std::strtod(p, &e);
            if (e == p) {
                return false;
            }
            p = e;
            return true;
        }
        bool skip()
        {
            ws();
            if (p >= end) {
                return false;
            }
            if (*p == '"') {
                std::string s;
                return str(s);
            }
            if (*p == '{' || *p == '[') {
                char close = *p == '{' ? '}' : ']';
                p++;
                if (eat(close)) {
                    return true;
                }
                do {
                    if (close == '}') {
                        std::string k;
                        if (!str(k) || !eat(':')) {
                            return false;
                        }
                    }
                    if (!skip()) {
                        return false;
                    }
                } while (eat(','));
                return eat(close);
            }
            while (p < end && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n') {
                p++;
            }
            return true;
        }
    };

    bool readFile(const std::string& path, std::vector<uint8_t>& out)
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f == nullptr) {
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size <= 0) {
            std::fclose(f);
            return false;
        }
        out.resize(static_cast<size_t>(size));
        bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
        std::fclose(f);
        return ok;
    }

} // namespace

bool HdArtCpu::open(const std::string& root, int tier)
{
    close();
    struct stat st;
    if (tier < 2 || tier > 4 || ::stat(root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        return false;
    }
    _root = root;
    while (_root.size() > 1 && (_root.back() == '/' || _root.back() == '\\')) {
        _root.pop_back();
    }
    _tier = tier;
    _open = true;
    return true;
}

void HdArtCpu::close()
{
    _open = false;
    _packs.clear();
    _lru.clear();
}

HdArtCpu::Pack* HdArtCpu::get(const std::string& key)
{
    constexpr size_t kMaxResident = 12; // decoded-on-demand DDS files kept in memory
    auto it = _packs.find(key);
    if (it != _packs.end()) {
        if (it->second.valid) {
            _lru.remove(key);
            _lru.push_front(key);
            return &it->second;
        }
        return nullptr;
    }

    Pack& pack = _packs[key]; // stays invalid on any failure (negative cache)
    std::string dir = _root + "/" + key;
    std::vector<uint8_t> json;
    if (!readFile(dir + "/pack.json", json)) {
        return nullptr;
    }

    // Walk: object -> "rotations": [ {"same_as": n} | {"frames": [[x,y,w,h,hash],...]} ]
    JsonCursor c { reinterpret_cast<const char*>(json.data()), reinterpret_cast<const char*>(json.data()) + json.size() };
    std::vector<int> sameAs;
    std::vector<std::vector<Frame>> rots;
    bool haveRot = false;
    if (!c.eat('{')) {
        return nullptr;
    }
    do {
        std::string k;
        if (!c.str(k) || !c.eat(':')) {
            return nullptr;
        }
        if (k != "rotations") {
            if (!c.skip()) {
                return nullptr;
            }
            continue;
        }
        haveRot = true;
        if (!c.eat('[')) {
            return nullptr;
        }
        if (!c.eat(']')) {
            do {
                if (!c.eat('{')) {
                    return nullptr;
                }
                int same = -1;
                std::vector<Frame> frames;
                if (!c.eat('}')) {
                    do {
                        std::string rk;
                        if (!c.str(rk) || !c.eat(':')) {
                            return nullptr;
                        }
                        if (rk == "same_as") {
                            double v;
                            if (!c.number(v)) {
                                return nullptr;
                            }
                            same = static_cast<int>(v);
                        } else if (rk == "frames") {
                            if (!c.eat('[')) {
                                return nullptr;
                            }
                            if (!c.eat(']')) {
                                do {
                                    if (!c.eat('[')) {
                                        return nullptr;
                                    }
                                    double v[5];
                                    for (int i = 0; i < 5; i++) {
                                        if ((i > 0 && !c.eat(',')) || !c.number(v[i])) {
                                            return nullptr;
                                        }
                                    }
                                    if (!c.eat(']')) {
                                        return nullptr;
                                    }
                                    Frame f;
                                    f.x = static_cast<int>(v[0]);
                                    f.y = static_cast<int>(v[1]);
                                    f.w = static_cast<int>(v[2]);
                                    f.h = static_cast<int>(v[3]);
                                    f.hash = static_cast<uint32_t>(v[4]);
                                    frames.push_back(f);
                                } while (c.eat(','));
                                if (!c.eat(']')) {
                                    return nullptr;
                                }
                            }
                        } else if (!c.skip()) {
                            return nullptr;
                        }
                    } while (c.eat(','));
                    if (!c.eat('}')) {
                        return nullptr;
                    }
                }
                sameAs.push_back(same);
                rots.push_back(std::move(frames));
            } while (c.eat(','));
            if (!c.eat(']')) {
                return nullptr;
            }
        }
    } while (c.eat(','));
    if (!haveRot || rots.empty()) {
        return nullptr;
    }
    for (size_t r = 0; r < rots.size(); r++) {
        int hops = 0;
        size_t cur = r;
        while (sameAs[cur] >= 0 && hops++ < 8 && static_cast<size_t>(sameAs[cur]) < rots.size()) {
            cur = static_cast<size_t>(sameAs[cur]);
        }
        if (cur != r) {
            rots[r] = rots[cur];
        }
    }

    char name[32];
    std::snprintf(name, sizeof(name), "/albedo_%dx.dds", _tier);
    if (!readFile(dir + name, pack.file) || !ddsParseBc7(pack.file, pack.dds)) {
        pack.file.clear();
        return nullptr;
    }
    pack.frames = std::move(rots);
    pack.valid = true;
    _lru.push_front(key);
    while (_lru.size() > kMaxResident) {
        Pack& old = _packs[_lru.back()];
        old = Pack(); // drops the file, forgets nothing else: it reloads on next use
        _packs.erase(_lru.back());
        _lru.pop_back();
    }
    return &pack;
}

bool HdArtCpu::load(const char* frmPath, int rotation, int frame, const uint8_t* indexed, int width, int height, HdRgba& out)
{
    std::string key;
    if (!_open || indexed == nullptr || width <= 0 || height <= 0 || !hdPackKey(frmPath, key)) {
        return false;
    }
    Pack* pack = get(key);
    if (pack == nullptr || rotation < 0 || static_cast<size_t>(rotation) >= pack->frames.size()) {
        return false;
    }
    const std::vector<Frame>& frames = pack->frames[rotation];
    if (frame < 0 || static_cast<size_t>(frame) >= frames.size()) {
        return false;
    }
    const Frame& f = frames[frame];
    if (f.w != width || f.h != height) {
        return false;
    }
    if (hdFnv1a(indexed, static_cast<size_t>(width) * height) != f.hash) {
        return false;
    }
    HdRgba tmp;
    if (!ddsDecodeRect(pack->dds, f.x * _tier, f.y * _tier, f.w * _tier, f.h * _tier, tmp)) {
        return false;
    }
    out = std::move(tmp);
    return true;
}

} // namespace fallout::hd
