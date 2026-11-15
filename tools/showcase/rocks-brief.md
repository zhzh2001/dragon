# Brief: the scanned rock kit in the world (S6, first part)

Work only in this worktree (`~/src/game-claude-wt/rocks`, branch `world-rocks`). Do not write
in `~/src/game-claude`. Commit on this branch with explicit paths, never `git add -A`. Run builds
and renders in the foreground. End commit messages with `Co-Authored-By: Claude Opus <noreply@anthropic.com>`.

## Why

The playtester marked the valley's rocks: "rocks look unnatural" (feedback report 0011,
`artifacts/showcase-feedback/0011-marked.jpg`). The world's rocks are six untextured shapes, generated or from
`assets/props/rocks.glb`, coloured by one palette entry through the plant shader. A kit of eight scanned pieces
with three levels of detail each is in `assets/props/rockkit/` (`tools/rockkit.md` has the file contract:
metres, Y up, base at Y 0, base colour, normal and occlusion-roughness maps, 2048 / 1024 / 512). It has never
been drawn in the world. Your job is to draw it there on the modern tier.

## Read first

`CLAUDE.md` in full: the conventions (rendering, lighting is one path, shaders, GPU calls through `rhi::Device`,
paths through `core/paths.h`) are rules, not background. Then `docs/WORLD.md` ("Row 7: rocks"),
`src/gfx/foliage.h` and `foliage.cpp` (`set_rock_mesh`, `set_rocks`, the `StaticSet`, the cells, culling, draw
distances, the shadow pass), `shaders/foliage.hlsl`, `src/game/vegetation.cpp` (`place_rocks`), `App::load_rocks`
in `src/app.cpp`, `gfx::load_static_gltf`, and `shaders/skinned.hlsl` for how a textured surface uses
`direct_sun`, `ambient_light`, `apply_fog`, the normal map and the ORM map.

## What to build

1. A textured, instanced rock path for the modern tier. A rock keeps riding the foliage cells, culling and draw
   distances. It needs a vertex with a UV and a tangent, the three maps bound, and a shader (`shaders/rock.hlsl`
   with its depth-only variant if the shadow pass needs one) that lights through the shared path:
   `sun_visibility`, `direct_sun`, `ambient_light` (times `terrain_sky` where the others do), `apply_fog`,
   `scene_out`. No per-shader ambient or wrap. The shader must stay inside what the D3D9 bake accepts (the
   Shaders paragraph of `CLAUDE.md`); if a retro tier cannot run it, the retro tiers keep the present rocks
   exactly as they are, chosen by `tier_.tier == gfx::Tier::Modern`.
2. The kit as the rock kinds. Map the eight pieces onto kinds (the count can change from 6; find every use of
   `ROCK_KINDS`). Use a piece's three LODs by distance. Texture memory: load the 1024 maps as the largest
   (LOD1's), not the 2048, unless you measure that the 2048 fit comfortably; say what you measured.
3. Placement that reads as geology (`place_rocks`). Boulders and slabs: sunk 15 to 30% into the ground, tilted
   part of the way to the slope, yaw random, scale varied, in loose groups with a large one and a few small ones
   beside it rather than one per grid cell. Cliff pieces: only on steep faces, their face (+Z) turned out along
   the slope's fall line, sunk well in, so they read as outcrops of the wall and not as objects placed on it.
   Nothing may float: check the lowest corner of each piece's footprint against the terrain. Keep the count and
   the cost near the present ones (about 3,250 rocks at a 48 m spacing).
   One random draw per statement (see `CLAUDE.md`, "Cross-cutting lessons").
4. Colour that belongs. The scans come from several places and are different greys and browns. Pull each
   toward the terrain's own rock colour (the palette's rock entry, and the tint the terrain shader gives steep
   ground) so a boulder looks broken from this mountain: match mean albedo, keep the scan's contrast. A moss or
   grass tint on up-facing parts near the valley floor is welcome if it is cheap.
5. Sliders for what you add (Terrain > Vegetation, beside the present rock dials).

## Check and judge

- `cmake --build build`, `ctest --test-dir build` (26 suites), and
  `~/.local/py314/bin/python3 tools/golden/golden.py compare`. The goldens will differ where rocks are in
  frame. Look at the diffs; if every difference is a rock, capture them again (`golden.py capture`) and say so.
- A retro tier still starts and draws its old rocks: `./build/dragon --headless --tier sm2 --frames 30
  --screenshot ...` and look at it.
- Cost: `gpu: median` from three headless runs before and after, same views, 1920x1080. Say the numbers.
- Pairs at full size, old on the left, in `artifacts/showcase-s6/rocks-*.jpg`, from at least:
  the report's camera (`--cam 333.36,98.76,-2119.04,313.80,94.73,-2117.97`), a close view of a boulder group
  on the valley floor, a close view of an outcrop on a steep face, a run valley from the chase camera
  (`--run 3 --frames 200`), and one from high up (`--run 3 --frames 60 --cam 0,420,900,0,150,-600`).
  Use `--model assets/embercrest-hero.glb --hide-ui`.
  Look at each yourself. A rock that floats, a cliff piece that reads as a pasted box, a colour that does not
  belong to the mountain, or a hard line where a piece meets the ground is not done: fix it before you report.
- Update `docs/WORLD.md` (what rocks are now) and add a section to `docs/SHOWCASE.md` before
  "## S4 and S5, first pass" with what you built, measured and left open.

## Report

The commits, the test and golden results, the cost numbers, each pair's path with one line on what it shows,
what is worse than before, and what you left open. Budget: about two and a half hours.
