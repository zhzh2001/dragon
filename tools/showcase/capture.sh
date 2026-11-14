#!/bin/sh
# The showcase slice's capture set (docs/SHOWCASE.md, "The slice").
# Every round of S1-S9 renders these eight shots again and compares them
# with the round before. Usage: tools/showcase/capture.sh OUT_DIR [BINARY]
#
# The slice is hoard-run seed 3 in late afternoon (sun 19 degrees up, at
# azimuth 235) with Embercrest. The HUD is off in every shot.
set -eu
out=${1:?usage: capture.sh OUT_DIR [BINARY]}
bin=${2:-./build/dragon}
mkdir -p "$out"
common="--headless --hide-ui --size 1920x1080 --model assets/embercrest.glb --sun 19,235"

shot() {  # shot NAME FLAGS...
    name=$1; shift
    # shellcheck disable=SC2086
    "$bin" $common "$@" --screenshot "$out/$name.bmp" >"$out/$name.log" 2>&1
    sips -s format jpeg -s formatOptions 88 "$out/$name.bmp" --out "$out/$name.jpg" >/dev/null
    rm "$out/$name.bmp"
    echo "$name: $(grep -o 'gpu: median[^,]*' "$out/$name.log" | tail -1)"
}

shot 1-chase   --run 3 --run-empty --frames 300
shot 2-side    --run 3 --run-empty --frames 300 --inspect 90 14 6
shot 3-bank    --run 3 --run-empty --frames 200 --input 0,0.8,0,0,0,0 --inspect 150 16 10
shot 4-head    --run 3 --run-empty --frames 300 --inspect 90 3.5 0 --inspect-head
shot 5-landing --run 3 --run-empty --frames 900 --input -0.25,0,0,0,0,0 --inspect 60 13 4
shot 6-idle    --studio 9 --frames 120 --inspect 60 11 3
shot 7-breath  --training --attack --frames 150 --inspect 120 16 8
shot 8-skim    --skim --frames 180 --inspect 70 15 5
