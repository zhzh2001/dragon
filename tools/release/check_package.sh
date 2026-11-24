#!/bin/zsh
# Check a release package where it runs, harder than the packager's one frame.
#
#   tools/release/check_package.sh macos VERSION
#   tools/release/check_package.sh linux VERSION [HOST]     # default x99
#   tools/release/check_package.sh windows VERSION [HOST]   # default x99-windows
#   tools/release/check_package.sh pi VERSION [HOST]        # default rpi
#   tools/release/check_package.sh android VERSION          # the phone on adb
#
# Each takes dist/Dragon-VERSION-<platform>.* and, from the unpacked
# package and never from the source tree:
#   1. renders the eight reference frames (tools/golden) with the package's
#      own executable and baked shaders, against the Metal set: tight on
#      macOS, --loose on every other backend;
#   2. flies a flap on the default dragon and reads the log for the roster
#      and the keyframed clips;
#   3. starts a hoard run from the start screen (--menu-start 2);
#   4. runs it PAIRS times (default 8) beside a second render. A package
#      hung for ever in this, and alone it never did (docs/RELEASE.md, "A
#      hang beside another render").
# The Pi and the phone run their own lists (check_package_pi.sh,
# check_package_android.sh): no reference frames on the Pi's fixed-function
# tier, the phone's against the Mac at the phone's preset, no second render
# on either, and on both a frame-rate floor under the demo pilot.
#
# It prints one PASS or FAIL line a step and exits non-zero on a FAIL. It
# leaves the frames it rendered in dist/check-VERSION-<platform>/ to be
# looked at: a PASS on the numbers is not a look at the picture.
#
# The remote hosts are the user's machines. This script copies files to a
# scratch directory on them and runs the game. It does not restart, boot
# another system on, or power off any of them: ask first (CLAUDE.md).
set -eu

platform=${1:?platform}; version=${2:?version}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
pairs=${PAIRS:-8}
python=${PYTHON:-$HOME/.local/py314/bin/python3}
out=$root/dist/check-$version-$platform
rm -rf "$out"; mkdir -p "$out"
fail=0
say() { # PASS|FAIL, text
  echo "$1 $platform: $2"
  [ "$1" = PASS ] || fail=1
}

# What a remote golden run needs beside the package: the tool, the Metal
# set, and the first Embercrest at full size, which every scene names.
stage_root() { # directory
  mkdir -p "$1/tools" "$1/tests/golden" "$1/assets"
  cp -R tools/golden "$1/tools/"
  cp -R tests/golden/metal "$1/tests/golden/"
  cp assets/embercrest.glb assets/embercrest.glb.*.cfg "$1/assets/"
}

case $platform in
macos)
  zip=dist/Dragon-$version-macos.zip
  work=$(mktemp -d)
  ditto -x -k "$zip" "$work"
  exe=$work/Dragon/Dragon.app/Contents/MacOS/dragon
  if "$python" tools/golden/golden.py compare --binary "$exe" --out "$out/golden" > "$out/golden.log" 2>&1; then
    say PASS "the eight reference frames, tight against the metal set"
  else
    say FAIL "reference frames: $(grep -v '^ok' "$out/golden.log" | head -3 | tr '\n' ' ')"
  fi
  (cd "$work" && "$exe" --headless --hide-ui --frames 215 --studio 1 --inspect 300 14 8 \
    --screenshot "$out/flap.bmp" > "$out/flap.log" 2>&1) || true
  grep -q "flight clips: 2" "$out/flap.log" && grep -q "model roster: 13" "$out/flap.log" && [ -s "$out/flap.bmp" ] \
    && say PASS "a flap: 13 dragons in the roster, the keyframed wingbeat loaded" \
    || say FAIL "a flap: $(grep -E 'model roster|flight clips|ERROR' "$out/flap.log" | head -3 | cut -c40-160 | tr '\n' ' ')"
  (cd "$work" && "$exe" --headless --menu-start 2 --frames 240 --hide-panels \
    --screenshot "$out/run.bmp" > "$out/run.log" 2>&1) || true
  grep -q "run: seed" "$out/run.log" && [ -s "$out/run.bmp" ] \
    && say PASS "a hoard run from the start screen" || say FAIL "no run started from the start screen"
  hung=0
  for n in $(seq 1 $pairs); do
    ./build/dragon --headless --frames 300 > /dev/null 2>&1 & other=$!
    (cd "$work" && exec "$exe" --headless --frames 120 --screenshot "$work/s.bmp") > "$out/pair.log" 2>&1 & pid=$!
    for i in $(seq 1 60); do kill -0 $pid 2>/dev/null || break; sleep 1; done
    if kill -0 $pid 2>/dev/null; then hung=$((hung + 1)); kill -9 $pid; sleep 2; fi
    wait $other 2>/dev/null || true
  done
  [ $hung = 0 ] && say PASS "$pairs of $pairs runs beside a second render" || say FAIL "$hung of $pairs runs beside a second render hung"
  rm -rf "$work"
  ;;
