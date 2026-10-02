#!/bin/sh
# Build a macOS release package: dist/Dragon-VERSION-macos.zip.
#
#   tools/release/package_macos.sh [VERSION]      # default 0.1.0
#
# A clean Release build in build-release/ with SDL3 compiled from source and
# linked statically (DRAGON_FETCH_SDL), for macOS 11 and later, arm64 and
# x86_64. It carries no source-tree fallback (DRAGON_DEV_ROOTS=OFF), so the
# binary holds no build-machine path and runs from the bundle's own
# Resources. The bundle is staged in dist/, the roster's textures are shrunk
# to 2048² (shrink_glb.py), it is ad-hoc signed, and it is zipped together
# with the licence files. Then it checks itself: a home-directory path in
# the binary fails the build, and a headless frame is rendered from the
# bundle with the source tree out of reach.
#
# What goes in is ATTRIBUTION.md's "public release" column: the code, the
# original props and textures, the fonts, and the finished species. It
# carries neither Sketchfab dragon, and leaves Sunspear and Rimeplume (work
# in progress) out.
set -eu

version=${1:-0.1.0}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
species="embercrest rimefang frostvein blightmaw ironroot stormsail tidewrack"
for s in $species; do
  [ -f "assets/$s.glb" ] || { echo "missing assets/$s.glb" >&2; exit 1; }
done

cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDRAGON_DEV_ROOTS=OFF -DDRAGON_FETCH_SDL=ON \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" >/dev/null
cmake --build build-release --target dragon

dist=$root/dist
app=$dist/Dragon.app
rm -rf "$app" "$dist/stage"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources/assets"
cp build-release/dragon "$app/Contents/MacOS/dragon"
res=$app/Contents/Resources

cat > "$app/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Dragon</string>
  <key>CFBundleDisplayName</key><string>Dragon</string>
  <key>CFBundleIdentifier</key><string>io.github.zhzh2001.dragon</string>
  <key>CFBundleExecutable</key><string>dragon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>$version</string>
  <key>CFBundleVersion</key><string>$version</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
</dict>
</plist>
EOF

cp -R shaders "$res/shaders"
cp -R assets/props assets/textures assets/fonts "$res/assets/"
for s in $species; do
  python3 tools/release/shrink_glb.py "assets/$s.glb" "$res/assets/$s.glb" --max 2048
  for kind in rig flight breath; do
    [ -f "assets/$s.glb.$kind.cfg" ] && cp "assets/$s.glb.$kind.cfg" "$res/assets/"
  done
done
for f in LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md; do cp "$f" "$res/"; done

codesign --force --deep --sign - "$app"

# The package must not know where it was built.
if strings -a "$app/Contents/MacOS/dragon" | grep -q "$HOME"; then
  echo "FAILED: the binary contains $HOME" >&2; exit 1
fi

mkdir -p "$dist/stage/Dragon"
mv "$app" "$dist/stage/Dragon/"
cp tools/release/README-package.txt "$dist/stage/Dragon/README.txt"
for f in LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md; do cp "$f" "$dist/stage/Dragon/"; done
zip=$dist/Dragon-$version-macos.zip
rm -f "$zip"
(cd "$dist/stage" && ditto -c -k --keepParent Dragon "$zip")

# Smoke test from an unpacked copy, the way a recipient gets it.
check=$(mktemp -d)
ditto -x -k "$zip" "$check"
"$check/Dragon/Dragon.app/Contents/MacOS/dragon" --headless --frames 40 \
  --screenshot "$check/frame.bmp" 2>&1 | grep -E 'data:|model roster|ERROR' || true
[ -s "$check/frame.bmp" ] || { echo "FAILED: the packaged build rendered nothing" >&2; exit 1; }
mv "$check/frame.bmp" "$dist/Dragon-$version-macos-check.bmp"
rm -rf "$check" "$dist/stage"
echo "$zip: $(du -h "$zip" | cut -f1)"
lipo -archs "$root/build-release/dragon"
