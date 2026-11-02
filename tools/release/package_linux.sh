#!/bin/sh
# Build the Linux release package: dist/Dragon-VERSION-linux.tar.gz.
#
#   tools/release/package_linux.sh [VERSION]        # default 0.1.0
#   LINUX_HOST=x99 ZIG=~/opt/zig-x86_64-linux-0.17.0/zig  (the defaults)
#
# The executable is built on a Linux machine over ssh (LINUX_HOST), from the
# committed sources copied there, with Zig's clang against glibc 2.31
# (cmake/zig-x86_64-linux.cmake): SDL3 from source and static, libc++ static,
# no source-tree fallback, no shader compiler. The result needs only glibc.
# Everything else is staged on the Mac as for Windows: SPIR-V shaders, the
# roster shrunk to 2048². No model packs: only the retro tiers read them, and
# Linux runs the modern tier on Vulkan. Check the package on Linux before
# uploading it (docs/RELEASE.md).
set -eu

version=${1:-0.1.0}
host=${LINUX_HOST:-x99}
zig=${ZIG:-'~/opt/zig-x86_64-linux-0.17.0/zig'}
remote=src/dragon-linux-build
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
# Package only what is committed (see package_windows.sh for why).
dirty=$(git status --porcelain --untracked-files=no -- src shaders cmake CMakeLists.txt \
  assets tools/release tools/linux third_party LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md)
if [ -n "$dirty" ]; then
  echo "refusing to package uncommitted changes:" >&2
  echo "$dirty" >&2
  exit 1
fi
species="embercrest rimefang frostvein blightmaw ironroot stormsail tidewrack"
for s in $species; do
  [ -f "assets/$s.glb" ] || { echo "missing assets/$s.glb" >&2; exit 1; }
done

# The build: HEAD's sources, not the working tree's, unpacked over the
# remote copy so its CMake cache and SDL build survive between packages.
ssh "$host" "mkdir -p $remote"
git archive HEAD src shaders cmake CMakeLists.txt tools/linux | ssh "$host" "tar xf - -C $remote"
ssh "$host" "cd $remote && export ZIG=$zig ZIG_GLOBAL_CACHE_DIR=\$PWD/zig-cache && \
  cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/zig-x86_64-linux.cmake -DCMAKE_BUILD_TYPE=Release \
    -DDRAGON_DEV_ROOTS=OFF -DDRAGON_FETCH_SDL=ON -DDRAGON_SHADERCROSS=OFF >/dev/null && \
  cmake --build build --target dragon && \
  strip -o build/dragon-stripped build/dragon"

stage=$root/dist/stage-linux/Dragon
rm -rf "$root/dist/stage-linux"
mkdir -p "$stage/assets"
scp -q "$host:$remote/build/dragon-stripped" "$stage/dragon"
chmod +x "$stage/dragon"
tools/release/bake_shaders.sh "$stage/shaders" spv
cp -R assets/textures assets/fonts assets/species "$stage/assets/"
mkdir -p "$stage/assets/props"
cp assets/props/*.glb "$stage/assets/props/"
for s in $species; do
  python3 tools/release/shrink_glb.py "assets/$s.glb" "$stage/assets/$s.glb" --max 2048
  for kind in rig flight breath; do
    [ -f "assets/$s.glb.$kind.cfg" ] && cp "assets/$s.glb.$kind.cfg" "$stage/assets/"
  done
done
cp tools/release/README-linux.txt "$stage/README.txt"
for f in LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md; do cp "$f" "$stage/"; done

# The gates: no home directory of either machine, nothing newer than glibc
# 2.31, and no library a bare system lacks (SDL opens the rest at run time).
if strings -a "$stage/dragon" | grep -q -E "$HOME|/home/"; then
  echo "FAILED: dragon contains a home directory" >&2; exit 1
fi
newest=$(ssh "$host" "objdump -T $remote/build/dragon-stripped | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1")
case "$newest" in
  GLIBC_2.[0-9]|GLIBC_2.[12][0-9]|GLIBC_2.3[01]) ;;
  *) echo "FAILED: dragon needs $newest, past glibc 2.31" >&2; exit 1 ;;
esac
if ssh "$host" "readelf -d $remote/build/dragon-stripped" | grep NEEDED | \
    grep -v -E '\[(libc|libm|libpthread|libdl|librt|ld-linux-x86-64)\.so'; then
  echo "FAILED: dragon links a library outside glibc" >&2; exit 1
fi

tarball=$root/dist/Dragon-$version-linux.tar.gz
rm -f "$tarball"
tar czf "$tarball" -C "$root/dist/stage-linux" Dragon
rm -rf "$root/dist/stage-linux"
echo "$tarball: $(du -h "$tarball" | cut -f1) (newest symbol $newest)"
