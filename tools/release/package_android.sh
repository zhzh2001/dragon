#!/bin/bash
# Build the Android release package: dist/Dragon-VERSION-android.apk.
#
#   tools/release/package_android.sh [VERSION]      # default 0.1.0
#
# One APK with the data inside it, for sideloading: arm64-v8a, Android 10
# (API 29) or later, a Vulkan 1.1 GPU. tools/android/build_apk.sh builds it;
# this stages what the Windows package carries (SPIR-V shaders, textures,
# fonts, species profiles, props, the roster shrunk to 2048²) into the APK's
# assets with a manifest, data.txt, whose first line is the data's version.
# core/paths.cpp unpacks the files to the app's external files directory on
# the first start of each version. No model packs: Android runs the modern
# tier.
#
# The APK is signed with the release key, ~/.android/dragon-release.keystore,
# made here the first time with a random password in the .pass file beside
# it. Keep both: an update signed with another key does not install over the
# last one, and the player must uninstall first, which deletes their records.
set -euo pipefail

version=${1:-0.1.0}
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
# Package only what is committed (see package_windows.sh for why).
dirty=$(git status --porcelain --untracked-files=no -- src shaders cmake CMakeLists.txt \
  assets tools/release tools/android android LICENSE THIRD_PARTY_NOTICES.md ATTRIBUTION.md AI_DISCLOSURE.md)
if [ -n "$dirty" ]; then
  echo "refusing to package uncommitted changes:" >&2
  echo "$dirty" >&2
  exit 1
fi
species="embercrest rimefang frostvein blightmaw ironroot stormsail tidewrack"
for s in $species; do
  [ -f "assets/$s.glb" ] || { echo "missing assets/$s.glb" >&2; exit 1; }
done

key=$HOME/.android/dragon-release.keystore
if [ ! -f "$key" ]; then
  export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home}
  mkdir -p "$(dirname "$key")"
  (umask 077 && openssl rand -hex 24 > "$key.pass")
  "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$key" -storetype PKCS12 \
    -storepass:file "$key.pass" -alias dragon -keyalg RSA -keysize 4096 -validity 36500 \
    -dname "CN=Paleshell, O=Paleshell" >/dev/null
  echo "made the release key $key; keep it and $key.pass" >&2
fi

stage=$root/build-android/release-data
rm -rf "$stage"
mkdir -p "$stage/assets/props"
tools/release/bake_shaders.sh "$stage/shaders" spv >/dev/null
cp -R assets/textures assets/fonts assets/species "$stage/assets/"
cp assets/props/*.glb "$stage/assets/props/"
for s in $species; do
  python3 tools/release/shrink_glb.py "assets/$s.glb" "$stage/assets/$s.glb" --max 2048 >/dev/null
  for kind in rig flight breath; do
    [ -f "assets/$s.glb.$kind.cfg" ] && cp "assets/$s.glb.$kind.cfg" "$stage/"assets/
  done
done
# The manifest: the version first (with the commit, so a rebuilt APK of the
# same version still unpacks fresh data), then every file.
{
  echo "$version-$(git rev-parse --short HEAD)"
  (cd "$stage" && find assets shaders -type f | LC_ALL=C sort)
} > "$stage/data.txt"

DRAGON_APK_DATA=$stage DRAGON_APK_VERSION=$version DRAGON_APK_KEYSTORE=$key tools/android/build_apk.sh
apk=$root/dist/Dragon-$version-android.apk
cp build-android/apk/dragon.apk "$apk"
echo "$apk: $(du -h "$apk" | cut -f1), $(($(wc -l < "$stage/data.txt") - 1)) data files"
