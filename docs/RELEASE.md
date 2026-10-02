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

Measured on 2026-10-01: the full history, before the screenshot rules, was
164 commits and 756 MB, and 664 MB squashed. Squashing buys little, because
the tree itself is most of the weight. With superseded image versions
stripped and the 72 unreferenced renders deleted from `main`, the published
mirror is **167 commits and 574 MB**. Keep the history. Its commit messages
are the project's record of why things are the way they are, and they carry
the `Co-Authored-By` trailers that `AI_DISCLOSURE.md` points to. A render that
no doc cites does not belong in `artifacts/`: cite it or delete it.

The superseded renders are stripped by a **frozen** list,
`tools/release/stripped_images.txt` (167 ids, fixed at the P1 publish). The
first version recomputed the list from each export's tree. That rewrote old
commits whenever a render was superseded later, and the P2 push was refused
as a non-fast-forward. So the public history is append-only now. A render
replaced after P1 stays in it, and pruning more means a forced push, which is
the user's decision.

`AGENTS.md`, `CLAUDE.md` and `.claude/skills/` go public too. They are
honest evidence of how the project was made, and they hold no secrets: the
2026-10-01 sweep found no keys or tokens. The machine names (x99, t5810) are
hostnames on a private network, not addresses.

**Published** on 2026-10-01 as https://github.com/zhzh2001/dragon (public,
MIT detected, `main` only). The mirror checkout is `~/src/dragon-public`, with
`origin` pointing there. To update it:

```sh
cd ~/src && rm -rf dragon-public-next
game-claude/tools/release/export_public.sh dragon-public-next
cd dragon-public-next && git remote add origin https://github.com/zhzh2001/dragon.git
git fetch origin && git merge-base --is-ancestor origin/main main && git push origin main
cd .. && rm -rf dragon-public && mv dragon-public-next dragon-public
```

The rewrite is deterministic, so unchanged history keeps its hashes and the
push is a fast-forward. If `merge-base` fails, something in the excludes or
the screenshot rule changed what old commits contain. Find out what before
anything is forced: a forced push breaks every clone.

## A copy of the game

```sh
tools/release/package_macos.sh 0.1.0     # -> dist/Dragon-0.1.0-macos.zip
```

The script, and what P0 changed to make a copy work, are in
`docs/PORTING.md`. Windows is `tools/release/package_windows.sh`, which
cross-builds on the Mac: `Dragon/` holds `dragon.exe`, the shaders baked to
DXIL and SPIR-V, the same `assets/`, a `README.txt`
(`tools/release/README-windows.txt`) and the licence files. Nothing on the
Mac can run the result, so unpack it on x99-windows and run the goldens
with `--binary` before uploading it (`docs/PORTING.md`, P2). There is no Linux
package yet; the same section says why. The zip holds `Dragon/`:

- `Dragon.app`. Its `Contents/Resources` carries `shaders/`, `assets/`
  (props, textures, fonts, and the seven species' `.glb` at 2048² with
  their `.cfg` files) and the four licence files.
- `README.txt` (`tools/release/README-package.txt`): how to get past
  Gatekeeper, the controls, the AI notice.
- `LICENSE`, `THIRD_PARTY_NOTICES.md`, `ATTRIBUTION.md`, `AI_DISCLOSURE.md`.

The script ends by unpacking the zip and rendering a frame from it, and
leaves that frame in `dist/` beside the zip. Look at it before uploading.

Upload it as a GitHub release asset, as a draft first, then publish once
the download has been tried on another Mac:

```sh
gh release create v0.1.0 dist/Dragon-0.1.0-macos.zip --repo zhzh2001/dragon \
  --draft --title "Dragon 0.1.0" --notes-file <notes>
```

The notes say the game is AI-assisted and link `AI_DISCLOSURE.md`. The
release tag points at the public mirror's `main`, not this repository.

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
