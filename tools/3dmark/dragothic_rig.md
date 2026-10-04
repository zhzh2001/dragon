# Rigging the 3DMark2001 dragon (a Codex brief, not yet run)

**Status (2026-10-04):** the mesh is converted, the rig is not. Codex had no
5-hour headroom the night the mesh came out; run this when it does, with
`codex exec -m gpt-6.1-sol -c model_reasoning_effort=high -s danger-full-access`
in a worktree of its own (CLAUDE.md, "Delegate complex 3D model rigging").

**What exists.** `tools/3dmark/carve_ras.py` carves 3DMark2001 SE's MAX-FX
archives; `tools/3dmark/lwo2gltf.py` converts a LightWave object to a static
glTF. Run on the Dragothic dragon (`form063`, 8,838 points, 16,817 polygons,
15 surfaces) they produce `assets/alt/3dmark2001/dragothic-static.glb`: a
19.5-unit wingspan (the engine's assumed span is 19 m, so `--scale 1`), +Z
forward is *not* guaranteed -- check (the engine yaws an asset by its head
and tail joints), UVs per surface, and six LightWave weight maps carried as
primitive extras: `L_wing`, `R_wing`, `L_leg`, `R_leg`, `NeckAndHead`, `Tail`.
No textures: the archive's directory is encrypted, so its 380 JPEG and 356
DDS files are unnamed; the surfaces name the clips they want (`spikes-1c`,
`vatsa_yo` the belly, `genericskin-2` the hide and wing, `dnuface_*` the
head, `tomatoe` the eye) and `assets/alt/3dmark2001/textures/` holds every
image, to be matched by eye or by UV coverage. Everything under
`assets/alt/3dmark2001/` is UL's copyrighted data: **never committed, never
shipped** (ATTRIBUTION.md).

**The job.** Fit the engine's deform skeleton to the mesh and export a rigged
glTF the loader takes (`docs/ANIMATION.md`, `docs/MODEL_GENERATION.md`,
`tools/skeletons/*.json`, `tools/rig_embercrest_candidate.py`): the bone
names are the contract with `src/anim/dragon_rig.cpp` (neck chain, tail
chain, wing root and fingers per side, legs, jaw). The dragon's own weight
maps are a starting point for the `wing_field` gates and the leg masks, not
a skeleton. Measure the leg positions and the jaw as the other skeleton
JSONs were measured; a shared file shares its author's measurements and
those do not transfer. Output: `assets/alt/3dmark2001/dragothic.glb` plus
its `.rig.cfg`, `.flight.cfg` and `.breath.cfg` beside it (copy a sibling's
blocks and retune by render), and the renders the working agreements ask
for (side, front, rear, above; the head close; the feet on the ground;
landed in the game) in `artifacts/`. Textures: the four or five the hide
needs, picked from the carved images and baked into the glTF.

**Then** the loader finds it with `--model assets/alt/3dmark2001/dragothic.glb`,
and the roster can carry it locally through `--models`.
