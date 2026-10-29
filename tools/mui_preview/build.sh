#!/bin/bash
# Off-screen preview of the mobile UI's skin and the detailed map (no game
# data, no device): renders with SDL's software renderer to PNG files.
#
#   tools/mui_preview/build.sh <desktop build dir> [output dir]
#
# The build dir is a desktop CMake build of the game (cmake -S . -B <dir> &&
# cmake --build <dir> --target fallout2-ce). The software renderer has no
# custom blend modes: map textures show white or black margins there that
# the device's GPU renderer doesn't.
set -e
BUILD=$(cd "$1" && pwd)
OUT=$(mkdir -p "${2:-preview}" && cd "${2:-preview}" && pwd)
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$BUILD"
INC=$(grep -o '\-I[^ ]*' CMakeFiles/fallout2-ce.dir/flags.make | sort -u | tr '\n' ' ')
DEFS=$(grep "^CXX_DEFINES" CMakeFiles/fallout2-ce.dir/flags.make | cut -d= -f2-)
g++ -std=c++17 -g $DEFS $INC -I"$ROOT/src" -c "$ROOT/tools/mui_preview/preview.cc" -o "$OUT/preview.o"
OBJS=$(ls CMakeFiles/fallout2-ce.dir/src/*.o | grep -v "/win32.cc.o")
c++ -g "$OUT/preview.o" $OBJS CMakeFiles/fallout2-ce.dir/third_party/lodepng/lodepng.cpp.o -o "$OUT/preview" \
    third_party/fpattern/libfpattern-static.a third_party/fpattern/libfpattern-windows-static.a \
    _deps/zlib-build/libz.a _deps/sdl2-build/libSDL2d.a -lm -pthread -ldl
# The UI's font is read from the engine's own data.
cd "$ROOT/files/ce.dat"
"$OUT/preview" "$OUT/bottom.bmp" "$OUT/companion.bmp" "$OUT/widgets.bmp" "$OUT/pieces.bmp" "$OUT/poweron.bmp" "$ROOT/files/ce.dat/text" || GLYPHS=$?
cd "$OUT"
for name in bottom companion widgets pieces poweron; do
    magick "$name.bmp" "$name.png" 2>/dev/null || convert "$name.bmp" "$name.png"
    rm "$name.bmp"
done
echo "Previews in $OUT"
# 2 = the engine's texts use characters the UI font lacks (shown as '?').
exit "${GLYPHS:-0}"
