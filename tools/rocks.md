# Rocks and the terrain detail maps

Two assets for the valley slopes, both generated entirely in code and verified
by their own builders. Tested with Blender 5.2.1 and the system `python3`
(numpy 2.5, Pillow 12.3). Both outputs are byte-identical across fresh builds.

## `assets/props/rocks.glb`

Six static meshes for instanced scatter, built by `tools/build_rocks.py`.
Dimensions are metres in glTF coordinates (X, Y up, Z); the base is the
lowest vertex, the ground line is Y=0.

| Node | Reads as | X × Y × Z | Base Y | Triangles | Vertices | COLOR_0 grey |
|---|---|---|---:|---:|---:|---|
| `rock_0` | rounded boulder, one cleft | 4.04 × 2.99 × 4.01 | -0.60 | 548 | 1,620 | 0.35..0.69 (mean 0.54) |
| `rock_1` | big angular boulder | 8.06 × 5.00 × 7.01 | -0.60 | 936 | 2,756 | 0.35..0.71 (mean 0.54) |
| `rock_2` | flat slab, chipped rim | 9.02 × 1.80 × 6.00 | -0.60 | 638 | 1,913 | 0.35..0.72 (mean 0.56) |
| `rock_3` | leaning crag shard, broken tip | 4.00 × 9.99 × 3.99 | -0.60 | 790 | 2,337 | 0.35..0.67 (mean 0.53) |
| `rock_4` | five fused scree stones | 7.00 × 2.50 × 6.97 | -0.60 | 984 | 2,784 | 0.35..0.72 (mean 0.51) |
| `rock_5` | outcrop with a lower shoulder | 16.00 × 8.02 × 12.02 | -0.60 | 1,462 | 4,305 | 0.35..0.71 (mean 0.54) |

Total 5,358 triangles, 540 KB.

### Engine contract

- One GLB, six nodes, all scene roots, named `rock_0`..`rock_5`, each with an
  identity transform (no `translation`/`rotation`/`scale`/`matrix` keys) and
  one mesh of the same name. No skins, armatures, animations, morph targets,
  cameras, lights, images or textures. No extensions used.
- Each mesh is one primitive, default mode (triangles), indexed with
  `unsigned short` (5123) indices, attributes `POSITION` (float VEC3),
  `NORMAL` (float VEC3, unit length) and `COLOR_0` (**VEC4 unsigned short
  normalized**, componentType 5123 with `normalized: true`; alpha is 1). Buffer
  views are tightly packed, no `byteStride`. Vertices are split per facet
  (smooth across bends under 28 degrees, sharp across the cleaved planes), so
  the vertex count is about three times the triangle count.
- One shared material `rock` (metallic 0, roughness 0.95, double-sided, no
  base colour texture). The colour comes from `COLOR_0` alone: neutral grey,
  R=G=B, linear 0.35..0.75, dark in crevices (ray-cast ambient occlusion and
  concavity), lighter on upward faces (weathering), with a small per-facet
  tint. Multiply it by the palette's rock colour; do not add another AO.
- Centred on X/Z within 0.1 m, top at the height above, geometry continuing
  to Y=-0.60 with a flat cap, so a rock can be dropped on a slope with its
  origin on the terrain and its downhill edge stays buried until the slope
  exceeds roughly `atan(0.6 / half-width)`. Each mesh is a single closed
  shell (the scree stones and the outcrop's shoulder are boolean-unioned), so
  there are no interior faces and no stray fragments.
- Use at scale 1 for these dimensions; uniform scaling by 0.5..2 stays inside
  the budget and the silhouettes hold (they were checked at 250 m).

### How they are built

Each variant starts as an icosphere, stretched, displaced radially by two
scales of Perlin noise plus a Chebyshev-metric Voronoi term that pushes out
blocky, joint-bounded facets, and tapered toward the top. Random planes then
cleave facets off the outside (`bisect_plane` with the outer side cleared and
the cut capped), biased toward vertical cuts for the crag and horizontal ones
for the slab's top. Any lump a deep cleave severs is dropped (largest shell
kept); the scree's stones and the outcrop's shoulder are exact-solver boolean
unions. The result is decimated (collapse) to the triangle target, cut flat at
Y=-0.6, welded and cleaned of slivers, triangulated, then painted: 32
cosine-weighted rays per vertex against the mesh's own BVH out to 0.6 of its
size, a concavity term from the neighbour edges, the upward-facing bonus and
a noise grain, clamped to 0.35..0.75 and written per corner with a random
per-face tint of ±0.04. Everything is seeded (`random.Random(9000 + index)`)
and Blender's `mathutils.noise` is deterministic, so the GLB is byte-identical
between builds; this was checked by building twice and `cmp`.

