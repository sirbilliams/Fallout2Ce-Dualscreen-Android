#include "picker_picture.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#define CHECK(x)                                \
    do {                                        \
        if (!(x)) {                             \
            std::cerr << "failed: " #x << '\n'; \
            return 1;                           \
        }                                       \
    } while (0)
int main()
{
    using namespace fallout;
    char dir[] = "/tmp/picker-picture-XXXXXX";
    CHECK(mkdtemp(dir));
    std::string path = std::string(dir) + "/tiny.rgba";
    const unsigned char tiny[] = { 'P', 'R', 'A', 'W', 0, 0, 0, 2, 0, 0, 0, 1, 255, 0, 0, 255, 0, 255, 0, 128 };
    auto write = [&](const unsigned char* bytes, size_t size) { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(bytes), size); };
    write(tiny, sizeof(tiny));
    PickerPicture picture;
    CHECK(pickerPictureLoad(path, picture));
    CHECK(picture.width == 2 && picture.height == 1 && picture.rgba[0] == 255 && picture.rgba[7] == 128);
    write(tiny, sizeof(tiny) - 1);
    CHECK(!pickerPictureLoad(path, picture) && picture.rgba.empty());
    auto bad = std::vector<unsigned char>(tiny, tiny + sizeof(tiny));
    bad[4] = 255;
    write(bad.data(), bad.size());
    CHECK(!pickerPictureLoad(path, picture));
    bad[0] = 'X';
    write(bad.data(), bad.size());
    CHECK(!pickerPictureLoad(path, picture));
    CHECK(!pickerPictureLoad(path + ".missing", picture));
    CHECK(pickerPicturePath("/top", "ettu", true) == "/top/games/ettu/picker/art.rgba");
    CHECK(pickerPicturePath("", "sonora", false) == "picker-preview/sonora/art.rgba");
    CHECK(pickerPicturePath("/top", "../bad", true).empty());
    std::filesystem::remove_all(dir);
    return 0;
}
