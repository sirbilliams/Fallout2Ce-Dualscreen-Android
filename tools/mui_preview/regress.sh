#!/bin/bash
# Screenshot regression for the bottom-screen UI without game data:
# renders tools/mui_preview's fixtures, checks the engine texts for glyphs
# the font lacks, compares the PNGs with tools/mui_preview/golden.
#
#   tools/mui_preview/regress.sh <desktop build dir> [output dir]
#   (--update as a third argument after an intended UI change refreshes the goldens)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="${2:-preview}"
rc=0
timeout 600 bash "$HERE/build.sh" "$1" "$OUT" || { echo "preview build/render/glyph check failed ($?)"; rc=1; }
OUT=$(cd "$OUT" && pwd)
if [ "${3:-}" = "--update" ]; then
    python3 "$HERE/compare_golden.py" "$OUT" --update
    exit 0
fi
python3 "$HERE/compare_golden.py" "$OUT" "$OUT/diff" || rc=1
exit $rc
