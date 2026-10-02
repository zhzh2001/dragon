#!/bin/sh
# Translate every pipeline shader for a package that carries no compiler.
#
#   tools/release/bake_shaders.sh OUT_DIR
#
# A development build compiles shaders/*.hlsl at runtime through
# SDL_shadercross. A package is built with DRAGON_SHADERCROSS=OFF and instead
# loads what this writes: for each shader with an fs_main (the headers have
# none), <stem>.vertex.msl and <stem>.fragment.msl, each beside its .json, the
# reflection that holds the resource counts SDL needs (gfx/pipeline.cpp).
# The CLI makes the same DXC-then-SPIRV-Cross translation as the runtime path,
# with the same per-stage define. It is found under
# ~/.local/opt/shadercross, or $SHADERCROSS.
set -eu
out=$1
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
sc=${SHADERCROSS:-$HOME/.local/opt/shadercross/bin/shadercross}
[ -x "$sc" ] || { echo "no shadercross at $sc (docs/PORTING.md, P1)" >&2; exit 1; }
mkdir -p "$out"
count=0
for src in "$root"/shaders/*.hlsl; do
  grep -q 'fs_main(' "$src" || continue
  stem=$(basename "$src" .hlsl)
  for stage in vertex fragment; do
    if [ $stage = vertex ]; then entry=vs_main; define=VERTEX_STAGE; else entry=fs_main; define=FRAGMENT_STAGE; fi
    for dest in MSL JSON; do
      ext=$(echo $dest | tr 'A-Z' 'a-z')
      "$sc" "$src" -s HLSL -d $dest -t $stage -e $entry -I "$root/shaders" -D$define \
        -o "$out/$stem.$stage.$ext" >/dev/null 2>"$out/.err" || { cat "$out/.err" >&2; exit 1; }
    done
    count=$((count + 1))
  done
done
rm -f "$out/.err"
echo "baked $count shader stages into $out"
