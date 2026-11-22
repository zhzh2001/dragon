# Brief: storm3's postures and wingbeat, a second pass

You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree. The task is animation and rig work on one creature, `storm3`, a wyvern (two hind legs, the wings are the forelimbs) that an earlier job rigged. You tune its profiles by render, and you repair the asset in Blender, headless, where a profile cannot do it.

## What the playtester said

- "storm wyvern also seems a bit unnatural in postures."
- "wingbeat is too low on new designs": the wings swing far below the body and hardly rise above the back.
- "leg tucking is not implemented like embercrest": in flight the legs hang and swing. The hero (`assets/embercrest-hero.glb`) holds its legs tucked.
- A marked frame of the wingbeat from the side, `artifacts/storm3/pass2-refs/report-0032-flap-folding.jpg`, with the note "folding is bad". The pink ring is on the membrane in the downstroke.

The state before your pass: `artifacts/storm3/pass2-refs/before-nine-views.jpg` (the glide from the side, front, rear and above, the head, landed, the dive, the stand) and `before-flap.jpg` (six frames of the beat from behind over six from the side).

My reading of "unnatural", from those renders. Check it against what you see and add what I missed:

1. In the glide the body hangs about 40 degrees nose up with the neck near vertical, like a standing theropod carried through the air. A flying wyvern lies along its line of flight: hip to shoulder within about 15 degrees of level, the neck reaching forward in a shallow S, the head level.
2. The legs dangle under the body. They must fold up against the belly and the tail root, feet trailing, and hold still through the beat.
3. The beat sits low. In a cruise the wing must travel about as far above the level as below it, and at the bottom it must not hang straight down beside the legs.
4. Through the beat the membrane must stay a clean sheet between its ribs: no crumple at the wrist, no rib crossing its neighbour, no step where the inner membrane meets the flank. The hand folds on the upstroke (wrist and ribs flex, the wing narrows) and opens for the downstroke.
5. Landed, it crouches on its wrists with the wings as raised pleated fans. The target is `docs/concept/landed-wyvern-reference.png` and `docs/concept/standing-wyvern-reference.png`: the wrists planted wide and forward as front feet, the hand folded back along the forearm, the membrane gathered against the body, the chest off the ground, the neck up, the head level.
6. The dive and the turn must read as the same animal: wings swept back and narrowed, not crossed.

## What you may change

