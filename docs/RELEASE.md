# Releasing: the public repository and the copies

How this repository becomes a public GitHub repository, and how a playable copy
is built and handed out. Which material may go where is decided in
`ATTRIBUTION.md` ("What ships where"). This file covers how to carry those
decisions out.

## The public repository is a filtered mirror

This working repository is never pushed directly. It holds local branches
(`codex/*`, `fable/*`, the worktree branches), and its history contains
Blender re-exports of the CC BY-NC dragon. Rewriting it in place would break
every worktree and every other session sharing it. Instead:

```sh
tools/release/export_public.sh ../dragon-public                 # history kept
tools/release/export_public.sh ../dragon-public --email ID+Paleshell@users.noreply.github.com
tools/release/export_public.sh ../dragon-public --squash        # one commit
```

The script clones `main` alone into a new directory and rewrites the clone:

- every path in `tools/release/public_excludes.txt` is dropped from all of
  history;
- the local home path becomes `~` in every text file and commit message;
- PNG text chunks that name the home path are dropped. Blender writes the
  `.blend` path into each render's metadata; the pixels are untouched;
- with `--email`, the human commits' address is rewritten.

It then checks that the excluded paths and the home path are gone, and
prints the size. It pushes nothing.

Measured on 2026-10-01:

| Variant | Commits | `.git` |
|---|---|---|
| history kept | 164 | 756 MB |
| `--squash` | 1 | 664 MB |

Squashing buys little, because the tree itself is most of it: `artifacts/` is
646 MB of renders. **Keep the history.** Its commit messages are the
project's record of why things are the way they are, and they carry the
`Co-Authored-By` trailers that `AI_DISCLOSURE.md` points to.

`AGENTS.md`, `CLAUDE.md` and `.claude/skills/` go public too. They are
honest evidence of how the project was made, and they hold no secrets: the
2026-10-01 sweep found no keys or tokens. The machine names (x99, t5810) are
hostnames on a private network, not addresses.

Publishing, once the mirror is built:

1. Create an empty GitHub repository (no README or licence, so the push
   is a fast-forward).
2. In the mirror: `git remote add origin git@github.com:<user>/dragon.git`,
   then `git push -u origin main`. Push `main` only, never `--all`.
3. Topics and description: say "AI-assisted" in the description. The label
   is a term of the Hunyuan agreement (`AI_DISCLOSURE.md`).

Later updates: re-run the export into a fresh directory and push. The
rewrite is deterministic, so unchanged history keeps its hashes. Check that
the push is a fast-forward before pushing; if it is not, find out why
before anything is forced.

## A copy of the game

A copy needs Phase P0 of `docs/PORTING.md` first. Today's binary finds its
shaders and assets through absolute source-tree paths compiled into it, and
writes its records into `assets/`, so a copied build runs only on the
machine that built it.

After P0, a release package holds:

- the binary and SDL3 (as `SDL3.framework` in the `.app` on macOS, `SDL3.dll`
  on Windows);
- `shaders/`, or compiled blobs after P1;
- `assets/`: the cfg files, `props/`, `textures/`, `fonts/`, and the
  generated roster;
- `LICENSE`, `THIRD_PARTY_NOTICES.md`, `ATTRIBUTION.md`, `AI_DISCLOSURE.md`,
  and a short `README.txt` with the controls.

**The roster, downsampled.** Twelve species at about 50 MB each, at 4096² PBR,
is too much to hand out. The release step resizes their textures to 2048²,
about a quarter of the size, which is indistinguishable at chase-camera
distance; this step is to be written. It must keep the files' metadata,
because the Hunyuan agreement forbids removing its AI marks (3.6(1)).

**The default model** is the generated roster, Embercrest first
(`kDefaultRoster` in `src/app.cpp`); a package that lacks some of them
simply loads the rest. The two Sketchfab dragons are not shipped
(`ATTRIBUTION.md`).

## Checklist before a public push or package

- [ ] `tools/release/export_public.sh` passed its own checks.
- [ ] `git -C <mirror> log --all --format='%an <%ae>' | sort -u` shows only
      the addresses you mean to publish.
- [ ] No file in the package is listed as "no" in `ATTRIBUTION.md`; no `dragon.glb` or `alt/`.
- [ ] The release notes say the game is AI-assisted and link `AI_DISCLOSURE.md`.
- [ ] On itch.io, the AI-generated tags are set (*Graphics*, *Code*).
