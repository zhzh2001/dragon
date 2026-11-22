# Brief: rig the second-round elemental dragons

You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree. The task is Blender work, headless: turn static generated dragon meshes into rigged creatures the engine can fly.

The creatures you rig are named at the end of the prompt that pointed you at this file. Rig only those.

## The inputs

Each `assets/<name>-cand-oneshot.glb` is a Hunyuan one-shot: 1.5 million triangles, textured, about 1 unit long, static. In Blender after import it faces -Y with Z up and the wingspan along X. All have four legs, two wings of an arm and three fingers, a neck, a tail and a parted mouth.

| Name | Element | Design | Nearest sibling for profiles |
|---|---|---|---|
| `frost2` | frost | navy, ice horns and spikes, aurora wings | `frostvein` |
| `stone2` | stone | obsidian plates, amethyst crystals | `ironroot` |
| `storm2` | storm | slate grey, pale wings, tail fluke | `frostvein` for the rig, `stormsail` for the breath |
| `tide2` | tide | teal, sail fin, finned tail, webbed feet | `frostvein` for the rig, `tidewrack` for the breath |
| `blight2` | blight | black with yellow bands, amber wings, tail stinger | `blightmaw` |

The sheets they were generated from are `docs/concept/elemental-options/round2/<element>-turnaround.jpg`. Previews of the raw meshes are `artifacts/elemental-round2/<name>-preview.jpg`. Look at both first.

Known about these meshes: on four of the five the wings are swept back and held up, not spread level. The rig script levels a raised wing at bind (`wing_bind_level_deg` in the skeleton file). The wingspan is 0.9 to 1.2 of the body length.

## Read first

- `docs/MODEL_GENERATION.md`, section "The pipeline", steps 3 and 4, and "Adding a creature that is not a dragon" for what a skeleton file holds.
- `artifacts/frostvein/README.md`, `tools/frostvein_measure.py`, `tools/frostvein_landmarks.py` and `tools/skeletons/frostvein.json`: the one species built with its own measured skeleton from the start. Follow that route. Do not reuse a shared skeleton file: a shared file carries another animal's leg positions, `wing_field` gates and `jaw_mask`.
- `tools/rig_embercrest_candidate.py`: the decimate, fit, bind and export step.
- `tools/repair_model_materials.py`: the data-map repair.
- `docs/ANIMATION.md`: the bone-name contract with `src/anim/dragon_rig.cpp`, the standing stance, the wing fold, and "the frame/scale traps".
- `CLAUDE.md`, "A creature is a species, not just a mesh": the three profile files and the trap of a partial profile (a new species that lists only its character fields cannot close its wings or stand).

## What to deliver for each creature

1. `tools/skeletons/<name>.json`: its own measured skeleton. Record how you measured it in `artifacts/<name>/README.md`.
2. `assets/<name>-raw.glb`: decimated to 80,000 triangles and rigged, UVs and maps kept.
3. `assets/<name>.glb`: the same with the data maps repaired. Check the log's zone coverage against what the animal is made of (crystals and ice spikes are not horn).
4. `assets/<name>.glb.rig.cfg`, `.flight.cfg` and `.breath.cfg`: copied from the sibling in the table, with every block (stance, standing wing fold, flight legs), then tuned by render until the creature stands, folds its wings and flies without a fault. Set `element` in the breath file. Copy the three files beside the raw model too.
5. The engine's load line must read `mapped rig: neck N, tail N, wing root N/N, fingers N/N, legs N/N, front legs N/N, feet N, jaw jaw` with every chain mapped and no joint warning.

## Engine check, required

The main checkout at `~/src/game-claude` has a built engine. Run it read-only on your file, from the main checkout's directory, and write screenshots into your worktree. Write nothing else in the main checkout. The main session rebuilds the engine while you work: if a run fails to start, wait 60 seconds and run it again.

