#!/bin/sh
# Build SDL_shadercross, DXC included, for a development checkout.
#
#   tools/build_shadercross.sh [PREFIX]     # default ~/.local/opt/shadercross
#
# A development build compiles shaders/*.hlsl at runtime through it, so it is
# a prerequisite (CMakeLists.txt, DRAGON_SHADERCROSS). Homebrew packages no
# DXC, so shadercross is built "vendored": its own pinned SPIRV-Cross,
# SPIRV-Tools and DirectXShaderCompiler, against the system SDL3. The first
# build compiles DXC, about half an hour on an M-series Mac; it installs about
# 32 MB. The commit is pinned to the one P1 was verified against
# (docs/PORTING.md). Bump it deliberately, and re-run tools/golden after.
set -eu
prefix=${1:-$HOME/.local/opt/shadercross}
commit=1ff05bec573988a98ef9e0260b4da44f512b8367
src=${SHADERCROSS_SRC:-$HOME/src/third/SDL_shadercross}

[ -d "$src/.git" ] || git clone https://github.com/libsdl-org/SDL_shadercross.git "$src"
git -C "$src" fetch --quiet origin
git -C "$src" checkout --quiet "$commit"
git -C "$src" submodule update --init --recursive --depth 1

brew_prefix=$(brew --prefix 2>/dev/null || echo /usr/local)
cmake -S "$src" -B "$src/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSDLSHADERCROSS_VENDORED=ON -DSDLSHADERCROSS_DXC=ON \
  -DSDLSHADERCROSS_SHARED=ON -DSDLSHADERCROSS_STATIC=OFF \
  -DSDLSHADERCROSS_CLI=ON -DSDLSHADERCROSS_INSTALL=ON \
  -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_PREFIX_PATH="$brew_prefix"
cmake --build "$src/build"
cmake --install "$src/build"

# The installed CLI is linked against @rpath with no rpath of its own; point
# it at the libraries beside it, and re-sign after editing the binary.
install_name_tool -add_rpath @executable_path/../lib "$prefix/bin/shadercross" 2>/dev/null || true
codesign --force --sign - "$prefix/bin/shadercross" 2>/dev/null || true
"$prefix/bin/shadercross" --help >/dev/null 2>&1 && echo "shadercross installed in $prefix"
