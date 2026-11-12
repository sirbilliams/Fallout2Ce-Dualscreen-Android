#!/usr/bin/env bash
# Check that an install leaves the saves untouched (read-only on the device's saves).
#   thor-mods-check.sh <dir>          record sha256 + count of files/data/SAVEGAME into <dir>
#   thor-mods-check.sh <dir> after    compare against the record, print SAME/DIFFERENT per file
#   thor-mods-check.sh push-fixture <zip>   copy a fixture zip to /sdcard/Download
# Env: THOR (adb serial, default 10.0.0.56:38239).
set -u
THOR="${THOR:-10.0.0.56:38239}"
PKG=io.github.sirbilliams.fallout2ce.dualscreen.debug
SAVES="files/data/SAVEGAME"

adbq() { timeout 60 adb -s "$THOR" "$@"; }
# Run a command inside the app's sandbox (read only use).
runas() { adbq shell run-as "$PKG" "$@"; }

snapshot() {
    # prints "<sha256>  <relative path>" for each file, sorted by path
    runas sh -c "cd $SAVES 2>/dev/null && find . -type f | sort | while read -r f; do sha256sum \"\$f\"; done" | tr -d '\r'
}

if [ "${1:-}" = push-fixture ]; then
    [ -f "${2:-}" ] || { echo "usage: $0 push-fixture <zip>" >&2; exit 2; }
    timeout 300 adb -s "$THOR" push "$2" /sdcard/Download/ || exit 1
    exit 0
fi

dir="${1:-}"
[ -n "$dir" ] || { echo "usage: $0 <dir> [after] | push-fixture <zip>" >&2; exit 2; }
mkdir -p "$dir"

if [ "${2:-}" != after ]; then
    snapshot > "$dir/saves.sha256" || { echo "adb failed" >&2; exit 1; }
    wc -l < "$dir/saves.sha256" | tr -d ' ' > "$dir/saves.count"
    echo "recorded $(cat "$dir/saves.count") save files in $dir"
    exit 0
fi

[ -f "$dir/saves.sha256" ] || { echo "no record in $dir" >&2; exit 2; }
snapshot > "$dir/saves.after.sha256" || { echo "adb failed" >&2; exit 1; }
rc=0
while read -r sum path; do
    now=$(awk -v p="$path" '$2==p{print $1}' "$dir/saves.after.sha256")
    if [ "$now" = "$sum" ]; then echo "SAME       $path"; else echo "DIFFERENT  $path (${now:-missing})"; rc=1; fi
done < "$dir/saves.sha256"
while read -r _ path; do
    grep -q " $path\$" "$dir/saves.sha256" || { echo "DIFFERENT  $path (new file)"; rc=1; }
done < "$dir/saves.after.sha256"
before=$(cat "$dir/saves.count"); after=$(wc -l < "$dir/saves.after.sha256" | tr -d ' ')
echo "count before=$before after=$after"
[ "$before" = "$after" ] || rc=1
exit $rc
