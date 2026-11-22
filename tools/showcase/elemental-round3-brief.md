# Brief: rig the third-round frost and storm dragons

You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree. The task is Blender work, headless: turn two static generated dragon meshes into rigged creatures the engine can fly.

The playtester rejected the second-round frost and storm dragons for their designs, picked these two from new sheets, and asked for one thing: the final model must look like its sheet. Judge every step against the sheet.

## The inputs

Each `assets/<name>-cand-oneshot.glb` is a Hunyuan one-shot: 1.5 million triangles, textured, about 1 unit long, static. In Blender after import it faces -Y with Z up and the wingspan along X.

| Name | Element | Anatomy | Sheet | Sibling for profiles |
|---|---|---|---|---|
| `frost3` | frost | four legs, two wings | `docs/concept/elemental-options/round3/frost-3b.jpg` | `frost2` |
| `storm3` | storm | a wyvern: two hind legs, the wings are the forelimbs | `docs/concept/elemental-options/round3/storm-1.jpg` | `stormsail` |

Previews of the raw meshes are `artifacts/elemental-round2/frost3-preview.jpg` and `storm3-preview.jpg`. Look at the sheets and the previews first.

`frost3` is white and pale blue with a ruff of ice spikes behind the head, a row of spikes down the back, and a tail that ends in a spray of icicles. Colour sits only on the wing edges. The spikes and icicles are ice, not horn, and they are rigid: each must ride one bone whole.

`storm3` is the old `stormsail` design refined. `stormsail` is in the roster today (`assets/stormsail.glb`, a symlink here), rigged on the shared file `tools/skeletons/winged-biped.json`. Its bone names are the contract for a wyvern: a three-bone arm (`wing_root_`, `wing_arm_`, `wing_wrist_`), four finger ribs per wing, two hind legs, no foreleg bones, and a tail that ends in a rudder fin. Read that file and `assets/stormsail.glb.rig.cfg` before you measure. Do not fit `storm3` to the shared file: measure its own skeleton with the same bone names.

## Read first

- `docs/MODEL_GENERATION.md`: "The pipeline", steps 3 and 4; "Adding a creature that is not a dragon"; "The five rigged"; "The wings' weight on the body".
- `artifacts/frost2/README.md`, `tools/frost2_rig.py`, `tools/frost2_wing_surface.py`, `tools/skeletons/frost2.json`: the last frost dragon, built by the measured route. `frost3` follows it.
- `artifacts/storm2/README.md`, `tools/storm2_rig.py`, `tools/storm2_membrane.py`: the last storm dragon. It has four legs, so only its measuring method transfers.
- `tools/blight2_flapcheck.py` and `artifacts/blight2/README.md`, "Flap check": the check that found wing weight on body skin, and the fix.
- `tools/rig_embercrest_candidate.py`: the shared decimate, fit, bind and export step. `tools/repair_model_materials.py`: the data-map repair.
- `docs/ANIMATION.md`: the bone-name contract with `src/anim/dragon_rig.cpp`, the standing stance, "Wings on the ground", "A wyvern stands on its wings", and the frame and scale traps.
- `CLAUDE.md`, "A creature is a species, not just a mesh": the three profile files, and the trap of a partial profile.

## What to deliver for each creature

1. `tools/skeletons/<name>.json`: its own measured skeleton. Record how you measured it in `artifacts/<name>/README.md`.
2. `assets/<name>-raw.glb`: decimated to 80,000 triangles and rigged, UVs and maps kept.
3. `assets/<name>.glb`: the same with the data maps repaired. Check the log's zone coverage against what the animal is made of.
4. `assets/<name>.glb.rig.cfg`, `.flight.cfg` and `.breath.cfg`: copied whole from the sibling in the table, every block, then tuned by render until the creature stands, folds its wings and flies without a fault. Set `element` in the breath file (`frost`, `storm`). Copy the three files beside the raw model too.
5. `tools/<name>_flapcheck.py`, like `tools/blight2_flapcheck.py`: pose only the wing bones through a deep stroke and report the largest displacement of any vertex that is not wing. No body vertex outside the shoulder blend band may move more than 2 cm at engine scale. On the wyvern the membrane joins the flank and the leg: define "not wing" by geometry and say in the README where you drew the line. The ruff, the back spikes, the horns, the legs, the tail and its tip must not move with the wings at all.
6. The engine's load line must read `mapped rig: ...` with every chain the anatomy has mapped and no joint warning. For `storm3`, compare with the line `assets/stormsail.glb` prints.

## Likeness, required

For each creature make `artifacts/<name>/likeness.jpg`: the sheet's side view beside the engine's side view of the bind pose (`--bind-pose --inspect 90 14 6`), and the sheet's front view beside the engine's front view, each pair at the same scale. Look at it and list in the README what differs from the sheet: proportions, colour, the wing's outline and where the membrane joins the body, the spikes, the tail tip. Fix what the rig step caused (a decimation that ate the icicles, a data-map repair that recoloured ice as horn, a bind that moved the wing root). Do not resculpt what the generator made. If the generated mesh itself departs from the sheet, say where, and stop there.

