// Unit test for src/hd/hd_art_cpu: a synthetic pack in Dustlight's format
// (pack.json + a tiny BC7 DDS), decoded against pixels worked out by hand,
// and the hash-mismatch fallback. No GL, no SDL.

#include "hd/hd_art_cpu.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace fallout::hd;

static int gFailures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            gFailures++;                                                  \
        }                                                                 \
    } while (0)

struct Bits {
    std::vector<int> bits;
    void put(unsigned v, int n)
    {
        for (int i = 0; i < n; i++) {
            bits.push_back((v >> i) & 1);
        }
    }
    void block(uint8_t out[16]) const
    {
        std::memset(out, 0, 16);
        for (size_t i = 0; i < bits.size(); i++) {
            out[i >> 3] |= static_cast<uint8_t>(bits[i] << (i & 7));
        }
    }
};

// Mode 6 block: endpoint e0 / e1 as 7 bit RGBA + pbit each; pixel i has
// index idx[i] (pixel 0 is the anchor: 3 bits, top bit implicit 0).
static void mode6(uint8_t out[16], const int e0[4], int p0, const int e1[4], int p1, const int idx[16])
{
    Bits b;
    b.put(1u << 6, 7);
    for (int ch = 0; ch < 4; ch++) {
        b.put(e0[ch], 7);
        b.put(e1[ch], 7);
    }
    b.put(p0, 1);
    b.put(p1, 1);
    for (int i = 0; i < 16; i++) {
        b.put(idx[i], i == 0 ? 3 : 4);
    }
    b.block(out);
}

static void put32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; i++) {
        v.push_back(static_cast<uint8_t>(x >> (8 * i)));
    }
}

static std::vector<uint8_t> makeDds(int w, int h, const std::vector<uint8_t>& blocks)
{
    std::vector<uint8_t> f = { 'D', 'D', 'S', ' ' };
    put32(f, 124);
    put32(f, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000);
    put32(f, h);
    put32(f, w);
    put32(f, w * h);
    put32(f, 0);
    put32(f, 1);
    for (int i = 0; i < 11; i++) put32(f, 0);
    put32(f, 32);
    put32(f, 4);
    f.push_back('D'); f.push_back('X'); f.push_back('1'); f.push_back('0');
    for (int i = 0; i < 5; i++) put32(f, 0);
    put32(f, 0x1000);
    for (int i = 0; i < 4; i++) put32(f, 0);
    put32(f, 98); // BC7_UNORM
    put32(f, 3);
    put32(f, 0);
    put32(f, 1);
    put32(f, 0);
    f.insert(f.end(), blocks.begin(), blocks.end());
    return f;
}

static void writeFile(const std::string& path, const void* data, size_t size)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::printf("cannot write %s\n", path.c_str());
        std::exit(2);
    }
    std::fwrite(data, 1, size, f);
    std::fclose(f);
}

static const int kW4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

