#ifndef FALLOUT_HD_BC7_H
#define FALLOUT_HD_BC7_H
namespace fallout::hd {
// Decode one BC7 block, row-major RGBA8. All eight modes, linear bytes;
// DDS sRGB declarations intentionally use UNORM, as in Dustlight.
void bc7Decode(const unsigned char block[16], unsigned char rgba[64]);
} // namespace fallout::hd
#endif
