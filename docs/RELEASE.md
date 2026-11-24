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

A Codex job that judges its work by render commits every frame it
looked at unless its brief says not to. Four such jobs on 2026-11-22
left 869 full-size captures (682 MB) beside their contact sheets. They
were deleted from `main` the same night, and since they had never been
exported their paths went into `public_excludes.txt`, which keeps them
out of the mirror's history too. A brief must say: commit the sheets
and the comparisons, keep single frames in the scratch directory.

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

The macOS, Windows and Linux packages carry thirteen dragons since
0.4.0: the seven first designs and the six second designs the modern
tier shows by default (`kDesignPairs` in `src/app.cpp`), each at 2048².
The clips of models that do not ship are left out of `assets/clips/`.

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

## A hang beside another render (2026-11-24)

The packaged macOS build, checked harder than the packager's one frame,
stopped for ever in 6 of 8 headless runs when a second process was
rendering on the same GPU. It spun at full CPU inside
`SDL_WaitForGPUIdle`. The 0.3.0 package did the same in 4 of 6. Alone,
neither hung in 8 runs, which is why no package check had seen it.

The cause was two threads waiting inside SDL's GPU API at once. The
frame timer (`SdlGpuDevice::run_gpu_timer`) waits on each frame's fence
on a thread of its own. The main thread called `SDL_WaitForGPUIdle`
meanwhile: every frame of a headless run, and in a window at every
resize, restart and change of resolution. On Metal the idle wait then
looped on a command buffer the fence wait had already recycled.

`wait_idle` now drains the timer first (`drain_gpu_timer`): it blocks
until the timer thread holds no fence and has none queued, and only
then calls SDL. The two other main-thread fence waits do the same.
After it, the macOS package ran 12 of 12 beside a second render, and
the Linux package 8 of 8 on x99's 5060 Ti.

The development build never showed it in thousands of runs. It links
Homebrew's SDL 3.4.20 where a package builds 3.4.16, and it is not
known which of the two differences, the version or the optimised
build, hides the race. A package check is now more than one frame:
run it beside another render.

## The 0.4.0 packages (2026-11-24)

Built for the first public update since 0.3.0. What changed in the
packagers is in `docs/showcase/PLAYTEST.md`, "Packaging, and what it
turned up".

| Package | Size | Checked on | How |
|---|---|---|---|
| `Dragon-0.4.0-macos.zip` | 525 MB | the M5 Mac, Metal | The eight reference frames with the package's baked shaders, against the `metal` set: all within tolerance, the largest difference 16 of 255 in one frame. A flap on the hero with its clips, a run from the start screen, and 8 of 8 runs beside a second render |
| `Dragon-0.4.0-windows.zip` | 587 MB | x99, Windows 10, the RTX 5060 Ti | The eight frames on Direct3D 12 and on Vulkan, loose against the `metal` set: all pass. The same flap and run. 6 of 6 runs beside a second render on each backend |
| `Dragon-0.4.0-linux.tar.gz` | 513 MB | x99, CachyOS, the RTX 5060 Ti | The eight frames on Vulkan, loose against the `metal` set: all pass. The same flap and run. 8 of 8 runs beside a second render. Newest symbol GLIBC_2.30 |

All three were built from commit `2fb8264`. The commits after it change
docs and the export tools, nothing a package carries. SHA-256:
`552c25e0…c950f9` (macOS), `a8d4f540…03317d` (Windows),
`df6f9503…ec7526` (Linux).

The sizes are about 330 MB over 0.3.0's: six more dragons at 20 MB
each, and the rock kit and landmarks at 240 MB. The XP, Android and Pi
packages were not rebuilt: the retro tiers' content did not change.

The reference frames on Windows and Linux are compared against the
Metal set with `--loose`. The `d3d12` and `vulkan` sets in
`tests/golden/` date from 2026-11-02 and no longer match the picture.
Recapture them on x99 when the picture settles.

A trial export of `main` fast-forwards from the published mirror
(`59978b5`). It would have added 1.8 GB to the mirror's 1.1 GB: 1.29 GB
of `artifacts/`, 263 MB of `assets/` (the rock kit, the landmarks), and
236 MB of reference frames. At the user's call, 507 MB of review
renders were first added to `public_excludes.txt`: the PNGs of the rock
kit's and the run props' reviews, the five prey folders, and storm3's
single frames. None had been published, so no public commit changed.
The export script takes `glob:` lines for this, so the JSON and text
records beside the PNGs still go out. One push must stay under GitHub's
2 GB.

