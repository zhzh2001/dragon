#!/bin/sh
# Build swvp.exe, sampler.exe, tris.exe, kprof.exe, pcicfg.exe and
# gltris.exe with the XP toolchain
# (cmake/llvm-mingw-i686-xp.cmake's flags), with the XP build's import thunks
# linked in so they start on XP.
#   tools/d3d9/swvp/build.sh OUTDIR
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=$1
mkdir -p "$out"
m=${DRAGON_LLVM_MINGW:-$HOME/.local/opt/llvm-mingw-msvcrt}
llvm=${DRAGON_LLVM:-/opt/homebrew/opt/llvm@21}
lld=${DRAGON_LLD:-/opt/homebrew/opt/lld@21}
tmp=$(mktemp -d)
cxx="$llvm/bin/clang++ --target=i686-w64-mingw32 --sysroot=$m -resource-dir=$m/clang21-resource -stdlib=libc++ -msse2 -mfpmath=sse -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -O2"
$cxx -fno-threadsafe-statics -I"$root/src" -c "$root/src/platform/xp/xp_thunks.cpp" -o "$tmp/xp_thunks.o"
for tool in swvp sampler tris kprof pcicfg gltris; do
    $cxx -c "$root/tools/d3d9/swvp/$tool.cpp" -o "$tmp/$tool.o"
    $cxx -fuse-ld="$lld/bin/ld.lld" -rtlib=compiler-rt -unwindlib=libunwind -static \
        -Wl,--major-os-version,5,--minor-os-version,1,--subsystem,console:5.1 \
        "$tmp/$tool.o" "$tmp/xp_thunks.o" -ld3d9 -lwinmm -ladvapi32 -lopengl32 -lgdi32 -o "$out/$tool.exe"
    # The recorded exports have no opengl32.dll; gltris uses only OpenGL
    # 1.1, which every opengl32.dll since NT 3.5 exports.
    [ $tool = gltris ] || python3 "$root/tools/xp/check_imports.py" "$out/$tool.exe"
done
rm -rf "$tmp"
