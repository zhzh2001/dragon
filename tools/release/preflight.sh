#!/bin/zsh
# The gates a release passes before any package is built.
#
#   tools/release/preflight.sh [--export]
#
# Every step prints PASS or FAIL and the script goes on, so one run shows
# every fault. It exits non-zero on any FAIL. About ten minutes on the Mac.
#
#   1. The tracked tree is clean: a packager refuses anything else.
#   2. The development build builds, and the suites pass.
#   3. The eight reference frames match the metal set.
#   4. Every shader bakes for every variant a package carries: MSL for
#      macOS, and GLES for the Pi's three tiers. The 0.4.0 packages found
#      two shaders that no retro variant could compile, at the first
#      packager and not before (docs/RELEASE.md).
#   5. The Windows and XP builds link, and the XP build imports nothing XP
#      lacks.
#      With WINE set, the D3D9 shaders bake for the three retro tiers.
#   6. With --export: a trial export of main into a scratch directory. It
#      must fast-forward from the published mirror, and it prints what the
#      push would add. Nothing is pushed.
set -u

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
python=${PYTHON:-$HOME/.local/py314/bin/python3}
export PATH=$(dirname "$python"):$PATH
scratch=$(mktemp -d)
fail=0
say() { echo "$1 $2"; [ "$1" = PASS ] || fail=1; }
step() { # name, command...
  name=$1; shift
  if "$@" > "$scratch/step.log" 2>&1; then say PASS "$name"; else say FAIL "$name: $(grep -v '^\[' "$scratch/step.log" | tail -2 | tr '\n' ' ' | cut -c1-220)"; fi
}

dirty=$(git status --porcelain --untracked-files=no)
[ -z "$dirty" ] && say PASS "the tracked tree is clean" || say FAIL "uncommitted changes: $(echo "$dirty" | head -3 | tr '\n' ' ')"

step "the development build" cmake --build build
step "the suites" ctest --test-dir build
step "the eight reference frames" "$python" tools/golden/golden.py compare

step "every shader bakes to MSL, every variant" tools/release/bake_shaders.sh "$scratch/msl"
for tier in sm2 sm2-noshadows ff; do
  step "the GLES bake, tier $tier" "$python" tools/gles/bake_gles.py "$scratch/gles" --tier $tier
done

# The D3D9 bake compiles under Wine, so it runs only where WINE names one
# (tools/release/package_xp.sh has the same need). A shader shared with the
# XP build that fxc refuses otherwise shows at the XP packager.
if [ -n "${WINE:-}" ]; then
  for tier in sm3 sm2 ff; do
    step "the D3D9 bake, tier $tier" "$python" tools/d3d9/bake_d3d9.py "$scratch/d3d9" --tier $tier
  done
else
  echo "SKIP the D3D9 bake: set WINE (and WINEPREFIX) to run it"
fi

if [ -d build-win ]; then step "the Windows cross-build" cmake --build build-win; else say FAIL "no build-win directory: configure it (CLAUDE.md, Build & run)"; fi
if [ -d build-xp ]; then
  step "the XP cross-build" cmake --build build-xp
  step "the XP build's imports" "$python" tools/xp/check_imports.py build-xp/dragon.exe
else
  say FAIL "no build-xp directory: configure it (CLAUDE.md, Build & run)"
fi

if [ "${1:-}" = --export ]; then
  mirror=${MIRROR:-$HOME/src/dragon-public}
  trial=$scratch/export
  if tools/release/export_public.sh "$trial" > "$scratch/export.log" 2>&1; then
    published=$(git -C "$mirror" rev-parse HEAD)
    if git -C "$trial" merge-base --is-ancestor "$published" main 2> /dev/null; then
      say PASS "the export fast-forwards from the mirror's $(git -C "$mirror" rev-parse --short HEAD)"
    else
      say FAIL "the export does not fast-forward from the mirror: an exclude or a stripped image changed a published commit"
    fi
    echo "     $(git -C "$trial" rev-list --count "$published"..main) new commits; the mirror $(du -sh "$mirror/.git" | cut -f1), the export $(du -sh "$trial/.git" | cut -f1)"
    echo "     heaviest new directories:"
    git -C "$trial" rev-list --objects "$published"..main | git -C "$trial" cat-file --batch-check='%(objecttype) %(objectsize:disk) %(rest)' \
      | awk '$1=="blob"{ n=split($3,a,"/"); k=a[1]; if (n>1) k=k"/"a[2]; if (n>2) k=k"/"a[3]; s[k]+=$2 } END{ for (k in s) printf "%8.0f MB %s\n", s[k]/1e6, k }' \
      | sort -rn | head -8 | sed 's/^/     /'
  else
    say FAIL "the export: $(tail -2 "$scratch/export.log" | tr '\n' ' ')"
  fi
fi

rm -rf "$scratch"
exit $fail
