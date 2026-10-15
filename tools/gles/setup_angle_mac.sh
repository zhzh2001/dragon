#!/bin/bash
# The Mac's GLES 2.0 for developing the GLES backend (src/rhi/gles,
# docs/porting/MOBILE.md M4): ANGLE, out of an installed Electron app (VS
# Code's by default), into ~/.local/opt/angle with the Khronos headers from
# the NDK. ANGLE is BSD-licensed; this is a development dependency and is
# never shipped -- the Pi's GLES is Mesa's.
#
#   tools/gles/setup_angle_mac.sh [ELECTRON_LIBRARIES_DIR]
#
# Electron's dylibs are named "./libEGL.dylib", resolved against the current
# directory; the copies are renamed @rpath/... and signed again (ad hoc).
set -euo pipefail
src=${1:-"/Applications/Visual Studio Code.app/Contents/Frameworks/Electron Framework.framework/Versions/A/Libraries"}
dest=${ANGLE_DIR:-$HOME/.local/opt/angle}
ndk_inc=$(ls -d /opt/homebrew/share/android-ndk/toolchains/llvm/prebuilt/*/sysroot/usr/include | head -1)
mkdir -p "$dest/lib" "$dest/include"
for lib in libEGL libGLESv2; do
  cp "$src/$lib.dylib" "$dest/lib/"
  install_name_tool -id "@rpath/$lib.dylib" "$dest/lib/$lib.dylib" 2>/dev/null
  codesign -f -s - "$dest/lib/$lib.dylib" 2>/dev/null
done
# libEGL loads libGLESv2 by its old name; point it at the copy.
install_name_tool -change ./libGLESv2.dylib @rpath/libGLESv2.dylib "$dest/lib/libEGL.dylib" 2>/dev/null || true
codesign -f -s - "$dest/lib/libEGL.dylib" 2>/dev/null
cp -R "$ndk_inc/EGL" "$ndk_inc/GLES2" "$ndk_inc/GLES3" "$ndk_inc/KHR" "$dest/include/"
otool -L "$dest/lib/libEGL.dylib" | head -3
echo "ANGLE in $dest"
