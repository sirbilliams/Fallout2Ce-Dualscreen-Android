#ifndef FALLOUT_HD_HD_ART_CPU_H_
#define FALLOUT_HD_HD_ART_CPU_H_

// CPU-side HD art reader (no GL). Reads the runtime packs written by
// Dustlight's `fo2hd pack` (see hd_art.h for the pack layout):
//
//   <root>/art/<type>/<name>/pack.json + albedo_<tier>x.dds   (BC7)
//
// and decodes single frames to RGBA8 for the SDL based mui_* screens. A pack
// is only used when the FNV-1a hash of the frame's palette indices matches
// the one in pack.json; on a mismatch (or anything missing) nothing is
// returned and the caller draws the original art.

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace fallout::hd {

struct HdRgba {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA8, width * height * 4, rows top to bottom
};

// Decodes one 16 byte BC7 block to 16 RGBA pixels (row major, 4x4).
void bc7DecodeBlock(const uint8_t* block, uint8_t out[16][4]);

// A BC7 DDS file in memory (DX10 header, dxgiFormat 98 or 99, mip 0).
struct DdsBc7 {
    int width = 0;
    int height = 0;
    const uint8_t* blocks = nullptr; // into the file bytes
    size_t blockBytes = 0;
};
bool ddsParseBc7(const std::vector<uint8_t>& file, DdsBc7& out);

// Decodes the pixel rectangle (x, y, w, h) of the image into RGBA8.
bool ddsDecodeRect(const DdsBc7& dds, int x, int y, int w, int h, HdRgba& out);

// FNV-1a over palette indices (the engine and pack.py hash the same way).
uint32_t hdFnv1a(const uint8_t* data, size_t size);

// "art\intrface\foo.frm" (any case, either slash) -> "art/intrface/foo";
// extensions other than .frm are kept ("hmwarrab.fr0" -> "art/.../hmwarrab_fr0").
bool hdPackKey(const char* path, std::string& key);

class HdArtCpu {
public:
    // root: the pack folder (e.g. "<game>/hd"); tier 2, 3 or 4.
    // True when root exists as a directory.
    bool open(const std::string& root, int tier);
    void close();
    bool isOpen() const { return _open; }
    int tier() const { return _tier; }

    // The HD frame for the art CE has in hand: `indexed` is the original
    // width x height palette indices of (rotation, frame) of `frmPath`.
    // Returns false (out untouched) when there is no pack, the tier file is
    // missing, the frame is not in the pack, the size differs or the hash
    // doesn't match.
    bool load(const char* frmPath, int rotation, int frame, const uint8_t* indexed, int width, int height, HdRgba& out);

private:
    struct Frame {
        int x = 0, y = 0, w = 0, h = 0;
        uint32_t hash = 0;
    };
    struct Pack {
        bool valid = false;
        std::vector<std::vector<Frame>> frames; // per rotation (same_as resolved)
        std::vector<uint8_t> file; // albedo DDS
        DdsBc7 dds;
    };

    Pack* get(const std::string& key);

    bool _open = false;
    int _tier = 2;
    std::string _root;
    std::map<std::string, Pack> _packs;
    std::list<std::string> _lru; // most recent first, valid packs only
};

} // namespace fallout::hd

#endif /* FALLOUT_HD_HD_ART_CPU_H_ */
