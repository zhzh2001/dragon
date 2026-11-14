# Brief: build the hero dragon's mesh from two generated sculpts

You work in a git worktree of a C++ game engine (a dragon flight game). Your current directory is that worktree. You build one game-ready, unrigged dragon in headless Blender from two AI-generated sculpts. The result is judged by eye on renders, against the design, so look at your renders and keep working until they are right.

## Read first

- `docs/SHOWCASE.md`: "2. The dragon is one mesh with one material", "The hero dragon's design", "The first candidate", "The head, generated on its own", and "The hero dragon, as a specification".
- `docs/concept/showcase/hero-dragon-design.png` and `hero-dragon-views/*.png`: the design. `artifacts/showcase-study/hero-head-plates/*.png`: the head.
- `docs/MODEL_GENERATION.md`, sections "What the engine needs from an asset" and "The pipeline": what the engine loads, and what earlier builds learned.
- `tools/rockkit.md` and `tools/build_rockkit.py`: how the last asset task baked maps and wrote its record.

## Inputs

- `assets/embercrest-cand-hero-hunyuan.glb`: the whole dragon. 1,500,000 triangles, one 4K PBR set. The body, legs, wings and tail are good. Its head is a smear with no eye. Do not use its head.
- `assets/embercrest-cand-hero-head-hunyuan.glb`: the head and upper neck alone. 1,500,000 triangles, one 4K PBR set. It has the eye, lids, scale plates, horns and lip line.
- Both are symlinks to large files. Read them; never modify or delete them.

## What to build

`assets/embercrest-hero-static.glb`: one dragon, unrigged, in the engine's frame (wings spread, forward at -Z, +Y up, metres, 19 m wingspan, feet at Y=0).

1. Join. Scale and place the head sculpt on the body sculpt so the necks agree in width, direction and colour, cut both at a clean ring on the neck, and blend the seam in geometry and in texture. From 3 m the seam must not be findable.
2. Game mesh. Reduce the joined sculpt to 80,000 to 120,000 triangles for the hide. Spend the triangles where the camera goes: the head gets at least a quarter of them. Keep the silhouette of the horns, the frill spines, the back spines, the claws and the wing fingers. Keep the wing membrane thin and two-sided without holes. Decimation is acceptable where a clean retopology is out of reach, but the mesh must have supporting loops around the eyes, along the lip line, and at each limb joint and wing finger joint, because it will be skinned and animated next. Say in the record which regions have real loops and which are decimated.
3. Separate parts, each its own mesh and material in the one file:
   - `eye_l`, `eye_r`: real eyeballs behind the lids. A sphere each, with an amber iris, a vertical slit pupil, and a cornea bulge that catches a highlight. Cut or push back the sculpt's painted eye so the ball shows through the lid opening. The eyes must be able to rotate without clipping the lids.
   - `mouth`: cut the lip line open so the jaw can drop 35 degrees later. Add the mouth interior: upper and lower gums, a row of teeth each, a tongue, and a dark throat. With the jaw closed nothing of it shows, and the lips meet as they do in the sculpt.
   - `membrane`: the wing membranes as their own material, split from the hide along the arm and finger bones.
   - `horn`: the four horns and the claws, as their own material.
4. Maps. Bake from the two sculpts to the game mesh: base colour, tangent-space normal, and occlusion-roughness-metallic packed as glTF expects. Hide at 4K with the head given at least a quarter of the UV area; membrane 2K with an extra thickness map (thin between the fingers, thick near the bones) stored as `assets/embercrest-hero-membrane-thickness.png`; horn 1K; eye 512; mouth 1K. Horn and claw roughness about 0.3, hide 0.5 to 0.7 varying with the scales, eye cornea 0.05, membrane 0.6. Remove baked sunlight from base colours. Export tangents.
5. Check. `baseColorFactor` is ignored by the engine, so all colour is in textures. Triangles only, no Draco. The file must be at most 60 MB.

## Deliver

- `assets/embercrest-hero-static.glb` and the thickness map.
- `tools/build_hero.py`: rebuilds everything from the two inputs with `blender -b --factory-startup --python tools/build_hero.py`, with `-- --verify-only` and `-- --render`. The verifier asserts the triangle budget, the five materials and their maps, tangents, the frame and scale, the file size, that the eyeballs are spheres that clear the lids through a rotation of 25 degrees each way, and that the mouth interior is hidden when closed. It fails loudly.
- `artifacts/hero-build/`, 1920x1080 renders under a sun at 30 degrees with a sky light: `turnaround.png` (side, front, rear, top, three-quarter), `head.png` (the head from the side and three-quarter at a distance that fills the frame), `eye.png` (one eye filling a third of the frame), `mouth.png` (the lower jaw vertices rotated open 35 degrees by a temporary transform, to show the interior), `seam.png` (the neck seam from two sides), `wire.png` (the wireframe of the head and of one wing hand), and `compare.png` (your head beside `docs/concept/showcase/hero-dragon-design.png`'s inset and beside the head sculpt).
- `tools/hero-build.md`: the record. What was done at each step, the numbers, the decisions, what is weak, and what the rigging step must know (where the jaw hinge is, the eye centres, the joint loop positions, all in the file's coordinates). Short plain sentences, active voice, no filler.

## Rules

- Write only: `assets/embercrest-hero-static.glb`, `assets/embercrest-hero-membrane-thickness.png`, `tools/build_hero.py`, `tools/hero-build.md`, `artifacts/hero-build/`, `build-hero-cache/` (add it to `.gitignore`). Never write outside this worktree. Do not touch engine source or other assets.
- Use `/opt/homebrew/bin/blender` headless only. No Blender MCP, no running Blender.
- Run every Blender command in the foreground with a timeout long enough to finish (up to 10 minutes; split a longer job into cached stages). Do not start it in the background and wait: an idle wait longer than a few minutes is expensive. Cache each stage in `build-hero-cache/` so an iteration re-runs only what changed.
- Convert a render to a JPEG of at most 1600 px before you look at it.
- Do not build or run the game. Do not ask questions; decide, and record the decision.
- If a step cannot reach the brief (for example the mouth cut), deliver the best state that verifies, and say exactly what is missing and why.
- Commit on the current branch when the verifier passes and you have looked at every render. Stage explicit paths. The `.glb` is not ignored by git here; commit it. End the commit message with: Co-Authored-By: Claude Mythos 5.2 <noreply@anthropic.com>
- Finish with a short report: what you built, the verifier's output, what is weak, and how long it took.
