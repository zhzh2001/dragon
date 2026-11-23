# Brief: keyframed wingbeats for tide2 and storm3

You author a keyframed wingbeat for two dragons in Blender, by script, check each by script and by a probe, and judge each in the game engine by render. You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree.

## Why

The hero dragon got a keyframed wingbeat on 2026-11-22 (`tools/clips/hero-flight.md`, `tools/clips/make_hero_flight.py`). The playtester: "keyframe looks a bit better, try on ... another problematic dragon (likely tide)", and "storm might also benefit from codex fix". Both dragons still beat procedurally, and the playtester marked both in the studio's flap that evening:

| Report | Model | View | Note |
|---|---|---|---|
| 0035 | tide2 | from the side, bottom of the stroke | "low reach still too low and overlapping" |
| 0036 | tide2 | from above and ahead, top of the stroke | "high reach also clipping" |
| 0037 | storm3 | from the front quarter, bottom of the stroke | "also a bit too low and folding wings in air is weird" |

In 0035 the two wings hang under the body and wrap toward each other below the belly. In 0036 the two wings meet over the back. In 0037 the wings hang straight down like curtains, with the hand folded so the membrane's chord is vertical.

So the fault to fix is the extent of the stroke and the fold, more than its timing. A shallow, open, readable beat is the target. The playtester has now said "too low" about these wings in three rounds: when in doubt, keep the wing higher and more open.

## What the engine does with a clip

Read `tools/clips/hero-flight-brief.md`, the section "What the engine does with the clip", and `tools/clips/hero-flight.md`. The contract is the same for every model:

- The file is `assets/clips/<model>.flight.glb` with animations `flap` (cruise) and `flap_climb` (the deeper beat). The engine finds it by the model's name.
- One beat as a loop, sampled at the flight model's phase. Phase 0 is the top of the downstroke. The downstroke ends at the model's `flap_downstroke_fraction` (`assets/<model>.glb.flight.cfg`; 0.5 for tide2, 0.48 for storm3). The last key equals the first.
- Keys are deltas from each joint's rest rotation, composed onto the finished procedural pose: `rotation = procedural x delta`. The procedural beat is removed under the clip. The glide's dihedral, `wing_rest_deg`, the levelling twist, the load flex and the load-dependent finger curl stay under it. The clip keys wing joints only, both sides.
- Blender shows the clip and not the result. Judge the result in the engine.

## The two models

- `assets/tide2.glb` (a symlink): four legs, seven ribs a wing, a fan like a sail fin. `tools/skeletons/` and `artifacts/` have its rig's record; `assets/tide2.glb.rig.cfg` and `.flight.cfg` are the procedural beat.
- `assets/storm3.glb` (a symlink): a wyvern, two legs, the wings are its arms, four ribs a wing. Its bind was re-posed for flight (`tools/storm3_flight_bind.py`, `artifacts/storm3/README.md`), and its rig profile has `wing_rest_deg`. Read `tools/showcase/storm3-posture-brief.md` for what was fixed before.

Measure each model's wing joint axes as the hero's were measured (`tools/clips/hero_flight_measure.py`). Do not assume the hero's axes.

## The motion

Start from the hero's beat (its timing, its lags, its asymmetry between the quick bottom and the floating top) and change the extents below. Angles are from the wing's glide position as the engine shows it, about the body's long axis. They are starting points. The limits in the next section are requirements, and the engine render decides the rest.

tide2, `flap`: the arm about 40 degrees above the glide at the top and about 22 below at the bottom. The wrist and the tips lag as the hero's do. The recovery flexes the elbow 20 to 28 degrees and the wrist 25 to 35, and the span at the peak fold stays above 0.70 of full. The seven ribs keep their order and their spacing: the fan closes evenly by a few degrees, and no rib crosses its neighbour. `flap_climb`: 50 above, 32 below, the fold a quarter larger.

storm3, `flap`: the arm about 45 degrees above the glide at the top and about 22 below at the bottom. The recovery is a flex and not a fold: elbow at most 20 degrees, wrist at most 25, span at the peak above 0.80 of full. The fingers stay spread: no finger closes more than 8 degrees toward its neighbour. The hand never hangs: seen from the side, the membrane's chord stays within 35 degrees of the horizontal through the whole beat. `flap_climb`: 55 above, 32 below, the same limits on the fold.

Both sides are exact mirrors within a clip.

## Limits, measured by the probe

The main checkout has a probe built for you: `~/src/game-claude/build/rib_probe-r11` (source: `tools/rig_probes/rib_centre_probe.cpp`). Run it from the main checkout's directory:

`cd ~/src/game-claude && ./build/rib_probe-r11 assets/tide2.glb --clip <worktree>/assets/clips/tide2.flight.glb [--climb]`

It runs the studio's flap through the rig with your clip, skins the mesh and prints, in metres at the game's scale: each rib's nearest approach to the body's centre plane, the wing's highest skin above the back, the wing's lowest skin below the belly, and how near the centre plane any wing skin comes while more than 0.5 m below the belly. `--no-clip` is the procedural beat.

| Measure | tide2 procedural | tide2 required | storm3 procedural | storm3 required |
|---|---|---|---|---|
| Every rib's nearest approach to the centre plane | +0.18 m | at least +0.80 m | +0.27 m | at least +0.60 m |
| Wing's lowest skin from the belly | -3.27 m | no lower than -2.2 m | -2.55 m | no lower than -1.6 m |
| Under the belly, nearest the centre plane | +1.03 m | at least +2.8 m, or none | +2.66 m | at least +3.0 m, or none |

These hold for `flap` and for `flap_climb` (`--climb`). The climb may use up to 0.4 m more below the belly. Put the probe's output for all four runs in the record.