Colour is part of likeness. The playtester asked for a white and pale blue frost dragon with aurora colour only on the wing edges, and rejected the last one for being too dark and too coloured. The generator's base colour for `frost3` is about 10% darker than the sheet (texture median 146,164,187 against 162,179,196 for the sheet's creature pixels). If the engine's bind-pose side view under `--sun 35,235` reads as blue-grey and not as white with pale blue, grade the base colour texture in the build: lift it, keep the pale blue on the back, keep the green and violet only at the wing's trailing edge. Record the curve in the README and in the script. `storm3`'s base colour already matches its sheet (mean 119,127,140 against 137,142,151): leave it unless the engine render says otherwise.

The playtester's note on the frost sheet was that the wings looked disconnected from the body in the side view. Check that the membrane and the wing arm join the body at the shoulder in the mesh, and that the bind keeps them joined through the wingbeat.

## Engine check, required

The main checkout at `~/src/game-claude` has a built engine. Run it read-only on your file, from the main checkout's directory, and write screenshots into your worktree. Write nothing else in the main checkout. The main session rebuilds the engine while you work: if a run fails to start, wait 60 seconds and run it again.

`cd ~/src/game-claude && ./build/dragon --headless --hide-ui --size 1920x1080 --sun 35,235 --model <worktree>/assets/<name>.glb <VIEW> --screenshot <worktree>/artifacts/<name>/NAME.bmp`

Convert each BMP to JPEG and delete the BMP. Views, all required. In `--inspect A D E`, angle 0 is from behind, 180 from the front, 90 from the side.

| NAME | VIEW |
|---|---|
| bind | `--bind-pose --skeleton --inspect 90 14 6 --frames 40` |
| glide-side | `--studio 0 --inspect 90 14 6 --frames 200` |
| glide-above | `--studio 0 --inspect 90 20 85 --frames 200` |
| glide-front | `--studio 0 --inspect 180 14 6 --frames 200` |
| flap-rear | `--studio 1 --inspect 0 16 15` at `--frames` 200, 208, 216, 224, 232, 240 |
| flap-side | `--studio 1 --inspect 90 14 6` at `--frames` 208 and 232 |
| head | `--studio 0 --inspect 90 3.5 0 --inspect-head --frames 200` |
| jaw | `--studio 8 --inspect 90 3.5 0 --inspect-head --frames 150` |
| landed | `--input -0.25,0,0,0,0,0 --inspect 60 11 3 --frames 900` |
| landed-rear | `--input -0.25,0,0,0,0,0 --inspect 0 14 6 --frames 900` |
| dive | `--studio 5 --inspect 90 16 6 --frames 300` |
| chase | `--frames 200` |

Look at every render at full size. A rig is not judged by its build statistics. Fix what you see: a wing that tears or folds through the body, a leg under the ground or in the air, a jaw that bends the muzzle, spikes or icicles that smear with the wrong bone, a tail tip that crumples, body skin that moves with the wing. Put a contact sheet of the views at `artifacts/<name>/views.jpg`.

Sizes: the engine sizes a model by its wingspan, 19 m unless the rig profile sets `model_span_m`. Each creature must come out between 0.85 and 1.0 of the hero's body length. Render `assets/embercrest-hero.glb` with the same side view and compare. If the creature is too long or too short, set `model_span_m` in its rig profile and say what you set.

The wingbeat: `frost2`'s profile ends with a block copied from the hero (a deep stroke with a recovery fold). Keep it on `frost3` and reduce `flap_up_angle_deg` and `flap_down_angle_deg` in the flight profile if the wings cross over the back or close under the belly. `storm3` keeps `stormsail`'s own wingbeat: its wing has different bones.

## Rules

- Write only these paths: `tools/skeletons/<name>.json`, `tools/<name>_*.py`, `assets/<name>*.glb`, `assets/<name>*.cfg`, `artifacts/<name>/`, and a scratch directory `build-rig-cache/` in the worktree. `<name>` is `frost3` or `storm3`. The `.glb` files are ignored by git: leave them in place and the main session collects them.
- Do not change `tools/rig_embercrest_candidate.py`, `tools/repair_model_materials.py` or any existing skeleton, script or profile file. If the rig script cannot do what a creature needs, copy it to `tools/<name>_rig.py`, change the copy, and record why in a comment beside the change.
- The candidate files and the siblings' `.glb` files in `assets/` are symlinks into the main checkout: never write through them.
- Use the `blender` command line only (`/opt/homebrew/bin/blender -b --factory-startup --python ...`). Do not use a Blender MCP server or a running Blender. Run Blender in the foreground, one process at a time.
- The material repair needs a Python environment with numpy, Pillow, scipy and trimesh with embree. Make one in `build-rig-cache/venv` with `uv venv` and `uv pip install numpy pillow scipy trimesh embreex`.
- Do not build the game engine. Do not edit engine source or docs outside the paths above.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Order: `frost3`, then `storm3`. Commit after each creature passes, on your branch. Stage explicit paths, never `git add -A`. Do not push.
- Write short plain sentences in the READMEs, active voice, no filler.

## Budget and stopping

One creature at a time, finished and committed before the next. At most three rig attempts a creature. If a creature cannot pass, write what failed in its README, commit what there is, and go on. Finish with a short report: for each creature the load line, its size against the hero, the flap check's worst number, what differs from the sheet, what you tuned in the profiles, what is still weak, and the minutes it took.
