# Second-generation run props

Generated entirely in code by `tools/build_props.py`, tested with Blender 5.2.1.
All dimensions and effect centres below are metres in glTF coordinates (X/Y/Z).

| Asset | Overall X × Y × Z | Triangles | Effect centre |
|---|---|---:|---|
| `watchtower.glb` | 11.042 × 40.000 × 11.042 | 13,260 | Brazier / flame: (0, 35.2, 2) |
| `spire_tower.glb` | 7.000 × 44.000 × 7.000 | 1,388 | Orb: (0, 42, 0) |
| `hoard_pile.glb` | 16.000 × 5.500 × 16.000 | 10,228 | — |
| `hoard_trove.glb` | 14.000 × 4.000 × 14.000 | 10,074 | — |
| `ward_stone.glb` | 11.634 × 14.850 × 11.499 | 3,108 | Ward: (0, 0, 0) |

The keep has a roughly 9 m square lower shaft, an 11 m stepped plinth,
three tapering masonry tiers, broad string courses, base buttresses, framed
arrow slits, warm upper windows, a timber hoarding gallery and crenellations.
Its rear slate pyramid reaches 40 m; the front platform and iron cradle remain
open to the sky. The cradle rim is at Y=34.9 and its prongs reach about 35.45 m.
The flame centre is offset toward +Z and must replace the old 16.5 m placement;
do not place the flame at the roof tip. A sphere of radius 0.65 m around the
reported centre is verified clear of all triangles.

The spire has eight ribs, two attached stone rings with supporting fins, and
four curved polygonal iron prongs surrounding an empty orb socket. A radius
1.2 m sphere at its reported centre is verified clear. No orb or flame is baked
into either model; both effects belong to the engine.

The large hoard has three open chests with visible coin fill and spills,
150 scattered coin discs, two ingot stacks, a hollow pointed crown, two shields,
a diamond-section sword, goblets, and oversized red, green and blue gems.
The trove uses a lower heap in a broken stone altar ring, two chests, two
sideways urns, and a red folded banner. Gold atlas cells use linear colours
around (0.85, 0.55, 0.12), converted to sRGB for PNG storage, with darker
variation. The mound uses broad colour variation instead of tiny repeating
coin textures; the readable coins are geometry.

## Engine contract

Each GLB contains one mesh / triangle primitive, one skin and one bone named
`root`, with identity rest, inverse bind and every node transform. Every vertex
has root weight 1.0. The four ground props have horizontally centred origins at ground level, Y=0.
The ward stone origin is its floating effect centre.
Geometry is authored in Blender metres, -Y forward, then exported as Y-up,
+Z forward. All geometry modifiers are baked; only the armature binding remains
before export. Each asset has one Principled material and one embedded
512 × 512 base-colour PNG atlas. No external resources, vertex colours,
animations, morphs, cameras or lights ship. Disconnected decorative components
are parts of the same mesh, all rigidly bound to root.

Use the assets at scale 1 to preserve these dimensions and effect offsets.
Engine loading, variant selection, collision, and effect placement were not
changed by this Blender-only task. Blender inspection is not an in-engine
lighting or tonemap test.

## Rebuild and inspect

```sh
blender -b --factory-startup --python tools/build_props.py -- --render
blender -b --factory-startup --python tools/build_props.py -- --verify-only
```

The default build regenerates and verifies all five GLBs.
Use `--prop ward_stone` to build only the ward stone.
Verification reads actual accessor buffers and checks dimensions and socket clearance.
Triangle ceilings are 20,000 for ground props and 8,000 for the ward.
The verifier imports each GLB back into Blender and checks skinning, material texture connections, and bounds again. Exceptions
exit nonzero even without Blender's `--python-exit-code` flag.

`--render` writes twelve PNGs under `artifacts/props/v2/`.
The original four ground props supply eight images: each has a
three-quarter near view and a perspective far view at exactly 250 m from its
aim point, both at 30 degrees elevation, 50 mm lens, 1200 × 1000 pixels.
All eight ground-prop images were inspected at full size.
The four ward views are described below. The keep's wide crown and the spire's
rings/prongs distinguish them at distance; the caches differ through the broad
gold mound versus the low stone ring and banner. At 250 m, small treasure
pieces naturally merge, while the overall shape and colour remain visible.

