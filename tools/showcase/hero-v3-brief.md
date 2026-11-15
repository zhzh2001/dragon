# Brief: version 3 of the hero dragon's mesh, from the playtest's marks

You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree. An earlier worker built the hero dragon's mesh from three AI-generated sculpts (version 2). The user flew it and marked what is wrong on screenshots. You fix those marks in the mesh. The result is judged by eye, in the engine, from the user's own views.

## Read first

- `tools/hero-build.md` and `tools/build_hero.py`: the mesh's record and its staged build script. Start from them. The stage caches are gone; the default run rebuilds from the inputs in about 5 minutes.
- `tools/showcase/hero-build-brief.md`: the original brief. Its requirements hold except where this one changes them.
- `docs/SHOWCASE.md`, sections "The static mesh, version 2", "The rig on version 2", and "The first reports from the feedback tool".
- `artifacts/showcase-feedback/000N-marked.jpg` and `000N-report.md`: the user's four reports. Look at every picture.
- `tools/hero-rig.md`, `tools/rig_hero.py`: the rig that is fitted to this mesh. You do not change the rig script unless a landmark you move forces a number in `tools/skeletons/embercrest-hero.json` to follow; see "The rig" below.

## Inputs (symlinks; read only)

- `assets/embercrest-cand-hero-body-hunyuan.glb`: wingless body sculpt (body, legs, tail).
- `assets/embercrest-cand-hero-head-hunyuan.glb`: head sculpt.
- `assets/embercrest-cand-hero-hunyuan.glb`: first whole-body sculpt; version 2 took its wings.
- `assets/embercrest-cand-hero-wing-hunyuan.glb`: NEW. One wing generated alone from two plates (`artifacts/showcase-study/hero-wing-plates/`: upper surface and underside). 1.5 million triangles and a 4K PBR set on one wing. The main session looked at it (`artifacts/showcase-study/hero-cand-wing-hunyuan.jpg`): the membrane's wrinkles and veins, the ringed finger bones, the scaled arm and the claws are far better than version 2's, but two finger bones stop in the middle of the membrane as stubs that neither plate shows, and the colour is washed to pale pink on both faces where the upper plate is a saturated orange-red. Treat it as a source of surface detail, not as a wing to use whole. In order of preference: (a) use the sculpt, cut away the stub bones, keep the fingers that match the upper plate, and grade the colours to the plates (upper surface saturated orange-red, underside paler); (b) keep version 2's wing geometry and bake the sculpt's normal detail and the plates' colour onto it; (c) keep version 2's wing geometry and project the two plates onto it as texture. Say which you did and why.

## What to fix, in this order

