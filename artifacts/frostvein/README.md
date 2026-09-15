# Frostvein

An original ice dragon, generated as a new sculpt and fitted to its own
measurements. Rimefang and every existing rig/profile are preserved.

The authoritative design and exact imagegen prompt are in
`../dragon-options/frostvein-prompt.md`. `generation.json` identifies the
corrected four-view Hunyuan one-shot, its source hash and input plate hashes.
The first generation was rejected because its front plate was cropped
incorrectly; `rejected-input/` records that error. Only the corrected source
was used for the final skeleton.

## Rebuild

```sh
python3 tools/hunyuan_oneshot.py fetch 4f7e34c5-0062-415c-b62a-6e5f1636f5d1 assets/frostvein-cand-oneshot.glb
blender --background --factory-startup --python tools/rig_embercrest_candidate.py -- \
  --input assets/frostvein-cand-oneshot.glb --stem frostvein \
  --skeleton tools/skeletons/frostvein.json --keep-uvs --target 80000
cp assets/frostvein.glb assets/frostvein-raw.glb
python3 tools/repair_model_materials.py assets/frostvein-raw.glb -o assets/frostvein-final.glb
cp assets/frostvein-final.glb assets/frostvein.glb
```

The source, final GLB, raw GLB and editable Blender file stay local under the
existing generated-asset policy. The input art, skeleton, profiles, tools,
measurements and inspection evidence are tracked. The rigger writes a fresh
flight profile when rebuilding; preserve any subsequently tuned profile.

## Measurement and fit

`tools/frostvein_measure.py` imports the source exactly as the rigger does,
records its SHA-256, bounds, wing samples and mouth ray crossings, and renders
orthographic views. `tools/frostvein_landmarks.py` plots the source points and
the dedicated skeleton together. Run the latter with numpy and matplotlib;
it asserts that its coordinate conversion matches the Blender measurements.

- **74 bones**, with four finger chains per wing, three neck segments,
  eight tail segments, distinct fore/hind legs, feet, toes, head and jaw.
- Coordinates are measured in the corrected source's canonical frame:
  +Y forward and +Z up. Its own `reference_bounds` makes the rigger's affine
  fit the identity, rather than stretching an Embercrest skeleton into it.
- The foreleg centers follow the almost vertical upper limbs and the forward
  slope into the paws. Hind knees sit around Y=-0.02, hocks around Y=-0.078,
  with separate ankle-to-paw segments. See `measurement/landmarks-side.png`
  and `landmarks-front.png` for the actual overlay.
- The wing elbow is near (±0.133, 0.026, 0.332), wrist near
  (±0.210, 0.080, 0.460). The four finger tips and their bends follow the
  sculpted ribs, including the short innermost rib.
- Each finger includes a distal tip joint. The first 66-bone export had only
  two joints per finger; the standing aim could orient the first segment
  but not the last one, leaving a curved fan around the tail. The overhead
  inspection in `tuning/ground-top.png` caught this. Tip joints let the
  existing runtime aim both segments without an engine change.
- Wing-plane fits measure about 49.2° from body up. The shoulder-to-wrist
  vector rises about 54°; the 45° bind leveling leaves modest arm elevation
  while the wrist counter-rotation preserves the outer membrane.
- The continuous membrane field fades across |X|=0.095–0.115 and
  Z=0.145–0.17, then fades out toward the neck at Y=0.14–0.18.
  The initial wider transition left the two inner membrane surfaces with
  differing heat weights, causing overlapping patches in rear-view flight.
  Fully controlling the inner membrane removes those patches.
- The jaw mask has a floor at Z=0.32 to exclude the chest, and a
  piecewise line through the measured mouth gap. At centerline Y=0.3299,
  the lower surface of the palate is Z=0.3867 and the mandible's upper
  surface is Z=0.3789; the split is Z=0.383. At Y=0.3407, the gap widens
  and the split drops to Z=0.376. Above the line, jaw weights are reclaimed
  for the head. The hinge fades between Y=0.305 and 0.329.

## Engine inspection

`python3 tools/capture_frostvein.py` captures the full-size native Metal
inspection set, including side/front/rear/above, head close-ups, feet,
gameplay landing and flight, model switching, frost breath and a 7,200-frame
turn soak. `engine/captures.json` records exact commands and model-load lines.
Only a dozen of those renders are kept in the repository (the full set was
19 MB); the script regenerates all of them into `engine/`.

To fly it:

```sh
./build/dragon --model assets/frostvein.glb --combat
```

To switch from the default dragon onto it:

```sh
./build/dragon --models assets/dragon.glb,assets/frostvein.glb --studio 1
```

Press **M** to switch. Human playtest: inspect a full wingbeat, hold breath,
dive, brake and land on uneven terrain; watch the wrist membranes, upper
snout stability and all four paws through the landing transition.