### Rebuild, verify, look

```sh
blender -b --factory-startup --python tools/build_rocks.py                 # build + verify
blender -b --factory-startup --python tools/build_rocks.py -- --render     # ... and artifacts/rocks/*.png
blender -b --factory-startup --python tools/build_rocks.py -- --verify-only
```

The verifier parses the GLB binary itself (struct/json, reading the accessor
buffers), checks the node and mesh contract above, the triangle budget
(300..1,500), the bounds (base Y in -1.0..-0.3, top within 10% of the target
height, X/Z within 15% and centred), that `COLOR_0` is present, neutral and in
range, that no mesh has a piece smaller than 3% of its triangles (a stray
fragment) and that only the scree has more than one piece; then imports the
GLB back into Blender and checks six unparented mesh objects at identity, the
triangle counts, the vertex colour, the material sampling it, and the bounds
again. A Python exception exits nonzero; a copy with `COLOR_0` removed from
`rock_3` was refused with `rock_3: missing COLOR_0`, exit 1.

`--render` writes `artifacts/rocks/rocks.png` (the six in a row, three-quarter
view from a dragon's-eye elevation), `rocks_far.png` (the same row from
250 m, the silhouette check) and `rock_0.png`..`rock_5.png` (each alone at 3.2
times its longest side). Cycles, a sun and a dark neutral ground; the material
samples the shipped `COLOR_0` directly. The first two builds looked like
marshmallows: the icosphere had 1,280 faces (bmesh counts subdivisions from
20), the displacement was sampled too coarsely, and a 0.35..0.71 albedo range
rendered flat white until the exposure came down a stop. Both are fixed in
the script; the renders in `artifacts/rocks/` are of the shipped file.

Final verifier output:

```text
VERIFY assets/props/rocks.glb: PASS
  six mesh nodes rock_0..rock_5 at identity; no skins/animations/cameras/lights/textures; POSITION+NORMAL+COLOR_0; Blender round trip matches: PASS
  rock_0: size X/Y/Z=(4.04, 2.99, 4.01) m, base Y=-0.60, triangles=548, vertices=1620, grey 0.35..0.69 (mean 0.54)
  rock_1: size X/Y/Z=(8.06, 5.00, 7.01) m, base Y=-0.60, triangles=936, vertices=2756, grey 0.35..0.71 (mean 0.54)
  rock_2: size X/Y/Z=(9.02, 1.80, 6.00) m, base Y=-0.60, triangles=638, vertices=1913, grey 0.35..0.72 (mean 0.56)
  rock_3: size X/Y/Z=(4.00, 9.99, 3.99) m, base Y=-0.60, triangles=790, vertices=2337, grey 0.35..0.67 (mean 0.53)
  rock_4: size X/Y/Z=(7.00, 2.50, 6.97) m, base Y=-0.60, triangles=984, vertices=2784, grey 0.35..0.72 (mean 0.51)
  rock_5: size X/Y/Z=(16.00, 8.02, 12.02) m, base Y=-0.60, triangles=1462, vertices=4305, grey 0.35..0.71 (mean 0.54)
```

## `assets/textures/terrain_detail.png`

