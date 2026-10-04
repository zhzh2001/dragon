#!/bin/sh
# Build the Windows XP release package on the Mac: dist/Dragon-VERSION-xp.zip.
#
#   tools/release/package_xp.sh [VERSION]      # default 0.1.0
#
# docs/PORTING.md, R6. 32-bit, cross-compiled with the Mac's clang against
# llvm-mingw's msvcrt target files (cmake/llvm-mingw-i686-xp.cmake) into
# build-xp-release/: the D3D9 backend alone, src/platform/xp's SDL3 subset in
# place of SDL3, one self-contained GUI-subsystem dragon.exe. The shaders are
# the D3D9 tiers' (sm3, sm2, sm2 without shadows, fixed function), baked
# with d3dcompiler_47 under Wine (WINE, DYLD_FALLBACK_LIBRARY_PATH and
# WINEPREFIX as tools/d3d9/bake_d3d9.py documents). The models and packs are
# the Windows package's. The gate is tools/xp/check_imports.py: every import
# must be in XP SP3's own DLLs (tools/xp/xp-sp3-exports.txt). Nothing here
# can run the result on XP: check it on the G41 before uploading it.
set -eu

version=${1:-0.1.0}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
dirty=$(git status --porcelain --untracked-files=no -- src shaders cmake CMakeLists.txt \
  assets tools/release tools/d3d9 tools/xp LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md)
if [ -n "$dirty" ]; then
  echo "refusing to package uncommitted changes:" >&2
  echo "$dirty" >&2
  exit 1
fi
[ -n "${WINE:-}" ] || { echo "set WINE (and its library path) to bake the D3D9 shaders" >&2; exit 1; }
species="embercrest rimefang frostvein blightmaw ironroot stormsail tidewrack"
for s in $species; do
  [ -f "assets/$s.glb" ] || { echo "missing assets/$s.glb" >&2; exit 1; }
done

cmake -S . -B build-xp-release -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-i686-xp.cmake -DCMAKE_BUILD_TYPE=Release \
  -DDRAGON_XP=ON -DDRAGON_DEV_ROOTS=OFF -DDRAGON_SHADERCROSS=OFF >/dev/null
cmake --build build-xp-release --target dragon

python3 tools/xp/check_imports.py build-xp-release/dragon.exe

stage=$root/dist/stage-xp/Dragon
rm -rf "$root/dist/stage-xp"
mkdir -p "$stage/assets/props" "$stage/shaders"
cp build-xp-release/dragon.exe "$stage/"
for tier in sm3 sm2 sm2-noshadows ff; do
  python3 tools/d3d9/bake_d3d9.py "$stage/shaders" --tier "$tier"
done
cp -R assets/textures assets/fonts assets/species "$stage/assets/"
cp assets/props/*.glb "$stage/assets/props/"
staged_models=""
for s in $species; do
  python3 tools/release/shrink_glb.py "assets/$s.glb" "$stage/assets/$s.glb" --max 2048
  for kind in rig flight breath; do
    [ -f "assets/$s.glb.$kind.cfg" ] && cp "assets/$s.glb.$kind.cfg" "$stage/assets/"
  done
  staged_models="$staged_models${staged_models:+,}$stage/assets/$s.glb"
done
[ -x build/dragon ] || { echo "build/dragon is needed to bake the model packs" >&2; exit 1; }
./build/dragon --bake-packs --models "$staged_models" >/dev/null 2>&1
cp assets/props/*.glb.pack "$stage/assets/props/"
for s in $species; do
  [ -f "$stage/assets/$s.glb.pack" ] || { echo "missing pack for $s" >&2; exit 1; }
done
cp tools/release/README-xp.txt "$stage/README.txt"
for f in LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md; do cp "$f" "$stage/"; done

if strings -a "$stage/dragon.exe" | grep -q "$HOME"; then
  echo "FAILED: dragon.exe contains $HOME" >&2; exit 1
fi

zip=$root/dist/Dragon-$version-xp.zip
rm -f "$zip"
(cd "$root/dist/stage-xp" && zip -qr "$zip" Dragon)
rm -rf "$root/dist/stage-xp"
echo "$zip: $(du -h "$zip" | cut -f1)"
