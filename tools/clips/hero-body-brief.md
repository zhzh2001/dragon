# Brief: keyframed body loops for the hero dragon

You author five keyframed loops for the hero dragon's neck, tail and legs in Blender, by script, check them by script, measure them with a probe, and judge them in the game engine by render. You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree.

## Why

The hero's wingbeat was keyframed on 2026-11-22 (`tools/clips/hero-flight.md`). The playtester: "keyframe looks a bit better, try on legs/tails/neck/etc." Everything but the wings in the beat, and the hero's three strikes, is still procedural: springs, a sine wave down the tail, a tucked leg pose that sways with the load. It is correct and it reads as a held pose. This job gives the rest of the body authored motion in the four states a player sees most: beating, gliding, walking and standing.

An earlier keyframed layer of 2 to 6 degrees a joint did not read at all. A change the playtester cannot see is not done. The sizes below are meant to show from the chase camera, 20 metres behind the dragon.

## What the engine does with the clips

Read `src/anim/dragon_rig.h` (`set_body_clips`, `BodyClip`, `RigTuning::body_clip`) and the block in `src/anim/dragon_rig.cpp` that begins "The keyframed body loops, the same way". In short:

- The file is `assets/clips/embercrest-hero.body.glb`. The engine looks for five animations by name. Each is optional.

| Animation | Sampled at | Plays when |
|---|---|---|
| `flap_body` | the flight model's flap phase: 0 is the top of the downstroke, 0.5 the bottom | beating, by the beat's amplitude, blended with the next by the effort |
| `flap_climb_body` | the same | the deeper beat of a climb or a take-off |
| `glide` | the clock, at the clip's own length | in the air and not beating |
| `walk` | the walk's own phase: 0 as the right hind and left fore feet lift, 0.5 as the left hind and right fore lift | on the ground, by the walking speed |
| `stand` | the clock, at the clip's own length | on the ground and not walking |

- Keys are deltas from each joint's rest rotation, composed onto the finished procedural pose in the joint's own frame: `rotation = procedural x delta`. Nothing procedural is removed under a body clip, with one exception: the tail's own sine wave in the beat (`tail_beat_deg 40` in the hero's rig profile) is replaced by `flap_body`'s tail keys. So everything else you key is added to motion that is already there: the body's heave and nod with each beat, the neck holding the head still (`beat_head_hold`), the head's aim, the tail's spring, the legs' tuck and sway.
- The wings are not yours. The wingbeat clip (`assets/clips/embercrest-hero.flight.glb`) plays at the same time. Do not key a wing joint in `flap_body`, `flap_climb_body` or `glide`.
- On the ground the feet are planted before a clip is added. `walk` and `stand` must not key the root, a spine or chest joint, or a leg joint: that would slide the feet. They may key the neck, the head, the tail and the folded wings.
- No clip keys the root joint or the jaw. Rotation keys only.

Because a clip is composed onto a pose that is not the rest pose, Blender shows you the clip and not the result. Judge the result in the engine.

## Read first

