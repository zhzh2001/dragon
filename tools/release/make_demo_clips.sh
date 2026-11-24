#!/bin/zsh
# Build the README's pictures and clips into docs/media/.
#
#   tools/release/make_demo_clips.sh [SCRATCH_DIR]
#
# Every scene is a headless run, so the same commit renders the same
# frames. The clips are animated WebP: at 640x360 and 20 frames a second
# a clip is 1 to 2 MB, where the same frames as a GIF were 7 to 10 MB.
# Needs build/dragon and Pillow with WebP (~/.local/py314/bin/python3).
#
# The roster is named explicitly. The default roster also loads the local
# gold and silver studies when they are on the machine, and those may not
# appear in anything published (ATTRIBUTION.md).
set -eu

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
scratch=${1:-$(mktemp -d)}
python=${PYTHON:-$HOME/.local/py314/bin/python3}
out=$root/docs/media
mkdir -p "$out" "$scratch"
cd "$root"

roster=assets/embercrest-hero.glb,assets/frost3.glb,assets/storm3.glb,assets/stone2.glb,assets/tide2.glb,assets/blight2.glb

record() { # name, first frame, last frame, args...
  name=$1; from=$2; last=$3; shift 3
  rm -rf "$scratch/$name"; mkdir -p "$scratch/$name"
  ./build/dragon --headless --size 960x540 "$@" --frames $((last + 1)) \
    --record "$scratch/$name,$from,3" > "$scratch/$name.log" 2>&1
}

# The demo pilot flying a hoard run: the flight down the valley, and its
# first fight. One run, two clips.
record run 300 2235 --hide-panels --models $roster --run 7 --autopilot
# The hero's wingbeat through the studio's s-turns, from the front quarter.
record wingbeat 200 500 --hide-ui --studio 4 --inspect 200 15 10
# The fire breath, held, from the side.
record breath 150 420 --hide-ui --studio 0 --breathe --element fire --inspect 70 26 6

# Stills: the hero, and the six designs in the glide.
mkdir -p "$scratch/stills"
./build/dragon --headless --hide-ui --size 1920x1080 --studio 4 --inspect 200 15 10 \
  --frames 335 --screenshot "$scratch/stills/hero.bmp" > /dev/null 2>&1
for model in embercrest-hero frost3 storm3 stone2 tide2 blight2; do
  ./build/dragon --headless --hide-ui --size 1280x720 --model assets/$model.glb --studio 0 \
    --inspect 215 15 8 --frames 200 --screenshot "$scratch/stills/$model.bmp" > /dev/null 2>&1
done

"$python" - "$scratch" "$out" "$root" <<'PY'
import os, sys
from PIL import Image, ImageDraw, ImageFont
scratch, out, root = sys.argv[1:4]

def clip(name, source, first, count):
    frames = [Image.open(f"{scratch}/{source}/f{first + 3 * i:06d}.bmp").convert("RGB").resize((640, 360), Image.LANCZOS)
              for i in range(count)]
    path = f"{out}/{name}.webp"
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=50, loop=0, quality=60, method=4)
    print(f"{name}.webp: {count} frames, {os.path.getsize(path) / 1e6:.2f} MB")

clip("flight", "run", 300, 90)
clip("fight", "run", 1965, 90)
clip("wingbeat", "wingbeat", 200, 100)
clip("breath", "breath", 150, 90)

hero = Image.open(f"{scratch}/stills/hero.bmp").convert("RGB").resize((1280, 720), Image.LANCZOS)
hero.save(f"{out}/hero.jpg", quality=88)

names = [("embercrest-hero", "Embercrest · fire"), ("frost3", "Shardmane · frost"), ("storm3", "Squallfin · storm"),
         ("stone2", "Geodeback · stone"), ("tide2", "Deepwake · tide"), ("blight2", "Ambersting · blight")]
font = ImageFont.truetype(f"{root}/assets/fonts/BarlowCondensed-SemiBold.ttf", 34)
sheet = Image.new("RGB", (640 * 3, 360 * 2))
for i, (model, label) in enumerate(names):
    im = Image.open(f"{scratch}/stills/{model}.bmp").convert("RGB").resize((640, 360), Image.LANCZOS)
    draw = ImageDraw.Draw(im)
    width = draw.textlength(label, font=font)
    draw.rectangle((14, 306, 14 + width + 24, 346), fill=(24, 24, 26))
    draw.text((26, 307), label, fill=(236, 232, 220), font=font)
    sheet.paste(im, ((i % 3) * 640, (i // 3) * 360))
sheet.save(f"{out}/roster.jpg", quality=88)
PY

echo "wrote $out; the frames are in $scratch (about 1.5 GB): delete it"
