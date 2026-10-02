#!/bin/sh
# Translate every pipeline shader for a package that carries no compiler.
#
#   tools/release/bake_shaders.sh OUT_DIR [msl|spv|dxil ...]   # default msl
#
# A development build compiles shaders/*.hlsl at runtime through
# SDL_shadercross. A package is built with DRAGON_SHADERCROSS=OFF and instead
# loads what this writes: for each shader with an fs_main (the headers have
# none), <stem>.vertex.<ext> and <stem>.fragment.<ext>, each beside its .json,
# the reflection that holds the resource counts SDL needs (gfx/pipeline.cpp).
# The formats are the package platform's: msl for macOS, spv for Linux
# (Vulkan), dxil and spv for Windows (D3D12, and Vulkan with --gpu-driver).
# The loader takes whichever the device accepts.
# The CLI makes the same DXC-then-SPIRV-Cross translation as the runtime path,
# with the same per-stage define. It is found under
# ~/.local/opt/shadercross, or $SHADERCROSS.
set -eu
out=$1
shift
[ $# -gt 0 ] || set -- msl
for format in "$@"; do
  case $format in msl|spv|dxil) ;; *) echo "unknown format $format" >&2; exit 2 ;; esac
done
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
sc=${SHADERCROSS:-$(command -v shadercross || echo "$HOME/.local/opt/shadercross/bin/shadercross")}
[ -x "$sc" ] || { echo "no shadercross at $sc (docs/PORTING.md, P1)" >&2; exit 1; }
mkdir -p "$out"
count=0
for src in "$root"/shaders/*.hlsl; do
  grep -q 'fs_main(' "$src" || continue
  stem=$(basename "$src" .hlsl)
  for stage in vertex fragment; do
    if [ $stage = vertex ]; then entry=vs_main; define=VERTEX_STAGE; else entry=fs_main; define=FRAGMENT_STAGE; fi
    pairs="JSON:json"
    for format in "$@"; do
      case $format in msl) pairs="$pairs MSL:msl" ;; spv) pairs="$pairs SPIRV:spv" ;; dxil) pairs="$pairs DXIL:dxil" ;; esac
    done
    for pair in $pairs; do
      d=${pair%%:*}; ext=${pair#*:}
      "$sc" "$src" -s HLSL -d $d -t $stage -e $entry -I "$root/shaders" -D$define \
        -o "$out/$stem.$stage.$ext" >/dev/null 2>"$out/.err" || { cat "$out/.err" >&2; exit 1; }
    done
    count=$((count + 1))
  done
done
rm -f "$out/.err"
echo "baked $count shader stages into $out"
