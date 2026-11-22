# Brief: version 7 of the hero dragon, wings with thickness

You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree. The task is Blender work, headless, on the hero dragon's wings and their rig.

## The fault

The playtester on the hero's wings: "they are like paper". The playtester does not mean the outline. A version 6 with a broader outline was built on 2026-11-21 and withdrawn the same day. The outline, the span and the chord of version 5c stay.

Look first at `artifacts/hero-build/v7-fault.jpg` (the current wings in the chase view, edge-on from the front, close from above, and from under in the beat). What reads as paper:

- The membrane is a shell 19.8 mm thick on a wing 9 m long. Edge-on it is a line. The trailing edge is a knife cut.
- Each bone is a rod laid on a flat sheet. The sheet meets the rod at a hard seam.
- A panel between two fingers is a plane with one smooth orange colour. It has no relief that catches the light, and its value hardly changes from the bone to the middle.

The target is hide stretched over bone: a heavy leather wing. A thick dark skin wraps each bone and thins toward the middle of a panel. The trailing edge has a rolled hem. Tension folds run from the joints and catch the light.

## Read first

- `tools/hero-build.md`, sections "Version 5c", "Version 5b", "Version 5", "Commands", "File contract" and "For the rigging step". The record of the mesh and its staged script `tools/build_hero.py`.
- `tools/hero-rig.md`, sections "Files and commands", "Bone contract", "Weights: Wings", "Checks that changed". The rig script is `tools/rig_hero.py`. It finds the membrane's two faces as twins `SHEET_GAP` apart: a thicker shell must still pass through that code, changed as needed.
- `docs/showcase/HERO.md`, sections "A membrane that billows" and "Fingers that bend". The engine bows each panel in the vertex shader from the bind pose (`App::mark_membranes`). It moves both faces together, so a thicker shell follows.
- `docs/showcase/CRITIC.md`, what rounds 4 to 7 say about the wings.

## Start state

`build-hero-cache/` holds stage outputs from the withdrawn version 6. The scripts in this worktree are version 5c's again. Before you measure anything, run every stage once (`blender -b --factory-startup --python tools/build_hero.py`, about 5 minutes) and the rig script. The 86 mesh checks and the 56 rig checks must pass on that rebuild. If they do not, stop and report why.

## What to build

1. Thickness. Measure between the two faces of the membrane:
   - within 0.15 m of a bone (arm, finger, rib) or of the body: 60 mm to 90 mm;
   - at the middle of a panel: 30 mm to 40 mm;
   - a smooth change between them, no step.
2. Hem. Along the whole trailing edge, a rounded bead 60 mm to 80 mm across, with at least six segments round its section. No knife edge anywhere on the wing's outline. Seen edge-on from the front at 9 m (`--inspect 180 9 3`), the trailing edge must draw as a band, not a line.
3. Skin over bone. Where the membrane meets a finger, the arm or the rib, both faces rise onto the bone over 120 mm to 180 mm. The bone reads as a ridge under one skin. No hard seam, no gap, no z-fighting strip. A finger tapers to a third of its wrist thickness at its tip, and its tip ends in the hem.
4. Camber. A panel between two spars sags 3% to 4% of its width at rest, downward. The engine adds more under load.
5. Shading normals. The membrane's vertex normals are smooth across a panel and across the fillet. No flat facet: the angle between a vertex normal and any face it belongs to is at most 25 degrees, except round the hem.
6. Relief, in the normal map. Tension folds fan from the wrist, the elbow and each finger joint along the lines of pull, 20 mm to 60 mm apart, plus raised veins that branch from the bones. They must show at 15 m in the chase view. Measure the mean tilt of the normal map over the panels before and after. It must at least double.
7. Colour, in the base map. The skin is darker and less saturated within 0.2 m of a bone and near the body, and lighter in the thin middle of a panel. The value range across one panel, bone to middle, is at least 35% in linear colour. Veins are darker than the skin round them. Keep the hue family and keep the upper face darker than the under face. No region with one plain colour.
8. Roughness. 0.45 on the stretched thin skin to 0.80 on the thick skin and in the folds.
9. Thickness map. Regenerate `assets/embercrest-hero-membrane-thickness.png` from the new shell. White is thick.
10. Budget. The membrane can use up to 24,000 triangles (7,992 today). Hide, membrane and horn together stay under 136,000.
11. The body, head, legs, tail, horns and their maps do not change. The wing's outline, span (19 m within 0.1 m) and bone positions do not change.

Choose how, and record the choice. The sculpt `assets/embercrest-cand-hero-wing2-hunyuan.glb` is the source of the present surface detail.

## The rig

