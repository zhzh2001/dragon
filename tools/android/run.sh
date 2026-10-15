#!/bin/bash
# Runs the game on the phone with a command line, as a headless capture is
# run on the desktop, and brings the log and the screenshot back:
#
#   tools/android/run.sh [--out DIR] -- ARGS...
#   tools/android/run.sh -- --headless --frames 40 --screenshot shot.bmp --preset low
#
# ARGS reach the game through the launch intent (android/src, DragonActivity).
# A relative --screenshot or --model path is taken inside the app's external
# files directory; the screenshot is pulled into DIR (default: the current
# directory) and converted to PNG. The log is the game's lines from logcat,
# also written to DIR/run.log. The phone may be locked: a launch with
# arguments shows over the lock screen.
set -euo pipefail
out=.
while [ $# -gt 0 ]; do
  case $1 in
    --out) out=$2; shift 2 ;;
    --) shift; break ;;
    *) break ;;
  esac
done
pkg=com.paleshell.dragon
files=/sdcard/Android/data/$pkg/files
args=()
shot=
while [ $# -gt 0 ]; do
  a=$1; shift
  case $a in
    --screenshot|--model)
      p=$1; shift
      [ "${p#/}" = "$p" ] && p=$files/$p
      [ "$a" = --screenshot ] && shot=$p
      args+=("$a" "$p") ;;
    *) args+=("$a") ;;
  esac
done

mkdir -p "$out"
adb shell am force-stop $pkg
adb logcat -c
adb shell am start -W -n $pkg/.DragonActivity --es args "'${args[*]}'" >/dev/null
# The game is done when SDL's thread returns from SDL_main.
while ! adb logcat -d -s SDL:V | grep -q "Finished main function"; do
  adb shell pidof $pkg >/dev/null || { echo "the game died" >&2; break; }
  sleep 1
done
adb logcat -d | grep -E " (SDL/[A-Z]+|DEBUG|AndroidRuntime) |F libc|Finished main" > "$out/run.log" || true
grep -E "frames:|ERROR|error|FAILED|DEBUG|Fatal" "$out/run.log" || true
if [ -n "$shot" ]; then
  name=$(basename "$shot")
  adb pull "$shot" "$out/$name" >/dev/null
  sips -s format png "$out/$name" --out "$out/${name%.*}.png" >/dev/null && rm "$out/$name"
  echo "$out/${name%.*}.png"
fi
