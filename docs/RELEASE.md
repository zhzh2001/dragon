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
game-claude/tools/release/export_public.sh dragon-public-next --ref <last commit worth shipping>
cd dragon-public-next && git remote add origin https://github.com/zhzh2001/dragon.git
git fetch origin && git merge-base --is-ancestor origin/main main && git push origin main
cd .. && rm -rf dragon-public && mv dragon-public-next dragon-public
```

The rewrite is deterministic, so unchanged history keeps its hashes and the
push is a fast-forward. If `merge-base` fails, something in the excludes or
the screenshot rule changed what old commits contain. Find out what before
anything is forced: a forced push breaks every clone.

**When to publish.** Only when something new is ready to ship -- a release,
a finished feature -- never after every commit, and never with work in
progress (the user's call, 2026-10-03). `--ref` exports `main` up to a given
commit, so a mirror update can stop before whatever is still in progress.
The retro track's R2 commits, for one, stay private until the retro build
has something to show.

**The one forced update (2026-10-03).** The D&D-derived gold studies
(`ATTRIBUTION.md`, "Gold reference studies") had been published on
2026-10-02. At the user's call the mirror was re-exported with
`docs/concept/metallic-dragons-2026-10-02/`,
`artifacts/dragon-options/gold-ribbon-test/` and the ribbon preview tool
added to the excludes, at `--ref ce13d7a` (the commit already public, so no
new work went out with it), and force-pushed over `d1a1fe3` with a lease. 176
of the 186 commits kept their hashes, `v0.1.0` among them; the ten from the
first gold commit on were rewritten. The repository had no forks, stars or
watchers. GitHub keeps serving the old commits by SHA until it garbage-collects
them, and only a GitHub Support request purges that sooner.

## A copy of the game

There are six packages, one script each, all run on the Mac:

| Script | Package | Built how | Checked where |
|---|---|---|---|
| `package_macos.sh` | `Dragon-V-macos.zip` | natively, MSL baked | the script renders a frame from the unpacked zip |
| `package_windows.sh` | `Dragon-V-windows.zip` | MinGW-w64, DXIL and SPIR-V | x99-windows: the goldens with the unpacked `dragon.exe` |
| `package_xp.sh` | `Dragon-V-xp.zip` | llvm-mingw for XP, D3D9 shaders baked under Wine (`WINE=`, `docs/porting/D3D9.md`) | `check_imports.py` in the script; then the G41 or the Atom (`docs/porting/CARDS.md`) |
| `package_linux.sh` | `Dragon-V-linux.tar.gz` | on `LINUX_HOST` (x99) over ssh, Zig against glibc 2.31 | the script gates on glibc and imports; then the goldens on x99's CachyOS |
| `package_android.sh` | `Dragon-V-android.apk` | `tools/android/build_apk.sh`, the data inside the APK | the K30 over adb: a first start unpacks the data, then a run |
| `package_pi.sh` | `Dragon-V-pi.tar.gz` | `tools/pi/build.sh`, one species | the Pi 1B from its console |

Each refuses to package uncommitted changes. The Windows and XP packages
carry model packs for the D3D9 tiers; the XP package is also how a
Direct3D 9 card on Windows 10 runs the game.

**The Linux package** has no container. Zig's clang targets glibc 2.31
(Ubuntu 20.04, Debian 11) from x99's CachyOS (glibc 2.43), the way the Pi
build pins Bullseye. SDL opens X11, Wayland, Vulkan and the sound servers
at run time, so the build machine's headers are enough
(`tools/linux/zig-cc`). The executable imports only glibc. Zig 0.17.0 is
in `~/opt` on x99 (`ZIG=` overrides).

**The Android APK** carries its data and a manifest (`data.txt`), and
`core/paths.cpp` copies the files to the app's external files directory
on the first start of each version. It is signed with
`~/.android/dragon-release.keystore`, password in the `.pass` file beside
it, made by the first `package_android.sh` on 2026-11-01. Keep both, and
back them up: an APK signed with another key does not install over the
last one, and uninstalling deletes the player's records.

The macOS script, and what P0 changed to make a copy work, are in
`docs/PORTING.md`. The macOS zip holds `Dragon/`:

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
