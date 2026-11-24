#!/bin/sh
# The Linux half of tools/release/check_package.sh, run on the Linux host.
#
#   sh check_package_posix.sh EXE ROOT OUT
#
# EXE is the unpacked package's executable, ROOT a directory holding
# tools/golden, tests/golden/metal and assets/embercrest.glb, OUT where the
# frames go. PYTHON names a Python 3 with Pillow, PAIRS the paired runs.
exe=$1; root=$2; out=$3
python=${PYTHON:-python3}
pairs=${PAIRS:-8}
mkdir -p "$out"
dir=$(dirname "$exe")

if (cd "$root" && "$python" tools/golden/golden.py compare --set metal --loose --binary "$exe" --out "$out/golden" > "$out/golden.log" 2>&1); then
  echo "PASS the eight reference frames, loose against the metal set"
else
  echo "FAIL reference frames: $(grep -v '^ok' "$out/golden.log" | head -3 | tr '\n' ' ')"
fi

(cd "$dir" && "$exe" --headless --hide-ui --frames 215 --studio 1 --inspect 300 14 8 --screenshot "$out/flap.bmp" > "$out/flap.log" 2>&1)
if grep -q "flight clips: 2" "$out/flap.log" && grep -q "model roster: 13" "$out/flap.log" && [ -s "$out/flap.bmp" ]; then
  echo "PASS a flap: 13 dragons in the roster, the keyframed wingbeat loaded, on $(grep -m1 -o 'Vulkan Device: .*' "$out/flap.log")"
else
  echo "FAIL a flap: $(grep -E 'model roster|flight clips|ERROR' "$out/flap.log" | head -3 | tr '\n' ' ')"
fi

(cd "$dir" && "$exe" --headless --menu-start 2 --frames 240 --hide-panels --screenshot "$out/run.bmp" > "$out/run.log" 2>&1)
if grep -q "run: seed" "$out/run.log" && [ -s "$out/run.bmp" ]; then
  echo "PASS a hoard run from the start screen"
else
  echo "FAIL no run started from the start screen"
fi

hung=0; n=0
while [ $n -lt "$pairs" ]; do
  n=$((n + 1))
  (cd "$dir" && exec "$exe" --headless --frames 300) > /dev/null 2>&1 & other=$!
  (cd "$dir" && exec "$exe" --headless --frames 120 --screenshot "$out/pair.bmp") > "$out/pair.log" 2>&1 & pid=$!
  i=0; while [ $i -lt 60 ] && kill -0 $pid 2>/dev/null; do sleep 1; i=$((i + 1)); done
  if kill -0 $pid 2>/dev/null; then hung=$((hung + 1)); kill -9 $pid; fi
  wait $other 2>/dev/null
done
rm -f "$out/pair.bmp"
if [ $hung = 0 ]; then echo "PASS $pairs of $pairs runs beside a second render"; else echo "FAIL $hung of $pairs runs beside a second render hung"; fi