1. Wings (report 0004 mark 3, "wings are worse"). Replace both wings with the new wing sculpt: one wing for the left, its mirror for the right. Fit it to version 2's wing skeleton so the rig's wing bones still lie inside the arm and fingers: the shoulder, elbow, wrist and each finger's base and tip must land within 0.10 m of version 2's positions (`tools/hero-build.md` has them; the sculpt has four fingers and a thumb claw, version 2's wing has three finger chains plus a spur, so say which sculpt finger maps to which chain and what happens to the extra one). Keep the membrane thin and two-sided, with a clean border against the bones, and an upper and an under surface that differ (darker saturated above, paler below). The root must join the body with no crease and no stretched band.
2. Flank and belly (report 0004 mark 1, "low res texture and geometry"; report 0003, "more geometry artifacts" on the belly between the forelegs). Give the torso between the legs more of the hide atlas, so its scales are as sharp as the neck's in the user's view. Remove the spike that hangs between the forelegs. Clean the jagged belly plates there.
3. Hind hip (report 0001 mark 1, "artifact"). A flat rectangular plate stands off the thigh near the tail root, and a seam shows where the leg meets the tail. Find what it is in the mesh (a sliver, a plate cut by the joint loops, a UV seam) and remove it.
4. Fore shoulder (report 0002, "unnatural joint"). When the foreleg tucks back in flight a hard dark crease pinches the chest. Part of this is weights, which the rig owns, but give the shoulder the geometry a fold needs: enough loops across the armpit and no plate bridging the joint.
5. Keep what the user liked: the neck's plates (report 0004 mark 2) and the head. Do not change the head, eyes, lids or mouth.

Everything else stands: the budget of 80,000 to 120,000 triangles for hide, membrane and horn, the five materials and map sizes, tangents, the frame (19 m wingspan, forward -Z, +Y up, feet at Y=0), the 60 MB limit.

## The rig

After the mesh verifies, rebuild the rigged file with `blender -b --factory-startup --python tools/rig_hero.py` so the result can be seen in the engine. If the rig's verifier fails only because a landmark moved, change that number in `tools/skeletons/embercrest-hero.json`, and record it. If it fails in a way that needs rig work (weights, new checks), stop there: deliver the mesh, say what failed, and leave the rig to the next worker.

## Engine check, required

The main checkout has a built engine. Run it read-only on your rigged file, from the main checkout's directory, writing screenshots into your worktree:

`cd ~/src/game-claude && ./build/dragon --headless --hide-ui --size 1600x900 --model <your worktree>/assets/embercrest-hero.glb --sun 35,235 --studio 1 --frames 800 --inspect 270 6.5 0 --screenshot <your worktree>/artifacts/hero-build/engine-NAME.bmp`

Convert each BMP to JPEG and delete the BMP. One engine process at a time. Write nothing else in the main checkout. The rig profile beside your model is picked up; copy `assets/embercrest-hero.glb.rig.cfg` and `.flight.cfg` from the main checkout's `assets/` into your worktree's `assets/` first if they differ (the main checkout's are newer).

Deliver these, each beside version 2's render of the same view (render version 2 first, from the main checkout's `assets/embercrest-hero.glb`):

- `--studio 1 --frames 800 --inspect 270 6.5 0`: the user's side view (reports 0001, 0002).
- `--studio 0 --frames 300 --inspect 270 5 5`: the flank and wing root close (report 0004).
- `--studio 0 --frames 300 --inspect 180 7 -15`: from the front and below, the belly (report 0003).
- `--studio 0 --frames 300 --inspect 90 14 85`: from above, both wings.
- `--studio 0 --frames 300 --inspect 0 12 10`: from behind, as the chase camera sees it.
- `--studio 1 --frames 790 --inspect 60 14 10` and `--frames 805`: the wings mid-beat.
- `--studio 9 --frames 200 --inspect 270 12 5`: standing, wings folded.

Look at every pair. A mark is fixed when the pair shows it plainly at full size.

## Deliver

- `assets/embercrest-hero-static.glb` and `assets/embercrest-hero.glb` in your worktree's `assets/` (git ignores `assets/*.glb`; do not force-add them).
- `tools/build_hero.py`, `tools/hero-build.md` updated to describe version 3 fully, with a table: each mark, what it was in the mesh, what you did, and the pair that shows it.
- `artifacts/hero-build/`: the Blender renders re-made, plus `engine-v2-*.jpg` and `engine-v3-*.jpg`, plus `pairs-*.jpg` (the two side by side).
- `tools/skeletons/embercrest-hero.json` and `tools/hero-rig.md` only if a landmark moved.

## Rules

- Write only: `assets/embercrest-hero-static.glb`, `assets/embercrest-hero.glb`, `assets/embercrest-hero.glb.rig.cfg` and `.flight.cfg` (copies), `assets/embercrest-hero-membrane-thickness.png`, `tools/build_hero.py`, `tools/hero-build.md`, `tools/skeletons/embercrest-hero.json`, `tools/hero-rig.md`, `artifacts/hero-build/`, `artifacts/hero-rig/`, `build-hero-cache/`, `build-hero-rig-cache/`. Never write outside this worktree except the engine's screenshots, which go into this worktree anyway.
- `/opt/homebrew/bin/blender` headless only, always in the foreground with a timeout long enough to finish (up to 10 minutes; cache stages). Never start a job in the background and wait on it.
- Look at renders as JPEGs of at most 1600 px.
- Disk space is short: keep `build-hero-cache/` under 4 GB and delete stage files you no longer need.
- Commit on the current branch after each state that verifies, staging explicit paths. End commit messages with: Co-Authored-By: Claude Mythos 5.2 <noreply@anthropic.com>
- Do not ask questions; decide and record. If a mark cannot be fixed in the mesh, say exactly why and what would fix it.
- Finish with a short report: each mark and whether it is fixed, the verifiers' output, what is weak, and how long it took.
