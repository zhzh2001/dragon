#!/bin/bash
# Builds the Android APK without Gradle (docs/porting/MOBILE.md):
#
#   tools/android/build_apk.sh [--install]
#
# 1. the game as libmain.so, arm64-v8a, through the NDK's CMake toolchain
#    (build-android/, SDL fetched and linked statically, shaders baked);
# 2. SDL's Java activity and ours (android/src) through javac and d8;
# 3. the manifest through aapt2, the library and the dex zipped in, then
#    zipalign and apksigner with the debug key.
#
# The APK carries no data: tools/android/push_assets.sh puts it in the app's
# external files directory. --install also adb-installs the result.
#
# Needs the NDK (brew install --cask android-ndk), the SDK's platform 35 and
# build-tools 35 (brew install --cask android-commandlinetools, then
# sdkmanager), and a JDK 17 (brew install openjdk@17).
set -euo pipefail
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"

NDK=${ANDROID_NDK_HOME:-/opt/homebrew/share/android-ndk}
SDK=${ANDROID_HOME:-/opt/homebrew/share/android-commandlinetools}
BT=$SDK/build-tools/35.0.0
JAR=$SDK/platforms/android-35/android.jar
export JAVA_HOME=${JAVA_HOME:-/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home}
build=build-android
out=$build/apk
min_api=29

if [ ! -f "$build/build.ninja" ]; then
  cmake -S . -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$min_api \
    -DCMAKE_BUILD_TYPE=Release \
    -DDRAGON_FETCH_SDL=ON -DDRAGON_SHADERCROSS=OFF -DDRAGON_DEV_ROOTS=OFF
fi
cmake --build "$build" --target dragon

rm -rf "$out"
mkdir -p "$out/classes" "$out/dex" "$out/zip/lib/arm64-v8a"
sdl_java=$build/_deps/sdl3-src/android-project/app/src/main/java
"$JAVA_HOME/bin/javac" -nowarn --release 11 -classpath "$JAR" -d "$out/classes" \
  $(find "$sdl_java" android/src -name '*.java') 2>&1 | grep -v -E '^Note:' || true
"$BT/d8" --release --min-api $min_api --lib "$JAR" --output "$out/dex" \
  $(find "$out/classes" -name '*.class')

"$BT/aapt2" link -o "$out/unaligned.apk" -I "$JAR" --manifest android/AndroidManifest.xml \
  --min-sdk-version $min_api --target-sdk-version 35 --debug-mode \
  --version-code "$(git rev-list --count HEAD)" --version-name "$(git describe --always --dirty)"
cp "$out/dex/classes.dex" "$out/zip/"
"$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip" -o "$out/zip/lib/arm64-v8a/libmain.so" \
  "$build/libmain.so"
(cd "$out/zip" && zip -q -r ../unaligned.apk classes.dex lib)
"$BT/zipalign" -P 16 -f 4 "$out/unaligned.apk" "$out/aligned.apk"

key=$HOME/.android/debug.keystore
if [ ! -f "$key" ]; then
  mkdir -p "$(dirname "$key")"
  "$JAVA_HOME/bin/keytool" -genkeypair -keystore "$key" -storepass android -keypass android \
    -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Android Debug,O=Android,C=US" >/dev/null
fi
"$BT/apksigner" sign --ks "$key" --ks-pass pass:android --out "$out/dragon.apk" "$out/aligned.apk"
echo "$out/dragon.apk: $(du -h "$out/dragon.apk" | cut -f1)"

if [ "${1:-}" = "--install" ]; then
  adb install -r "$out/dragon.apk"
fi