linux)
  host=${3:-x99}; remote=/mnt/Data/scratch/dragon-check-$version
  stage=$(mktemp -d); stage_root "$stage/root"
  cp tools/release/check_package_posix.sh "$stage/"
  ssh "$host" "rm -rf $remote && mkdir -p $remote"
  rsync -a "$stage/" dist/Dragon-$version-linux.tar.gz "$host:$remote/"
  rm -rf "$stage"
  # SDL_VIDEO_DRIVER=offscreen: no login session is needed at the greeter.
  ssh "$host" "cd $remote && tar xzf Dragon-$version-linux.tar.gz && SDL_VIDEO_DRIVER=offscreen PYTHON=/mnt/Data/scratch/venv/bin/python PAIRS=$pairs sh check_package_posix.sh $remote/Dragon/dragon $remote/root $remote/out" \
    | tee "$out/remote.log" | grep --line-buffered -E '^(PASS|FAIL)' | while read -r verdict text; do say "$verdict" "$text"; done
  grep -q '^FAIL' "$out/remote.log" && fail=1
  grep -q '^PASS' "$out/remote.log" || { say FAIL "the remote check printed nothing"; }
  rsync -a "$host:$remote/out/" "$out/" 2>/dev/null || true
  ssh "$host" "rm -rf $remote"
  ;;
windows)
  host=${3:-x99-windows}; remote='D:\scratch\dragon-check-'$version; remote_scp=D:/scratch/dragon-check-$version
  stage=$(mktemp -d); stage_root "$stage/root"
  cp tools/release/check_package.ps1 "$stage/"
  ssh "$host" "if (Test-Path '$remote') { Remove-Item -Recurse -Force '$remote' }; New-Item -ItemType Directory -Force '$remote' | Out-Null"
  scp -q -r "$stage/root" "$stage/check_package.ps1" dist/Dragon-$version-windows.zip "$host:$remote_scp/"
  rm -rf "$stage"
  ssh "$host" "pwsh -NoProfile -File $remote\\check_package.ps1 -Base $remote -Version $version -Pairs $pairs" \
    | LC_ALL=C sed 's/\x1b\[[0-9;]*m//g' | tee "$out/remote.log" | grep --line-buffered -a -E '^(PASS|FAIL)' | while read -r verdict text; do say "$verdict" "$text"; done
  grep -a -q '^FAIL' "$out/remote.log" && fail=1
  grep -a -q '^PASS' "$out/remote.log" || { say FAIL "the remote check printed nothing"; }
  scp -q "$host:$remote_scp/out/*.bmp" "$out/" 2>/dev/null || true
  ssh "$host" "Remove-Item -Recurse -Force '$remote'"
  ;;
pi)
  host=${3:-rpi}
  scp -q dist/Dragon-$version-pi.tar.gz tools/release/check_package_pi.sh "$host:"
  ssh "$host" "sh check_package_pi.sh $version" | tee "$out/remote.log" | grep --line-buffered -E '^(PASS|FAIL)' | while read -r verdict text; do say "$verdict" "$text"; done
  grep -q '^FAIL' "$out/remote.log" && fail=1
  grep -q '^PASS' "$out/remote.log" || { say FAIL "the remote check printed nothing"; }
  scp -q "$host:dragon-check-$version/*.bmp" "$out/" 2>/dev/null || true
  ssh "$host" "rm -rf dragon-check-$version Dragon-$version-pi.tar.gz check_package_pi.sh"
  ;;
android)
  tools/release/check_package_android.sh "$version" "$out" | tee "$out/phone.log" | grep --line-buffered -E '^(PASS|FAIL)' | while read -r verdict text; do say "$verdict" "$text"; done
  grep -q '^FAIL' "$out/phone.log" && fail=1
  grep -q '^PASS' "$out/phone.log" || { say FAIL "the phone check printed nothing"; }
  ;;
*) echo "unknown platform $platform" >&2; exit 2 ;;
esac

# The frames, as JPEGs to look at.
for bmp in "$out"/*.bmp(N); do sips -s format jpeg "$bmp" --out "${bmp%.bmp}.jpg" > /dev/null 2>&1 && rm -f "$bmp"; done
echo "frames to look at: $out"
exit $fail