`cd ~/src/game-claude && ./build/dragon --headless --hide-ui --size 1920x1080 --sun 35,235 --model <worktree>/assets/<name>.glb <VIEW> --screenshot <worktree>/artifacts/<name>/NAME.bmp`

Convert each BMP to JPEG and delete the BMP. Views, all required:

| NAME | VIEW |
|---|---|
| bind | `--bind-pose --skeleton --inspect 90 14 6 --frames 40` |
| glide-side | `--studio 0 --inspect 90 14 6 --frames 200` |
| glide-above | `--studio 0 --inspect 90 20 85 --frames 200` |
| glide-front | `--studio 0 --inspect 0 14 6 --frames 200` |
| flap-rear | `--studio 1 --inspect 180 16 15 --frames 215` and again at `--frames 230` |
| flap-side | `--studio 1 --inspect 90 14 6 --frames 222` |
| head | `--studio 0 --inspect 90 3.5 0 --inspect-head --frames 200` |
| jaw | `--studio 8 --inspect 90 3.5 0 --inspect-head --frames 150` |
| landed | `--input -0.25,0,0,0,0,0 --inspect 60 11 3 --frames 900` |
| landed-rear | `--input -0.25,0,0,0,0,0 --inspect 180 14 6 --frames 900` |
| dive | `--studio 5 --inspect 90 16 6 --frames 300` |
| chase | `--frames 200` |

Look at every render at full size. A rig is not judged by its build statistics. Fix what you see: a wing that tears or folds through the body, a leg under the ground or in the air, a jaw that bends the muzzle, spikes or fins that smear with the wrong bone, a tail fin that crumples. Put a contact sheet of the twelve views at `artifacts/<name>/views.jpg`.

Sizes: the engine draws a model at the size its profile and skeleton give. Each creature must come out between 0.85 and 1.0 of the hero's body length. For the hero's size, render `assets/embercrest-hero.glb` in the main checkout with the same view and compare.

## Optional second pass, only after every creature passes the first

The wingspans are short for this game: the hero's is about 1.9 of its body length. If a creature's wing skins cleanly, widen it at the mesh stage (scale the wing along its own span about the shoulder, before binding) toward 1.6, refit and render again. Keep both results and say which is better. Drop it if the membrane or the fingers distort.

## Rules

- Write only these paths: `tools/skeletons/<name>.json`, `tools/<name>_*.py` (new helper scripts), `assets/<name>*.glb`, `assets/<name>*.cfg`, `artifacts/<name>/`, and a scratch directory `build-rig-cache/` in the worktree. The `.glb` files are ignored by git: leave them in place and the main session collects them.
- Do not change `tools/rig_embercrest_candidate.py`, `tools/repair_model_materials.py` or any existing skeleton or profile file. If the rig script cannot do what a creature needs, copy it to `tools/<name>_rig.py`, change the copy, and record why.
- The candidate files in `assets/` are symlinks into the main checkout: never write through them. The siblings' `.glb` files are symlinks too.
- Use the `blender` command line only (`/opt/homebrew/bin/blender -b --factory-startup --python ...`). Do not use a Blender MCP server or a running Blender. Run Blender in the foreground. One Blender process at a time in your job: the machine is shared, and another job like yours runs beside you.
- The material repair needs a Python environment with numpy, Pillow, scipy and trimesh with embree. Make one in `build-rig-cache/venv` with `uv venv` and `uv pip install numpy pillow scipy trimesh embreex`.
- Do not build the game engine. Do not edit engine source or docs outside the paths above.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Commit after each creature passes, on your branch. Stage explicit paths, never `git add -A`. Do not push.
- Write short plain sentences in the READMEs, active voice, no filler.

## Budget and stopping

One creature at a time, finished and committed before the next. If a creature cannot pass after three rig attempts, write what failed in its README, commit what there is, and go on. Finish with a short report: for each creature the load line, its size against the hero, what you tuned in the profiles, what is still weak, and the minutes it took.
