#!/bin/bash
# Stages the game's data and pushes it to the phone (docs/porting/MOBILE.md):
#
#   tools/android/push_assets.sh [SPECIES...]   # default: the seven release species
#
# The APK carries no data. This stages what the Windows package carries
# (tools/release/package_windows.sh): the shaders baked to SPIR-V, the
# textures, fonts, species profiles and props, and each species' glTF with
# its textures shrunk to 2048 (shrink_glb.py), with its three .cfg files.
# The stage is kept in build-android/stage, and `adb push --sync` sends only
# what changed, into the app's external files directory, where
# core/paths.cpp looks on Android.
set -euo pipefail
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
pkg=com.paleshell.dragon
dest=/sdcard/Android/data/$pkg/files
stage=build-android/stage
species=${*:-embercrest rimefang frostvein blightmaw ironroot stormsail tidewrack}

mkdir -p "$stage/assets/props"
tools/release/bake_shaders.sh "$stage/shaders" spv >/dev/null
rsync -a assets/textures assets/fonts assets/species "$stage/assets/"
rsync -a --include='*.glb' --exclude='*' assets/props/ "$stage/assets/props/"
for s in $species; do
  [ -f "assets/$s.glb" ] || { echo "missing assets/$s.glb" >&2; exit 1; }
  if [ ! "$stage/assets/$s.glb" -nt "assets/$s.glb" ]; then
    python3 tools/release/shrink_glb.py "assets/$s.glb" "$stage/assets/$s.glb" --max 2048
  fi
  for kind in rig flight breath; do
    [ -f "assets/$s.glb.$kind.cfg" ] && cp "assets/$s.glb.$kind.cfg" "$stage/assets/"
  done
done
adb shell mkdir -p "$dest"
adb push --sync "$stage/assets" "$stage/shaders" "$dest/" | tail -1
