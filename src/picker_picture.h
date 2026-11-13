#ifndef FALLOUT_PICKER_PICTURE_H_
#define FALLOUT_PICKER_PICTURE_H_
#include <string>
#include <vector>
namespace fallout {
struct PickerPicture {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};
// PRAW, big-endian width/height, tightly packed straight RGBA. Bounded before allocation.
bool pickerPictureLoad(const std::string& path, PickerPicture& picture);
std::string pickerPicturePath(const std::string& top, const std::string& id, bool installed);
} // namespace fallout
#endif
