# Mossback prey asset

`assets/props/grazer.glb` is a 4.5 m long, in-place animated Mossback for a prey herd. Input GLB and rigging scene remain read-only.

```sh
blender -b --factory-startup --python tools/build_grazer.py
blender -b --factory-startup --python tools/build_grazer.py -- --verify-only
# Rebuild asset and verify without rendering, or render the current deliverable:
blender -b --factory-startup --python tools/build_grazer.py -- --build-only
blender -b --factory-startup --python tools/build_grazer.py -- --render-only
```

The build uses Blender 5.2.1 and no external Python packages. Its source is the repaired `assets/mossback.glb`; the `.blend` is not required. It welds coincident glTF split vertices before decimation (114,316 to 39,918), retains corner UVs, and collapses to **8,480 triangles**. Vertex weights are interpolated by decimation, limited/normalized to four influences; the lowest 25 cm of each hoof is consolidated onto its existing `hand_*` / `foot_*` bone for firm soles. All **37 source bones and their hierarchy** remain, including toes. The material uses a downsampled, 0.72-value base colour PNG, **1024 × 1024**, embedded, matte roughness 0.88; no normal map.

Bind dimensions are **X 2.1121 × Y 3.3940 × Z 4.5000 m**. Y is up, +Z points toward the muzzle. The origin lies at ground level below the torso centre between shoulders and hips; the hanging tail and projecting head make the bounding box slightly asymmetric about it. The fur hump determines the maximum height. One mesh, one primitive, one skin, one material; no cameras, lights, or root motion are exported.

| Clip | Duration | Keys per bone | Motion |
|---|---:|---:|---|
| `graze` | 4.00 s | 121 | Lower and raise head, tiny chewing/shake, tail swish, planted crouch and weight shift |
| `walk` | 1.20 s | 73 | Lateral four-beat: hind left, fore left, hind right, fore right; 66% stance; 0.64 m hoof sweep |
| `run` | 0.55 s | 67 | Bound with slightly offset paired limbs, 28% support phases, suspended gather, spine flex, raised head/tail |

All channels are quaternion rotations with LINEAR interpolation and bit-identical first/last keys. Leg targets are solved into rotations, including counter-rotation to keep soles level. No translation or scale animation exists. The runtime must retain bind translations/scales, sample these clips directly, and place/move the animal externally. The engine's procedural `DragonRig` can overwrite sampled poses and should not drive this prey playback. The requested +Z asset forward differs from the engine's general -Z convention: orient the prey instance accordingly.

`head` is the skull bone; `jaw` and `jaw_tip` retain their source names. Approximate mouth position at bind is **(0, 1.464, 2.434) m**; an approximate attachment in the exported `head` bone's local coordinates is **(0, -0.108, 1.056) m**. Transform that point by the animated head world matrix. At deepest graze the mouth is about 12 cm above the ground and ahead of the front hooves. There are no independent ear bones; the small head shake supplies the requested ear-flick-scale accent.

The independent verifier reads raw GLB accessors, hierarchy, inverse binds, image header, and clips. It evaluates normalized quaternion interpolation and linear blend skinning at 241 times per clip, including between baked keys. It checks sole vertices (not bone tips), all graze feet and each walk foot during its scheduled stance, plus continuous nearest-foot contact. Allowed penetration is 5 cm and maximum stance gap is 6.5 cm. Run may suspend. Verification failures exit nonzero, including when Blender's default Python error handling would otherwise return zero.

Latest verifier output:

```text
Dimensions X/Y/Z: 2.1121 / 3.3940 / 4.5000 m
Triangles: 8480; joints: 37; bind maximum error: 0.000000559 m
graze: 4.00 s; 241 sampled poses; sole min 0.0079 m; worst nearest sole 0.0083 m; stance gap 0.0087 m; rotation-only/loop PASS
walk: 1.20 s; 241 sampled poses; sole min 0.0027 m; worst nearest sole 0.0082 m; stance gap 0.0142 m; rotation-only/loop PASS
run: 0.55 s; 241 sampled poses; sole min 0.0041 m; worst nearest sole 0.2187 m; stance gap N/A; rotation-only/loop PASS
PASS: bounds, budget, mesh/skin/material, hierarchy, embedded 1024 texture, bind reproduction, normalized weights, clips, rotations, exact loops, interpolated sole contact.
```

Visual evidence is in `artifacts/prey/`: three eight-frame side sheets (left to right across the top row, then bottom; phases 0/8 through 7/8), the individual full-size frames, a three-quarter still, front/rear/top run views, grazing head close-up, and walking feet close-up. These are rendered from a **re-import of the final GLB**, not the authoring scene. The source's shaggy silhouette remains readable; the welded surface stays continuous through reach/gather and grazing. No engine code or procedural rig settings were changed; game integration/playtesting remains engine-side work.

Reproducibility was checked with a second `--build-only` pass: the complete GLB SHA-256 remained `79d9337970852450895b2c312c3dab071a16406849f7671bce0c0db03298feb4`. Independent in-memory corruption/rejection tests also confirmed verifier failures return exit status 1, without writing a damaged asset. To update selected supplemental views without rebuilding, use for example `--views=walk-feet,run-top` after Blender's `--` separator.
