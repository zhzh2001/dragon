#!/bin/bash
# The phone's half of tools/release/check_package.sh: the phone on adb.
#
#   tools/release/check_package_android.sh VERSION OUT_DIR
#
# Installs dist/Dragon-VERSION-android.apk over the copy on the phone and,
# through tools/android/run.sh:
#   1. a first start, which unpacks the APK's data (core/paths.cpp);
#   2. the eight reference frames, loose against the same scenes rendered
#      here on the Mac at the phone's preset. The phone runs the `phone`
#      preset and the Metal set is Ultra, so against that set every frame
#      fails on content alone;
#   3. a hoard run from the start screen;
#   4. 7,200 frames of the demo pilot in a window at the phone's own
#      preset, for the frame rate once the phone is warm. Under
#      PHONE_FPS_FLOOR (30, M1's exit test in docs/porting/MOBILE.md) it
#      fails: the 0.4.0 package ran at 11.2 fps where October's ran at
#      33.8, and nothing else in the release had measured it.
#
# It never uninstalls. An APK signed with another key does not install over
# the copy on the phone, and uninstalling deletes the player's records:
# that is the user's call (docs/RELEASE.md).
set -uo pipefail
version=$1; out=$2
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
python=${PYTHON:-$HOME/.local/py314/bin/python3}
apk=dist/Dragon-$version-android.apk
mkdir -p "$out"

adb get-state > /dev/null 2>&1 || { echo "FAIL no phone on adb"; exit 0; }
if install=$(adb install -r "$apk" 2>&1); then
  echo "PASS installed over the copy on the phone ($(adb shell getprop ro.product.model | tr -d '\r'), Android $(adb shell getprop ro.build.version.release | tr -d '\r'))"
else
  echo "FAIL install: $(echo "$install" | tail -1). Not uninstalled: that deletes the records on the phone"
  exit 0
fi

# The first start unpacks the data; time it by the log.
start=$(date +%s)
tools/android/run.sh --out "$out/first" -- --headless --frames 40 --screenshot first.bmp > "$out/first.txt" 2>&1
if [ -s "$out/first/first.png" ]; then
  echo "PASS a first start, data unpacked, in $(( $(date +%s) - start )) s"
else
  echo "FAIL the first start rendered nothing: $(tail -2 "$out/first.txt" | tr '\n' ' ')"
  exit 0
fi

# The reference frames: the golden tool's own command lines, one a scene.
mkdir -p "$out/renders"
"$python" tools/golden/golden.py commands --exe x | while read -r line; do
  name=$(echo "$line" | sed -E 's/.*--screenshot ([a-z-]+)\.bmp.*/\1/')
  args=$(echo "$line" | sed -E 's/^x //')
  # shellcheck disable=SC2086
  tools/android/run.sh --out "$out/renders" -- $args < /dev/null > "$out/renders/$name.txt" 2>&1
done
"$python" tools/golden/golden.py capture --out "$out/reference" --extra "--preset phone" > "$out/reference.log" 2>&1
if "$python" tools/golden/golden.py compare --set "$out/reference" --loose --renders "$out/renders" --out "$out/golden" > "$out/golden.log" 2>&1; then
  echo "PASS the eight reference frames, loose against the Mac at the phone preset"
else
  echo "FAIL reference frames: $(grep -v '^ok' "$out/golden.log" | head -3 | tr '\n' ' ')"
fi

tools/android/run.sh --out "$out/run" -- --headless --menu-start 2 --frames 240 --hide-panels --screenshot run.bmp > "$out/run.txt" 2>&1
if grep -q "run: seed" "$out/run/run.log" && [ -s "$out/run/run.png" ]; then
  echo "PASS a hoard run from the start screen"
else
  echo "FAIL no run started from the start screen"
fi

# Two minutes of the demo pilot in a window, at the phone's own preset.
temp_before=$(adb shell dumpsys battery | awk '/temperature/{print $2 / 10}')
tools/android/run.sh --out "$out/demo" -- --run 7 --autopilot --frames 7200 --hide-panels > "$out/demo.txt" 2>&1
temp_after=$(adb shell dumpsys battery | awk '/temperature/{print $2 / 10}')
line=$(grep -m1 "frames:" "$out/demo/run.log" | sed -E 's/.*(frames: .*)/\1/')
fps=$(echo "$line" | sed -E 's/.* ([0-9.]+) fps.*/\1/')
floor=${PHONE_FPS_FLOOR:-30}
if [ -n "$line" ] && awk "BEGIN{exit !($fps >= $floor)}"; then
  echo "PASS the demo pilot: $line; battery ${temp_before} to ${temp_after} C"
elif [ -n "$line" ]; then
  echo "FAIL the demo pilot ran under $floor fps: $line; battery ${temp_before} to ${temp_after} C"
else
  echo "FAIL the demo pilot did not finish: $(tail -2 "$out/demo.txt" | tr '\n' ' ')"
fi
