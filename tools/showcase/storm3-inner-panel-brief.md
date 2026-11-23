# Brief: storm3's inner wing panel

You repair one fault in one creature's model in Blender, headless, and check it in the game engine by render. You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree.

## The fault

`storm3` is a wyvern: its wings are its arms, four ribs a wing. Between the innermost rib and the flank, each wing has an inner panel of membrane. In flight that panel reads as torn cloth:

- `artifacts/storm3/pass3-refs/inner-panel-patch.jpg`: the far wing's inner panel, three times size, from the playtester's own view of the wingbeat. The panel is a pale sheet with ragged dark patches along its top, and it ends in a straight vertical edge.
- `artifacts/storm3/pass3-refs/report-0037-view-procedural-over-clip.jpg`: three frames of the beat from that view, the procedural beat above and the keyframed beat below. The patch shows in both rows.
- Earlier records name it: `artifacts/storm3/README.md` ("A small notch remains beside the innermost rib", "the inner membrane stays broad across the flank") and `tools/clips/wingbeat.md` ("The far panel still shows a pale patch against its dark underside", "Two dark triangles showed against its pale membrane", "The dark patches on the inner panels stand out more from this angle").

The playtester marked this dragon's wing three times in two days ("folding is bad", "folding wings in air is weird"). The beat itself is fixed by a keyframed clip. This patch is what is left on the wing.

Find the cause before you change anything. Candidates, from the pictures alone: the panel's two faces (upper and lower skin of a thick shell) pass through each other as the rib moves, so the dark side shows through the pale one; the panel's vertices are weighted to the body and to the rib with nothing between, so it stretches flat and its trailing edge stays straight; the pale colour is the panel's own texture, lighter than the rest of the wing's underside; triangles face the wrong way. Measure which. Say what you found, in numbers, in the record.

## The fix

The panel must read as part of the same wing in every frame of the beat, from above and from below: one sheet, the wing's colour and value on each side, a trailing edge that curves from the rib to the flank, no patch, no hole, no step where it meets the body.

You may change, in `assets/storm3-raw.glb` and `assets/storm3.glb` (real copies in your worktree, not tracked by git):

- the weights of the panel's vertices, and of the vertices next to it;
- the panel's vertex positions, within about 5% of the wingspan, to curve its trailing edge or to separate its two faces;
- its normals and its triangles' winding;
- the base colour and the data maps inside the panel's UV area, if the pale colour is the texture's.

You may not change: the bone names or the rest pose (the keyframed wingbeat `assets/clips/storm3.flight.glb` and the profiles depend on both), the triangle count beyond 1%, the UV layout, anything outside the two inner panels and their borders, the creature's design. Do not edit a profile, a clip, or `src/`.

Work through the tools that built the model: `tools/storm3_rig.py`, `tools/storm3_web.py`, `tools/storm3_flight_bind.py`, `tools/storm3_grade.py`, `tools/storm3_flapcheck.py`, `tools/skeletons/storm3.json`, and `tools/repair_model_materials.py` for the repaired model. Read `artifacts/storm3/README.md` first: it records how the model was built, re-posed, graded and checked, and how to rebuild it. Put new scripts in `tools/storm3_panel_*.py`. The flap check must still report no body vertex moving with the wings. Keep a copy of each model as it was in `build-rig-cache/`.

## The engine

A built engine is kept for you in the main checkout: `~/src/game-claude/build/dragon-r11c`. Run it from the main checkout's directory, read-only, and write screenshots into your worktree:

`cd ~/src/game-claude && ./build/dragon-r11c --headless --hide-ui --size 1920x1080 --sun 55,60 --model <worktree>/assets/storm3.glb <VIEW> --screenshot <worktree>/artifacts/storm3/pass3/NAME.bmp`

The model's profiles are beside it in your worktree. The engine finds the keyframed wingbeat by the model's name in the main checkout's `assets/clips/`, and the log line `flight clips: 2 in ...` says it was read. Add `--rig flap_clip=0` for the procedural beat. Convert each BMP to JPEG and delete the BMP.

In `--inspect A D E`, angle 0 is from behind, 180 from the front, 90 and 270 the two sides, E 85 from above and a negative E from below. `--studio 1` is the cruise beat, `--studio 0` the glide, `--studio 2` a turn, `--studio 5` the dive, `--studio 9` standing. One beat is the flight profile's `flap_period` seconds at 60 frames a second.

Views, before and after, all required:

| NAME | VIEW |
|---|---|
| report-0..7 | `--studio 1 --inspect 223 12.6 -1`, eight frames spread over one beat from frame 200: the playtester's view |
| panel-above-0..7 | `--studio 1 --inspect 200 8 35` at the same frames: the far panel from above and behind |
| panel-below-0..7 | `--studio 1 --inspect 320 8 -35` at the same frames: a panel from below |
| rear-0..7 | `--studio 1 --inspect 0 16 14` at the same frames |
| procedural-report-0..3 | the report view with `--rig flap_clip=0`, four frames |
| glide-rear, glide-above, glide-below | `--studio 0 --frames 200` with `--inspect 0 16 14`, `90 18 85`, `90 14 -60` |
| turn | `--studio 2 --inspect 0 16 14 --frames 260` |
| dive | `--studio 5 --inspect 270 16 6 --frames 300` |
| stand-side, stand-front, stand-rear | `--studio 9 --frames 200` with `--inspect 270 14 6`, `180 14 6`, `0 14 6`: the folded wing must not be worse |

Look at every render at full size. Fix what you see, rebuild, render again.

Deliver under `artifacts/storm3/pass3/`:

- `panel-before-after.jpg`: the report view and the two panel views, eight frames each, before above and after below, each cropped to the wing at full size.
- `views.jpg`: a contact sheet of every after view.
- `README.md`: the cause with its measurements, what you changed, what you tried that did not work, the flap check's output, what is still wrong, the SHA-256 of each model before and after, and how to rebuild.

## Rules

- Write only these paths: `assets/storm3.glb`, `assets/storm3-raw.glb`, new `tools/storm3_panel_*.py` scripts, `artifacts/storm3/pass3/`, and a scratch directory `build-rig-cache/`.
- Every other `.glb` under `assets/` is a symlink into the main checkout or a tracked file: never write through one.
- Use the `blender` command line only (`/opt/homebrew/bin/blender -b --factory-startup --python ...`). No Blender MCP server, no running Blender. Run Blender in the foreground, one process at a time. Another job may be using the GPU at the same time: expect renders to take longer than usual.
- Do not build the game engine.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Commit your scripts and the `pass3/` directory after each round, on your branch. Stage explicit paths, never `git add -A`. The models are not tracked: record their SHA-256. Do not push.
- Write short plain sentences in the record, active voice, no filler.

## Budget and stopping

Find the cause first and write it down. At most four rounds of fix, rebuild and render. If the panel cannot be made clean without a change this brief forbids, say exactly which and stop. Finish with a short report: the cause, what changed that can be seen, what is still wrong, the model files' SHA-256, and the minutes it took.