Validation: all four GLBs were byte-identical across fresh Blender builds.
A deliberately removed skin produced exit 1 (`Expected one skin`); the
original bytes were restored and the full verifier passed again.

Actual final verifier output:

```text
VERIFY watchtower: PASS
  effect centre glTF metres: (0.000, 35.200, 2.000); empty sphere radius=0.65 m: PASS
  glTF Y-up bbox metres: min=(-5.521, 0.000, -5.521) max=(5.521, 40.000, 5.521)
  dimensions X/Y/Z metres: (11.042, 40.000, 11.042); triangles=13260; bones=1 [root]
  materials=[watchtower_atlas]; textures=[watchtower_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; base/centering/budget: PASS
VERIFY spire_tower: PASS
  effect centre glTF metres: (0.000, 42.000, 0.000); empty sphere radius=1.20 m: PASS
  glTF Y-up bbox metres: min=(-3.500, 0.000, -3.500) max=(3.500, 44.000, 3.500)
  dimensions X/Y/Z metres: (7.000, 44.000, 7.000); triangles=1388; bones=1 [root]
  materials=[spire_tower_atlas]; textures=[spire_tower_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; base/centering/budget: PASS
VERIFY hoard_pile: PASS
  glTF Y-up bbox metres: min=(-8.000, 0.000, -8.000) max=(8.000, 5.500, 8.000)
  dimensions X/Y/Z metres: (16.000, 5.500, 16.000); triangles=10228; bones=1 [root]
  materials=[hoard_pile_atlas]; textures=[hoard_pile_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; base/centering/budget: PASS
VERIFY hoard_trove: PASS
  glTF Y-up bbox metres: min=(-7.000, 0.000, -7.000) max=(7.000, 4.000, 7.000)
  dimensions X/Y/Z metres: (14.000, 4.000, 14.000); triangles=10074; bones=1 [root]
  materials=[hoard_trove_atlas]; textures=[hoard_trove_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; base/centering/budget: PASS
```


## Ward stone cage

Seven unequal basalt shards surround the empty origin socket. Their widths, spacing, shoulders, and crown heights differ without axis symmetry.
Two iron bands tilt in opposite directions. Each band carries 28 six-sided studs.
The long lower keel reaches Y=-7.95 m. The highest crown reaches Y=6.90 m.
Broad shard shoulders sit at radii 4.82–5.27 m. Bands span radii 5.30–5.66 m before tilting.
These dimensions leave the effect visible through broad gaps while keeping the cage compact.
Only the keel and crown tips extend beyond radius 7.5 m.

Basalt uses linear RGB (0.09, 0.10, 0.115), with edge and facet highlights near (0.20, 0.20, 0.20).
Iron uses (0.12, 0.08, 0.06). Rune cuts use (0.45, 0.42, 0.36), bordered by dark grooves.
Runes occupy outward shard faces. The atlas converts linear colours to sRGB for PNG storage.
The exported material has no emission. The effect sphere belongs to the engine.

Build and inspect only this prop:

```sh
blender --factory-startup --python tools/build_props.py -- --prop ward_stone --render
blender --factory-startup --python tools/build_props.py -- --prop ward_stone --verify-only
```

The selective build leaves the four existing GLBs unchanged.
The verifier measures exported triangles, rather than source vertices or accessor bounds.
Socket clearance uses the closest point on every triangle.
Visibility uses 2,000 evenly distributed Fibonacci rays from the origin.
The area bound counts only triangles whose three vertices lie inside radius 7.5 m.
A sphere is convex, so these complete triangles lie inside it.
Triangles crossing that boundary contribute nothing to the bound, even when part of their area lies inside.

The first pass looked too regular. The second varied shard widths, lean, shoulder radii, and blunt crown tips.
The second pass also moved the keel into the near silhouette.
Two design passes produced the final inspection images. All four images were opened at their full 1200 × 1000 resolution.
`ward_stone_near.png` shows the three-quarter view from 31 m.
`ward_stone_far.png` uses the same direction from exactly 250 m, with a 50 mm lens.
`ward_stone_top.png` and `ward_stone_bottom.png` show both polar openings from 31 m.
A temporary orange emissive sphere of radius 3.8 m appears in the near and far inspections only.
The sphere is added after export and verification. It is absent from the GLB.
The floor and sphere are hidden in the polar views.

