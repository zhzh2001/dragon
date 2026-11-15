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
