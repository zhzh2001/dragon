# Brief: rigged prey animals from Tripo meshes

You work in a git worktree of a C++ game engine (a dragon flight game over an alpine valley). Your current directory is that worktree. The task is Blender work only, headless.

## Goal

The valley's herds are one animal, `assets/props/grazer.glb` (a Mossback, 4.5 m long), played with three baked clips. The game needs real prey to hunt from the air: an elk, a bighorn ram, a boar. Tripo generated static meshes of them standing square on four straight legs, the pose a rig needs. They have no skeleton. Rig each one, bake the grazer's three clips for it, and deliver it under the grazer's contract, so the engine can load it with the code that loads the grazer.

Read first: `tools/grazer.md` (the contract, the clips, the verifier), `tools/build_grazer.py` (reuse its export, clip baking and verifier where they fit; do not edit it), `docs/MODEL_GENERATION.md` section "The second spend (2026-11-16, day)", and the contact sheet `artifacts/tripo-overnight/day2-prey.jpg`.

## Sources

The raw files are read-only inputs in `assets/tripo-raw/` (a symlink; never write there): `tripo-elk-01.glb`, `tripo-bighorn-01.glb`, `tripo-boar-01.glb`. If all three pass and budget remains, also `tripo-chamois-01.glb` and `tripo-goat-01.glb`. Do not rig the wolf, the eagle or the raven.

## What to deliver

For each animal, `assets/props/prey_<name>.glb` (`prey_elk`, `prey_bighorn`, `prey_boar`, then `prey_chamois`, `prey_goat`), built by a new script `tools/build_prey.py`, with a record `tools/prey.md`.

The grazer's contract applies in full:

- glTF binary, Y-up, metres, +Z toward the muzzle, the origin at ground level under the torso's centre. One mesh, one primitive, one skin, one material. No cameras, lights or root motion.
- Natural size: the elk 2.4 m long and about 1.5 m at the shoulder (antlers above that), the bighorn 1.7 m long, the boar 1.6 m long, a chamois 1.2 m, a goat 1.4 m.
- At most 9,000 triangles. The antlers and horns must keep their shape: protect them in the decimation.
- One embedded 1024 x 1024 base colour PNG baked from the source's colour, matte (roughness 0.85 to 0.9), no normal map. Remove directional light from the colour if the source has it.
- A quadruped skeleton of at most 40 bones: a root, a spine of three or four, a neck of two, `head`, `jaw` if the mouth is modelled open or can open, a tail of one to three, and four legs of four bones each with the hoof on its own bone. Name the bones as the grazer's skeleton names its own where a bone has the same role (read them from `assets/props/grazer.glb`), so that the engine's code that looks up `head` works. Every vertex has at most four influences, normalized. Antlers and horns are rigid on `head`.
- Three clips named exactly `graze`, `walk` and `run`, rotations only, LINEAR, with bit-identical first and last keys, as `tools/grazer.md` describes: `graze` 4.0 s (the head lowers to the grass and rises, weight shifts), `walk` 1.2 s (a lateral four-beat, soles level in stance), `run` 0.55 s (a bound or gallop with a suspended phase). The boar's `graze` is rooting with the snout at the ground. Scale stride lengths to each animal's leg length.
- The verifier's checks from `tools/grazer.md` apply: bind reproduction, normalized weights, exact loops, sole contact in stance (penetration at most 5 cm, stance gap at most 6.5 cm at this scale; scale both limits by the animal's shoulder height over the grazer's if needed and record it).

Deformation quality matters more than count. Look at each clip on the re-imported GLB: no collapsing shoulder or hip, no candy-wrapper twist at the knee, no leg passing through the belly, the neck bending over its length and not at one joint. Fix the weights where a render shows a fault. If an animal still deforms badly after two passes, reject it and record why, with the render.

Renders in `artifacts/prey/<name>/`, 1600x900, from a re-import of the final GLB, on a neutral green-brown ground, a sun at 30 degrees elevation with a sky light: one eight-frame side sheet for each clip, a three-quarter still, a front and a rear view of the walk, and a close view of the head in the deepest graze. Label each frame with its phase.

Record: `tools/prey.md` with the table of animals (source, size, triangles, bones, the mouth's position at bind in metres and in the `head` bone's local coordinates), what was done, the rejected animals with reasons, the rebuild commands, the verifier's output, and what is weak. Write short plain sentences, active voice, no filler. Add one line per accepted Tripo source to `AI_DISCLOSURE.md` in the style of its existing Tripo lines, and one line for the prey props to the table in `ATTRIBUTION.md` beside "Tripo additions in `assets/props/rockkit/`".

## Rules

- Write only these paths: `assets/props/prey_*.glb`, `tools/build_prey.py`, `tools/prey.md`, `artifacts/prey/`, `build-prey-cache/` (add it to `.gitignore`), `AI_DISCLOSURE.md`, `ATTRIBUTION.md`. Do not edit `tools/build_grazer.py` or `assets/props/grazer.glb`. Do not touch engine source or other docs. Never write outside this worktree.
- Use the `blender` command line only (`/opt/homebrew/bin/blender`). Do not use a Blender MCP server or a running Blender. Run Blender in the foreground. Run at most one Blender process at a time: the machine is shared.
- Do not build or run the game engine. Engine integration is a later task.
- Do not wait on standard input. Do not ask questions. Decide, and record the decision.
- Commit after each animal passes, on the current branch. Stage explicit paths, never `git add -A`. Do not push.
- Finish with a short report: the accepted animals, the verifier's output, what is weak, and the minutes it took.