If a limit cannot be met by the clip because of what the engine leaves under it (for example the glide position itself is too low), say exactly which term, and with what `--rig` or `--flight` override it would be met. Do not edit a profile.

## Output

- `assets/clips/tide2.flight.glb` and `assets/clips/storm3.flight.glb`: the armature alone, every joint with the model's exact rest pose and names, rotation keys only on the wing joints, 61 keys over 1.0 s for each of `flap` and `flap_climb`.
- A generator and a checker that take the model's name: `tools/clips/make_wingbeat.py` and `tools/clips/check_wingbeat.py`, with each model's poses and measured axes in `tools/clips/wingbeat/<model>.json` or a Python module beside it. Do not edit the hero's files (`make_hero_flight.py`, `check_hero_flight.py`, `hero_flight_*.py`, `hero-flight.md`): another job is working on them. Copy what you need.
- The checker runs the hero checker's nine checks, with each model's own ranges, and prints PASS or FAIL per line. Prove it as the hero's was proved: damaged exports that each check rejects.

## The engine

A built engine is kept for you in the main checkout: `build/dragon-r11`. Run it from the main checkout's directory, read-only, and write screenshots into your worktree:

`cd ~/src/game-claude && DRAGON_FLIGHT_CLIPS=<worktree>/assets/clips/tide2.flight.glb ./build/dragon-r11 --headless --hide-ui --size 1920x1080 --sun 55,60 --model assets/tide2.glb <VIEW> --screenshot <worktree>/artifacts/wingbeat/tide2/NAME.bmp`

The log line `flight clips: 2 in ...` says the file was read. Without the variable the same command is the procedural beat: the "before". Convert each BMP to JPEG and delete the BMP.

In `--inspect A D E`, angle 0 is from behind, 180 from the front, 90 and 270 the two sides, E 85 from above. `--studio 1` is the cruise beat; add `--flight flap_effort_bias=1` for the climb. One beat is `flap_period` seconds at 60 frames a second (tide2 0.85 s, 51 frames; read storm3's from its flight profile, or the hero's default if it has none). Take eight frames spread evenly over one beat, starting at frame 200.

Views for each model, before and after:

| NAME | VIEW |
|---|---|
| cruise-side-0..7 | `--studio 1 --inspect 270 14 6` |
| cruise-rear-0..7 | `--studio 1 --inspect 0 16 14` |
| cruise-front-0..7 | `--studio 1 --inspect 180 16 10` |
| cruise-above-0..7 | `--studio 1 --inspect 90 18 85` |
| climb-side-0..7, climb-rear-0..7 | the side and rear views with `--flight flap_effort_bias=1` |
| report views | tide2: `--studio 1 --inspect 271 13.9 6` and `--studio 1 --inspect 300 10.8 53`. storm3: `--studio 1 --inspect 223 12.6 -1`. Eight frames each: these are the playtester's own views |
| turn-0..3 | `--studio 2 --inspect 0 16 14` at `--frames` 230, 245, 260, 275 |
| glide | `--studio 0 --inspect 0 16 14 --frames 200`: the clip must leave the glide alone |
| chase-0..3 | `--autopilot --course 1` at `--frames` 300, 313, 326, 339 |
| takeoff-0..5 | `--input 0.2,0,0,1,0,0` at `--frames` 40, 52, 64, 76, 88, 100 |

Look at every render at full size. Fix what you see, rebuild, render and probe again.

What must not happen, in the engine: the wings meet over the back or under the belly; a wing hangs below the feet line or covers the legs from the side for most of the beat; a rib crosses its neighbour; the membrane creases or tears at the root; a wing passes through the body, the legs or the tail; a pop between the last frame and the first.

Deliver, for each model, under `artifacts/wingbeat/<model>/`:

- `cruise-before-after.jpg` and `climb-before-after.jpg`: the eight side frames and the eight rear frames, the procedural beat above and the clip below, each frame cropped to the dragon at full size.
- `report-views-before-after.jpg`: the playtester's views, procedural above and the clip below.
- `views.jpg`: a contact sheet of every after view.
- `probe.txt`: the probe's output, procedural and clip, cruise and climb.

And `tools/clips/wingbeat.md`: for each model what the clip does, the axis table, the measured peaks and lags, the check output, the probe table before and after, what you changed after looking at the engine and why, and what is still wrong.

## Rules

- Write only these paths: `tools/clips/make_wingbeat.py`, `tools/clips/check_wingbeat.py`, `tools/clips/wingbeat/`, `tools/clips/wingbeat.md`, other new `tools/clips/wingbeat_*.py` helpers, `assets/clips/tide2.flight.glb`, `assets/clips/storm3.flight.glb`, `artifacts/wingbeat/`, and a scratch directory `build-clip-cache/`. Do not edit `src/`, any profile, or any existing tool.
- Every `.glb` under `assets/` except your two outputs is a symlink into the main checkout or a tracked file: never write through one.
- Use the `blender` command line only (`/opt/homebrew/bin/blender -b --factory-startup --python ...`). No Blender MCP server, no running Blender. Run Blender in the foreground, one process at a time.
- Do not build the game engine or the probe.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Commit after each model's checks pass and again after each round of engine fixes, on your branch. Stage explicit paths, never `git add -A`. Do not push.
- Write short plain sentences in the record, active voice, no filler.

## Budget and stopping

Order: tide2 first, whole (axes, `flap` through its checks and the probe, the engine until it reads right, `flap_climb`, the other views), then storm3 the same way. At most four rounds of look-and-fix in the engine for each clip. Commit tide2 before starting storm3. Finish with a short report: for each model what reads better than the procedural beat, what reads worse, what is still wrong, the probe table, any engine change you needed and did not have, and the minutes it took.
