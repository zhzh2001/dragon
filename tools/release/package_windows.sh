#!/bin/sh
# Build a Windows release package on the Mac: dist/Dragon-VERSION-windows.zip.
#
#   tools/release/package_windows.sh [VERSION]      # default 0.1.0
#
# Cross-compiled with MinGW-w64 (cmake/mingw-w64-x86_64.cmake) into
# build-win-release/: SDL3 from source and static, no source-tree fallback,
# no shader compiler, one self-contained GUI-subsystem dragon.exe that imports
# only Windows system DLLs and the Universal CRT (Windows 10 has it). The
# shaders are baked to DXIL for D3D12 and SPIR-V for Vulkan, and the loader
# takes whichever the device accepts. The roster is shrunk to 2048² as on
# macOS, and each shipped model gets its pack (anim/model_pack.h), baked from
# the shrunk file so the freshness check passes, for the D3D9 tiers' load.
# Nothing here can run the result: check it on Windows (docs/RELEASE.md)
# before uploading it.
set -eu

version=${1:-0.1.0}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
# Package only what is committed: the sources compiled and the files shipped
# come from this working tree, and the repository is shared with other
# sessions whose edits in progress must not ride along into a release.
dirty=$(git status --porcelain --untracked-files=no -- src shaders cmake CMakeLists.txt \
  assets tools/release LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md)
if [ -n "$dirty" ]; then
  echo "refusing to package uncommitted changes (commit them, or package from a" >&2
  echo "clean worktree with the .glb roster linked in):" >&2
  echo "$dirty" >&2
  exit 1
fi
species="embercrest rimefang frostvein blightmaw ironroot stormsail tidewrack"
for s in $species; do
  [ -f "assets/$s.glb" ] || { echo "missing assets/$s.glb" >&2; exit 1; }
done

cmake -S . -B build-win-release -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release \
  -DDRAGON_DEV_ROOTS=OFF -DDRAGON_FETCH_SDL=ON -DDRAGON_SHADERCROSS=OFF >/dev/null
cmake --build build-win-release --target dragon

stage=$root/dist/stage-win/Dragon
rm -rf "$root/dist/stage-win"
mkdir -p "$stage/assets"
cp build-win-release/dragon.exe "$stage/"
tools/release/bake_shaders.sh "$stage/shaders" dxil spv
# assets/species/: the profiles of models not distributed (the gold and
# silver studies), so a player who has one can drop it in beside them.
cp -R assets/textures assets/fonts assets/species "$stage/assets/"
mkdir -p "$stage/assets/props"
cp assets/props/*.glb "$stage/assets/props/"
staged_models=""
for s in $species; do
  python3 tools/release/shrink_glb.py "assets/$s.glb" "$stage/assets/$s.glb" --max 2048
  for kind in rig flight breath; do
    [ -f "assets/$s.glb.$kind.cfg" ] && cp "assets/$s.glb.$kind.cfg" "$stage/assets/"
  done
  staged_models="$staged_models${staged_models:+,}$stage/assets/$s.glb"
done
# The packs: the Mac's own build bakes them, from the staged files, so the
# recorded fingerprints are the shipped files'. Props are baked in place and
# copied, since they ship unshrunk.
[ -x build/dragon ] || { echo "build/dragon is needed to bake the model packs" >&2; exit 1; }
./build/dragon --bake-packs --models "$staged_models" >/dev/null 2>&1
cp assets/props/*.glb.pack "$stage/assets/props/"
for s in $species; do
  [ -f "$stage/assets/$s.glb.pack" ] || { echo "missing pack for $s" >&2; exit 1; }
done
cp tools/release/README-windows.txt "$stage/README.txt"
for f in LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md; do cp "$f" "$stage/"; done

if strings -a "$stage/dragon.exe" | grep -q "$HOME"; then
  echo "FAILED: dragon.exe contains $HOME" >&2; exit 1
fi
if x86_64-w64-mingw32-objdump -p "$stage/dragon.exe" | grep 'DLL Name' | grep -v -i -E 'api-ms-win-crt|d3dcompiler_47|kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|imm32|setupapi|version|winmm|d3d9|d3d11|dxgi'; then
  echo "FAILED: dragon.exe imports a DLL Windows does not ship" >&2; exit 1
fi

zip=$root/dist/Dragon-$version-windows.zip
rm -f "$zip"
(cd "$root/dist/stage-win" && zip -qr "$zip" Dragon)
rm -rf "$root/dist/stage-win"
echo "$zip: $(du -h "$zip" | cut -f1)"
