#!/bin/bash
# Stages the Pi build and its data and copies them to the Pi (`ssh rpi`,
# ~/dragon): the stripped executable, the GLSL for the tiers GLES runs
# (tools/gles/bake_gles.py: sm2, sm2 without shadows, ff) and the assets the
# Windows package carries, one species unless named, its glTF's textures
# shrunk to 1024 (the tiers cap at 512; the decode is what the Pi's 429 MB
# feels).
#
#   tools/pi/deploy.sh [SPECIES...]
set -euo pipefail
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
stage=build-pi/stage
species=${*:-embercrest}
mkdir -p "$stage/assets/props" "$stage/shaders"
"$(ls /opt/homebrew/share/android-ndk/toolchains/llvm/prebuilt/*/bin/llvm-strip | head -1)" \
  -o "$stage/dragon" build-pi/dragon
for tier in sm2 sm2-noshadows ff; do python3 tools/gles/bake_gles.py "$stage/shaders" --tier $tier >/dev/null; done
rsync -a assets/textures assets/fonts assets/species "$stage/assets/"
rsync -a --include='*.glb' --exclude='*' assets/props/ "$stage/assets/props/"
for s in $species; do
  if [ ! "$stage/assets/$s.glb" -nt "assets/$s.glb" ]; then
    python3 tools/release/shrink_glb.py "assets/$s.glb" "$stage/assets/$s.glb" --max 1024 >/dev/null
  fi
  for kind in rig flight breath; do
    [ -f "assets/$s.glb.$kind.cfg" ] && cp "assets/$s.glb.$kind.cfg" "$stage/assets/"
  done
done
rsync -a --delete "$stage/" rpi:dragon/
ssh rpi 'du -sh dragon'
