# Brief: keyframed wingbeats for the gold and silver dragons

You author a keyframed wingbeat for two dragons in Blender, by script, check each by script and by a probe, and judge each in the game engine by render. You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree.

## Why

The playtester, on 2026-11-24: "the gold/silver wingbeat still unnatural". And one report on gold in the studio's flap, seen from the front quarter and a little below, with the mark on the wing at the top of its stroke: "paused here unnaturally".

Three other dragons already have a keyframed wingbeat: the hero (`tools/clips/hero-flight.md`), and tide2 and storm3 (`tools/clips/wingbeat.md`, built by `tools/clips/make_wingbeat.py` from `tools/clips/wingbeat/<model>.json`). Read both records before you start. You add gold and silver to the second tool.

The playtester's verdict on tide2's and storm3's clips matters here. Those two were made shallow to stay inside limits, and the reports the next day said: "wingbeat too conservative now, need wider range". The playtester then found that a half mix of the clip and the old deep procedural beat "seems to be good balance". So do not make these two beats shallow. Make a full, generous beat that reads as flying.

## What I saw, on 2026-11-24

`artifacts/gold-dragon-integration/wingbeat-refs/gold-now.jpg` and `artifacts/silver-dragon-integration/wingbeat-refs/silver-now.jpg`: eight frames of each procedural beat from the report's view.

Gold, a long serpentine dragon with four short legs and small sail wings of four rays (the small wings are the design, keep them):

1. The wing never goes below the body's line. The whole stroke is between a steep V over the back and level. It reads as a sail waved over the back, not a wing that pushes air down.
2. The wing is a stiff board. The tip does not lag the arm, the hand does not flex, the rays do not spread or close.
3. The wing holds at the top of the stroke for several frames, then drops. That is the "paused" of the report.

Silver, a four-legged dragon with long narrow wings:

1. The same stiff board: the long wing swings from the shoulder as one straight blade between level and about 25 degrees up. A wing this long must show a wave travelling from the shoulder to the tip.
2. The stroke is nearly all above the body's line, and small.
3. The motion looks mechanical: the same speed up and down, no quick downstroke, no floating recovery.

Check my reading against what you see and add what I missed.

## What the engine does with a clip

Read `tools/clips/hero-flight-brief.md`, the section "What the engine does with the clip". The contract is the same for every model:

- The file is `assets/clips/<model>.flight.glb` with animations `flap` (cruise) and `flap_climb` (the deeper beat). The engine finds it by the model's name: `assets/clips/gold-dragon.flight.glb` and `assets/clips/silver-dragon.flight.glb`.
- One beat as a loop, sampled at the flight model's phase. Phase 0 is the top of the downstroke. The downstroke ends at the model's `flap_downstroke_fraction` (the flight profile, or the code default in `src/game/flight.h` when the profile has none). The last key equals the first.
- Keys are deltas from each joint's rest rotation, composed onto the finished procedural pose: `rotation = procedural x delta`. The procedural beat is removed under the clip. The glide's dihedral, `wing_rest_deg`, the levelling twist, the load flex and the load-dependent finger curl stay under it. The clip keys wing joints only, both sides.
- Blender shows the clip and not the result. Judge the result in the engine.

