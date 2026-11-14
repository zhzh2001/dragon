# Brief: a rock and cliff kit from CC0 scans

You work in a git worktree of a C++ game engine (a dragon flight game over an alpine valley). Your current directory is that worktree. Build a kit of rock assets in Blender, headless. Two workers receive this same brief in separate worktrees, and the results are compared on renders, so the quality of the picture matters more than speed.

## Goal

The engine's mountains are a smooth heightfield. The kit gives it real rock: boulders for scree, and cliff pieces that are set into steep slopes to make cliff bands. The look to reach is `docs/concept/showcase/slice-target-ceiling.png` and `slice-target-floor.png` (read both; look at the valley walls). Read `docs/SHOWCASE.md`, sections "4. The mountains are a heightfield" and "Order" row S6, and `tools/props.md` for how an earlier asset task was built and recorded.

## What to deliver

Eight pieces in `assets/props/rockkit/`, each a `.glb`:

- `boulder_a`, `boulder_b`, `boulder_c`: 2 m to 6 m across, distinct shapes.
- `slab_a`, `slab_b`: flat or tilted slabs, 4 m to 10 m long.
- `cliff_a`, `cliff_b`, `cliff_c`: wall segments 20 m to 40 m wide and 15 m to 30 m high, with strata or fractures that read from 300 m away. They are pushed into a slope of 45 to 70 degrees, so the back can be open.

Source the geometry and textures from Poly Haven's CC0 models and textures (https://api.polyhaven.com; the API returns 403 without a User-Agent header, so send a generic one such as `rockkit-build/1.0`, with no personal details). You can scale, cut, combine and re-sculpt scans. A piece built by script from a displaced primitive with a Poly Haven rock texture set is also allowed if it looks better. Nothing with a licence other than CC0 can go in.

Requirements for each piece:

- glTF binary, Y-up, metres, +Z forward, origin at the centre of the base at ground level, all transforms applied.
- Three levels of detail in three files: `name.glb` (boulders and slabs at most 8,000 triangles, cliffs at most 20,000), `name_lod1.glb` (about 25%), `name_lod2.glb` (about 6%). The silhouette must hold at each level.
- One mesh, one material, with a base colour map, a tangent-space normal map baked from the full-detail scan, and an occlusion-roughness-metallic map packed as glTF expects (R occlusion, G roughness, B metallic 0). 2K maps; a cliff can use 4K. Tangents exported.
- No baked sunlight or directional shadow in the base colour. Remove it if the scan has it.
- The eight pieces must look like one kind of rock. Grade the base colours to one grey-brown alpine rock so the mean albedo of every piece is within 8% of the kit's mean.
- The whole of `assets/props/rockkit/` must be at most 80 MB. JPEG is allowed for base colour and ORM, PNG for normals.

Also deliver:

- `tools/build_rockkit.py`: one script that rebuilds everything with `blender -b --factory-startup --python tools/build_rockkit.py`. It caches downloads in `build-rockkit-cache/` (add that to `.gitignore`), and has `-- --verify-only` and `-- --render` modes. Verification asserts triangle counts, bounds, origin, the three maps, tangents, the albedo spread and the size limit, and fails loudly.
- `artifacts/rockkit/`: `sheet.png`, every piece at LOD0 from two angles on a neutral ground under a sun at 20 degrees elevation with a sky light; `wall.png`, the three cliff pieces set side by side into a 60 degree slope plane 120 m wide with the boulders and slabs as scree below, seen from 250 m away at the camera height of the cliff's middle, same light; `lods.png`, one boulder and one cliff at all three levels. Render at 1920x1080. Look at your renders and iterate until the wall reads as a cliff band, not as objects placed on a plane.
- `tools/rockkit.md`: the record. The Poly Haven asset ids used, what was done to each, the table of pieces with size and triangle counts, how to rebuild, what you measured, and what is weak. Write it in short plain sentences, active voice, no filler.

## Rules

- Write only these paths: `assets/props/rockkit/`, `tools/build_rockkit.py`, `tools/rockkit.md`, `artifacts/rockkit/`, `build-rockkit-cache/`, and one line in `.gitignore`. Do not touch engine source, other assets or other docs. Never write outside this worktree.
- Use the `blender` command line only (`/opt/homebrew/bin/blender`, version 5.x). Do not use any Blender MCP server or a running Blender.
- Do not build or run the game engine. Engine integration is a later task.
- Do not wait on standard input. Do not ask questions; decide, and record the decision in `tools/rockkit.md`.
- When the verification passes and you have looked at the three renders, commit everything on the current branch with a descriptive message. Stage explicit paths, never `git add -A`.
- Finish with a short report: what you built, the verification output, what is weak, and roughly how long it took.