The export at `--ref 366ceb1` was pushed on 2026-11-24 at 13:59:
`59978b5..32bb354`, 307 commits, a fast-forward. The mirror is 672
commits and 2.27 GB. The push took 22 minutes at about 1 MB a second.
`v0.4.0` is a draft release on the mirror's `main` with the three
packages and the notes in `dist/notes-0.4.0.md`. It is public once the
user has tried a download and pressed Publish.

## The README's pictures and clips

`tools/release/make_demo_clips.sh` renders `docs/media/`: the hero still,
the roster of six, and four clips (a hoard run's flight and first fight
from the demo pilot, the hero's wingbeat, the fire breath). Every scene
is a headless run recorded with `--record`, so a commit renders the same
frames again. Run it again when the picture changes enough to show.

The clips are animated WebP at 640x360 and 20 frames a second, 0.7 to
2.1 MB each. The same frames as GIF were 7 to 10 MB each. The mirror's
history is append-only, so every replaced clip stays in it: do not
re-render for a small change.

The script names its roster. The default roster also loads the gold and
silver studies when they are on the machine, and no picture of those may
be published (`ATTRIBUTION.md`).

## The release workflow (2026-11-24)

0.4.0 was released by hand, and three of its faults were found by a
packager or a check that ran late. The steps are now written down and
scripted:

- `.claude/skills/release/SKILL.md` is the procedure: seven steps, each
  with a gate, and the three rules (nothing leaves without the user's
  word, the mirror only grows, the test machines are the user's).
- `tools/release/preflight.sh [--export]` is the gate before any
  packager: a clean tree, the build, the suites, the reference frames,
  every shader variant a package bakes, the Windows and XP cross-builds,
  and a trial export that must fast-forward from the mirror. It ran on
  `b6670c9` with every step PASS in 301 s.
- `tools/release/check_package.sh PLATFORM VERSION` checks one package
  on the machine it runs on, from the unpacked package alone. It prints
  PASS or FAIL a step and leaves the frames in
  `dist/check-VERSION-PLATFORM/`. Its remote halves are
  `check_package_posix.sh` (Linux), `check_package.ps1` (Windows),
  `check_package_pi.sh` and `check_package_android.sh`.

The `macos`, `pi` and `android` paths have run. The `linux` and
`windows` paths are the commands 0.4.0 was checked with by hand, put in
a script, and have not run as scripts: x99 was in use.

## The small packages of 0.4.0, checked (2026-11-24)

The XP, Pi and Android packages were built after the release was
drafted, to test them. They are not in the draft.

| Package | Size | Built from | Result |
|---|---|---|---|
| `Dragon-0.4.0-pi.tar.gz` | 58 MB | `ea9c33d` | PASS on the Pi 1B. Five runs headless at 640x480, the ff tier, preset pi: the demo pilot on run 7 at 38.8 and 38.7 fps over 900 frames (median 24.6 ms, 1% low 28.8 ms), the arena with three bots at 49 fps, the start screen, a run from it. Peak 78 MB, 49.8 C, not throttled |
| `Dragon-0.4.0-android.apk` | 177 MB | `b6670c9` | FAIL on the Redmi K30 5G: it runs right and too slowly. Installed, unpacked 201 MB at first start, rendered. The eight reference frames match the Mac at the phone preset. The demo pilot ran 7,200 frames at 11.2 fps (median 75.5 ms). October's build ran the same preset at 33.8 |
| `Dragon-0.4.0-xp.zip` | 240 MB | `b6670c9` | The import gate passed. Under Wine the valley, the start screen and a run from it render on the sm3 tier. Not run on a card: `docs/porting/CARDS.md`, "Which cards to run 0.4.0 on" |

SHA-256: `e25571b6…fcebed` (Pi), `9ae594be…cf417` (Android),
`a46d91b7…da107b` (XP). The frames are in
`artifacts/release-0.4.0/`, and the two device runs have records in
`~/src/gpu-hist/research/dragon-devices/`.

What the builds turned up:

- The GLES bake stopped at `rock.hlsl`. The five modern-only shaders
  were skipped in the MSL and D3D11 bakes and not in
  `tools/gles/bake_gles.py`.
- The Android packager did not copy `assets/clips`, so the phone had no
  keyframed strikes.
- The APK did not install over the phone's copy:
  `INSTALL_FAILED_UPDATE_INCOMPATIBLE: Package com.paleshell.dragon
  signatures do not match previously installed version`. The copy was a
  debug-signed development build of 2026-10-15. Uninstalling deletes
  the records on the phone, so the check script never does it. The user
  approved it this once.
- The phone's frame rate. `docs/porting/MOBILE.md`, "The 0.4.0 package
  on the K30", has the eleven variants that take the frame apart: the
  atmosphere is 35 of its 61 ms. The check now fails a phone under 30
  fps and a Pi under 30, so the next release sees this at step 5.
  Since then the cloud cover is read from a map and the terrain's light
  path is lighter: 17.5 fps live, which still fails (`MOBILE.md`, "The
  phone's frame, taken apart").
- The APK carries one dragon, no rock kit and no landmarks. The Pi's
  start screen shows a "designs" row that does nothing there.

## The new designs in every package (2026-11-24)

The user's call: "include/use the new models for all releases, can skip
some on mobile/pi if space limited". The desktop packages already load
both designs. The other three carried the first designs alone, because a
retro tier never loaded a second and the packagers listed the first
seven.

`roster_paths` (`src/app.cpp`) now puts a second design in its first's
place where the first is not in the package, and on a retro tier asked
for the new designs. A retro tier loads one design a species, not both:
its machines hold seven dragons. The three packagers list
`embercrest-hero rimefang frost3 blight2 stone2 storm3 tide2`. None was
left out for space. By the packagers' own count the APK is 163 MB (177
with the first designs) and the XP zip 240 MB as before. The Pi's
tarball is 68 MB with the hero and its clips.

| Package | Built from | Checked |
|---|---|---|
| `Dragon-0.4.0-android.apk` | `87082ed` | PASS on the Redmi K30 5G: installs over the copy, unpacks and removes 35 files of the older version, the eight reference frames on the hero against the Mac at the phone preset, a run from the start screen, the demo pilot for 7,200 frames at 31.9 fps |
| `Dragon-0.4.0-xp.zip` | `c01a606` | PASS under Wine: the valley, a run from the start screen and a seven-dragon arena on sm3, a flap on sm2 and on ff, seven new designs in the roster. Not on a card yet |
| `Dragon-0.4.0-pi.tar.gz` | `87082ed` | Not checked: the Pi stopped answering (no ping, no ssh) between the afternoon's check and this build. The hero renders on the ff tier on the Mac and under Wine |

SHA-256: `48e25032…c66d1f` (Android), `e1d9be22…32e26` (XP),
`39bf506e…51d531` (Pi).

Faults this turned up:

- The XP packager copied `assets/clips` whole, the gold and silver
  studies' wingbeats among them. The other packagers strip them. Two XP
  packages built that way on this day were never released.
  `check_package.sh` now lists every package first and fails on a file
  of a model that does not ship; the three released desktop packages
  pass it.
- The phone's unpack kept an older version's files
  (`docs/porting/MOBILE.md`, "The phone preset at 30 fps").
- The phone check's reference frames name the first Embercrest, which
  the APK no longer has. They run on the hero.
- The hero simplifies worse than the others at the bottom: 140,279
  triangles to 1,193 on the ff tier is an error of 166% of its size by
  meshoptimizer's measure, against 4 to 8% for the other six. It still
  reads as the dragon. `artifacts/release-0.4.0/designs-retro-tiers.jpg`
  has all six on sm3, sm2 and ff.

## Checklist before a public push or package

- [ ] `tools/release/export_public.sh` passed its own checks.
- [ ] `git -C <mirror> log --all --format='%an <%ae>' | sort -u` shows only
      the addresses you mean to publish.
- [ ] No file in the package is listed as "no" in `ATTRIBUTION.md`; no `dragon.glb` or `alt/`.
- [ ] The release notes say the game is AI-assisted and link `AI_DISCLOSURE.md`.
- [ ] On itch.io, the AI-generated tags are set (*Graphics*, *Code*).