The near view exposes rune cuts, studs, unequal crown tips, and the lower keel.
The far view retains the cage silhouette and visible effect opening; small runes merge at this distance.
The polar views show clear openings and the single extended keel.

Actual ward verifier output:

```text
VERIFY ward_stone: PASS
  glTF Y-up bbox metres: min=(-5.599, -7.950, -5.997) max=(6.035, 6.900, 5.502)
  dimensions X/Y/Z metres: (11.634, 14.850, 11.499); triangles=3108; bones=1 [root]
  materials=[ward_stone_atlas]; textures=[ward_stone_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  origin socket minimum triangle distance=4.383413 m; required >=4: PASS
  maximum vertex radius=8.323742 m; required <=8.5: PASS
  surface area=553.627896 m2; entirely inside radius 7.5=523.524195 m2 (94.5625% conservative bound): PASS
  outside radius 7.5: polar tips only: PASS
  origin Fibonacci rays: 1049/2000 escape (52.45%); required 30-55%: PASS
  one Principled material; embedded 512 PNG; no emission: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
```

## Third generation: Tripo (2026-11-16)

Tripo generated the source meshes with paid credits. The raw files remain read-only under `assets/tripo-raw/`.
Blender 5.3.0 converted the selected sources. `tools/tripo_props.py` supplies the conversion path in `tools/build_props.py`.
The procedural builder remains available without `--from-tripo`.

All ten candidates were rendered and opened at full size under `artifacts/props/v3/candidates/`.
The selection used watchtower 01, spire 01, hoard 01, hoard 02, and wardstone 02.
Watchtower 02 lost the square keep silhouette. Watchtower 03 had a broken, narrow roof.
Spire 02 had a large crystal and weaker stone detail. Hoard 03 contained large purple letter-like blocks.
Wardstone 01 had more uniform slabs. Wardstone 02 had unequal shards and a lower keel.

The converter fits each imported mesh before decimation. The following table uses Blender coordinates: X, Y, Z-up.
For each axis, it adds the source offset and then multiplies by the scale.
Ground offsets centre the footprint and place the base at zero. The ward offset centres its initial bounds.

| Prop | Source | Source offset X/Y/Z | Scale X/Y/Z |
|---|---|---|---|
| watchtower | `tripo-watchtower-01.glb` | (-0.000091553, -0.000045776, 0.490478516) | (16.42281757, 15.41100517, 40.72582650) |
| spire | `tripo-spire-01.glb` | (-0.000274658, 0.000793457, 0.489013672) | (20.85994907, 20.74486750, 45.01801605) |
| hoard pile | `tripo-hoard-01.glb` | (0.000045776, 0.000137329, 0.268798828) | (16.35180738, 16.30400846, 10.26274130) |
| trove | `tripo-hoard-02.glb` | (0.029937744, -0.038330078, 0.197219849) | (15.25207793, 15.62187564, 10.14881920) |
| ward | `tripo-wardstone-02.glb` | (-0.000167847, 0, -0.000015259) | (13.72878246, 12.84012539, 15.18315080) |

The watchtower roof moves rearward by up to 0.9 m above Blender Z=31 m.
A small iron bracket supports a new empty flame cradle at glTF (0, 35.2, 2).
The spire loses everything above Blender Z=36.7 m, including its crystal and original cradle.
Its remaining shaft stretches vertically by 39.3/36.7. New iron prongs surround glTF (0, 42, 0).
Neither tower contains a flame or orb mesh.

The converter makes a new UV atlas after decimation. It bakes colour, tangent-space normals, and metallic-roughness from the full source mesh.
The ground maps are 2048 × 2048. The ward maps are 1024 × 1024.
Colour uses sRGB. The other maps use linear data. Normal maps use the OpenGL convention.
These three embedded PNG maps replace the procedural texture rule for accepted Tripo assets.
The one-mesh, one-primitive, one-material, identity-root skin contract remains unchanged.
All socket, dimension, triangle, ward-area, and ward-visibility checks remain unchanged.
A build verifies a temporary GLB before replacing its destination.