- `assets/storm3.glb.rig.cfg` and `assets/storm3.glb.flight.cfg`, freely. Keep `assets/storm3-raw.glb.rig.cfg` and `.flight.cfg` identical copies. The keys are the fields of `anim::RigTuning` in `src/anim/dragon_rig.h` (each has a comment that says what it does) and `game::FlightTuning` in `src/game/flight.h`. Keys worth knowing here: `wing_rest_deg` (the wing's elevation in flight against the bind, -20 now; the stroke angles are measured from it), `flap_up_angle_deg`, `flap_down_angle_deg`, `flap_cruise_down_angle_deg` (the downstroke's end in a cruise; the studio's flap is the cruise beat, and `--flight flap_effort_bias=1` shows the climb beat), `wing_flap_limit_deg`, `upstroke_fold_deg`, the `recovery_*` keys, `wing_*_fold_scale`, `leg_flight_pose` with `hind_flight_hip_deg`, `hind_flight_knee_deg`, `hind_flight_hock_deg`, `leg_flight_splay_deg`, the `limb_*` keys that hold a tucked leg still, the `ground_*` keys of the stance, and `wing_level_share`. `assets/embercrest-hero.glb.rig.cfg` is the hero's profile: its leg block and its wingbeat block are what the playtester likes. `assets/stormsail.glb.rig.cfg` is the old wyvern's, with long comments on why its standing wing is built as it is. `docs/ANIMATION.md` has "Wings on the ground", "A wyvern stands on its wings" and "A wing bound raised glides in a V".
- You can try a value without editing a file: `--rig key=value,...` and `--flight key=value,...` on the engine's command line.
- `assets/storm3-raw.glb` and `assets/storm3.glb`, through `tools/storm3_rig.py`, `tools/skeletons/storm3.json` and new `tools/storm3_*.py` scripts, when a posture cannot be reached from the profile. The likely case is fault 1: the flight pose is the bind pose plus offsets, and this model is bound standing upright. If no profile key lays the body along the flight line, re-pose the bind (rotate the spine, the neck and the hips so the trunk is near level, legs and wings following, the skin carried with the bones) and rebuild. Read `artifacts/storm3/README.md` first: it records how the model was built, graded and checked. Keep the bone names, 80,000 triangles, the UVs, the maps and the colour grade. The flap check (`tools/storm3_flapcheck.py`) must still report no body vertex moving with the wings.
- Do not change the creature's design: proportions, the crest, the fluke, the wing's outline. The playtester asked for the model to look like its sheet, `docs/concept/elemental-options/round3/storm-1.jpg`.

You cannot change the engine. If a posture needs a key the engine does not have, say exactly which and why in your report, and get as close as the existing keys allow.

## The engine

The main checkout at `~/src/game-claude` has a built engine kept for you: `build/dragon-r9`. Use that binary, not `build/dragon`, which the main session rebuilds while you work. Run it from the main checkout's directory, read-only, and write screenshots into your worktree:

`cd ~/src/game-claude && ./build/dragon-r9 --headless --hide-ui --size 1920x1080 --sun 55,60 --model <worktree>/assets/storm3.glb <VIEW> --screenshot <worktree>/artifacts/storm3/pass2/NAME.bmp`

Convert each BMP to JPEG and delete the BMP. `--sun 55,60` puts the studio in sunlight. In `--inspect A D E`, angle 0 is from behind, 180 from the front, 90 and 270 the two sides, E 85 from above.

The playtester's own view of the beat: `--studio 1 --inspect 269 12.7 6`. One beat is about 51 frames.

Views, all required, before and after:

| NAME | VIEW |
|---|---|
| glide-side, -front, -rear, -above | `--studio 0 --frames 200` with `--inspect 90 14 6`, `180 14 6`, `0 14 6`, `90 16 85` |
| flap-side-0..7 | `--studio 1 --inspect 269 12.7 6` at `--frames` 200, 207, 214, 221, 228, 235, 242, 249 |
| flap-rear-0..7 | `--studio 1 --inspect 0 16 14` at the same frames |
| climb-side-0..3 | the same side view with `--flight flap_effort_bias=1` at 200, 213, 226, 239 |
| turn | `--studio 2 --inspect 0 16 14 --frames 260` |
| dive | `--studio 5 --inspect 90 16 6 --frames 300` |
| brake | `--studio 7 --inspect 90 16 6 --frames 260` |
| head | `--studio 0 --inspect 90 3.5 0 --inspect-head --frames 200` |
| landed-side, -front, -rear, -feet | `--input -0.25,0,0,0,0,0 --frames 900` with `--inspect 90 12 5`, `180 12 5`, `0 12 5`, `60 9 2` |
| stand | `--studio 9 --inspect 150 12 8 --frames 200` |
| walk-0..3 | `--studio 13 --inspect 90 12 5` at `--frames` 200, 215, 230, 245 |
| chase | `--frames 200` |
| hero-glide-side, hero-flap-side-0..7 | the hero, `assets/embercrest-hero.glb`, in the same views: what a tucked leg and a liked wingbeat look like |

Look at every render at full size. A posture is not judged by its numbers. Deliver:

- `artifacts/storm3/pass2/before-after.jpg`: for the glide side, the glide front, four frames of the beat from the side, the landed side and the landed front, before above and after below, cropped to the creature, at full size.
- `artifacts/storm3/pass2/views.jpg`: a contact sheet of every after view.
- `artifacts/storm3/README.md`, a new section "Second pass: postures and wingbeat": each of the six faults, what caused it, what you changed (key and value, or the asset step), and what is still wrong. Say in numbers how much moved: the trunk's angle in the glide before and after, the stroke's top and bottom from level, the foot's travel through a beat.
- A comment beside each changed value in the profile that says what it was and why it changed.

## Rules

- Write only these paths: `assets/storm3*.cfg`, `assets/storm3.glb`, `assets/storm3-raw.glb`, `tools/storm3_*.py`, `tools/skeletons/storm3.json`, `artifacts/storm3/`, and a scratch directory `build-rig-cache/`. The two `.glb` files are copies you may overwrite and are ignored by git: leave them in place and the main session collects them. Every other `.glb` in `assets/` is a symlink into the main checkout: never write through one.
- Use the `blender` command line only (`/opt/homebrew/bin/blender -b --factory-startup --python ...`). No Blender MCP server, no running Blender. Run Blender in the foreground, one process at a time.
- The material repair needs a Python environment with numpy, Pillow, scipy and trimesh with embree. Make one in `build-rig-cache/venv` with `uv venv` and `uv pip install numpy pillow scipy trimesh embreex`.
- Do not build the game engine. Do not edit engine source or docs outside the paths above.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Commit after each fault you close, on your branch. Stage explicit paths, never `git add -A`. Do not push.
- Write short plain sentences in the README, active voice, no filler.

## Budget and stopping

Order: the glide's body line (1), the legs (2), the beat (3, 4), the stance (5), the dive and turn (6). At most three attempts a fault, and at most two asset rebuilds in all. If a fault cannot be closed, write what failed, commit what there is, and go on. Finish with a short report: for each fault closed or not, the numbers, any engine key you needed and did not have, and the minutes it took.