int main()
{
    // FNV-1a test vectors.
    CHECK(hdFnv1a(reinterpret_cast<const uint8_t*>("a"), 1) == 0xE40C292Cu);
    CHECK(hdFnv1a(nullptr, 0) == 2166136261u);

    std::string key;
    CHECK(hdPackKey("ART\\INTRFACE\\Mainmenu.FRM", key) && key == "art/intrface/mainmenu");
    CHECK(hdPackKey("art/critters/hmwarrab.fr0", key) && key == "art/critters/hmwarrab_fr0");
    CHECK(!hdPackKey("data/foo.frm", key));

    char tmpl[] = "/tmp/hd_art_cpu_test_XXXXXX";
    char* root = mkdtemp(tmpl);
    CHECK(root != nullptr);
    if (root == nullptr) return 1;
    std::string dir = std::string(root) + "/art/intrface/testfrm";
    mkdir((std::string(root) + "/art").c_str(), 0700);
    mkdir((std::string(root) + "/art/intrface").c_str(), 0700);
    mkdir(dir.c_str(), 0700);

    // Atlas at 1x: 8 x 4. Frame A (0,0) 4x4, frame B (4,0) 4x4. Tier 2 file = 16 x 8
    // = 4 x 2 blocks; frame A covers blocks (0,0),(1,0),(0,1),(1,1), B (2,0),(3,0),(2,1),(3,1).
    // A: gradient black(alpha 0) -> white, pixel i has index i.
    // B: flat (101, 51, 25, 255) from 7 bit values 50, 25, 12, 127 with pbit 1.
    int idx[16];
    for (int i = 0; i < 16; i++) idx[i] = i;
    int zero[4] = { 0, 0, 0, 0 };
    int full[4] = { 127, 127, 127, 127 };
    uint8_t gradient[16];
    mode6(gradient, zero, 0, full, 1, idx);
    int flat0[4] = { 50, 25, 12, 127 };
    int idx0[16] = {};
    uint8_t flat[16];
    mode6(flat, flat0, 1, flat0, 1, idx0);
    std::vector<uint8_t> blocks;
    for (int by = 0; by < 2; by++) {
        for (int bx = 0; bx < 4; bx++) {
            const uint8_t* src = bx < 2 ? gradient : flat;
            blocks.insert(blocks.end(), src, src + 16);
        }
    }
    std::vector<uint8_t> dds = makeDds(16, 8, blocks);
    writeFile(dir + "/albedo_2x.dds", dds.data(), dds.size());

    // Source palette indices for the frames: A = 0..15, B = 100.
    uint8_t srcA[16], srcB[16];
    for (int i = 0; i < 16; i++) { srcA[i] = static_cast<uint8_t>(i); srcB[i] = 100; }
    uint32_t hashA = hdFnv1a(srcA, 16);
    uint32_t hashB = hdFnv1a(srcB, 16);
    char json[512];
    std::snprintf(json, sizeof(json),
        "{\"version\":1,\"source\":\"art\\\\intrface\\\\testfrm.frm\",\"category\":\"interface\",\"hard_alpha\":false,"
        "\"tiers\":[2,3,4],\"layout\":[8,4],\"rotations\":[{\"frames\":[[0,0,4,4,%u],[4,0,4,4,%u]]},{\"same_as\":0}]}",
        hashA, hashB);
    writeFile(dir + "/pack.json", json, std::strlen(json));

    HdArtCpu art;
    CHECK(!art.open(std::string(root) + "/nope", 2));
    CHECK(!art.open(root, 7));
    CHECK(art.open(root, 2));

    HdRgba out;
    CHECK(art.load("art\\intrface\\testfrm.frm", 0, 0, srcA, 4, 4, out));
    CHECK(out.width == 8 && out.height == 8 && out.pixels.size() == 8 * 8 * 4);
    bool gradOk = out.width == 8;
    for (int y = 0; y < 8 && gradOk; y++) {
        for (int x = 0; x < 8; x++) {
            int w = kW4[(y % 4) * 4 + (x % 4)];
            int expect = (255 * w + 32) >> 6; // e0 = 0, e1 = 255
            const uint8_t* p = &out.pixels[(y * 8 + x) * 4];
            if (p[0] != expect || p[1] != expect || p[2] != expect || p[3] != expect) {
                std::printf("gradient mismatch at %d,%d: %d %d %d %d expected %d\n", x, y, p[0], p[1], p[2], p[3], expect);
                gradOk = false;
                break;
            }
        }
    }
    CHECK(gradOk);

    HdRgba flatOut;
    CHECK(art.load("ART\\INTRFACE\\TESTFRM.FRM", 0, 1, srcB, 4, 4, flatOut));
    CHECK(flatOut.width == 8 && flatOut.height == 8);
    bool flatOk = flatOut.width == 8;
    for (int i = 0; i < 64 && flatOk; i++) {
        const uint8_t* p = &flatOut.pixels[i * 4];
        // (v << 1 | 1) expanded from 8 bits: 101, 51, 25, 255
        flatOk = p[0] == 101 && p[1] == 51 && p[2] == 25 && p[3] == 255;
    }
    CHECK(flatOk);

    // same_as rotation reads rotation 0's frames.
    HdRgba rot;
    CHECK(art.load("art\\intrface\\testfrm.frm", 1, 0, srcA, 4, 4, rot));

    // Hash mismatch: a patched FRM falls back (nothing returned, out untouched).
    uint8_t patched[16];
    std::memcpy(patched, srcA, 16);
    patched[5] ^= 1;
    HdRgba untouched;
    untouched.width = 123;
    CHECK(!art.load("art\\intrface\\testfrm.frm", 0, 0, patched, 4, 4, untouched));
    CHECK(untouched.width == 123 && untouched.pixels.empty());

    // Size mismatch, bad frame / rotation, unknown FRM, no tier file.
    CHECK(!art.load("art\\intrface\\testfrm.frm", 0, 0, srcA, 2, 8, out));
    CHECK(!art.load("art\\intrface\\testfrm.frm", 0, 2, srcA, 4, 4, out));
    CHECK(!art.load("art\\intrface\\testfrm.frm", 5, 0, srcA, 4, 4, out));
    CHECK(!art.load("art\\intrface\\other.frm", 0, 0, srcA, 4, 4, out));
    CHECK(!art.load("not-art.frm", 0, 0, srcA, 4, 4, out));
    CHECK(art.open(root, 3));
    CHECK(!art.load("art\\intrface\\testfrm.frm", 0, 0, srcA, 4, 4, out));
    art.close();
    CHECK(!art.load("art\\intrface\\testfrm.frm", 0, 0, srcA, 4, 4, out));

    // Cleanup (own temp files only).
    unlink((dir + "/albedo_2x.dds").c_str());
    unlink((dir + "/pack.json").c_str());
    rmdir(dir.c_str());
    rmdir((std::string(root) + "/art/intrface").c_str());
    rmdir((std::string(root) + "/art").c_str());
    rmdir(root);

    if (gFailures == 0) {
        std::printf("hd_art_cpu_test: all passed\n");
    }
    return gFailures == 0 ? 0 : 1;
}