No clear directional light/dark split appeared in the inspected source renders.
The colour bake uses emission and adds no Blender lighting. It preserves source colour without a lighting correction or colour grade.
Source crevice shading remains in the colour maps.

Rebuild and inspect a selected prop:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python tools/build_props.py -- --from-tripo assets/tripo-raw --prop watchtower --render
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python tools/build_props.py -- --from-tripo assets/tripo-raw --verify-only --render
```

The Tripo render path writes near and 250 m views under `artifacts/props/v3/`.
It also writes the ward's top and bottom views. The temporary orange inspection sphere never enters the exported GLB.

Final acceptance:

| Prop | Result | Triangles | Map size |
|---|---|---:|---|
| watchtower | Replaced from Tripo 01 after pass 1 | 19,498 | Three 2048 PNGs |
| spire | Replaced from Tripo 01 after pass 2 | 19,655 | Three 2048 PNGs |
| hoard pile | Kept the original v2 file | 10,228 | One 512 PNG |
| hoard trove | Kept the original v2 file | 10,074 | One 512 PNG |
| ward stone | Kept the original v2 file | 3,108 | One 512 PNG |

The watchtower's shaft is narrower than v2. Its stone footing, roof, and keep silhouette remain readable at 250 m.
The spire's stone shaft, metal rings, and empty prongs remain readable at that distance.
Both towers passed the in-game inspection. Their effects sit in the intended cradles.

Pass 1 decimation broke the spire's added prong tips. Pass 2 preserved the authored cradle outside decimation.
The preserved mesh also introduced a second UV layer. Blender used its baked UV1 atlas, but the engine read UV0.
The exported accessor and texture references were remapped to UV0 without another bake.
The builder now removes unused UV layers after baking. The verifier rejects textures that reference another UV set.

The pile and trove failed pass 1 because their shading looked crumpled and their texture projection stretched across triangles.
Pass 2 reset smooth normals and combined normal projections from three ray distances, using the rock-kit routine.
The pile failed that routine's coverage gate with this exact error:

```text
Normal projection left 0.20% of the surface uncovered. Inspect the bake cage.
```

The trove passed its structural checks, but retained stretched wood grain and crumpled stone and metal shading.
Neither hoard replaced its original file. Their second-pass settings remain in the helper for inspection, but normal builds skip them.

The ward's first pass expanded inner vertices to radius 4.45 m and tapered its shoulders to radius 7.45 m.
It allowed polar tips to radius 8.4 m. The boundary between these envelopes produced stretched tip triangles.
Pass 2 used a continuous 7.45 m envelope and reset source and destination normals.
It passed all geometric checks: minimum socket distance 4.343825 m, 100% area inside radius 7.5 m, and 42.55% ray escape.
Its 7,682 triangles still showed stretched tips and crumpled dark faces. The original ward remained unchanged.
No third conversion pass ran.

`--from-tripo` skips these three rejected props and prints each reason. Their existing files remain unchanged.
An explicit build of a rejected `--prop` exits with that reason. `--verify-only` still checks every final file.

Rejected bench views are under `artifacts/props/v3/rejected/pass1/` and `rejected/pass2/`.
The root v3 tower images show the accepted conversions.
The root v3 hoard and ward images are reference copies from v2 because those GLBs remain byte-identical.
All candidate and conversion views were opened at full size, including both ward polar views.
The final mixed set passed the verifier below. `verify-final.log` contains the full Blender log.

```text
VERIFY watchtower: PASS
  effect centre glTF metres: (0.000, 35.200, 2.000); empty sphere radius=0.65 m: PASS
  glTF Y-up bbox metres: min=(-5.495, 0.005, -5.489) max=(5.498, 39.986, 5.497)
  dimensions X/Y/Z metres: (10.993, 39.981, 10.986); triangles=19498; bones=1 [root]
  materials=[watchtower_tripo]; textures=[watchtower_basecolor: embedded PNG 2048x2048, baseColor]
  normal and metallic-roughness: embedded PNG 2048x2048; tangent-space normal: PASS
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
VERIFY spire_tower: PASS
  effect centre glTF metres: (0.000, 42.000, 0.000); empty sphere radius=1.20 m: PASS
  glTF Y-up bbox metres: min=(-3.500, -0.005, -3.503) max=(3.498, 44.000, 3.511)
  dimensions X/Y/Z metres: (6.998, 44.005, 7.014); triangles=19655; bones=1 [root]
  materials=[spire_tower_tripo]; textures=[spire_tower_basecolor: embedded PNG 2048x2048, baseColor]
  normal and metallic-roughness: embedded PNG 2048x2048; tangent-space normal: PASS
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
VERIFY hoard_pile: PASS
  glTF Y-up bbox metres: min=(-8.000, 0.000, -8.000) max=(8.000, 5.500, 8.000)
  dimensions X/Y/Z metres: (16.000, 5.500, 16.000); triangles=10228; bones=1 [root]
  materials=[hoard_pile_atlas]; textures=[hoard_pile_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
VERIFY hoard_trove: PASS
  glTF Y-up bbox metres: min=(-7.000, 0.000, -7.000) max=(7.000, 4.000, 7.000)
  dimensions X/Y/Z metres: (14.000, 4.000, 14.000); triangles=10074; bones=1 [root]
  materials=[hoard_trove_atlas]; textures=[hoard_trove_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
VERIFY ward_stone: PASS
  glTF Y-up bbox metres: min=(-5.599, -7.950, -5.997) max=(6.035, 6.900, 5.502)
  dimensions X/Y/Z metres: (11.634, 14.850, 11.499); triangles=3108; bones=1 [root]
  materials=[ward_stone_atlas]; textures=[ward_stone_basecolor: embedded PNG 512x512, baseColor]
  single mesh/skin; root rest+inverse bind=identity; root weights=1; transforms=identity; no animations/morphs/extra nodes: PASS
  origin socket minimum triangle distance=4.383413 m; required >=4: PASS
  maximum vertex radius=8.323742 m; required <=8.5: PASS
  surface area=553.627896 m2; entirely inside radius 7.5=523.524195 m2 (94.5625% conservative bound): PASS
  outside radius 7.5: polar tips only: PASS
  origin Fibonacci rays: 1049/2000 escape (52.45%); required 30-55%: PASS
  one Principled material; embedded PNG maps; no emission: PASS
  axis mapping: Blender (X,Y,Z) -> glTF (X,Z,-Y); forward authored -Y -> +Z; placement/budget: PASS
```

A procedural spire rebuild passed in `build/procedural-props-check/`, without `--from-tripo` or rendering.
It retained 1,388 triangles, one 512 PNG, and the original dimensions and socket clearance.
The rejected-pile CLI check exited 1 and left its file unchanged.
Byte comparisons confirmed that both hoards and the ward match their saved v2 GLBs.

## Fourth hoard attempt: Tripo hoard 10 (2026-11-20)

We kept the existing hoard after two conversions of `tripo-hoard-10.glb`.
Pass 1 used the smoothed atlas and three-distance projection. Its coverage check passed, but neighbour filling affected 20.83% of surface texels.
Pass 2 kept source UVs and baked the reduced mesh directly. Rock detail improved, but gold retained folded highlights.
Both candidates read as dark rock discs at 250 m. The original showed clearer gold, chests, and colour separation.

We inspected all six full-size views in `artifacts/props/v4/{original,pass1,pass2}/`. The renders used matching cameras, lighting, floor and AgX settings.
Both cameras used a 1200 × 1000 frame at distances of 27 m and 250 m.
Both candidates passed the skin, identity-transform, map, triangle and dimension checks.
Both had 19,503 triangles, dimensions (16.020, 5.519, 16.026) m, and three embedded 2048 PNG maps.
Pass 2's metallic channel ranged from 0 to 1. Values exceeded 0.5 in 50.85% of the texture.
Normal builds retain the rejection guard. Review builds force all outputs into `build-landmarks-cache/hoard/passN/`.

Set `hoard_pass` to 1 or 2 to repeat a pass.

```sh
hoard_pass=2
mkdir -p build-landmarks-cache/hoard/tmp
TMPDIR="$PWD/build-landmarks-cache/hoard/tmp" /opt/homebrew/bin/blender -b --factory-startup --python tools/build_props.py -- --from-tripo assets/tripo-raw --prop hoard_pile --hoard-review-pass "$hoard_pass" --render
```
