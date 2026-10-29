#!/usr/bin/env bash
# Runs inside the emulator (reactivecircus/android-emulator-runner). Output goes to ./smoke/.
# Fails on: FATAL EXCEPTION, native crash (Fatal signal / tombstone), UnsatisfiedLinkError,
# ANR, install/launch failure, the import screen not showing, or our process dying.
set -u
PKG=io.github.sirbilliams.fallout2ce.dualscreen.debug
MAIN="$PKG/com.alexbatalov.fallout2ce.MainActivity"
OUT=smoke; mkdir -p "$OUT"
FAIL=0
fail() { echo "::error::$*"; FAIL=1; }
shot() { adb exec-out screencap -p > "$OUT/$1.png" || echo "screencap $1 failed"; }
alive() { adb shell pidof "$1" > /dev/null 2>&1; }
key() { adb shell input keyevent "$1"; sleep 2; }
resumed() { adb shell dumpsys activity activities | grep -E "ResumedActivity|topResumedActivity" | head -3; }

adb shell settings put secure immersive_mode_confirmations confirmed   # no 'Viewing full screen' hint in shots
adb logcat -c
adb logcat -v threadtime > "$OUT/logcat-live.txt" 2>&1 &
LOGPID=$!

adb install -r app-debug.apk || { echo "::error::install failed"; exit 1; }

# 1. First start without the game's data: MainActivity hands over to the import screen (its own process).
adb shell am start -W -n "$MAIN" || fail "MainActivity failed to start"
sleep 8; shot import-00
resumed > "$OUT/resumed-1.txt"
grep -q "ImportActivity" "$OUT/resumed-1.txt" || fail "the import screen is not in front (see resumed-1.txt)"
alive "$PKG:import" || fail "import process not running"

# 2. Its real controls: the d-pad focuses the buttons, Enter presses one (the folder picker opens), Back returns.
key KEYCODE_DPAD_DOWN; shot import-focus
key KEYCODE_DPAD_CENTER; sleep 3; shot import-picker
key KEYCODE_BACK; shot import-back
alive "$PKG:import" || fail "import process died during navigation"
[ "$(md5sum "$OUT"/import-00.png "$OUT"/import-picker.png | awk '{print $1}' | sort -u | wc -l)" -ge 2 ] \
  || fail "pressing the import screen's button changed nothing (screenshots identical)"

# 3. Second display (1240x1080 overlay = the Thor's bottom screen) and the game path with placeholder game files:
# the engine library loads and starts (it stops on the empty data; that is not a failure, a crash is).
adb shell settings put global overlay_display_devices "1240x1080/240"
sleep 6
adb shell dumpsys display | grep -iE "Display .*(overlay|1240)|mBaseDisplayInfo" | head -8 > "$OUT/displays.txt" || true
DATA=/sdcard/Android/data/$PKG/files
adb shell "mkdir -p $DATA && touch $DATA/master.dat $DATA/critter.dat" || echo "note: could not place the placeholder game files"
adb shell am force-stop "$PKG"
adb shell am start -W -n "$MAIN" || fail "MainActivity failed to start (game path)"
sleep 12; shot game-default
for d in $(adb shell dumpsys SurfaceFlinger --display-id 2>/dev/null | grep -o "Display [0-9]*" | awk '{print $2}'); do
  adb exec-out screencap -p -d "$d" > "$OUT/display-$d.png" 2>/dev/null || true
done
adb shell dumpsys activity activities | grep -iE "display|fallout2ce" | head -30 > "$OUT/activities.txt" || true
adb shell "cat /sdcard/Android/data/$PKG/files/display-log.txt" > "$OUT/display-log.txt" 2>/dev/null || true
alive "$PKG" && echo "game process still running" || echo "note: game process ended (expected with placeholder data)"

# 4. Verdict from logcat
sleep 2; kill "$LOGPID" 2>/dev/null
adb logcat -d -v threadtime > "$OUT/logcat.txt"
rm -f "$OUT/logcat-live.txt"
if grep -E "FATAL EXCEPTION|Fatal signal|SIGSEGV|SIGABRT|SIGILL|SIGBUS|UnsatisfiedLinkError|dlopen failed|>>> $PKG|ANR in $PKG|am_crash" "$OUT/logcat.txt" > "$OUT/crash-lines.txt"; then
  echo "::error::crash markers in logcat:"; head -30 "$OUT/crash-lines.txt"; FAIL=1
fi
echo "smoke result: $([ $FAIL = 0 ] && echo PASS || echo FAIL)"
exit $FAIL
