# Brief: a keyframed wingbeat for the hero dragon

You author a keyframed wingbeat for one dragon in Blender, by script, check it by script, and judge it in the game engine by render. You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree.

## Why

Every wingbeat in the game is procedural: the rig computes each wing joint from the flight model's flap phase with a stack of terms (a stroke angle, delayed outer joints, a recovery fold, a twist). The playtester has called it "mechanical" and "unnatural" over several rounds, and asked: "are the current animations keyframed? try it with codex if not." This is that trial. One beat, authored as an animator would, played in place of the procedural beat.

An earlier trial put a small keyframed layer (2 to 6 degrees a joint) under the procedural pose, and it did not read. This clip is not a layer. It is the whole motion of the wings, at full size.

## What the engine does with the clip

Read `src/anim/dragon_rig.h` (`set_flight_clips`, `RigTuning::flap_clip`) and the block in `src/anim/dragon_rig.cpp` that begins "The keyframed wingbeat, on the finished pose". In short:

- The clip file is `assets/clips/<model>.flight.glb`, with animations named `flap` and `flap_climb`.
- A clip is one beat as a loop. The engine samples it at `phase x duration`, where the flight model's phase runs 0 to 1: 0 is the top of the downstroke, the downstroke lasts until 0.5, and the upstroke fills the rest. The clip's length in seconds does not matter. Its last key must equal its first.
- Keys are read as deltas from each joint's rest rotation and composed onto the finished procedural pose in the joint's own frame: `rotation = procedural x delta`.
- Under the clip the procedural beat is switched off. What stays procedural and is still under your clip: the wing's levelling twist at the shoulder (`wing_level_share`), the load flex in a turn, the flutter at speed, the glide's dihedral as a beat fades in or out, the body's heave and nod with each beat, the neck holding the head, the tail's answer, and the tucked legs. So the clip keys the wing joints only, both sides, and nothing else.
- `flap` is the cruise beat. `flap_climb` is the deeper beat of a take-off or a climb. The engine blends the two by the flight model's effort.

Because the clip is composed onto a pose that is not the rest pose, Blender shows you the clip and not the result. Judge the result in the engine.

## Read first

- `tools/clips/make_hero_attacks.py`, `tools/clips/check_hero_attacks.py` and `tools/clips/hero-attacks.md`: the hero's strike clips, the same file format, with a measured table of each joint's bend axes. Follow its export settings and its rest-pose handling exactly. Measure the wing joints' axes the same way: pose a joint, read where the child's tail moved in world space.
- `tools/skeletons/embercrest-hero.json` and `tools/hero-rig.md`: the hero's joints and names.
- `assets/embercrest-hero.glb` (a symlink): the rigged model. Its armature's rest pose is the clip's rest pose. The wings are bound level. The model faces +Z in glTF space, Y up.
- `docs/MOTION_REFERENCE.md`, the sections on the wingbeat: timings and amplitudes measured from films, games and animals.
- `assets/embercrest-hero.glb.rig.cfg` and `.flight.cfg`: the procedural beat the clip replaces. It is the comparison, not the target.

## The motion

One beat, phase 0 to 1. Angles are from the level wing, about the body's long axis at the shoulder unless said otherwise. They are targets to within about 20%. Where a number here and what looks right in the engine disagree, the engine wins: say what you changed and why.

`flap`, the cruise beat:

- Phase 0, the top. The arm is about 55 degrees above level, the wing fully open, the hand a little higher than the arm.
- 0 to 0.5, the downstroke, the powered half. The shoulder drives down to about 40 degrees below level. It accelerates fast and eases into the bottom: not a sine. The outer wing lags the arm: the wrist turns 0.04 to 0.06 of a beat after the shoulder and the finger tips 0.08 to 0.10 after, so through the stroke the tip trails above the arm and the wing bows upward along its length (the fingers 10 to 20 degrees up at mid-stroke, as a loaded sheet does). The leading edge pitches down 10 to 15 degrees through mid-stroke. The stroke plane tilts forward: the wrist ends the downstroke forward of where it began, by a sweep of about 15 degrees at the shoulder.
- 0.5 to 1, the upstroke, the recovery. The shoulder starts up first while the tips are still going down. The elbow flexes 30 to 40 degrees and the wrist 40 to 55, reaching the peak fold between phase 0.62 and 0.70, so the span shortens to about two thirds and the wing slips up through the air. The fingers close toward each other by 10 to 20 degrees and their tips droop below the hand. The arm sweeps back about 12 degrees. The leading edge pitches up. From about 0.80 the wing opens again, and it is fully open and taut by 0.95, before the top.
- No joint moves linearly between keys, no joint holds still for more than a tenth of the beat except at the two reversals, and the two reversals are not symmetric: the bottom is a quick turn, the top is a float.

`flap_climb`, the deeper beat: the top about 65 degrees, the bottom about 60 degrees below level, the forward sweep on the downstroke about 25 degrees, the recovery fold larger (elbow 45, wrist 65), the tip's lag larger by a third. It must read as the same animal working harder, not as a different animation.

Both sides are exact mirrors within a clip.

What must not happen, in the engine: the wings cross over the back at the top or meet under the belly; a finger crosses its neighbour; the membrane creases or tears at the root; the wing passes through the body, the legs or the tail; a pop between the last frame and the first.

## Output

`assets/clips/embercrest-hero.flight.glb`: the armature alone (no mesh), every joint present with the model's exact rest pose and names, rotation keys only on the wing joints, 61 keys over 1.0 s for each of `flap` and `flap_climb` (key 61 equals key 1).

