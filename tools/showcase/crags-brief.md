# Brief: wall crags and more rocks for the rock kit, from Tripo sources

You work in a git worktree of a C++ game engine (a dragon flight game over an alpine valley). Your current directory is that worktree. The task is Blender work only, headless.

## Goal

The valley walls are a smooth heightfield. The rock kit (`assets/props/rockkit/`, record in `tools/rockkit.md`) has boulders, slabs, three cliff shells, and six Tripo pieces. No piece reads as a valley wall. Long Tripo wall pieces failed three times (`tools/rockkit.md`, "Rejected candidates"). The shape that works is one massive lopsided crag. The engine will stand a row of such crags against a steep face, each buried at the back, so that the row reads as a cliff band from 150 m to 600 m away.

Read first: `tools/rockkit.md` from "Tripo pieces (2026-11-16)" to the end, `tools/build_rockkit.py`, `docs/MODEL_GENERATION.md` section "The second spend (2026-11-16, day)", and the contact sheets `artifacts/tripo-overnight/day2-walls.jpg` and `day2-crags-rocks.jpg`.

## Sources

The raw files are read-only inputs in `assets/tripo-raw/` (a symlink; never write there).

Wall crag candidates: `tripo-crag2-02`, `tripo-crag2-05`, `tripo-crag2-06`, `tripo-crag2-07`, `tripo-crag2-08`, `tripo-wall2-05`, `tripo-wall2-07`, `tripo-wall2-08`, `tripo-crag-02`.

Rock candidates: `tripo-pinnacle-27`, `tripo-outcrop-12`, `tripo-outcrop-31`, `tripo-outcrop-32`, `tripo-talus-23`, `tripo-riverbank-21`, `tripo-riverbank-22`, `tripo-slab-25`, `tripo-tor-16`, `tripo-boulder-29`, `tripo-boulder-33`.

## What to deliver

Extend `tools/build_rockkit.py` with the existing Tripo path. Do not change the output of the 14 existing pieces: their 42 files must stay byte-identical, and the record must show the hash check.

Wall crags: at least five and at most eight pieces, named `crag_a` to `crag_h`, kind `wall_crag` in `pieces.json`.

- Height 45 m to 60 m. Scale uniformly. Do not stretch one axis more than 15% against another.
- Yaw each source so that its best face looks along +Z. The engine buries the -Z third of the piece in a slope of 40 to 62 degrees, so a mirrored or weak back is acceptable.
- Reject a source with a flat cut top, a masonry or tile pattern, or a black cavity on the +Z face. A top that a camera sees from above must be broken rock.
- LOD0 at most 24,000 triangles, `_lod1` about 25%, `_lod2` about 6%. The silhouette must hold (use the existing overlap check).
- The existing base cut, atlas bake, light removal, grade and map contract apply. All pieces must stay one kind of rock: the existing albedo target and spread check apply to the new pieces too.
- The crags must differ from each other in outline. If two read as the same shape from +Z, keep one.

Rocks: at most six more pieces, chosen from the rock candidates by render. Suggested names and kinds: `pinnacle_a` (crag), `outcrop_a`, `outcrop_b` (crag), `talus_a` (mound), `bank_a` (a riverbank strip; kind `bank`), `slab_c` (slab). Keep each near its natural size: a pinnacle 18 m to 26 m high, an outcrop 14 m to 24 m wide, a talus mound 16 m to 24 m wide, a bank strip 16 m to 24 m long, a slab 6 m to 10 m. Cut a plinth, a baked grass base or a separate base slab away. Skip a candidate that needs more than that.

Placement data: append every accepted piece to `assets/props/rockkit/pieces.json` with the existing schema (`name`, `kind`, `size`, `source`, `placement`, check points from LOD2, at most 26). For a wall crag, add `rear_points`: at most 12 points of LOD2 with z < 0, spread over the height, farthest-point sampled. The engine uses them to measure how deep the back sits in a slope.

Renders, in `artifacts/rockkit/tripo2/`, 1920x1080, a sun at 20 degrees elevation with a sky light, on a neutral ground:

- Each accepted piece from the front (+Z), the side, above, and close.
- `row-250.png` and `row-80.png`: every accepted wall crag side by side in one row, 15% overlap in X, random yaw within 20 degrees, each set into a 55-degree slope plane until its base check points are under the plane, on a slope 400 m wide coloured like the kit's rock target. Camera 250 m and 80 m from the row, at the height of the row's middle.
- `row-top.png`: the same row from 60 degrees above the horizon.

Look at every render at full size. The row must read as a cliff band, not as objects on a plane. If it does not, change the selection, the yaw or the scale, and render again. Record what you rejected and why, with the evidence render.

Record: add a section "Wall crags and more rocks (2026-11-20)" at the end of `tools/rockkit.md`. Include the table of accepted pieces (source, size, triangles per LOD, mean linear albedo), the rejected candidates with reasons, the rebuild commands, the verification output, and what is weak. Write short plain sentences, active voice, no filler. Add one line per accepted Tripo source to `AI_DISCLOSURE.md` in the style of the existing Tripo lines, and to `ATTRIBUTION.md` if the existing Tripo rock pieces are listed there.

## Rules

- Write only these paths: `assets/props/rockkit/`, `tools/build_rockkit.py`, `tools/rockkit.md`, `artifacts/rockkit/tripo2/`, `build-rockkit-cache/`, `AI_DISCLOSURE.md`, `ATTRIBUTION.md`. Do not touch engine source, other assets or other docs. Never write outside this worktree. Another job runs in a sibling worktree at the same time; do not read or write it.
- Use the `blender` command line only (`/opt/homebrew/bin/blender`). Do not use a Blender MCP server or a running Blender. Run Blender in the foreground. Run at most one Blender process at a time: the machine is shared.
- Do not build or run the game engine. Engine integration is a later task.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Size limit: the new files must add at most 90 MB to `assets/props/rockkit/`.
- Commit after the wall crags pass, and again after the rocks pass, on the current branch. Stage explicit paths, never `git add -A`. Do not push.
- Finish with a short report: the accepted pieces, the verification output, what is weak, and the minutes it took.
