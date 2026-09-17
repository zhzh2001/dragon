# Selected Embercrest candidate

`embercrest-cand-hunyuan-1p5m.glb` is the selected source. Its open jaw,
symmetric spread wings, separate feet/claws, and sharper dorsal detail make it
the most useful of the four candidates for this game's dragon rig. The hosted
TRELLIS candidate has swept wings and a fused mouth; local TRELLIS has softer
anatomy/membrane detail; Hunyuan's downloaded low-poly version has severely
elongated horns. All four source files and the old procedural Embercrest remain
unchanged.

## Deliverables

- `assets/embercrest-selected.glb`: 80,000 triangles, 39,994 Blender vertices
  (51,743 exported vertices after normal/UV splits), 61 deform bones, 4.02 MB.
  Triangle count is reduced 94.7% from the 1.5 million triangle source.
- `assets/embercrest-scripted/selected/embercrest-selected.blend`: editable mesh, FK
  armature, grouped bones, preview camera/lights, and a deformation-check action.
  Scrub frames 1/101 for rest, 21 for flap, 41 for fold/leg tuck, 61 for
  head/jaw, and 81 for tail curl. These diagnostic poses are excluded from GLB;
  the game supplies procedural animation.
- `assets/embercrest-selected.glb.flight.cfg`: measured ground clearance at
  the runtime's 19 m wingspan (3.954605 m).
- `tools/rig_embercrest_candidate.py`: reproducible simplification, skeleton
  fitting, skinning, export, and Blender renders.

This is triangle decimation, not quad retopology. The source has no authored
textures; the deliverable uses a packed neutral clay texture and simple UVs.
It is a deform/FK rig, without IK controllers or a locomotion clip set.

The skeleton includes root/chest, three neck bones, head/jaw, eight tail bones,
four articulated legs with twelve toe bones, and two wings with three arm bones
and three two-segment finger rays each. The inner membrane attaches to the
body/arm, without an extra finger pulling it out of place. Bone heat binds the
body. The membrane field blends into the root and wrist; its fourth influence
falls to zero before group membership changes, avoiding abrupt weight seams.
Rigid assignments preserve the skull and lower jaw. Every vertex has normalized
weights and at most four influences.

The source's raised-wing stance is converted to a flight bind pose: the shoulder
rotates down 35 degrees and the wrist counter-rotates 35 degrees. The mesh and
skeleton are baked together. This removes the large full-tuck overlap while
preserving the sculpt, wingspan silhouette and runtime's default rig behavior.

## Verification

The exact exported GLB passes **508 animation checks, 0 failures**, including
all mapped chains and response to flap, tuck, turn, attack and grounding. The
loader reports bind error **0.000000**. Blender reports zero unweighted
vertices and maximum weight-sum error **5.22e-8**.

Blender rest, flap, fold, jaw and tail renders are in
`artifacts/embercrest-selected/`. Native Metal game captures are in its
`runtime/` subdirectory: three bind views, glide, flap, tuck, attack, ground,
and a 7,200-frame turn soak. Captures establish actual loading/deformation,
not just a successfully written file. The final bind-pose and attachment
corrections remove the large overlap and inner-membrane pull seen in the first
rig. Rest, flap, full tuck, jaw, tail and grounded views were inspected. This is
a linear-skinned game rig; its folds do not simulate membrane self-collision.
The neutral material remains a placeholder for a later texture-painting pass.

```sh
/Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
  --python tools/rig_embercrest_candidate.py
cmake --build build --target test_anim
EMBERCREST_TEST_MODEL=assets/embercrest-selected.glb ./build/test_anim
python3 tools/capture_embercrest.py --model assets/embercrest-selected.glb \
  --output artifacts/embercrest-selected/runtime
./build/dragon --model assets/embercrest-selected.glb
```

The new test override fails if its path is missing; default optional validation
of the old `assets/embercrest-scripted.glb` is unchanged. Model files remain covered by
the repository's existing asset-ignore rules. No default game model was changed.

## Textured Hunyuan variant

See [EMBERCREST_TEXTURED.md](EMBERCREST_TEXTURED.md) for the revision-1 checkpoint
and human review findings. Numerical validation below is not animation-quality
acceptance.

`tools/rig_embercrest_candidate.py -- --textured` applies the same workflow to
`assets/embercrest-cand-hunyuan-textured.glb`. It writes a separate
`assets/embercrest.glb` and
`assets/embercrest-scripted/textured/embercrest.blend`.

The output has **80,000 triangles, 40,000 mesh vertices (73,550 exported
vertices after UV/normal splits), and 62 bones** in revision 2. The 1.5 million triangle input
is reduced 94.7%. The GLB is 54,795,812 bytes, including the original 4K base
colour, normal, and packed metallic/roughness maps. UVs and material connections
are retained; no planar replacement UVs or neutral-clay material is applied.
A binary audit confirms that all three embedded PNGs are byte-identical to the
source and material settings (including its specular extension) are preserved.
Five undefined tangents at UV-singular triangles are replaced with orthogonal
unit vectors by `tools/repair_gltf_tangents.py` after export; the operation leaves
all texture, UV, geometry and skin bytes unchanged.
Exact geometric duplicates are welded before binding while per-corner UVs
remain available. Bone landmarks and the weight field are fitted to the
textured source's measured bounds; the source mesh is not resized to match the
untextured model.

The same wing bind correction, three finger rays, smooth weight transitions,
FK diagnostic action and pose markers are included. The measured ground
clearance is 3.993398 m. Revision 1 passed **508 checks with zero
failures**, with zero reported bind error and three loaded textures. See
[the revision history](EMBERCREST_TEXTURED.md) for current animation fixes and
validation. Blender pose renders and native Metal captures are retained under
`artifacts/embercrest/`.

```sh
/Applications/Blender.app/Contents/MacOS/Blender --background --factory-startup \
  --python tools/rig_embercrest_candidate.py -- --textured
EMBERCREST_TEST_MODEL=assets/embercrest.glb ./build/test_anim
python3 tools/capture_embercrest.py --model assets/embercrest.glb \
  --output artifacts/embercrest/runtime
./build/dragon --model assets/embercrest.glb
```
