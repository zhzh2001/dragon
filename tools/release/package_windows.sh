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
# macOS. Nothing here can run the result: check it on Windows
# (docs/RELEASE.md) before uploading it.
set -eu

version=${1:-0.1.0}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
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
cp -R assets/props assets/textures assets/fonts "$stage/assets/"
for s in $species; do
  python3 tools/release/shrink_glb.py "assets/$s.glb" "$stage/assets/$s.glb" --max 2048
  for kind in rig flight breath; do
    [ -f "assets/$s.glb.$kind.cfg" ] && cp "assets/$s.glb.$kind.cfg" "$stage/assets/"
  done
done
cp tools/release/README-windows.txt "$stage/README.txt"
for f in LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md; do cp "$f" "$stage/"; done

if strings -a "$stage/dragon.exe" | grep -q "$HOME"; then
  echo "FAILED: dragon.exe contains $HOME" >&2; exit 1
fi
if x86_64-w64-mingw32-objdump -p "$stage/dragon.exe" | grep 'DLL Name' | grep -v -i -E 'api-ms-win-crt|kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|imm32|setupapi|version|winmm'; then
  echo "FAILED: dragon.exe imports a DLL Windows does not ship" >&2; exit 1
fi

zip=$root/dist/Dragon-$version-windows.zip
rm -f "$zip"
(cd "$root/dist/stage-win" && zip -qr "$zip" Dragon)
rm -rf "$root/dist/stage-win"
echo "$zip: $(du -h "$zip" | cut -f1)"
