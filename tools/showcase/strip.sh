#!/bin/sh
# A motion strip: one studio scenario rendered at evenly spaced frames and
# laid side by side, cropped to the dragon. A single screenshot shows a
# pose; a strip shows the timing, which is what an attack or a gesture is.
# Usage: tools/showcase/strip.sh OUT.jpg MODEL SCENARIO FIRST LAST STEP [inspect args...]
#   tools/showcase/strip.sh /tmp/bite.jpg assets/embercrest-hero.glb 10 30 110 8 90 16 6
set -eu
out=$1; model=$2; scenario=$3; first=$4; last=$5; step=$6; shift 6
bin=${DRAGON_BIN:-./build/dragon}
tmp=$(mktemp -d)
f=$first; n=0
while [ "$f" -le "$last" ]; do
    name=$(printf '%s/%03d' "$tmp" "$n")
    "$bin" --headless --hide-ui --hide-vegetation --size 960x720 --model "$model" --sun 35,235 \
        --studio "$scenario" --frames "$f" --inspect "$@" --screenshot "$name.bmp" >/dev/null 2>&1
    magick "$name.bmp" -gravity center -crop 62%x80%+0+0 +repage -gravity northwest \
        -font /System/Library/Fonts/Helvetica.ttc -fill white -undercolor '#000a' -pointsize 26 -annotate +8+6 " f$f " "$name.png"
    rm "$name.bmp"
    f=$((f + step)); n=$((n + 1))
done
magick montage "$tmp"/*.png -tile "${STRIP_COLUMNS:-6}x" -geometry +2+2 -background '#111' -quality 86 "$out"
rm -r "$tmp"
