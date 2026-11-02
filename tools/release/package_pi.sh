#!/bin/sh
# Build the Raspberry Pi release package: dist/Dragon-VERSION-pi.tar.gz.
#
#   tools/release/package_pi.sh [VERSION]       # default 0.1.0
#
# tools/pi/build.sh cross-builds the armv6 executable (glibc 2.31, GLES 2.0,
# SDL on KMS/DRM), and tools/pi/deploy.sh stages what the Pi loads -- the
# GLES shaders of its three tiers, the textures it reads, one species
# (Embercrest) shrunk to 1024, the model packs -- without copying it
# anywhere (PI_STAGE_ONLY). Check it on a Pi before uploading it
# (docs/RELEASE.md).
set -eu

version=${1:-0.1.0}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
# Package only what is committed (see package_windows.sh for why).
dirty=$(git status --porcelain --untracked-files=no -- src shaders cmake CMakeLists.txt \
  assets tools/release tools/pi tools/gles LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md)
if [ -n "$dirty" ]; then
  echo "refusing to package uncommitted changes:" >&2
  echo "$dirty" >&2
  exit 1
fi

tools/pi/build.sh
rm -rf build-pi/stage
PI_STAGE_ONLY=1 tools/pi/deploy.sh embercrest

out=$root/dist/stage-pi
rm -rf "$out"
mkdir -p "$out"
cp -R build-pi/stage "$out/Dragon"
cp tools/release/README-pi.txt "$out/Dragon/README.txt"
for f in LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md; do cp "$f" "$out/Dragon/"; done

if strings -a "$out/Dragon/dragon" | grep -q "$HOME"; then
  echo "FAILED: dragon contains $HOME" >&2; exit 1
fi

tarball=$root/dist/Dragon-$version-pi.tar.gz
rm -f "$tarball"
# No macOS extended attributes: GNU tar warns about each one.
COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs -czf "$tarball" -C "$out" Dragon
rm -rf "$out"
echo "$tarball: $(du -h "$tarball" | cut -f1)"