- Rebuild the rigged file with `tools/rig_hero.py`. The bones do not move.
- The bone contract holds: every bone name stays, and the engine's log line must still read `mapped rig: neck 3, tail 8, wing root 3/3, fingers 3/3, legs 3/3, front legs 3/3, feet 4, jaw jaw`.
- The mesh's checks (86) and the rig's checks (56) must pass. A check that fails only because the shell is meant to be thicker (the twin distance, a triangle budget) can change. Record each one with the old and the new number. The fold check does not change: the longest folded membrane edge stays at most 1.6 of rest.
- The two faces of the shell must stay apart through the beat, the dive fold and the ground fold. Add a check: in each of those three poses, no upper-face vertex passes through the lower face.

## Engine check, required

The main checkout at `~/src/game-claude` has a built engine. Run it read-only on your rigged file, from the main checkout's directory, and write screenshots into your worktree. One engine process at a time. Write nothing else in the main checkout.

`cd ~/src/game-claude && ./build/dragon --headless --hide-ui --size 1920x1080 --sun 35,235 --model <worktree>/assets/embercrest-hero.glb <VIEW> --screenshot <worktree>/artifacts/hero-build/v7-NAME.bmp`

The main session can rebuild the engine while you work. If a run fails to start, wait 60 seconds and run it again. Render the two halves of a pair one after the other. Convert each BMP to JPEG and delete the BMP. The rig profile beside your model is read: the worktree's `assets/embercrest-hero.glb.rig.cfg` and `.flight.cfg` are tracked and current.

Views, each beside the same view of the main checkout's `assets/embercrest-hero.glb` (version 5c) as a pair, old on the left, cropped to the dragon and not scaled down:

| NAME | VIEW |
|---|---|
| chase | `--frames 200` |
| edge | `--studio 0 --inspect 180 9 3 --frames 200` |
| close | `--studio 0 --inspect 120 7 30 --frames 200` |
| hem | `--studio 0 --inspect 20 8 10 --frames 200` |
| under | `--studio 1 --inspect 200 10 2 --frames 222` |
| above | `--studio 0 --inspect 90 20 80 --frames 200` |
| rear-beat | `--studio 1 --inspect 160 16 15 --frames 230` |
| landed | `--input -0.25,0,0,0,0,0 --inspect 60 11 3 --frames 900` |
| landed-rear | `--input -0.25,0,0,0,0,0 --inspect 180 14 6 --frames 900` |
| dive | `--studio 5 --inspect 90 16 6 --frames 300` |

Look at every pair at full size. The test is the chase pair and the edge pair: if the new wing does not plainly read as thicker and heavier there, the work is not done. Make the change larger inside the ranges above before you make it finer.

## Budget and stopping

Two passes. Pass 1: shell thickness, hem, skin over bone, smooth normals, rig, engine pairs. Commit. Pass 2: relief, colour, roughness, thickness map, and the faults pass 1's pairs show. Commit. If the fold cannot pass with the full thickness, reduce the thickness until it does, deliver that, and record the thickness that failed and why.

## Record

- `tools/hero-build.md`: a new first section "Version 7: wings with thickness (2026-11-22)" in the style of "Version 5c": state, what changed, the measured numbers (thickness near a bone and at mid panel, hem width, normal-map tilt before and after, value range, triangle counts), how, the checks that changed, what is weak.
- `tools/hero-rig.md`: a section "Mesh version 7 (2026-11-22)" with the verifier's output and what changed in the twin handling.
- Write short plain sentences, active voice, no filler.

## Rules

- Write only these paths: `tools/build_hero.py`, `tools/rig_hero.py`, `tools/hero-build.md`, `tools/hero-rig.md`, `assets/embercrest-hero.glb`, `assets/embercrest-hero-static.glb`, `assets/embercrest-hero-membrane-thickness.png`, `artifacts/hero-build/v7-*`, `artifacts/hero-rig/v7-*`, `build-hero-cache/`. The `.glb` files are ignored by git: leave them in place in the worktree and the main session collects them.
- `build-hero-cache/` in the worktree is a copy of the stage caches. The sculpts in `assets/embercrest-cand-hero-*.glb` are symlinks: never write through them. Never write outside this worktree, except the engine screenshots, which go into the worktree anyway.
- Do not change `assets/embercrest-hero.glb.rig.cfg`. If the ground fold or the dive fold fails with the thicker shell, report the pose and the key you would change.
- Use the `blender` command line only (`/opt/homebrew/bin/blender`). Do not use a Blender MCP server or a running Blender. Run Blender in the foreground. One Blender process at a time: the machine is shared.
- Do not build the game engine. Do not edit engine source or other docs.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Stage explicit paths, never `git add -A`. Do not push.
- Finish with a short report: the measured thickness, hem and relief against version 5c, the checks' output, what is weak, and the minutes it took.
