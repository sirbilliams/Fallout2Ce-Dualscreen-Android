#include "picker_picture.h"
#include <cstdint>
#include <fstream>
namespace fallout {
std::string pickerPicturePath(const std::string& top, const std::string& id, bool installed)
{
    if (id.empty() || id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789") != std::string::npos) return "";
    return (top.empty() ? "" : top + "/") + (installed ? "games/" + id + "/picker" : "picker-preview/" + id) + "/art.rgba";
}
bool pickerPictureLoad(const std::string& path, PickerPicture& picture)
{
    picture = {};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    auto size = file.tellg();
    if (size < 12 || size > 12 + 1920 * 1920 * 4) return false;
    file.seekg(0);
    unsigned char header[12];
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header))) return false;
    if (header[0] != 'P' || header[1] != 'R' || header[2] != 'A' || header[3] != 'W') return false;
    auto integer = [&](int offset) {
        return (uint32_t(header[offset]) << 24) | (uint32_t(header[offset + 1]) << 16)
            | (uint32_t(header[offset + 2]) << 8) | uint32_t(header[offset + 3]);
    };
    uint32_t w = integer(4), h = integer(8);
    if (w == 0 || h == 0 || w > 1920 || h > 1920 || size != 12 + uint64_t(w) * h * 4) return false;
    PickerPicture loaded;
    loaded.rgba.resize(w * h * 4);
    if (!file.read(reinterpret_cast<char*>(loaded.rgba.data()), loaded.rgba.size())) return false;
    loaded.width = w;
    loaded.height = h;
    picture = std::move(loaded);
    return true;
}
} // namespace fallout
