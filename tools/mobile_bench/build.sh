#!/bin/bash
# Builds the mobile-track benches (docs/porting/MOBILE.md, "Measured first").
#
#   tools/mobile_bench/build.sh mac|pi|android [OUT_DIR]
#
# cpu_bench compiles the game's simulation through ../game-mcu (its overlay
# and its software renderer), as dragon_sim does. The Pi build is a static
# armv6 musl binary from Zig; the Android one comes from the NDK's clang.
#
# gles_bench links each platform's own EGL and GLES 2.0:
#   mac      against ANGLE (an Electron app's libEGL/libGLESv2; run it from
#            OUT/../mac-angle/lib, the dylibs' install names are ./)
#   pi       gles_bench_brcm on Broadcom's /opt/vc, gles_bench_mesa on Mesa
#            (libegl-dev and libgles-dev on the board); both pulled from
#            `ssh rpi` into OUT/../pi-vc and OUT/../pi-mesa, then linked
#            with ~/src/rpi's Zig wrapper against its Bullseye sysroot
#   android  the NDK's clang, -lEGL -lGLESv2
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
GAME="$(cd "$HERE/../.." && pwd)"
MCU="${DRAGON_MCU_DIR:-$GAME/../game-mcu}"
TARGET="${1:?mac|pi|android}"
OUT="${2:-$GAME/build-mobile-bench/$TARGET}"
mkdir -p "$OUT"

CORE=(game/autopilot game/bot game/breath game/chase_camera game/combat game/course
      game/demo_pilot game/element game/flight game/hoard_run game/maneuver game/match
      game/prey game/rally core/noise)
ENGINE=(render/raster render/renderer render/terrain_draw render/dragon_model render/font
        render/particles render/props render/raster_queue audio/sfx_mixer audio/sfx_synth
        sim/game sim/run sim/draw)
SRCS=("$HERE/cpu_bench.cpp" "$MCU/overlay/game/terrain.cpp" "$MCU/shim/sdl_shim.cpp")
for s in "${CORE[@]}"; do SRCS+=("$GAME/src/$s.cpp"); done
for s in "${ENGINE[@]}"; do SRCS+=("$MCU/src/$s.cpp"); done
INC=(-I"$MCU/overlay" -I"$GAME/src" -I"$MCU/shim" -I"$MCU/src")
FLAGS=(-std=c++20 -O2 -DDRAGON_MCU_HAS_FILES=0 -DNDEBUG)

case "$TARGET" in
  mac) CXX=(clang++) ;;
  pi) CXX=(zig c++ -target arm-linux-musleabihf -mcpu=arm1176jzf_s -static) ;;
  android)
    NDK="${ANDROID_NDK_HOME:-$(ls -d /opt/homebrew/share/android-ndk 2>/dev/null || true)}"
    CLANG="$(ls "$NDK"/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android29-clang++ | head -1)"
    CXX=("$CLANG" -static-libstdc++) ;;
  *) echo "unknown target $TARGET" >&2; exit 2 ;;
esac
"${CXX[@]}" "${FLAGS[@]}" "${INC[@]}" "${SRCS[@]}" -o "$OUT/cpu_bench"
echo "$OUT/cpu_bench"

GLES="$HERE/gles_bench.c"
DEPS="$(dirname "$OUT")"
case "$TARGET" in
  mac)
    ANGLE="${ANGLE_DIR:-/Applications/Visual Studio Code.app/Contents/Frameworks/Electron Framework.framework/Versions/A/Libraries}"
    NDKINC="$(ls -d /opt/homebrew/share/android-ndk/toolchains/llvm/prebuilt/*/sysroot/usr/include | head -1)"
    A="$DEPS/mac-angle"
    mkdir -p "$A/lib" "$A/inc"
    cp "$ANGLE/libEGL.dylib" "$ANGLE/libGLESv2.dylib" "$A/lib/"
    cp -R "$NDKINC/EGL" "$NDKINC/GLES2" "$NDKINC/KHR" "$A/inc/"
    clang -O2 -I"$A/inc" "$GLES" -o "$OUT/gles_bench_angle" -L"$A/lib" -lEGL -lGLESv2
    echo "$OUT/gles_bench_angle" ;;
  pi)
    RPI_CC="${RPI_CC:-$HOME/src/rpi/zig-cross/arm-linux-gnueabihf-gcc}"
    VC="$DEPS/pi-vc" M="$DEPS/pi-mesa"
    mkdir -p "$VC" "$M/lib" "$M/inc"
    rsync -a rpi:/opt/vc/include rpi:/opt/vc/lib "$VC/"
    # Relative -L paths: Zig re-roots absolute ones under --sysroot.
    cd "$DEPS"
    "$RPI_CC" -O2 -DGB_BRCM -Ipi-vc/include -Ipi-vc/include/interface/vcos/pthreads \
        -Ipi-vc/include/interface/vmcs_host/linux "$GLES" -o "$OUT/gles_bench_brcm" -Lpi-vc/lib \
        -lbrcmEGL -lbrcmGLESv2 -lbcm_host -lvcos -lvchiq_arm -lm -Wl,-rpath,/opt/vc/lib
    rsync -a rpi:/usr/include/EGL rpi:/usr/include/GLES2 rpi:/usr/include/KHR "$M/inc/"
    rsync -aL rpi:/usr/lib/arm-linux-gnueabihf/libEGL.so.1 rpi:/usr/lib/arm-linux-gnueabihf/libGLESv2.so.2 \
        rpi:/usr/lib/arm-linux-gnueabihf/libGLdispatch.so.0 "$M/lib/"
    ln -sf libEGL.so.1 "$M/lib/libEGL.so"
    ln -sf libGLESv2.so.2 "$M/lib/libGLESv2.so"
    "$RPI_CC" -O2 -DGB_MESA -DEGL_NO_X11 -Ipi-mesa/inc "$GLES" -o "$OUT/gles_bench_mesa" -Lpi-mesa/lib \
        -lEGL -lGLESv2 -Wl,-rpath-link,pi-mesa/lib -lm
    echo "$OUT/gles_bench_brcm $OUT/gles_bench_mesa" ;;
  android)
    "${CLANG%++}" -O2 -DGB_ANDROID "$GLES" -o "$OUT/gles_bench" -lEGL -lGLESv2 -lm
    echo "$OUT/gles_bench" ;;
esac
