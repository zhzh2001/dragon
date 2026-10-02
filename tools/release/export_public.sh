#!/bin/sh
# Build the public mirror of this repository in a separate directory.
#
#   tools/release/export_public.sh OUT_DIR [--email ADDRESS] [--squash]
#
# The working repository is never touched: this clones `main` alone (no
# codex/*, fable/* or worktree branches), then rewrites the clone's history
# with git-filter-repo (`brew install git-filter-repo`):
#   - drops every path in tools/release/public_excludes.txt from all of
#     history -- the derivatives of the CC BY-NC dragon (ATTRIBUTION.md);
#   - replaces the local home path with `~` in files and commit messages;
#   - with --email, rewrites the author/committer address of the human
#     commits (Paleshell) to ADDRESS, e.g. a GitHub noreply address;
#   - drops every image version that is not in the current tree: renders
#     that were replaced or deleted (docs/RELEASE.md);
#   - with --squash, replaces the history with one commit.
# It prints the resulting size and pushes nothing: pushing is a separate,
# deliberate step (docs/RELEASE.md).
set -eu

[ $# -ge 1 ] || { sed -n 2,18p "$0"; exit 2; }
out=$1; shift
email=""; squash=0
while [ $# -gt 0 ]; do
  case $1 in
    --email) email=$2; shift 2 ;;
    --squash) squash=1; shift ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
done

src=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
[ ! -e "$out" ] || { echo "$out already exists" >&2; exit 1; }
command -v git-filter-repo >/dev/null || { echo "needs git-filter-repo" >&2; exit 1; }

git clone --quiet --no-local --single-branch --branch main "$src" "$out"
cd "$out"

paths=""
while IFS= read -r p; do
  case $p in ''|'#'*) continue ;; esac
  paths="$paths --path $p"
done < "$src/tools/release/public_excludes.txt"

replace=$(mktemp)
printf '%s==>~\n' "$HOME" > "$replace"

# Text blobs get the home path replaced. PNGs cannot be edited that way, so
# a PNG's text chunks (Blender writes the .blend path into each render's
# metadata) are dropped whole when they name the home directory; the pixels
# and every other chunk are untouched, and each chunk carries its own CRC.
callback=$(mktemp)
cat > "$callback" <<PY
import struct
home = b"$HOME"
data = blob.data
if data.startswith(b"\\x89PNG\\r\\n\\x1a\\n"):
    out, i = [data[:8]], 8
    while i + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[i:i + 8])
        chunk = data[i:i + 12 + n]
        if not (kind in (b"tEXt", b"zTXt", b"iTXt") and home in chunk):
            out.append(chunk)
        i += 12 + n
    blob.data = b"".join(out)
elif b"\\0" not in data[:8000]:
    blob.data = data.replace(home, b"~")
PY

# Screenshots: history keeps only the image versions the current tree still
# has. A render that was replaced or deleted was superseded, and those are
# most of the history's weight. Ids are the original ones, so this runs
# before anything is rewritten.
stale=$(mktemp)
git ls-tree -r HEAD | awk '{print $3}' | sort -u > "$stale.head"
git rev-list --objects --all \
  | grep -i -E ' .*\.(png|gif|jpe?g|bmp)$' | awk '{print $1}' | sort -u \
  | comm -23 - "$stale.head" > "$stale"
echo "stripping $(wc -l < "$stale" | tr -d ' ') superseded image versions"

# shellcheck disable=SC2086
git filter-repo --force --invert-paths $paths --strip-blobs-with-ids "$stale"
rm -f "$stale" "$stale.head"
git filter-repo --force \
  --blob-callback "$(cat "$callback")" --replace-message "$replace"
rm -f "$callback"

if [ -n "$email" ]; then
  mailmap=$(mktemp)
  for old in $(git log --format='%ae' --author=Paleshell | sort -u); do
    printf 'Paleshell <%s> <%s>\n' "$email" "$old" >> "$mailmap"
  done
  git filter-repo --force --mailmap "$mailmap"
  rm -f "$mailmap"
fi
rm -f "$replace"

if [ "$squash" = 1 ]; then
  git checkout --quiet --orphan public
  git commit --quiet -m "Dragon: public release

The full development history is private; this is its tree at release."
  git branch -D main >/dev/null
  git branch -m main
  git reflog expire --expire=now --all
fi
git gc --quiet --prune=now

for p in $paths; do
  [ "$p" = --path ] && continue
  if git log --all --format=%H -- "$p" | grep -q .; then
    echo "FAILED: $p is still in history" >&2; exit 1
  fi
done
if git grep -q -a "$HOME" HEAD; then
  echo "FAILED: $HOME still appears in the tree" >&2; exit 1
fi

echo "commits: $(git rev-list --count HEAD)"
echo "pack:    $(du -sh .git | cut -f1)"
echo "tree:    $(git ls-files | wc -l | tr -d ' ') files"
echo "authors:"; git log --format='  %an <%ae>' | sort | uniq -c
