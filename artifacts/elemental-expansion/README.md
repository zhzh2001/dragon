# Sunspear and Rimeplume — playable asset candidates

2026-09-28. Six original elemental concepts were explored; two were generated
and rigged for this first playable batch. The [concept board](../dragon-options/elemental-expansion/concepts.png)
and its directory contain prompts, turnarounds, cardinal plates, and Hunyuan job
provenance. Galehook (storm), Brinebellows (tide), Cairnhorn (stone), and Mireveil
(blight) remain concept designs.

| Candidate | Design | Rig | Geometry |
|---|---|---|---|
| Sunspear / fire | Bronze hide, amber triangular sails, wedge muzzle, long tapering tail | 68 joints, three membrane fingers per wing | 80,000 triangles |
| Rimeplume / frost | Slate and silver feather fans, crested avian head, feathered tail | 82 joints, eight feather rays per wing | 80,000 triangles |

Both use their own measured skeleton JSON, jaw mask, leg positions, skin weights,
and rig/flight/breath sidecars. Four influences maximum, no unweighted vertices.
Rimeplume's angular feather ownership stays constant along each vane's length;
its distal feather joints are intentionally unweighted. The shared rigger and
DragonRig motion code are unchanged. Materials retain the generated base colour
and normals, with repaired roughness; Rimeplume is deliberately matte.

## Try them

From the repository root:

```sh
./build/dragon --models assets/sunspear.glb,assets/rimeplume.glb
# M switches model; use the normal flap, dive, brake and landing controls.
./build/dragon --models assets/sunspear.glb,assets/rimeplume.glb --studio 1
```

These are additional selectable candidates. Existing default assets and roster
selection are preserved. Final GLBs and editable Blender files are local under
`assets/`; hosted-generation binaries follow the existing exclusion policy in
`ATTRIBUTION.md`. The scripts, profiles, references, and review evidence are in Git.

## Validation and visual judgment

- `cmake --build build`: passed.
- `ctest --test-dir build --output-on-failure`: 15/15 suites passed (18.08 s).
- `EMBERCREST_TEST_MODEL=assets/<species>.glb ./build/test_anim`: each run passed
  1,225 checks, zero failures. Full stdout retained in each `test-anim.txt`.
- 26 native-renderer captures per species: full side/front/rear/top glide, flap,
  dive and standing; close heads in all four poses; open-jaw attack; bind pose;
  unobstructed planted feet; actual simulated flight and landing; switch from
  `dragon.glb` to the candidate. Inspected at native 1280x720 resolution.
- Capture commands and logs are beside every PNG, with `captures.json` recording
  the final commands. `asset-hashes.json` identifies the final mesh and skeleton.
- Measured mesh bounds, landmark plots, Blender rig previews and weight statistics
  live in `../sunspear/` and `../rimeplume/`.

The clean triangle silhouette survives Sunspear's glide and tuck. Rimeplume's
fans remain readable across the checked poses, with a visibly distinct avian
body and feathered tail. Both jaws open downward without moving the crest or
upper muzzle. Standing side views show planted feet and clear wing tips.

The ground profiles sweep the whole wing back by 55 degrees and disable finger
convergence. Tight fan folding crumpled the fused generated surfaces; rejected
intermediate experiments remain in `fold-trials/`. The accepted stance is a
relaxed fold, not a tightly packed bird wing. Rimeplume still has rough feather
undersides/seams at close range; these are candidate meshes, not finished hero
retopology. Actual landing screenshots retain vegetation and can be partially
occluded; the matching studio feet shots show the contact clearly.

**Human playtest remains:** judge sustained wingbeat motion, brake/dive release,
landing/takeoff transitions, ground silhouette, and switching during flight.
The automatic captures and tests do not replace that judgment. No previous
species has been replaced by these candidates.

## Reproduce

```sh
# With Blender available as `blender`, and the original downloaded candidates:
blender -b -t 4 --factory-startup --python tools/sunspear_build.py
blender -b -t 4 --factory-startup --python tools/rimeplume_build.py
.venv-mat/bin/python tools/repair_model_materials.py assets/sunspear-raw.glb -o assets/sunspear.glb
.venv-mat/bin/python tools/repair_model_materials.py assets/rimeplume-raw.glb -o assets/rimeplume.glb --rough-keratin 0.6 --rough-membrane 0.72
python3 tools/capture_elemental_expansion.py
```

The adapters verify the candidate SHA256 against the independently measured
skeleton before building. They export `*-raw.glb` and preserve production flight
profiles. The measurement scripts can regenerate canonical point clouds and
measurements; the landmark script plots them for reauthoring. The author scripts
encode the measured landmarks rather than guessing them again each build.

`--hide-vegetation` is a new optional render flag used for unobstructed studio
inspection, skipping trees, grass, and tree shadows only. Normal play and the
actual gameplay captures retain vegetation.
