---
name: release
description: Cut a public release of the dragon game - gates, packages for each platform, a check of each package on the machine it runs on, the filtered export to the public mirror, and a draft GitHub release. Use when asked to release, publish, package for the public repo, update the mirror, or make a version such as 0.5.0.
---

# Release

A release is seven steps in this order. Each step has a gate, and a gate
that fails stops the release until it is fixed. `docs/RELEASE.md` is the
record of every release so far and of each fault a step once found. Read
its last two sections before you start.

Three rules hold through all of it:

- Nothing leaves the machine without the user's word. The export pushes
  nothing. Ask before the push and before the release, and say the size.
- The public mirror's history is append-only. What is pushed stays. An
  exclude added after a path was published forces a rewrite, which is the
  user's decision and has happened once.
- The test machines are the user's. Copy to a scratch directory, run the
  game, clean up. Do not restart one, boot its other system, power it off,
  or uninstall anything on the phone without asking, each time.

## 1. Agree what ships

Ask, or read from the last playtest round, that the user wants a release.
"Publish only what ships": a release follows a playtest that came back
good. Fix the version (the last tag plus one minor). Decide which
platforms are rebuilt. macOS, Windows and Linux carry the modern tier and
change with the picture. XP, Android and the Pi change when their tiers'
content or the shared engine does.

## 2. Preflight

```sh
tools/release/preflight.sh --export
```

It checks a clean tree, the build, the 28 suites, the reference frames,
every shader variant a package bakes, the Windows and XP cross-builds, and
a trial export. Fix every FAIL before a packager runs.

Set `WINE`, `WINEPREFIX` and `DYLD_FALLBACK_LIBRARY_PATH` first, as for
the XP packager, and it also bakes the D3D9 shaders. Without them it
prints SKIP, and a shader the XP build cannot compile shows only at
`package_xp.sh`.

Read what the trial export prints. If the push would add more than about
500 MB, show the user the heaviest new directories and ask whether to trim
review renders first (`public_excludes.txt` takes paths and `glob:` lines).
Only paths the mirror has never held can be trimmed.

## 3. The README and the notes

If the picture changed enough to show, rebuild the README's media:

```sh
tools/release/make_demo_clips.sh
```

Look at every clip and still before committing them. Each replaced clip
stays in the mirror's history, so do not re-render for a small change.

Write `dist/notes-VERSION.md` from the playtest record since the last
release (`docs/showcase/PLAYTEST.md`, `docs/STATUS.md`). Copy the last
notes' shape: downloads and where each was checked, what changed, the AI
notice with the link to `AI_DISCLOSURE.md`. Check each claim's date
against `git log`: a feature older than the last release is not news.

## 4. Build the packages from one commit

Commit everything that a package carries first (`src`, `shaders`,
`assets`, `tools/release`, the four licence files). Then:

```sh
export PATH=$HOME/.local/py314/bin:$PATH
for p in macos windows linux; do tools/release/package_$p.sh VERSION; done
tools/release/package_xp.sh VERSION       # when the retro tiers changed
tools/release/package_android.sh VERSION  # when the phone is in the release
tools/release/package_pi.sh VERSION       # when the Pi is
shasum -a 256 dist/Dragon-VERSION-*
```

Each refuses a dirty tree. The Linux packager builds on x99 over ssh, so
x99 must be up in CachyOS. Do not edit a packager while it runs: the shell
reads the script as it goes. Commits after this point may change docs and
the export tools, and nothing a package carries.

## 5. Check each package where it runs

```sh
tools/release/check_package.sh macos VERSION
tools/release/check_package.sh linux VERSION       # x99, CachyOS
tools/release/check_package.sh windows VERSION     # x99-windows
tools/release/check_package.sh pi VERSION          # rpi
tools/release/check_package.sh android VERSION     # the phone on adb
```

Each unpacks the package and, from it alone, renders the eight reference
frames, flies a flap, starts a run from the start screen, and runs beside
a second render. Then look at the frames it leaves in
`dist/check-VERSION-<platform>/`: a PASS on numbers is not a look.

The Pi and the phone run their own lists and end on the demo pilot's
frame rate. Under 30 fps either fails (`PI_FPS_FLOOR`,
`PHONE_FPS_FLOOR`). The 0.4.0 phone package ran at 11.2 fps where
October's ran at 33.8, and only this check measured it: the desktop
gates cannot see a cost that only a phone's GPU pays. Build and check
the small packages before the release is drafted, not after.

- The phone check installs over the copy on the phone and never
  uninstalls. If the signatures differ, the install fails. Ask the user:
  uninstalling deletes the records on the phone.
- x99 runs one system at a time. Ask the user which is up and whether you
  may restart into the other. In the daytime the user is often on it.
- The reference frames on Windows and Linux are compared loosely against
  the Metal set, because the `d3d12` and `vulkan` sets go stale between
  releases. Recapture them on x99 when the picture settles.
- The XP package is checked by `tools/xp/check_imports.py` (inside its
  packager) and then on real cards. `docs/porting/CARDS.md` says how the
  G41 and the Atom are driven, and which card is in which board.
- A frame rate read on real hardware gets a run record in `~/src/gpu-hist`
  (`docs/benchmark-record-standard.md` there).

A package that fails its check is not released. Fix, commit, rebuild that
package and every other package the fix reaches, and check again.

## 6. Record, then ask

Fill in `docs/RELEASE.md`: a section for the version with each package's
size, where it was checked and how, the commit they were built from, and
their SHA-256. Record every fault the release turned up and its fix. Add
the round to `docs/showcase/PLAYTEST.md` if a playtest led to it. Commit.

Then ask the user two things, with the numbers:

1. Push the export? Say the commit count, what the push adds, and the
   mirror's size after it.
2. Create the GitHub release as a draft, publish it at once, or not yet?

## 7. Export, push, draft

Only after a yes to the first question:

```sh
cd ~/src && rm -rf dragon-public-next
game-claude/tools/release/export_public.sh dragon-public-next --ref COMMIT
cd dragon-public-next
git remote add origin https://github.com/zhzh2001/dragon.git && git fetch origin
git merge-base --is-ancestor origin/main main    # must succeed: a fast-forward
git grep -I -l "$USER" HEAD | wc -l              # must be 0
git log --format='%an <%ae>' | sort -u           # only the addresses already public
git push origin main
cd .. && rm -rf dragon-public && mv dragon-public-next dragon-public
```

Do not pass `--email`: the mirror's history was exported without it, and
passing it rewrites every commit. If `merge-base` fails, stop and find out
which exclude or stripped image changed a published commit. Never force.
A push must stay under GitHub's 2 GB: for more, push an older commit of
the export first.

Only after a yes to the second:

```sh
gh release create vVERSION dist/Dragon-VERSION-macos.zip dist/Dragon-VERSION-windows.zip \
  dist/Dragon-VERSION-linux.tar.gz --repo zhzh2001/dragon --draft --target main \
  --title "Dragon VERSION" --notes-file dist/notes-VERSION.md
gh release view vVERSION --repo zhzh2001/dragon --json isDraft,assets
```

A draft is not public. The user tries a download and presses Publish.
Record the push and the release in `docs/RELEASE.md`, and end the report
with what to playtest: the README on GitHub, one download.

## Afterwards

Remove the scratch directories on the test machines and the unpacked
packages on the Mac. Leave `dist/` as it is: the next release compares
sizes against it.