These two models keep their profiles in `assets/species/` (`gold-dragon.glb.rig.cfg`, `gold-dragon.glb.flight.cfg`, and silver's two), not beside the model. The engine and the probe find them there. `tools/clips/make_wingbeat.py` and its helpers may assume a profile beside the model: fix that where needed.

## The motion

Start from the hero's beat (its timing, its lags, its asymmetry between the quick downstroke and the floating recovery). Angles below are from the wing's glide position as the engine shows it, about the body's long axis. They are starting points. The engine render decides.

Both, `flap`:

- The motion never stops. No joint's angular speed stays under 15% of its peak for more than 8% of the beat, at the top or anywhere else. Put this in the checker.
- The stroke crosses the body's line. The arm reaches about 45 degrees above the glide at the top and about 30 below at the bottom. From the side at the bottom of the stroke, the wing tip must be plainly below the belly line.
- The tip lags the arm: the wrist by about 8% of the beat and the tips by about 14%. From behind, the wing shows a bend at the wrist on the way down (the tip still high) and on the way up (the tip still low).
- The recovery flexes: the elbow 20 to 30 degrees, the wrist 25 to 40, and the rays close a few degrees and open again for the downstroke. The span at the peak fold is between 0.70 and 0.85 of full.
- The downstroke is quicker than the recovery, as the profile's fraction says.

Gold: a sail of four rays. Let the rays fan open on the downstroke and close by a few degrees each on the recovery. No ray crosses its neighbour.

Silver: the long blade. The wave along the wing is the point: at mid downstroke the arm is level while the outer third still points up, and at mid recovery the reverse. Silver's wing bind was re-weighted on 2026-11-22 (`artifacts/silver-dragon-integration/pass3/README.md`): read what was wrong there before you bend it.

`flap_climb`, both: 55 above, 40 below, the fold a quarter larger, the same lags.

Both sides are exact mirrors within a clip.

## Limits, measured by the probe

The main checkout has a probe built for you: `~/src/game-claude/build/rib_probe-r12` (source: `tools/rig_probes/rib_centre_probe.cpp`). Run it from the main checkout's directory:

`cd ~/src/game-claude && ./build/rib_probe-r12 assets/gold-dragon.glb --clip <worktree>/assets/clips/gold-dragon.flight.glb [--climb]`

It runs the studio's flap through the rig with your clip, skins the mesh and prints, in metres at the game's scale: each rib's nearest approach to the body's centre plane, the wing's highest skin above the back, the wing's lowest skin from the belly (negative is below it), and how near the centre plane any wing skin comes while more than 0.5 m below the belly. `--no-clip` is the procedural beat.

| Measure | gold procedural | gold required | silver procedural | silver required |
|---|---|---|---|---|
| Every rib's nearest approach to the centre plane | +0.87 m | at least +0.60 m | +1.17 m | at least +0.60 m |
| Wing's lowest skin from the belly | +0.57 m (above it) | between -0.8 and -2.5 m | +1.02 m (above it) | between -1.0 and -3.0 m |
| Under the belly, nearest the centre plane | none | at least +2.5 m | none | at least +2.5 m |

These limits are loose on purpose. They only stop the wings from meeting over the back or wrapping under the belly. The "lowest skin" row has a floor as well as a ceiling: a wing that never drops below the belly fails it. The climb may go 0.5 m lower. Put the probe's output for all runs in the record.

If the engine leaves something under the clip that blocks a limit, say exactly which term, and with what `--rig` or `--flight` override it would be met. Do not edit a profile.

## Output

- `assets/clips/gold-dragon.flight.glb` and `assets/clips/silver-dragon.flight.glb`: the armature alone, every joint with the model's exact rest pose and names, rotation keys only on the wing joints, 61 keys over 1.0 s for each of `flap` and `flap_climb`.
- `tools/clips/wingbeat/gold-dragon.json` and `tools/clips/wingbeat/silver-dragon.json`: each model's poses and measured axes, for `make_wingbeat.py` and `check_wingbeat.py`. Measure each model's wing joint axes. Do not assume another model's.
- You may edit `make_wingbeat.py`, `check_wingbeat.py` and the `wingbeat_*.py` helpers to take these two models and the new no-hold check. After your edits, `make_wingbeat.py tide2` and `make_wingbeat.py storm3` must still write clips whose keys equal the committed ones (compare the decoded keys, not the bytes), and `check_wingbeat.py` must still pass on both. Show that in the record.
- Prove the no-hold check as the other checks were proved: a damaged export that it rejects.

## The engine

A built engine is kept for you in the main checkout: `build/dragon-r12`. Run it from the main checkout's directory, read-only, and write screenshots into your worktree:

`cd ~/src/game-claude && DRAGON_FLIGHT_CLIPS=<worktree>/assets/clips/gold-dragon.flight.glb ./build/dragon-r12 --headless --hide-ui --size 1920x1080 --sun 55,60 --model assets/gold-dragon.glb <VIEW> --screenshot <worktree>/build-clip-cache/gold/NAME.bmp`

The log line `flight clips: 2 in ...` says the file was read. Without the variable the same command is the procedural beat: the "before".

In `--inspect A D E`, angle 0 is from behind, 180 from the front, 90 and 270 the two sides, E 85 from above, a negative E from below. These are large animals: gold frames at about 28 m and silver at about 22 m (D below). `--studio 1` is the cruise beat; add `--flight flap_effort_bias=1` for the climb. One beat is `flap_period` seconds at 60 frames a second (gold 1.05 s, 63 frames; silver 1.1 s, 66 frames). Take eight frames spread evenly over one beat, starting at frame 200.

Views for each model, before and after:

| NAME | VIEW |
|---|---|
| cruise-side-0..7 | `--studio 1 --inspect 270 D 6` |
| cruise-rear-0..7 | `--studio 1 --inspect 0 D 14` |
| cruise-front-0..7 | `--studio 1 --inspect 180 D 10` |
| cruise-above-0..7 | `--studio 1 --inspect 90 D 85` |
| report-0..7 | `--studio 1 --inspect 240 D -13`: the playtester's own view |
| climb-side-0..7, climb-rear-0..7 | the side and rear views with `--flight flap_effort_bias=1` |
| turn-0..3 | `--studio 2 --inspect 0 D 14` at `--frames` 230, 245, 260, 275 |
| glide | `--studio 0 --inspect 0 D 14 --frames 200`: the clip must leave the glide alone |
| chase-0..3 | `--autopilot --course 1` at `--frames` 300, 316, 332, 348 |
| takeoff-0..5 | `--input 0.2,0,0,1,0,0` at `--frames` 40, 52, 64, 76, 88, 100 |

Look at every render at full size. Fix what you see, rebuild, render and probe again.

What must not happen, in the engine: a hold anywhere in the beat; a wing that moves as one stiff board; the wings meet over the back or under the belly; a rib or ray crosses its neighbour; the membrane creases or tears at the root; a wing passes through the body, the legs or the tail; a pop between the last frame and the first.

Deliver, for each model, under `artifacts/gold-dragon-integration/wingbeat/` and `artifacts/silver-dragon-integration/wingbeat/`:

- `cruise-before-after.jpg` and `climb-before-after.jpg`: the eight side frames and the eight rear frames, the procedural beat above and the clip below, each frame cropped to the dragon at full size.
- `report-view-before-after.jpg`: the playtester's view, procedural above and the clip below.
- `views.jpg`: a contact sheet of every after view.
- `probe.txt`: the probe's output, procedural and clip, cruise and climb.
- `README.md`: what the clip does, the axis table, the measured peaks and lags, the check output, the probe table before and after, what you changed after looking at the engine and why, and what is still wrong.

Commit these sheets and records only. Keep every single frame in `build-clip-cache/`, which git ignores. Do not commit a single-frame capture.

Renders of these two models may be written only inside those two `wingbeat/` directories and `build-clip-cache/`. Both parents are kept out of the public mirror. Do not put a render of either model, or its name's measurements, in `tools/clips/wingbeat.md`: add one line there that points to the two README files.

## Rules

- Write only these paths: `tools/clips/make_wingbeat.py`, `tools/clips/check_wingbeat.py`, `tools/clips/wingbeat_*.py`, `tools/clips/wingbeat/`, one line in `tools/clips/wingbeat.md`, `assets/clips/gold-dragon.flight.glb`, `assets/clips/silver-dragon.flight.glb`, the two `wingbeat/` directories under `artifacts/`, and a scratch directory `build-clip-cache/`. Do not edit `src/`, any profile, any model, or any other tool. Do not change `assets/clips/tide2.flight.glb` or `assets/clips/storm3.flight.glb`.
- Every model `.glb` under `assets/` is a symlink into the main checkout or a tracked file: never write through one.
- Use the `blender` command line only (`/opt/homebrew/bin/blender -b --factory-startup --python ...`). No Blender MCP server, no running Blender. Run Blender in the foreground, one process at a time.
- Do not build the game engine or the probe.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Commit after each model's checks pass and again after each round of engine fixes, on your branch. Stage explicit paths, never `git add -A`. Do not push.
- Write short plain sentences in the record, active voice, no filler.

## Budget and stopping

Order: gold first, whole (axes, `flap` through its checks and the probe, the engine until it reads right, `flap_climb`, the other views), then silver the same way. At most four rounds of look-and-fix in the engine for each clip. Commit gold before starting silver. Finish with a short report: for each model what reads better than the procedural beat, what reads worse, what is still wrong, the probe table, any engine change you needed and did not have, and the minutes it took.