## Checks (`tools/clips/check_hero_flight.py`, prints PASS or FAIL per line, exits non-zero on a failure)

1. The file loads; two animations with the names above; the model's joint count and names.
2. Rest pose equals the model's (rotation within 0.01 degrees, translation within 0.1 mm). No translation or scale keys. No key on a joint that is not a wing joint.
3. The loop closes: every joint's last key is within 0.05 degrees of its first.
4. The left side mirrors the right within 0.5 degrees at every key.
5. The arm's lowest point is at phase 0.5, within 0.03. Its highest is at phase 0, within 0.02.
6. The wrist's and the finger tips' reversals come after the shoulder's, by the lags above.
7. The peak fold of the elbow and the wrist falls between phase 0.60 and 0.72, and both are within 3 degrees of open at phase 0.95.
8. No joint turns more than 12 degrees between two consecutive keys.
9. The amplitudes are inside the ranges above, and `flap_climb` is larger than `flap` in the top, the bottom and the fold.

Also prove the checker: corrupt an export on purpose for each check and show that the check fails.

## The engine

The main checkout at `~/src/game-claude` has a built engine kept for you: `build/dragon-r10`. Use that binary. Run it from the main checkout's directory, read-only, and write screenshots into your worktree. The environment variable `DRAGON_FLIGHT_CLIPS` points it at your candidate file:

`cd ~/src/game-claude && DRAGON_FLIGHT_CLIPS=<worktree>/assets/clips/embercrest-hero.flight.glb ./build/dragon-r10 --headless --hide-ui --size 1920x1080 --sun 55,60 --model assets/embercrest-hero.glb <VIEW> --screenshot <worktree>/artifacts/hero-flight/NAME.bmp`

The log line `flight clips: 2 in ... (flap yes, flap_climb yes)` says the file was read. Without the variable the same command shows the procedural beat: that is the "before". `--rig flap_clip=0.5` shows a half blend, should you want to see where the two differ. Convert each BMP to JPEG and delete the BMP.

In `--inspect A D E`, angle 0 is from behind, 180 from the front, 90 and 270 the two sides, E 85 from above. The studio's flap (`--studio 1`) is the cruise beat; add `--flight flap_effort_bias=1` for the climb beat. One beat is about 51 frames.

Views, before and after, all required:

| NAME | VIEW |
|---|---|
| cruise-side-0..7 | `--studio 1 --inspect 269 12.7 6` at `--frames` 200, 207, 213, 219, 226, 232, 238, 245 |
| cruise-rear-0..7 | `--studio 1 --inspect 0 16 14` at the same frames |
| cruise-front-0..7 | `--studio 1 --inspect 180 16 10` at the same frames |
| cruise-above-0..7 | `--studio 1 --inspect 90 18 85` at the same frames |
| climb-side-0..7, climb-rear-0..7 | the side and rear views with `--flight flap_effort_bias=1` |
| turn-0..3 | `--studio 2 --inspect 0 16 14` at `--frames` 230, 245, 260, 275 |
| glide | `--studio 0 --inspect 0 16 14 --frames 200`: the clip must leave the glide alone |
| chase-0..3 | `--autopilot --course 1` at `--frames` 300, 313, 326, 339: the beat as a player sees it, from the chase camera |
| takeoff-0..5 | `--input 0.2,0,0,1,0,0` at `--frames` 40, 52, 64, 76, 88, 100: held flapping from the spawn |

Look at every render at full size. Fix what you see, rebuild, render again. Deliver:

- `artifacts/hero-flight/cruise-before-after.jpg` and `climb-before-after.jpg`: the eight side frames and the eight rear frames, the procedural beat above and the clip below, each frame cropped to the dragon at full size.
- `artifacts/hero-flight/views.jpg`: a contact sheet of every after view.
- `tools/clips/hero-flight.md`: what each clip does, the axis table for the wing joints, the measured peaks and lags, the check output, what you changed after looking at the engine and why, and what is still wrong. Say in numbers how the clip differs from the procedural beat: the tip's height at four phases, the span at the peak fold, the wrist's fore-aft travel.

## Rules

- Write only these paths: `tools/clips/make_hero_flight.py`, `tools/clips/check_hero_flight.py`, `tools/clips/hero-flight.md`, other new `tools/clips/hero_flight_*.py` helpers, `assets/clips/embercrest-hero.flight.glb`, `artifacts/hero-flight/`, and a scratch directory `build-clip-cache/`. Do not edit `src/`, any profile, or any existing tool.
- Every `.glb` under `assets/` except your output is a symlink into the main checkout or a tracked file: never write through one.
- Use the `blender` command line only (`/opt/homebrew/bin/blender -b --factory-startup --python ...`). No Blender MCP server, no running Blender. Run Blender in the foreground, one process at a time.
- Do not build the game engine.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Commit after the checks pass and again after each round of engine fixes, on your branch. Stage explicit paths, never `git add -A`. Do not push.
- Write short plain sentences in the record, active voice, no filler.

## Budget and stopping

Order: the axis table, `flap` through its checks, `flap` in the engine until it reads right, then `flap_climb` the same way, then the turn, the chase and the take-off views. At most four rounds of look-and-fix in the engine for each clip. If something cannot be fixed from the clip alone, because of what the engine leaves under it, say exactly what and stop there. Finish with a short report: what reads better than the procedural beat, what reads worse, what is still wrong, any engine change you needed and did not have, and the minutes it took.