1024 × 1024 RGBA, four independent greyscale detail maps, one per channel,
built by `tools/build_terrain_detail.py` (pure numpy + Pillow, about three
seconds). Each channel has mean 0.5 and is meant to multiply the terrain
colour by `0.5 + channel`, so a flat channel is a no-op and the map only ever
scales the palette colour by 0.5..1.5. The tile is designed for **16 m** of
world per repeat; the feature sizes below assume that.

| Channel | Material | What is in it | Feature size | Mean | Std |
|---|---|---|---|---:|---:|
| R | rock | near-horizontal strata with a bright weathered top and a dark undercut per bed, bent by a broad noise; thin wandering cracks along cell joints; stretched chip facets; fine grain | beds 1.6 m, cracks every ~2.5 m, chips 0.4..0.8 m | 0.500 | 0.150 |
| G | grass | tufts at two sizes, brighter at the crown, over patches that thin the sward to bare darker ground; blade-scale roughness | tufts 0.25 and 0.6 m, patches 1.5 m | 0.500 | 0.150 |
| B | dirt / scree | packed gravel and a sparser layer of larger pebbles, each lit from the upper left, over fine grain and a broad tone variation | gravel 0.06..0.16 m, pebbles 0.2..0.56 m | 0.500 | 0.150 |
| A | snow | soft drifts stretched along the wind, sastrugi crests confined to windward patches, a faint sparkle | drifts ~6 m, crests ~1 m | 0.500 | 0.060 |

The PNG's alpha channel is data, not coverage: a viewer will show the
image as mostly translucent. Sample it as a plain 4-channel texture with
alpha not premultiplied, linear (it is not colour; do not sRGB-decode it),
wrap mode repeat on both axes, mipmapped. Whether the tile spans 16 m or the
engine blends two scales (say 16 m and 90 m) to hide the repeat at 300 m is
the engine's call; the maps are built so that either works.

### Tileability

Every term is periodic by construction: the noise is white noise filtered
in the frequency domain over the tile (an FFT is periodic), the strata use an
integer number of beds per tile, and the point features (Worley cells,
pebble discs) are stamped with wrapped indices. No edge blending is applied.
The verifier measures the seam: the mean absolute step between column 0 and
column 1023 (and row 0 / row 1023) against the mean step between adjacent
interior columns (rows), and requires the ratio under 1.5:

```text
VERIFY assets/textures/terrain_detail.png: 1024x1024 RGBA
  R rock  mean=0.500 std=0.150 min=0.16 max=0.81 seam/interior step: cols 0.0545/0.0540 rows 0.0561/0.0547 (ratio 1.02) PASS
  G grass mean=0.500 std=0.150 min=0.18 max=0.84 seam/interior step: cols 0.0254/0.0248 rows 0.0298/0.0301 (ratio 1.02) PASS
  B dirt  mean=0.500 std=0.150 min=0.16 max=0.84 seam/interior step: cols 0.1055/0.1071 rows 0.1192/0.1071 (ratio 1.11) PASS
  A snow  mean=0.500 std=0.060 min=0.31 max=0.71 seam/interior step: cols 0.0113/0.0137 rows 0.0104/0.0101 (ratio 1.03) PASS
VERIFY terrain_detail: PASS
```

The first build failed this on the rock rows (ratio 3.3): the strata period
was 1.4 m, which does not divide 16 m. The bed count is now rounded to an
integer. The soft clip that folds the tails into 0..1 also rescales the
middle, so each map is renormalised to its target std after clipping.

### Rebuild, verify, look

```sh
python3 tools/build_terrain_detail.py                 # build, verify, write the preview
python3 tools/build_terrain_detail.py --verify-only
```

`artifacts/rocks/terrain_detail_tiled.png` shows each channel tiled 2 × 2
as greyscale (a seam would appear as a cross through the middle of each
panel). The rock panel was iterated three times: the first read as dried-mud
crackle (chips and cracks over the strata), the second as a contour map (the
beds bent too far); the shipped one reads as bedded rock with joints.
