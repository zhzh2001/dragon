#!/bin/sh
# Build the R0 spike (r0.cpp) for 32-bit Windows with the Mac's MinGW, and
# list what it imports -- the XP question is whether any import is newer
# than XP SP3.
#
#   tools/r0/build.sh [OUT_DIR]        # default build-r0/
set -eu
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
out=${1:-$root/build-r0}
mkdir -p "$out"
miniaudio=$(ls -d "$root"/build/_deps/miniaudio-src 2>/dev/null || true)
[ -n "$miniaudio" ] || { echo "configure the main build first (it fetches miniaudio)" >&2; exit 1; }

# -static: no libgcc/libstdc++/winpthread DLLs to ship. -D_WIN32_WINNT=0x0501:
# headers declare only what XP has, so a newer API fails to compile here
# rather than to load there.
i686-w64-mingw32-g++ -std=c++17 -O2 -static -s \
  -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 \
  -I"$miniaudio" "$root/tools/r0/r0.cpp" -o "$out/r0.exe" \
  -ld3d9 -lole32 -lwinmm -luuid
echo "built $out/r0.exe ($(wc -c < "$out/r0.exe" | tr -d ' ') bytes)"
i686-w64-mingw32-objdump -p "$out/r0.exe" | awk '
  /DLL Name:/ {dll=$3; next}
  /^\t[0-9a-f]+  <none>  [0-9a-f]+  / && dll {print dll, $4}' > "$out/r0.imports.txt"
echo "imports: $(wc -l < "$out/r0.imports.txt" | tr -d ' ') functions from $(cut -d' ' -f1 "$out/r0.imports.txt" | sort -u | tr '\n' ' ')"
