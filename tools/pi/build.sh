#!/bin/bash
# Cross-builds the game for the Raspberry Pi 1B (docs/porting/MOBILE.md, M5):
#
#   tools/pi/build.sh            # build-pi/dragon, an armv6 executable
#
# Zig's clang, ARMv6 hard-float, glibc 2.31 (Bullseye), libc++ linked in
# statically; SDL3 from source with KMSDRM and GLES only (no X11, Wayland or
# desktop GL); the GLES 2.0 backend as the default driver. The sysroot is
# ~/src/rpi's Bullseye one plus the Pi's DRM, GBM, EGL and GLES development
# files, copied once from `ssh rpi` (libdrm-dev libgbm-dev libegl-dev
# libgles-dev libudev-dev installed there).
set -euo pipefail
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
build=build-pi
sysroot=$build/sysroot
if [ ! -f "$sysroot/usr/include/gbm.h" ]; then
  mkdir -p "$sysroot"
  rsync -a "$HOME/src/rpi/.cross/rpi-sysroot/" "$sysroot/"
  ssh rpi 'cd / && tar cf - usr/include/gbm.h usr/include/libdrm usr/include/xf86drm.h usr/include/xf86drmMode.h \
    usr/include/EGL usr/include/GLES2 usr/include/GLES3 usr/include/KHR usr/include/libudev.h \
    $(ls -d usr/lib/arm-linux-gnueabihf/libdrm* usr/lib/arm-linux-gnueabihf/libgbm* usr/lib/arm-linux-gnueabihf/libEGL* \
      usr/lib/arm-linux-gnueabihf/libGLESv2* usr/lib/arm-linux-gnueabihf/libGLdispatch* usr/lib/arm-linux-gnueabihf/libudev* \
      usr/lib/arm-linux-gnueabihf/pkgconfig/libdrm*.pc usr/lib/arm-linux-gnueabihf/pkgconfig/gbm.pc \
      usr/lib/arm-linux-gnueabihf/pkgconfig/egl.pc usr/lib/arm-linux-gnueabihf/pkgconfig/glesv2.pc \
      usr/lib/arm-linux-gnueabihf/pkgconfig/libudev.pc)' | tar xf - -C "$sysroot"
fi
abs_sysroot=$(cd "$sysroot" && pwd)
# The GL headers alone, in a directory of their own: -I on the sysroot's
# /usr/include would put Bullseye's glibc headers ahead of Zig's.
gl_include=$root/$build/gles-include
mkdir -p "$gl_include"
rsync -a "$sysroot/usr/include/EGL" "$sysroot/usr/include/GLES2" "$sysroot/usr/include/GLES3" \
  "$sysroot/usr/include/KHR" "$gl_include/"
export PKG_CONFIG_SYSROOT_DIR=$abs_sysroot
export PKG_CONFIG_LIBDIR=$abs_sysroot/usr/lib/arm-linux-gnueabihf/pkgconfig:$abs_sysroot/usr/share/pkgconfig
export ZIG_GLOBAL_CACHE_DIR=$root/$build/zig-cache
cmake -S . -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/zig-armv6-pi.cmake -DCMAKE_BUILD_TYPE=Release \
  -DDRAGON_FETCH_SDL=ON -DDRAGON_SHADERCROSS=OFF -DDRAGON_DEV_ROOTS=OFF -DDRAGON_GLES=ON -DDRAGON_GLES_DEFAULT=ON -DDRAGON_GLES_INCLUDE="$gl_include" \
  -DDRAGON_GLES_LIBRARY="$abs_sysroot/usr/lib/arm-linux-gnueabihf/libGLESv2.so" \
  -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=ON -DSDL_OPENGL=OFF -DSDL_OPENGLES=ON -DSDL_VULKAN=OFF \
  -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_ALSA=OFF \
  -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBURING=OFF -DSDL_HIDAPI_LIBUSB=OFF >/dev/null
cmake --build "$build" --target dragon
ls -la "$build/dragon"