- `tools/clips/make_hero_flight.py`, `check_hero_flight.py`, `hero_flight_*.py` and `tools/clips/hero-flight.md`: the wingbeat clip, the same file format and export route, written by an earlier job. Follow its export settings and its rest-pose handling. Do not edit those files; copy what you need into new ones.
- `tools/clips/hero-attacks.md`: a measured table of the bend axes of the hero's neck, tail and leg joints. Measure any joint it lacks the same way.
- `tools/skeletons/embercrest-hero.json` and `tools/hero-rig.md`: the joints and names.
- `assets/embercrest-hero.glb` (a symlink): the rigged model. It faces +Z in glTF space, Y up.
- `assets/embercrest-hero.glb.rig.cfg`: the procedural motion your keys are added to (the leg block, `tail_beat_*`, `beat_*`, the neck's stiffness).
- `docs/MOTION_REFERENCE.md`: timings measured from films, games and animals.

## The motion

Angles are starting points, to within about 30%. Where a number here and what looks right in the engine disagree, the engine wins: say what you changed and why.

`flap_body`, one beat, both sides mirrored:

- Tail: a wave in the vertical plane that travels from the base to the tip once a beat. The base answers the body: as the downstroke lifts the chest, the base dips, about 0.15 of a beat after the wings. The wave grows toward the tip, and the tip flicks late and fast. Summed over the tail's joints about 40 degrees peak to peak near the base half and more at the tip. Not a sine: the down-flick is quicker than the return.
- Hind legs: tucked by the profile. On the downstroke the hips swing back 8 to 10 degrees and the knees open 6 to 8, as the body is thrown up and forward, peaking about 0.1 of a beat after mid-downstroke. On the upstroke they gather again and the toes curl 10 to 15 degrees.
- Forelegs: smaller and about 0.15 of a beat later than the hind legs: the elbows 5 degrees, the wrists 6 to 8, the fingers curl with the gather.
- Neck: the neck reaches forward and a little down through the downstroke and draws back in the recovery, 4 to 6 degrees at each neck joint, and the head counter-turns so that its direction changes by less than 2 degrees over the beat.
- Spine and chest, if the skeleton has joints there besides the root: 2 to 3 degrees of arch at the bottom of the stroke.

`flap_climb_body`: the same animal working harder. Every amplitude about one and a half times, the tail's wave deeper, the legs' kick later by 0.03.

`glide`, a loop of 8.0 seconds on the clock, not mirrored:

- Breath: the chest and spine joints, or the neck's base if there are none, 1.5 degrees, two breaths in the loop.
- Head and neck: the head looks about 8 degrees to one side near 2 s, returns, looks about 6 degrees to the other side and a little down near 6 s. Eased in and out, with the neck's joints sharing the turn base first.
- Tail: one slow sideways sway in the loop, about 14 degrees summed, the tip trailing the base by a quarter of the cycle, and one small quick twitch of the tip.
- Legs: the hind legs drift 3 to 4 degrees, out of step with each other. The toes of one foot flex once, 10 degrees. The forelegs shift once.

`walk`, one stride cycle, sampled at the walk's phase. Neck, head, tail and folded wings only:

- The head bobs: the neck dips 4 degrees twice a cycle, once as each pair of feet lands (phase 0.5 and 1.0), and the head counter-turns to stay level.
- The neck sways sideways once a cycle, 3 to 4 degrees, toward the side whose foreleg is planted.
- The tail sways sideways once a cycle, about 20 degrees summed, opposite the hips, the tip lagging the base by 0.15 of the cycle.
- The folded wings: the wrists bounce 2 to 3 degrees twice a cycle.

`stand`, a loop of 10.0 seconds on the clock. Neck, head, tail and folded wings only, not mirrored:

- Breath: three breaths in the loop. The neck's base rises 1 to 1.5 degrees and the folded wings lift 1.5 to 2 at the shoulders with each.
- Head: looks about 12 degrees to one side near 3 s and holds a second, returns, tilts about 5 degrees near 7 s.
- Tail: a slow sway, about 12 degrees summed, and a curl of the tip that comes and goes once.

No joint moves linearly between keys. Every loop closes: the last key equals the first, in value and in rate.

What must not happen, in the engine: a leg, the tail or the neck passes through the body or a wing; a foot slides on the ground in `walk` or `stand`; the head swims while the dragon aims; a pop between the last frame and the first; the tail's wave reads as a mechanical sine.

## Output

`assets/clips/embercrest-hero.body.glb`: the armature alone (no mesh), every joint present with the model's exact rest pose and names, the five animations above. 61 keys over 1.0 s for `flap_body`, `flap_climb_body` and `walk`. `glide` at 8.0 s and `stand` at 10.0 s, 30 keys a second.

## Checks (`tools/clips/check_hero_body.py`, prints PASS or FAIL per line, exits non-zero on a failure)

1. The file loads; the five animations by name; the model's joint count and names.
2. Rest pose equals the model's (rotation within 0.01 degrees, translation within 0.1 mm). No translation or scale keys.
3. No key on the root, the jaw, or a wing joint in the three flight loops. No key on the root, the jaw, a spine or chest joint or a leg joint in `walk` and `stand`.
4. Every loop closes: each joint's last key is within 0.05 degrees of its first, and the rate into the last key matches the rate out of the first within 20%.
5. `flap_body` and `flap_climb_body` mirror left to right within 0.5 degrees at every key.
6. No joint turns more than 10 degrees between two consecutive keys.
7. The amplitudes are inside the ranges above, and `flap_climb_body` is larger than `flap_body` on the tail, the hips and the neck.
8. In `flap_body`, the head's direction, composed from the neck and head keys alone, changes by less than 2 degrees.
9. In `walk`, the neck's dip has two peaks a cycle, within 0.05 of phase 0.5 and 1.0.

Prove the checker: corrupt an export on purpose for each check and show that the check fails.

## The probe

The main checkout has a probe built for you: `~/src/game-claude/build/trace_probe-r11b` (source: `tools/rig_probes/trace_probe.cpp`; `tools/rig_probes/plot_trace.py` draws its output). It runs a studio scenario through the rig with the clips and writes one CSV row a frame with the body-frame positions of the root, the chest, the neck's joints, the head, the tail's middle and tip, the right legs' joints, the left foot and hand and a wing tip. Run it from the main checkout's directory:

`cd ~/src/game-claude && DRAGON_BODY_CLIPS=<worktree>/assets/clips/embercrest-hero.body.glb ./build/trace_probe-r11b assets/embercrest-hero.glb <scenario> <from_s> <to_s> > <worktree>/build-clip-cache/trace.csv`

`DRAGON_BODY_CLIPS=none` is the procedural body: the "before". Scenarios: 0 glide, 1 flap, 9 grounded (standing), 13 walk. Positions are in the model's units times one constant, so compare with and without and report ratios and the head-to-tail length in the same units.

For each loop, report how far the tail's tip, the tail's middle, the head, a hind foot and a hand travel over one cycle, with and without the clip. The requirement: in the beat the hind foot's path is at least three times as long with the clip as without, and the tail tip's path is not shorter than the procedural wave's. In the glide, the walk and the stand, the tail tip's and the head's paths with the clip are at least 2% of the head-to-tail length where the procedural body's are near none. In the beat and the glide the head's path with the clip is at most 1.5 times the procedural head's: the head stays quiet.

## The engine

A built engine is kept for you in the main checkout: `build/dragon-r11b`. Run it from the main checkout's directory, read-only, and write screenshots into your worktree:

`cd ~/src/game-claude && DRAGON_BODY_CLIPS=<worktree>/assets/clips/embercrest-hero.body.glb ./build/dragon-r11b --headless --hide-ui --size 1920x1080 --sun 55,60 --model assets/embercrest-hero.glb <VIEW> --screenshot <worktree>/artifacts/hero-body/NAME.bmp`

The log line `body clips: 5 in ... (flap_body, flap_climb_body, glide, walk, stand)` says the file was read. Add `--rig body_clip=0` for the procedural body: the "before". Convert each BMP to JPEG and delete the BMP.

In `--inspect A D E`, angle 0 is from behind, 180 from the front, 90 and 270 the two sides, E 85 from above. The studio's flap (`--studio 1`) is the cruise beat; add `--flight flap_effort_bias=1` for the climb. One beat is about 51 frames. A headless frame is 1/60 s.

Views, before and after, all required:

| NAME | VIEW |
|---|---|
| beat-side-0..7 | `--studio 1 --inspect 270 14 6` at `--frames` 200, 207, 213, 219, 226, 232, 238, 245 |
| beat-rear-0..7 | `--studio 1 --inspect 0 16 14` at the same frames |
| beat-below-0..7 | `--studio 1 --inspect 300 13 -30` at the same frames: the legs from under the wing |
| climb-side-0..7 | the side view with `--flight flap_effort_bias=1` |
| glide-side-0..7, glide-above-0..7, glide-rear-0..7 | `--studio 0` with `--inspect 270 14 6`, `--inspect 90 16 85` and `--inspect 0 16 14`, at `--frames` 120, 180, 240, 300, 360, 420, 480, 540 |
| walk-side-0..7, walk-above-0..7, walk-front-0..7 | `--studio 13` with `--inspect 270 14 6`, `--inspect 90 14 85` and `--inspect 180 14 6`, eight frames spread over one stride cycle from frame 200 (find the cycle's length from the probe's CSV) |
| stand-side-0..7, stand-front-0..7, stand-above-0..7 | `--studio 9` with the same three views, at `--frames` 150, 225, 300, 375, 450, 525, 600, 675 |
| head-0..3 | `--studio 1 --inspect 90 3.5 0 --inspect-head` at four frames of a beat, and the same in `--studio 9` at four frames of the stand |
| feet | `--studio 13 --inspect 60 11 3` at four frames: the feet do not slide or sink |
| chase-0..3 | `--autopilot --course 1` at `--frames` 300, 313, 326, 339 |
| landed-0..3 | `--input -0.25,0,0,0,0,0` at `--frames` 900, 960, 1020, 1080: landed in the game and standing |

Look at every render at full size. Fix what you see, rebuild, render again.

Deliver under `artifacts/hero-body/`:

- `beat-before-after.jpg`, `glide-before-after.jpg`, `walk-before-after.jpg`, `stand-before-after.jpg`: for each loop the eight frames from two views, the procedural body above and the clips below, each frame cropped to the dragon at full size. If the difference is not plain at that size, the motion is too small: enlarge it.
- `traces.jpg`: `plot_trace.py` charts of the tail tip, the head and a hind foot for each loop, with and without.
- `views.jpg`: a contact sheet of every after view.
- `tools/clips/hero-body.md`: what each loop does, the axis table for any joint you measured, the measured amplitudes, the check output, the probe's travel table with and without, what you changed after looking at the engine and why, and what is still wrong.

## Rules

- Write only these paths: `tools/clips/make_hero_body.py`, `tools/clips/check_hero_body.py`, `tools/clips/hero-body.md`, other new `tools/clips/hero_body_*.py` helpers, `assets/clips/embercrest-hero.body.glb`, `artifacts/hero-body/`, and a scratch directory `build-clip-cache/`. Do not edit `src/`, any profile, or any existing tool.
- Every `.glb` under `assets/` except your output is a symlink into the main checkout or a tracked file: never write through one.
- Use the `blender` command line only (`/opt/homebrew/bin/blender -b --factory-startup --python ...`). No Blender MCP server, no running Blender. Run Blender in the foreground, one process at a time. Another job is using Blender and the GPU at the same time: expect renders to take longer than usual.
- Do not build the game engine or the probe.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Commit after the checks pass and again after each round of engine fixes, on your branch. Stage explicit paths, never `git add -A`. Do not push.
- Write short plain sentences in the record, active voice, no filler.

## Budget and stopping

Order: `flap_body` through its checks and the engine until it reads right, then `flap_climb_body`, then `glide`, `walk`, `stand`. At most three rounds of look-and-fix in the engine for each loop. Commit after each loop. If something cannot be done from a clip alone, because of what the engine does under or over it, say exactly what and stop there for that loop. Finish with a short report: for each loop what it adds that can be seen, what reads worse than the procedural body, what is still wrong, any engine change you needed and did not have, and the minutes it took.
