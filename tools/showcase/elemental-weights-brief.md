# Brief: stop the body moving with the wings on tide2, blight2 and stone2

You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree. The task is Blender work, headless: repair skin weights on three rigged dragons that an earlier job built.

## The fault

The playtester watched each creature flap from behind, close, and marked skin that is not wing but moves with the wingbeat:

- `artifacts/weights-refs/report-0025-blight2.jpg`: blight2, the top of the left haunch beside the tail root. Note: "moves while flapping".
- `artifacts/weights-refs/report-0026-tide2.jpg`: tide2, mark 1 on the haunch (the small fin on the thigh), mark 2 on a fin of the neck or cheek. Notes: "moves while flapping", "moves too".

The pink rings are the marks. Both views are from behind and above, 9 m away.

The likely cause is in each creature's `tools/<name>_rig.py` and `tools/skeletons/<name>.json`: the membrane field (`wing_field`) or the smoothing gives wing-bone weight to vertices of the body, the legs, the fins, the spikes or the tail. Find the real cause by measuring. Do not assume.

stone2 has no report. Check it the same way and fix what you find.

## What done means

For each creature:

1. A check script `tools/<name>_flapcheck.py`, run in Blender on the built `.glb`, that poses only the wing bones (every bone whose name starts with `wing_`) through a deep stroke, shoulder from +72 to -125 degrees about the body's long axis with the elbow, wrist and fingers folding 30 degrees, and reports the largest displacement of any vertex that is not part of the wing. Decide "part of the wing" by geometry (the membrane, the wing arm and the fingers, with a blend band of a few centimetres of body length at the shoulder), not by the weights you are testing. The script prints the worst vertex's position, its displacement in metres at engine scale, and which bones weight it.
2. Before the fix, record the script's numbers in `artifacts/<name>/README.md` under a new heading "Flap check". The marked places must show up in it. If they do not, the check is wrong: fix the check first.
3. Fix the rig build so that no body vertex outside the shoulder blend band moves more than 2 cm at engine scale (the body is about 13 m long), and the shoulder itself still blends without a crease. Rebuild `assets/<name>-raw.glb` and `assets/<name>.glb` with the scripts, as the first job did. Keep 80,000 triangles, the UVs and the maps. Do not change the bone names or the bone positions unless a bone position is the cause.
4. After the fix, record the script's numbers again.
5. Engine renders, before and after, in `artifacts/<name>/flap-weights/`. Run the engine from the main checkout, read-only, and write screenshots into your worktree:

   `cd ~/src/game-claude && ./build/dragon --headless --hide-ui --size 1920x1080 --sun 35,235 --model <worktree>/assets/<name>.glb --studio 1 --flight flap_period=0.85,flap_up_angle_deg=72,flap_down_angle_deg=-125 --inspect 0 9 14 --frames N --screenshot <worktree>/artifacts/<name>/flap-weights/NAME.bmp`

   with N = 200, 208, 216, 224, 232, 240 (one beat is about 51 frames). `--inspect 0` is from behind. Also `--inspect 90 12 6` (the side) at N = 208 and 232. Convert each BMP to JPEG and delete the BMP. Make one sheet `artifacts/<name>/flap-weights/before-after.jpg`: the six rear frames before in a row, the six after under them, cropped to the creature. Look at every frame at full size. The wing must still deform cleanly: no tear at the root, no membrane left behind, no finger detached.
6. The engine's load line must still read `mapped rig: ...` with every chain mapped and no joint warning.

The main session rebuilds the engine while you work. If a run fails to start, wait 60 seconds and run it again.

## Read first

- `artifacts/<name>/README.md` for each creature: how the first job measured and built it.
- `tools/<name>_rig.py`, `tools/skeletons/<name>.json`, and `tools/rig_embercrest_candidate.py` (the shared rig step; do not change it).
- `docs/MODEL_GENERATION.md`, "The five rigged", and `docs/ANIMATION.md` for the bone-name contract and the membrane field.

## Rules

- Write only these paths: `tools/<name>_*.py`, `tools/skeletons/<name>.json`, `assets/<name>.glb`, `assets/<name>-raw.glb`, `artifacts/<name>/`, and a scratch directory `build-rig-cache/`. `<name>` is `tide2`, `blight2` or `stone2`.
- Do not edit any `.cfg` profile. The main session owns the profiles and is changing them while you work. The `--flight` override in the command above gives the deep stroke they will have.
- The `.glb` files are ignored by git: leave them in `assets/` and the main session collects them. `assets/tide2.glb`, `blight2.glb`, `stone2.glb` and their `-raw` files are copies you may overwrite. Every other `.glb` in `assets/` is a symlink into the main checkout: never write through one.
- Use the `blender` command line only (`/opt/homebrew/bin/blender -b --factory-startup --python ...`). No Blender MCP server, no running Blender. Run Blender in the foreground, one process at a time.
- The material repair needs a Python environment with numpy, Pillow, scipy and trimesh with embree. Make one in `build-rig-cache/venv` with `uv venv` and `uv pip install numpy pillow scipy trimesh embreex`.
- Do not build the game engine. Do not edit engine source or docs outside the paths above.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Order: tide2, blight2, stone2. Commit after each creature passes, on your branch. Stage explicit paths, never `git add -A`. Do not push.
- Write short plain sentences in the READMEs, active voice, no filler. Record in each rig script, as a comment beside the change, what was wrong and why the change fixes it.

## Budget and stopping

At most three rebuild attempts a creature. If a creature cannot pass, write what failed in its README, commit what there is, and go on. Finish with a short report: for each creature the cause found, the worst displacement before and after, what is still weak, and the minutes it took.
