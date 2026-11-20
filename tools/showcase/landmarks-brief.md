# Brief: a landmark kit and a hoard pile, from Tripo sources

You work in a git worktree of a C++ game engine (a dragon flight game over an alpine valley). Your current directory is that worktree. The task is Blender work only, headless.

## Goal

The valley has rock, trees and a river, and nothing built or old in it except the run's two towers. A landmark kit gives a flight something to pass: a ruined keep on a bench, a bridge over the river, standing stones in a meadow, a dragon skeleton on a gravel bar. The engine will place each landmark a few times in a valley and draw it with the rock kit's renderer, so the kit follows the rock kit's file contract.

Read first: `tools/rockkit.md`, sections "File contract" and "Tripo pieces (2026-11-16)" to the end; `tools/build_rockkit.py` (import its routines; do not edit it); `tools/props.md`, section "Third generation: Tripo (2026-11-16)"; `docs/MODEL_GENERATION.md`, section "The second spend (2026-11-16, day)"; and the contact sheets `artifacts/tripo-overnight/day2-landmarks.jpg`, `props.jpg` and `wood-trees.jpg`.

## Sources

The raw files are read-only inputs in `assets/tripo-raw/` (a symlink; never write there).

## Part 1: the landmark kit

Deliver `assets/props/landmarks/`, built by a new script `tools/build_landmarks.py`, with a record `tools/landmarks.md`.

Pieces, each from the named source:

| Piece | Source | Size | Work |
|---|---|---|---|
| `keep_ruin` | `tripo-ruin-keep-01` | 22 m to 28 m wide | Keep the rubble skirt |
| `tower_ruin` | `tripo-ruin-tower-02` | 20 m to 26 m high | A blue disc is baked inside the open top. Replace it with dark stone rubble colour, or close the top with a dark floor below the rim |
| `bridge` | `tripo-bridge-03` | 40 m long, the span along X | The engine scales it to the river. Both ends must reach Y=0 or below |
| `arch_ruin` | `tripo-arch-04` | 12 m to 16 m high | |
| `stone_circle` | `tripo-standing-stones-05` | 24 m to 30 m across | No ground disc. If the source has one, cut it away and keep the stones as separate islands of one mesh |
| `monolith` | `tripo-monolith-06` | 10 m to 14 m high | |
| `cairn` | `tripo-cairn-15` | 5 m to 7 m high | |
| `dragon_bones` | `tripo-bones-16` | 30 m to 36 m long | Cut the sandy slab away. The bones must stand on Y=0 with their lowest parts 0.3 m below it |
| `snag` | `tripo-snag-01` | 9 m to 13 m high | Optional. A dead standing tree |
| `log` | `tripo-log-01` or `tripo-log-02` | 8 m to 12 m long | Optional. A fallen trunk |

Requirements:

- The rock kit's file contract: glTF binary, Y-up, metres, +Z forward, one node with no transform, one mesh, one triangle primitive, one material, the origin at the centre of the base at Y=0, `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0`, three embedded maps (base colour JPEG sRGB, tangent-space normal PNG in the OpenGL convention, one packed JPEG with occlusion in R, roughness in G, metallic 0 in B, shared by the occlusion and metallic-roughness slots). Maps 2048 for LOD0, 1024 for `_lod1`, 512 for `_lod2`, one atlas layout per piece.
- Three levels of detail: LOD0 at most 24,000 triangles (12,000 for the cairn, the monolith, the snag and the log), `_lod1` about 25%, `_lod2` about 6%. The silhouette must hold. An arch must stay open and a window must stay a hole at LOD1.
- The base: sink each piece so that its lowest 3% is below Y=0, and close no hole that the ground hides. The engine buries the base in uneven ground.
- Colour: do not apply the rock kit's grade. Keep each source's own colour. Remove directional light if a source has it. Masonry must not be brighter than linear 0.45 in its mean albedo, and bone not brighter than 0.60.
- Placement data: `assets/props/landmarks/pieces.json` in the schema of `assets/props/rockkit/pieces.json` (`name`, `kind` set to `landmark`, `size`, `source`, check points from LOD2, at most 26). For the bridge, also give `abutments`: the two points at the ends of the deck's centre line, and `deck_height`: the deck's highest Y on that line.
- The whole of `assets/props/landmarks/` must be at most 110 MB.

Verification in the script (`-- --verify-only`): triangle counts, bounds, origin, the three maps, tangents, the size limit, the albedo limits. It must fail loudly.

Renders in `artifacts/landmarks/`, 1920x1080, a sun at 20 degrees elevation with a sky light, on a neutral green-brown ground: each piece from two angles near and from 250 m, and `lods.png` with the keep and the arch at all three levels. Look at every render at full size and fix what reads wrong. Reject a piece that still reads wrong after two passes, and record why.

## Part 2: the hoard pile

`tripo-hoard-10` (coins, goblets and crowns on a low rock) is the candidate to replace `assets/props/hoard_pile.glb`. Two earlier conversions of other hoards failed (`tools/props.md`: crumpled shading, stretched projection, a normal-projection coverage gate). Use `tools/build_props.py --from-tripo` and `tools/tripo_props.py`. The props contract applies (one skin, one `root` bone, identity transforms), and so do the existing dimension checks for `hoard_pile`. This prop needs a real metallic channel for the gold: bake metallic-roughness from the source.

Replace `assets/props/hoard_pile.glb` only if the result passes the checks and reads better than the current file in the near and the 250 m renders. If not, keep the current file, and record the attempt with the renders. Budget: two passes.

## Record

`tools/landmarks.md`: the table of pieces (source, size, triangles per LOD, mean linear albedo), what was done to each, rejected pieces with reasons, the rebuild commands, the verification output, what is weak. `tools/props.md`: a short section for the hoard attempt. Write short plain sentences, active voice, no filler. Add one line per accepted Tripo source to `AI_DISCLOSURE.md` in the style of its existing Tripo lines, and a line for the landmark kit to the table in `ATTRIBUTION.md` beside "Tripo additions in `assets/props/rockkit/`".

## Rules

- Write only these paths: `assets/props/landmarks/`, `assets/props/hoard_pile.glb`, `tools/build_landmarks.py`, `tools/landmarks.md`, `tools/build_props.py`, `tools/tripo_props.py`, `tools/props.md`, `artifacts/landmarks/`, `artifacts/props/v4/`, `build-landmarks-cache/` (add it to `.gitignore`), `AI_DISCLOSURE.md`, `ATTRIBUTION.md`. Do not edit `tools/build_rockkit.py` or `assets/props/rockkit/`. Do not touch engine source or other docs. Never write outside this worktree. Another job runs in a sibling worktree at the same time; do not read or write it.
- Use the `blender` command line only (`/opt/homebrew/bin/blender`). Do not use a Blender MCP server or a running Blender. Run Blender in the foreground. Run at most one Blender process at a time: the machine is shared.
- Do not build or run the game engine. Engine integration is a later task.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Commit after each part passes, on the current branch. Stage explicit paths, never `git add -A`. Do not push.
- Finish with a short report: the accepted pieces, the verification output, what is weak, and the minutes it took.
