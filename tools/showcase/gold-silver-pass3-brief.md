# Brief: the gold and silver dragons, a new pass of fixes

You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree. The task is animation and rig work on two creatures, `gold-dragon` and `silver-dragon`, which earlier jobs rigged in October. You tune their profiles by render, and you repair an asset in Blender, headless, where a profile cannot do it.

## Why

The playtester: "gold/silver also needs new iteration of fixes." Both were last touched on 2026-10-03. Since then the hero and six newer dragons got a tucked flight leg, a folded standing wing and a shallower wingbeat, each after a playtest report. Gold and silver still run on the profiles of the first week, and they show every fault the others were fixed for. The playtester's standing complaints about the others apply here:

- "wingbeat is too low": a wing that swings far below the body, hangs beside the legs, or wraps under the belly.
- "leg tucking is not implemented like embercrest": in flight the legs hang and swing. The hero (`assets/embercrest-hero.glb`) holds its legs tucked.
- "folding wings in air is weird", "folding is bad": a recovery that crumples the hand.
- "high reach also clipping": wings that meet over the back.

## What I saw, on 2026-11-22

Renders of both as they are now: `artifacts/silver-dragon-integration/pass3-refs/silver-now.jpg` and `artifacts/gold-dragon-integration/pass3-refs/gold-now.jpg` (the glide from the side and behind, the beat's top and bottom from the side and behind, the dive, the brake, standing from the side and the front, the walk, the head). And a first trial with the hero's leg block and another dragon's standing fold laid over each profile by `--rig`: `silver-trial.jpg`, `silver-trial-air.jpg`, `silver-trial-ground.jpg`, `gold-trial.jpg`. The trial's overrides are in `pass3-refs/trial-overrides.txt`. They are a starting point and not a result.

Silver, a four-legged dragon with long wings:

1. In flight the legs hang straight down and swing. With the hero's leg block they tuck. Tune the block to this animal.
2. Standing, the wings are not folded: they droop half open with the tips on the ground behind. With the trial's fold they lie along the back as two long flat blades that reach past the hips, and a thin rod drops from under each wing to the ground. Bits of membrane are stretched across the fold.
3. The same thin rod shows in flight, pointing down and back beside the hind leg, under the wing's root. Find what it is (an inner rib or a strut whose skin does not follow its bone, or skin weighted to the wrong joint) and fix it, in the profile if a key does it, in the weights if not.
4. At the bottom of the beat the wing wraps down and forward under the chest. At the top it stands in a steep V.
5. In the glide the trunk flies about 15 to 20 degrees nose up with the wing raised and swept back. A flying dragon lies along its line of flight.

Gold, a long serpentine dragon with four short legs and small sail wings (the small wings are the design, keep them):

1. In flight the legs hang.
2. Standing, the wings stay spread open as two raised fans. With the trial's fold they close. Tune the fold to a sail of four rays.
3. Check the long tail (14 joints) in every posture: it must not pass through the ground when standing or walking, and it must not stand stiff.
4. Check the whiskers and the head in the close view.

Check my reading against what you see and add what I missed. Fix faults in this order for each animal: legs, standing wings, the rod or any other skin fault, the beat, the trunk's attitude.

## What you may change

- `assets/species/silver-dragon.glb.rig.cfg`, `assets/species/silver-dragon.glb.flight.cfg`, and the same two for `gold-dragon`, freely. The engine finds a profile beside the model first and in `assets/species/` second; these two models keep theirs in `assets/species/`. Do not create profile files beside the models. The keys are the fields of `anim::RigTuning` in `src/anim/dragon_rig.h` (each has a comment that says what it does) and `game::FlightTuning` in `src/game/flight.h`.
- A profile that lists only some keys inherits code defaults, and for the standing wing (`ground_wing_aim` and its directions) and the stance the default is off. Copy a sibling's block and re-tune it by render. `assets/embercrest-hero.glb.rig.cfg` has the leg block and the wingbeat the playtester likes. `assets/frost3.glb.rig.cfg` and `assets/tide2.glb.rig.cfg` are four-legged dragons fixed this month, with comments on each block. `docs/ANIMATION.md` has "Wings on the ground" and "A wing bound raised glides in a V" (`wing_rest_deg`). `docs/MODEL_GENERATION.md`, "The new designs in flight: the stroke, the legs, the names", records the same fixes on four other dragons and what went wrong on the way.
- You can try a value without editing a file: `--rig key=value,...` and `--flight key=value,...` on the engine's command line.
- `assets/silver-dragon.glb`, `assets/silver-dragon-raw.glb`, `assets/gold-dragon.glb` and `assets/gold-dragon-raw.glb` (real copies in your worktree, not tracked by git), through the tools that built them (`tools/silver_dragon_*.py`, `tools/gold_dragon_*.py`, `tools/skeletons/silver-dragon.json`, `tools/skeletons/gold-dragon.json`) and new `tools/silver_dragon_pass3_*.py` and `tools/gold_dragon_pass3_*.py` scripts, when a fault cannot be fixed from the profile. Read `artifacts/silver-dragon-rig/README.md` and `artifacts/gold-dragon-rig/README.md` first. Keep the bone names, the triangle count, the UVs, the maps and the colours. Keep a copy of each model as it was in `build-rig-cache/` before you write over it.
- For silver's trunk attitude: try the profile first. If only a re-posed bind reaches it, `tools/storm3_flight_bind.py` did that for another dragon. Do it only after everything else on both animals is done, and only if the result is clean.
- Do not change either creature's design: proportions, horns, crests, the wing's outline, the tail.

You cannot change the engine. If a posture needs a key the engine does not have, say exactly which and why in your report, and get as close as the existing keys allow.

## Limits, measured by the probe

The main checkout has a probe built for you: `~/src/game-claude/build/rib_probe-r11c` (source: `tools/rig_probes/rib_centre_probe.cpp`). Run it from the main checkout's directory with your worktree's model:

`cd ~/src/game-claude && ./build/rib_probe-r11c <worktree>/assets/silver-dragon.glb [--climb] [override.cfg]`

It runs the studio's flap through the rig with the model's profiles, skins the mesh and prints, in metres at the game's scale: each rib's nearest approach to the body's centre plane, the wing's highest skin above the back, the wing's lowest skin from the belly, and how near the centre plane any wing skin comes while more than 0.5 m below the belly. An override file is rig-profile syntax.

Required for both animals, cruise and climb: every rib stays at least 0.5 m from the centre plane; no wing skin more than 0.5 m below the belly comes within 2.5 m of the centre plane. As they are now: silver's ribs come within 0.68 m, gold's innermost within 0.78 m. The probe's belly is the lowest point of the trunk, so on a trunk that flies nose up it reads low: judge the bottom of silver's beat by the render as well. Put the probe's output before and after in the record.

## The engine

A built engine is kept for you in the main checkout: `build/dragon-r11c`. Use that binary. Run it from the main checkout's directory, read-only, and write screenshots into your worktree:

`cd ~/src/game-claude && ./build/dragon-r11c --headless --hide-ui --size 1920x1080 --sun 55,60 --model <worktree>/assets/silver-dragon.glb <VIEW> --screenshot <worktree>/artifacts/silver-dragon-integration/pass3/NAME.bmp`

The log line `loaded N rig tuning values from <worktree>/assets/species/...` says your profile was read. Convert each BMP to JPEG and delete the BMP. `--sun 55,60` puts the studio in sunlight. In `--inspect A D E`, angle 0 is from behind, 180 from the front, 90 and 270 the two sides, E 85 from above, D the distance in metres. These are large animals: silver frames at about 22 m and gold at about 28 m. Pick the distance that fills the frame with the whole animal, and use it for before and after alike.

Views for each animal, before and after, all required. D is your distance:

| NAME | VIEW |
|---|---|
| glide-side, -front, -rear, -above | `--studio 0 --frames 200` with `--inspect 270 D 6`, `180 D 6`, `0 D 10`, `90 D 85` |
| flap-side-0..7, flap-rear-0..7 | `--studio 1` with `--inspect 270 D 6` and `--inspect 0 D 14`, eight frames spread evenly over one beat from frame 200 (a beat is the profile's `flap_period` seconds at 60 frames a second) |
| climb-side-0..3 | the side view with `--flight flap_effort_bias=1`, four frames of a beat |
| turn | `--studio 2 --inspect 0 D 14 --frames 260` |
| dive | `--studio 5 --inspect 270 D 6 --frames 300` |
| brake | `--studio 7 --inspect 270 D 6 --frames 260` |
| head | `--studio 0 --inspect 90 3.5 0 --inspect-head --frames 200`, and the same in `--studio 9` |
| stand-side, -front, -rear, -above | `--studio 9 --frames 200` with the glide's four views |
| feet | `--studio 9 --inspect 60 D 3 --frames 200` at about half D: the feet on the ground, not in it |
| landed-side, -front | `--input -0.25,0,0,0,0,0 --frames 900` with `--inspect 270 D 5` and `180 D 5`: landed in the game |
| walk-0..3 | `--studio 13 --inspect 270 D 5` at `--frames` 200, 215, 230, 245 |
| chase | `--frames 200`: what a player sees |
| hero-glide-side, hero-flap-side-0..7, hero-stand-side | the hero, `assets/embercrest-hero.glb`, at 14 m in the same views: what a tucked leg, a liked wingbeat and a folded wing look like |

Look at every render at full size. A posture is not judged by its numbers.

Deliver, for each animal, under `artifacts/silver-dragon-integration/pass3/` and `artifacts/gold-dragon-integration/pass3/`:

- `before-after-air.jpg`: the glide from the side and behind, the beat's top and bottom from the side and behind, before above and after below, each cropped to the animal at full size.
- `before-after-ground.jpg`: standing from the side, the front and above, and the walk, the same way.
- `flap-before-after.jpg`: the eight side frames and the eight rear frames, before above and after below.
- `views.jpg`: a contact sheet of every after view.
- `probe.txt`: the probe's output before and after, cruise and climb.
- `README.md`: each fault, what fixed it (the key and its value before and after, or the asset change), what you tried that did not work, what is still wrong, and how to rebuild a changed model.

Write only inside those two `pass3/` directories under `artifacts/`. Both parents are kept out of the public mirror, and renders of these two models may not go anywhere else.

## Rules

- Write only these paths: the four profile files in `assets/species/` named above, the four model files named above, new `tools/silver_dragon_pass3_*.py` and `tools/gold_dragon_pass3_*.py` scripts, the two `pass3/` directories, and a scratch directory `build-rig-cache/`. Do not edit `src/`, any other profile, or any existing tool.
- Every other `.glb` under `assets/` is a symlink into the main checkout or a tracked file: never write through one. `assets/silver-dragon/` and `assets/gold-dragon/` (the Blender sources) are symlinks too: read them, save any changed `.blend` under `build-rig-cache/`.
- Use the `blender` command line only (`/opt/homebrew/bin/blender -b --factory-startup --python ...`). No Blender MCP server, no running Blender. Run Blender in the foreground, one process at a time. Another job may be using the GPU at the same time: expect renders to take longer than usual.
- Do not build the game engine or the probe.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Commit after each animal's profile pass and again after any asset change, on your branch. Stage explicit paths, never `git add -A`. The models are not tracked: record their SHA-256 in the README instead. Do not push.
- Write short plain sentences in the record, active voice, no filler.

## Budget and stopping

Order: silver's profile pass (legs, standing wings, the beat), silver's rod, gold's profile pass, gold's tail and head, then silver's trunk attitude if time is left. At most four rounds of look-and-fix for each fault. Finish with a short report: for each animal what changed that can be seen, what is still wrong, which model files changed (with their SHA-256), any engine key you needed and did not have, and the minutes it took.
