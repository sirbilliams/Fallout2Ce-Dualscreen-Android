#!/bin/bash
# Mod Organizer test fixtures (self-made content only). See docs/mods-testing.md.
#
#   tools/mods-fixtures.sh <game dir copy>   modstore/fixture-a, fixture-b,
#                                            profiles/T (list +a +b, active)
#   tools/mods-fixtures.sh --zips            tests/fixtures/mods/*.zip (committed)
#
# fixture-a/-b override line 20 of text/english/game/misc.msg (the main menu's
# copyright line) with "MODTEST A" / "MODTEST B" and carry a tiny
# data/scripts/modtest.int, so the save compatibility check counts them as
# game logic (removing one = the "x" mark).
# Env: CE_DAT_TOOL = ce-dat-tool binary (default: a build dir next to the repo).
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
tool=${CE_DAT_TOOL:-}
if [ -z "$tool" ]; then
    for t in "$here"/*/ce-dat-tool "$here"/out/build/*/ce-dat-tool "$HOME/Projects/Fallout2Ce-Dualscreen-Android/build-desktop/ce-dat-tool"; do
        [ -x "$t" ] && { tool=$t; break; }
    done
fi

if [ "${1:-}" = "--zips" ]; then
    out="$here/tests/fixtures/mods"
    mkdir -p "$out"
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    msg='{20}{}{MODTEST ZIP}'
    mk() { # mk <dir> : tiny self-made files
        mkdir -p "$tmp/$1"; }
    mk flat/data/text/english/game; echo "$msg" > "$tmp/flat/data/text/english/game/misc.msg"
    mk nested/MyMod/data/text/english/game; echo "$msg" > "$tmp/nested/MyMod/data/text/english/game/misc.msg"
    mk empty; echo "just a readme" > "$tmp/empty/readme.txt"
    mk withexe/data/text/english/game; echo "$msg" > "$tmp/withexe/data/text/english/game/misc.msg"
    echo "not a real program" > "$tmp/withexe/setup.exe"; echo "not a real dll" > "$tmp/withexe/ddraw.dll"
    # dat.zip: a tiny .dat (ce-dat-tool create) inside a zip.
    mk dat/src/text/english/game; echo "$msg" > "$tmp/dat/src/text/english/game/misc.msg"
    if [ -n "$tool" ]; then
        "$tool" create "$tmp/dat/src" "$tmp/dat/mymod.dat" >/dev/null
        rm -rf "$tmp/dat/src"
    else
        # No dat tool: a zip stands in for the dbase (the engine reads zips as dbases).
        (cd "$tmp/dat/src" && python3 -I -m zipfile -c "../mymod.dat" text) ; rm -rf "$tmp/dat/src"
    fi
    for z in flat nested empty withexe dat; do
        rm -f "$out/$z.zip"
        (cd "$tmp/$z" && python3 -I -m zipfile -c "$out/$z.zip" .)
    done
    ls -l "$out"
    exit 0
fi

game=${1:?usage: mods-fixtures.sh <game dir copy> | --zips}
[ -d "$game" ] || { echo "no such dir: $game" >&2; exit 1; }
orig=$(mktemp)
trap 'rm -f "$orig"' EXIT
if [ -n "$tool" ] && [ -f "$game/master.dat" ] && "$tool" "$game/master.dat" cat 'text\english\game\misc.msg' > "$orig" 2>/dev/null && [ -s "$orig" ]; then
    :
else
    echo "warning: original misc.msg not read (set CE_DAT_TOOL); the fixtures hold only line 20" >&2
    echo '{20}{}{Original}' > "$orig"
fi

mkfix() { # mkfix <id> <label>
    local d="$game/modstore/$1/data"
    rm -rf "$game/modstore/$1"
    mkdir -p "$d/text/english/game" "$d/scripts"
    sed "s/^{20}{}{[^}]*}/{20}{}{MODTEST $2}/" "$orig" > "$d/text/english/game/misc.msg"
    printf 'MODTEST %s script placeholder\n' "$2" > "$d/scripts/modtest.int"
}
mkfix fixture-a A
mkfix fixture-b B
grep -q 'MODTEST A' "$game/modstore/fixture-a/data/text/english/game/misc.msg" || { echo "line 20 not found in misc.msg" >&2; exit 1; }

mkdir -p "$game/profiles/T/SAVEGAME"
printf 'T\n' > "$game/profiles/profiles.txt"
printf 'T\n' > "$game/profiles/active.txt"
printf '+fixture-a\n+fixture-b\n' > "$game/profiles/T/modlist.txt"
echo "fixtures ready in $game (profile T active: +fixture-a +fixture-b)"
