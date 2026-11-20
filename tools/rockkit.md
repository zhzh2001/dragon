# Rock and cliff kit

`tools/build_rockkit.py` builds the kit from Poly Haven scans and local Tripo meshes.
It was tested with Blender 5.3.0.
The original scan kit contains eight pieces at three levels of detail (LOD).
The Tripo additions are recorded under "Tripo pieces (2026-11-16)" below.
All sizes use metres and glTF axes: X width, Y up, Z depth.

| Piece | Poly Haven id | X x Y x Z | Triangles LOD0 / 1 / 2 | Scan scale |
|---|---|---|---:|---:|
| `boulder_a` | `boulder_01` | 3.81 x 2.71 x 5.40 | 7,880 / 1,970 / 492 | 3.00 |
| `boulder_b` | `namaqualand_boulder_03` | 3.45 x 1.91 x 4.40 | 7,880 / 1,970 / 492 | 1.44 |
| `boulder_c` | `namaqualand_boulder_04` | 3.00 x 2.03 x 2.97 | 7,880 / 1,970 / 492 | 1.19 |
| `slab_a` | `namaqualand_boulder_05` | 7.00 x 1.74 x 4.21 | 7,880 / 1,970 / 492 | 5.13 |
| `slab_b` | `rock_09` | 4.44 x 1.70 x 8.60 | 7,880 / 1,970 / 492 | 59.9 |
| `cliff_a` | `rock_face_01` | 32.00 x 24.89 x 25.30 | 15,181 / 3,795 / 948 | 8.73 |
| `cliff_b` | `rock_face_02` | 27.99 x 25.53 x 18.29 | 19,699 / 4,923 / 1,231 | 10.33 |
| `cliff_c` | `mountainside` | 33.01 x 23.12 x 16.98 | 19,699 / 4,924 / 1,230 | 3.23 |

All eight scans are Poly Haven models, licence CC0. The script downloads the
glTF form with 4K maps from `https://api.polyhaven.com/files/<id>`. Nothing
else was in the original scan kit. The scans were not made by an AI tool.

## File contract

- glTF binary, Y-up, metres, +Z forward. A cliff's face looks along +Z.
- One node with no transform, one mesh, one triangle primitive, one material.
- The origin is the centre of the bounds at the base, Y=0.
- Attributes: `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0`.
- Three embedded maps: base colour (JPEG, sRGB), tangent-space normal (PNG,
  OpenGL convention, +Y up), and one packed map (JPEG) with occlusion in R,
  roughness in G and metallic 0 in B. The occlusion texture and the
  metallic-roughness texture are the same texture index.
- Map size is 2048 for LOD0, 1024 for `_lod1`, 512 for `_lod2`. Every LOD of
  a piece uses the same atlas layout.
- No skin, no bone. `tools/props.md` gives the run props a `root` bone. This
  brief did not ask for one. If the prop loader needs a skin, add it in the
  export step.

## What the script does to each scan

1. It welds the scan, drops loose flakes, orients it and scales it.
2. Boulders and slabs: it cuts the lowest 10% to 14% off and closes the cut.
   The flat underside gives a base that sits on the ground. The underside
   gets a plain colour and a tenth of its share of the atlas.
3. Cliffs: it turns the scan to face -Y in Blender and tips it to stand at
   72 to 74 degrees. Then it bends the rim back. See "Setting a cliff into a
   slope".
4. `cliff_a` only: it cuts the left 26% off. That part of the scan is a ledge
   over a gap, and the gap showed the slope through the wall.
5. `slab_a`, `cliff_a` and `cliff_c` are squashed in height (0.72, 0.80,
   0.68) to reach the brief's proportions.
6. It decimates a copy to the LOD0 budget and unwraps it into a new atlas.
   The islands come from a smoothed copy of the mesh. A projection of the
   rough mesh makes thousands of chips.
7. It bakes three maps from the scan onto LOD0 with Cycles: base colour, the
   scan's occlusion-roughness map, and the normal map. The normal bake reads
   the scan's mesh and the scan's own 4K normal map.
8. It removes directional light from the base colour and grades it.
9. It decimates LOD0 to 25% and 6.25% and exports the three files. Blender
   writes the geometry and the tangents. The script then writes the material
   and the three images into the file itself, so the exporter cannot
   re-encode them.

## Colour

The kit's colour is a mean linear albedo of (0.232, 0.208, 0.180). That is a
little under the engine palette's Rock (0.26, 0.23, 0.20), because a scan's
mean includes its dark cracks. The grade does this, in linear light:

- It fits `log luminance = a + b . n` over the texels, where `n` is the
  surface direction. A baked sun or sky shows as a `b` that is not zero. It
  divides that term out.
- It removes the scan's own colour cast and keeps 32% of its local hue
  variation. Lichen and iron stains stay visible but muted.
- It pulls the spread of log luminance toward 0.45 (gain limited to 0.75 to
  1.5).
- It scales each channel so the mean is the target.

Measured `b` before the grade, Blender axes (x, y, z-up):

| Piece | b before | Rest after, measured on the file |
|---|---|---:|
| `boulder_a` | (-0.07, -0.04, -0.03) | 0.012 |
| `boulder_b` | (0.34, -0.53, 0.51) | 0.080 |
| `boulder_c` | (-0.02, -0.14, 0.68) | 0.073 |
| `slab_a` | (-0.22, 0.06, 0.00) | 0.033 |
| `slab_b` | (-0.10, 0.16, 0.16) | 0.119 |
| `cliff_a` | (-0.14, 0.10, -0.04) | 0.052 |
| `cliff_b` | (-0.06, 0.01, 0.27) | 0.045 |
| `cliff_c` | (-0.23, -0.45, 0.01) | 0.124 |

The build's fit is per texel and ends at 0.000. The verifier measures the
same slope again at the vertices of the exported file, weighted by area. The
two samples differ, so a rest of up to 0.12 remains. The verifier's limit is
0.15. A value of 0.12 means a face turned toward the old light is about 12%
brighter than one at right angles to it.

Roughness is shifted so every piece has a mean of 0.86.

## Setting a cliff into a slope

A cliff piece is a shell with an open back. The script shapes it for a slope
of 60 degrees:

- A reference plane at 60 degrees passes through the face near its top.
- Every vertex within 22% of the piece's size from the scan's outer edge is
  pulled back toward a line 2.0 m behind that plane. The outer edge itself
  ends on that line.

So when the reference plane lies on the terrain, the whole rim is 2 m under
the ground. No free edge and no open back shows. The face stands 5 m to 8 m
proud of the slope at its foot. Scree or a terrain step must cover the foot.

To find the plane in a file: in glTF axes the plane is
`z = -(anchor + y / tan(60))`. `slope_anchor()` in the script computes
`anchor` from the rim. On a slope of 45 degrees the foot stands further out.
On a slope steeper than 60 degrees the rim's margin shrinks toward the top.
Neither case was rendered.

Two pieces side by side must overlap by 4 m to 6 m. Their bent sides then
meet in a gully.

## Rebuild

```sh
blender -b --factory-startup --python tools/build_rockkit.py
blender -b --factory-startup --python tools/build_rockkit.py -- --verify-only
blender -b --factory-startup --python tools/build_rockkit.py -- --render
blender -b --factory-startup --python tools/build_rockkit.py -- --only cliff_a,slab_b
blender -b --factory-startup --python tools/build_rockkit.py -- --render --shots wall
```

The cache is `build-rockkit-cache/`, ignored by git.
A default run rebuilds the scans and all available accepted Tripo sources, then verifies the kit.
The original scan-only build took about four minutes with cached downloads.
`--only` builds the named pieces and does not verify.
A failed check prints the reason and `ROCKKIT FAILED`. Blender exits with code 1.

## Verification

The verifier reads the accessor buffers and the embedded images of every
file. It asserts:

- one node without a transform, one mesh, one primitive, one material, no
  skin, no animation;
- `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0`; tangents of unit length with
  a handedness of 1 or -1; UVs inside 0..1;
- triangles: LOD0 within the budget, LOD1 at 20% to 30% of LOD0, LOD2 at
  4.5% to 8%;
- size: boulders 2 m to 6 m across, slabs 4 m to 10 m long and no higher
  than half their length, cliffs 20 m to 40 m wide and 15 m to 30 m high;
- origin: base at Y=0 and the bounds centred on X and Z, within 1 cm;
- silhouette: the filled outline of LOD1 and LOD2 from the front, the side
  and the top overlaps LOD0's by at least 95% and 90% (intersection over
  union on a 192-cell grid), and the bounds move by no more than 3% and 6%;
- maps: three images, the expected size per LOD, a PNG normal map of unit
  length that points out of the surface, one packed texture for occlusion
  and roughness, metallic 0;
- no baked light: the slope described under "Colour" is under 0.15;
- albedo: every piece's mean, per channel and in luminance, is within 8% of
  the kit's mean;
- the original scan group contains 24 files and remains at most 80 MB;
- additional files belong to registered Tripo pieces or `pieces.json`.

Final output:

```text
VERIFY boulder_a: PASS
  glTF Y-up size X/Y/Z metres: (3.81, 2.71, 5.40); base y=-0.001; origin offset (-0.002, 0.002)
  triangles LOD0/1/2: 7880/1970/492 (25.0%, 6.2%); budget 8000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.986, LOD2 0.959
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  mean linear albedo: (0.2338, 0.2092, 0.1813); directional light term 0.012 (limit 0.15)
  bytes LOD0/1/2: 6801876/1880856/548408
VERIFY boulder_b: PASS
  glTF Y-up size X/Y/Z metres: (3.45, 1.91, 4.40); base y=-0.001; origin offset (0.000, 0.003)
  triangles LOD0/1/2: 7880/1970/492 (25.0%, 6.2%); budget 8000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.993, LOD2 0.981
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  mean linear albedo: (0.2330, 0.2082, 0.1781); directional light term 0.080 (limit 0.15)
  bytes LOD0/1/2: 5273588/1559768/470260
VERIFY boulder_c: PASS
  glTF Y-up size X/Y/Z metres: (3.00, 2.03, 2.97); base y=-0.001; origin offset (-0.001, 0.003)
  triangles LOD0/1/2: 7880/1970/492 (25.0%, 6.2%); budget 8000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.993, LOD2 0.985
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  mean linear albedo: (0.2330, 0.2085, 0.1803); directional light term 0.073 (limit 0.15)
  bytes LOD0/1/2: 5846276/1742352/516656
VERIFY slab_a: PASS
  glTF Y-up size X/Y/Z metres: (7.00, 1.74, 4.21); base y=-0.004; origin offset (-0.001, 0.001)
  triangles LOD0/1/2: 7880/1970/492 (25.0%, 6.2%); budget 8000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.992, LOD2 0.982
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  mean linear albedo: (0.2344, 0.2093, 0.1812); directional light term 0.033 (limit 0.15)
  bytes LOD0/1/2: 6772356/1943148/543164
VERIFY slab_b: PASS
  glTF Y-up size X/Y/Z metres: (4.44, 1.70, 8.60); base y=-0.001; origin offset (0.000, -0.001)
  triangles LOD0/1/2: 7880/1970/492 (25.0%, 6.2%); budget 8000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.977, LOD2 0.968
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  mean linear albedo: (0.2362, 0.2114, 0.1804); directional light term 0.119 (limit 0.15)
  bytes LOD0/1/2: 4400620/1373392/418844
VERIFY cliff_a: PASS
  glTF Y-up size X/Y/Z metres: (32.00, 24.89, 25.30); base y=0.000; origin offset (0.000, 0.000)
  triangles LOD0/1/2: 15181/3795/948 (25.0%, 6.2%); budget 20000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.986, LOD2 0.963
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  mean linear albedo: (0.2302, 0.2060, 0.1781); directional light term 0.052 (limit 0.15)
  bytes LOD0/1/2: 7658656/2141636/587732
VERIFY cliff_b: PASS
  glTF Y-up size X/Y/Z metres: (27.99, 25.53, 18.29); base y=0.000; origin offset (0.003, 0.000)
  triangles LOD0/1/2: 19699/4923/1231 (25.0%, 6.2%); budget 20000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.992, LOD2 0.972
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  mean linear albedo: (0.2328, 0.2084, 0.1793); directional light term 0.045 (limit 0.15)
  bytes LOD0/1/2: 7378212/2144632/613564
VERIFY cliff_c: PASS
  glTF Y-up size X/Y/Z metres: (33.01, 23.12, 16.98); base y=0.003; origin offset (0.008, 0.008)
  triangles LOD0/1/2: 19699/4924/1230 (25.0%, 6.2%); budget 20000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.989, LOD2 0.972
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  mean linear albedo: (0.2324, 0.2083, 0.1804); directional light term 0.124 (limit 0.15)
  bytes LOD0/1/2: 7221056/2088232/611004
VERIFY kit: PASS
  kit mean linear albedo: (0.2332, 0.2087, 0.1799); worst piece deviation 1.31% (limit 8%)
  directory size: 70.54 MB of 80 MB, 24 files
```

## Renders

`--render` reads the files on disk and writes three 1920x1080 images to
`artifacts/rockkit/`. The light is a sun at 20 degrees elevation and a
uniform blue sky. The engine is Cycles with the AgX view.

- `sheet.png`: every piece at LOD0 from two angles on grey ground (albedo
  0.20).
- `lods.png`: `boulder_a` and `cliff_a` at the three levels.
- `wall.png`: a 60 degree slope plane 120 m wide. The three cliffs stand
  with their reference planes on it and overlap by about 4 m. The camera is
  250 m from the face at the height of the cliffs' middle (28.4 m), lens
  74 mm. Below the cliffs, 5,200 instances of the five boulders and slabs
  lie on a fan at 33 degrees, 0.4 m to 5 m across, small at the crest and
  large at the toe.

Three things in `wall.png` are staging and not part of the kit: the slope's
turf-and-rock material, the fan surface under the scree (a gravel-coloured
mesh), and the floor. The scree needs a surface to lie on, because a rock
cannot rest on a 60 degree plane.

All three renders were inspected at full size. Earlier rounds failed and
were fixed: the first wall showed three plates standing in front of a flat
olive plane, and `cliff_a` showed the slope through a gap under its ledge.

## Decisions

- Normal maps store 7 bits for X and Y and 6 bits for Z. The error is under
  one degree. Full 8-bit PNGs put the kit at 86 MB.
- Cliffs use 2K maps, not 4K. Three 4K normal PNGs do not fit in 80 MB.
- The LODs carry smaller copies of the maps, not references to LOD0's. A
  `.glb` then loads alone.
- `metallicFactor` is 1.0 and the blue channel is 0, as glTF multiplies the
  two.
- Width counts along X. `cliff_a` is 25 m deep along Z because its bent
  sides reach back into the slope.

## What is weak

- A cliff covers 30 m with a 2K map: 60 to 70 texels per metre. It holds at
  flight distance. From 10 m it is soft. The engine needs a tiling detail
  normal for close passes.
- `slab_b` and `boulder_a` are small stones scaled up 60 and 3 times. Their
  shapes are right, but their surface detail is the wrong scale close up.
- The bent rim stretches the scan's texture by up to 2 times on the sides
  and under the foot. It shows in `sheet.png` on the second view of each
  cliff.
- The foot of a cliff overhangs the slope and has no ground of its own.
  Scree or terrain must hide it. In `wall.png` the fan does.
- `cliff_b` has a flat top with a thin ledge. Two copies side by side will
  show the repeat.
- The three cliffs are 28 m to 33 m wide. A long band needs each one several
  times. Mirror and scale them by 0.8 to 1.2 to hide the repeat.
- The light removal is linear in the surface direction. Occlusion that the
  scan baked into its cracks stays in the base colour.
- The JPEG packed map leaves stray values in the metallic channel. The
  verifier allows a mean of 3/255 and 32/255 at the 99.9th percentile.
- Nothing was loaded in the engine. The files were checked by the verifier
  and by re-import into Blender for the renders.

## Tripo pieces (2026-11-16)

The local Tripo exports are read-only inputs in `assets/tripo-raw/`.
Their hashes are in `artifacts/rockkit/tripo/source-sha256.json`.
The cache uses a real directory inside this worktree.
The builder rejects output paths that resolve outside the worktree.
The original 24 scan files remain byte-identical.
Their hashes are in `artifacts/rockkit/tripo/original-sha256.json`.
No C++ file changed. Engine integration remains separate.

### Processing

The builder imports each full source mesh and keeps its original normal map for baking.
It removes loose fragments, cuts the bottom 3.5% flat, and closes the cut.
It scales the width to the target and adjusts height separately where specified.
The solid pieces retain their depth. Their rims are not bent into shells.
The builder sets XYZ rotation mode before applying a wall's yaw.
Tripo imports use quaternion mode, which ignored the first Euler rotations.

LOD0 receives a new atlas and three maps baked from the high mesh.
The maps use the existing kit sizes, image formats and material layout.
Occlusion is constant 1.0 because Tripo supplies no occlusion map.
Roughness uses the source map's green channel and targets a mean of 0.86.
The metallic channel is written as zero.

The existing grade removes directional light and mutes the source hue.
It targets linear albedo (0.232, 0.208, 0.180).
A second directional correction uses area-weighted samples at the mesh's UV coordinates.
This correction removes residual lighting that the texel fit missed.
The verifier measures the encoded GLB again, after JPEG compression.

The first scree LOD2 failed silhouette overlap at 0.878.
Outline weights now protect its projected boundary during decimation.
The modifier ratio stays at 0.0625. The resulting file contains 801 triangles.
Its weakest silhouette overlap is 0.905.

`boulder_e` LOD2 also exposed JPEG colour bleed into the metallic channel.
The packed-map encoder now supports JPEG 4:4:4, with full colour resolution.
New Tripo builds use that encoder. It requires `ffmpeg` on `PATH`.
The original scan encoder stays unchanged.

### Normal projection repair

The first bake projected some texels onto nearby back-facing surfaces.
The source normal maps had no negative-Z samples.
The builder now tries three ray offsets: 0.1, 0.35 and 1.0 times the original offset.
The original offset is 1.2% of the piece's bounding-box diagonal.
Each texel keeps the nearest valid outward hit.
The builder fills remaining rejected projections from nearby valid texels, then normalizes the result.
It does not reverse negative Z values or reduce valid normal strength.

`normal-repairs.json` records the offsets and replacement fractions for each piece.
The repair preserves geometry, base-colour JPEGs and packed-map JPEGs.
Every encoded Tripo normal must point outward and stay within the unit-length quantization bound.
The original scan verifier remains unchanged.
Tripo maps do not use the scan verifier's mean-Z threshold of 0.80.
That average rejected valid strong normals on scree, even after all reversed samples were removed.
The per-texel checks enforce the file contract directly.

The normal-only path also needs the source translation applied after LOD0 decimation.
`source-alignment.json` records that translation and its size in atlas texels.
The repair path now reads the recorded translation in Blender coordinates: X, Y, Z-up.
Ridge, scree and `boulder_f` needed aligned rebakes because their omitted shifts exceeded one average atlas texel.
The crag and the other boulders had sub-texel shifts.

### Placement data

`assets/props/rockkit/pieces.json` stores size, kind and ground check points for each accepted addition.
All coordinates use glTF axes and metres.
The points come from LOD2. Each piece has at most 26 points.
Sampling starts with the lower 3.5% of LOD2.
The band expands when fewer than eight vertices or 25% of the projected footprint are available.
The JSON records the band height and footprint coverage.
Walls also use rear points from successive height bands.

After yaw and horizontal placement, lower each piece until every check point sits at least 0.15 m below terrain.
The flat-ground check lowers `boulder_e` by 0.633306 m.
Its placed render shows continuous ground contact across most of the base, with a natural undercut on the left.
Use `--placed --only boulder_e` to repeat that render.

No wall ships from this round. The following wall rules apply to diagnostic builds.
Keep each piece's Y axis vertical. Rotate around Y to face the wall downhill.
Do not rotate its mounting normal onto the terrain normal.
That old shell-placement rule would tilt these solids.

For slope angle `a`, use normal `n = (0, cos(a), sin(a))`.
Set `d = max(dot(n, p)) + 0.15` over the wall's check points.
The mounting point is `(0, 0, d / n.z)`.
Translate that point onto the terrain plane after yaw rotation.
The JSON also stores the reference plane for a 53.5-degree slope.
Actual terrain must contain every check point after placement.

The mounted inspection views use this rule on 45-degree and 62-degree slopes.
`rejected-wall-placement.json` records exposed vertex fractions, relief and check-point distances for the rejected walls.
These are planar checks. They do not replace the engine's checks against curved terrain.

### Rejected candidates

`wall_a` used `tripo-cliff-01.glb`, with its original +Z face forward.
It passed the initial numeric checks after the light correction.
The final close view retained an isolated black cavity.
The top view showed repeated rectangular joints.
The three output GLBs were removed. The raw and final renders remain as evidence.

`wall_b` used `tripo-cliff-03.glb`, with its +X face rotated toward +Z.
It passed the initial numeric checks. Its front showed vertical rock fractures.
The top remained a broad, nearly flat cap with weak texture.
Mounted views showed that cap exposed on both 45-degree and 62-degree slopes.
The three output GLBs were removed. The mounted views remain as rejection evidence.
No wall candidate met the brief's visual requirement.
The valley-wall asset gap remains open.

`wall_c` used `tripo-cliff-04.glb`, rotated to show its broad ledge.
Its final top view showed a broad flat roof and a diagonal slab.
The measured 39.84 m Z extent was not solid wall thickness.
Its initial mean-Z check failed at 0.74967 against 0.80.
The later per-texel rule supersedes that heuristic. Its visual faults still require rejection.
The three output GLBs were removed. Its final renders and build measurements remain as evidence.

Normal builds exclude rejected candidates.
`--diagnostic` permits a deliberate rebuild for inspection.
Rejected candidates do not appear in the placement JSON.

### Accepted pieces

Six pieces passed the file checks and visual review.
They add 18 GLBs, totalling 46,094,152 bytes.
The table uses measurements from the final encoded files.
Each piece received the common base cut, atlas bake and grade described above.
`boulder_f` arrived in the source folder during this task and was included.

| Piece | Source | X, Y, Z metres | Triangles LOD0/1/2 | Mean linear albedo RGB | Shape work |
|---|---|---|---|---|---|
| `tor_a` | `tripo-cliff-05.glb` | 22.01, 24.01, 22.75 | 19699/4924/1230 | 0.2216, 0.1990, 0.1724 | Crag; fit width 22 m and height 24 m. |
| `ridge_a` | `tripo-ridge-01.glb` | 40.03, 16.05, 26.27 | 19699/4922/1230 | 0.2319, 0.2079, 0.1796 | Crag; fit width 40 m and height 16 m. |
| `scree_a` | `tripo-scree-01.glb` | 20.01, 7.03, 20.33 | 11819/2953/801 | 0.2206, 0.1986, 0.1723 | Mound; fit width 20 m and height 7 m. |
| `boulder_d` | `tripo-boulder-01.glb` | 4.51, 2.60, 4.54 | 7878/1968/492 | 0.2337, 0.2087, 0.1805 | Boulder; fit X to 4.5 m. |
| `boulder_e` | `tripo-boulder-02.glb` | 5.01, 3.99, 5.10 | 7880/1970/492 | 0.2338, 0.2090, 0.1807 | Boulder; fit X to 5 m. |
| `boulder_f` | `tripo-boulder-03.glb` | 3.23, 3.08, 4.00 | 7879/1969/491 | 0.2211, 0.1980, 0.1710 | Boulder; fit Z to 4 m. |

The crag, ridge and scree have irregular fractured forms.
Their 2K maps become soft in close views of these large surfaces.
The three boulders retain more surface detail per metre.
This round adds no accepted wall.

### Rebuild the additions

Run these commands from the worktree root.
The source files must be available under `assets/tripo-raw/` for a rebuild.
Verification and final rendering read the exported files and do not need the raw sources.
Use `--only` to preserve the original scan files.
Run Blender in the foreground.

```sh
tripo_pieces=tor_a,ridge_a,scree_a,boulder_d,boulder_e,boulder_f
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
  --python tools/build_rockkit.py -- --only "$tripo_pieces"
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
  --python tools/build_rockkit.py -- --metadata
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
  --python tools/build_rockkit.py -- --verify-only --only "$tripo_pieces"
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
  --python tools/build_rockkit.py -- --render --only "$tripo_pieces"
```

Each accepted piece has front, side, top and close renders in `artifacts/rockkit/tripo/`.
The rejected walls also have raw cardinal views and front views from 50 m.
`wall_b` has mounted views on both endpoint slopes.
`visual-review.json` records the acceptance decisions and rejection evidence.
`final-sha256.json` identifies the accepted GLBs.

The placement audit checked every listed point against its LOD2 vertex buffer.
Each footprint has at least three noncollinear points and at most 26 points.
The JSON sizes match the LOD0 buffers. Rejected walls are absent.
All 24 original files matched their baseline hashes.
`git diff --stat -- assets/props/rockkit` showed no changes to those tracked files before staging the additions.

### Final verification output

The command above produced this output for the six accepted pieces:

```text
VERIFY tor_a: PASS
  glTF Y-up size X/Y/Z metres: (22.01, 24.01, 22.75); base y=0.000; origin offset (0.000, 0.000)
  triangles LOD0/1/2: 19699/4924/1230 (25.0%, 6.2%); budget 20000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.992, LOD2 0.947
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  normal texels: outward; minimum Z 0.0039; maximum unit error 0.0251; LOD0 mean Z 0.8641
  mean linear albedo: (0.2216, 0.1990, 0.1724); directional light term 0.003 (limit 0.15)
  bytes LOD0/1/2: 5878012/1950860/660452
VERIFY ridge_a: PASS
  glTF Y-up size X/Y/Z metres: (40.03, 16.05, 26.27); base y=0.000; origin offset (0.000, 0.000)
  triangles LOD0/1/2: 19699/4922/1230 (25.0%, 6.2%); budget 20000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.987, LOD2 0.958
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  normal texels: outward; minimum Z 0.0039; maximum unit error 0.0251; LOD0 mean Z 0.8296
  mean linear albedo: (0.2319, 0.2079, 0.1796); directional light term 0.002 (limit 0.15)
  bytes LOD0/1/2: 6100852/2007640/661000
VERIFY scree_a: PASS
  glTF Y-up size X/Y/Z metres: (20.01, 7.03, 20.33); base y=0.000; origin offset (0.000, 0.000)
  triangles LOD0/1/2: 11819/2953/801 (25.0%, 6.8%); budget 12000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.967, LOD2 0.905
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  normal texels: outward; minimum Z 0.0039; maximum unit error 0.0251; LOD0 mean Z 0.7499
  mean linear albedo: (0.2206, 0.1986, 0.1723); directional light term 0.002 (limit 0.15)
  bytes LOD0/1/2: 6899040/2280104/763616
VERIFY boulder_d: PASS
  glTF Y-up size X/Y/Z metres: (4.51, 2.60, 4.54); base y=0.000; origin offset (0.000, 0.000)
  triangles LOD0/1/2: 7878/1968/492 (25.0%, 6.2%); budget 8000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.992, LOD2 0.982
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  normal texels: outward; minimum Z 0.0039; maximum unit error 0.0250; LOD0 mean Z 0.9647
  mean linear albedo: (0.2337, 0.2087, 0.1805); directional light term 0.041 (limit 0.15)
  bytes LOD0/1/2: 3726604/1220864/398136
VERIFY boulder_e: PASS
  glTF Y-up size X/Y/Z metres: (5.01, 3.99, 5.10); base y=0.000; origin offset (0.000, 0.000)
  triangles LOD0/1/2: 7880/1970/492 (25.0%, 6.2%); budget 8000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.990, LOD2 0.967
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  normal texels: outward; minimum Z 0.0039; maximum unit error 0.0251; LOD0 mean Z 0.9382
  mean linear albedo: (0.2338, 0.2090, 0.1807); directional light term 0.022 (limit 0.15)
  bytes LOD0/1/2: 4467784/1480716/532964
VERIFY boulder_f: PASS
  glTF Y-up size X/Y/Z metres: (3.23, 3.08, 4.00); base y=0.000; origin offset (0.000, 0.000)
  triangles LOD0/1/2: 7879/1969/491 (25.0%, 6.2%); budget 8000
  silhouette overlap with LOD0 (worst of front, side, top): LOD1 0.984, LOD2 0.932
  maps base/normal/ORM: 2048/1024/512 px per LOD; normal PNG; ORM packed, metallic 0; tangents: PASS
  normal texels: outward; minimum Z 0.0039; maximum unit error 0.0251; LOD0 mean Z 0.8807
  mean linear albedo: (0.2211, 0.1980, 0.1710); directional light term 0.006 (limit 0.15)
  bytes LOD0/1/2: 4892844/1629052/543612
VERIFY selection: PASS
  selection mean linear albedo: (0.2271, 0.2035, 0.1761); worst piece deviation 2.93% (limit 8%)
  selected bytes: 46094152; directory size: 116.65 MB; original scan kit: 70.54 MB of 80 MB
```

## Wall crags and more rocks (2026-11-20)

### Wall crags

| Piece | Tripo source | X, Y, Z (m) | Triangles LOD0/1/2 | Mean linear albedo RGB |
|---|---|---|---|---|
| `crag_a` | `tripo-crag2-02` | 49.86, 50.05, 55.78 | 23640/5910/1476 | 0.2237, 0.2000, 0.1730 |
| `crag_b` | `tripo-crag2-06` | 60.16, 58.03, 65.22 | 23640/5910/1476 | 0.2206, 0.1971, 0.1703 |
| `crag_c` | `tripo-crag2-07` | 53.42, 54.07, 50.70 | 23639/5909/1477 | 0.2282, 0.2044, 0.1771 |
| `crag_d` | `tripo-crag2-08` | 40.08, 47.03, 34.81 | 23640/5910/1476 | 0.2312, 0.2059, 0.1776 |
| `crag_e` | `tripo-wall2-05` | 61.74, 52.03, 43.48 | 23640/5910/1476 | 0.2303, 0.2062, 0.1786 |

The builder cuts the lowest 3.5% and scales each source uniformly to height.
It turns the selected face toward glTF +Z before it bakes the maps.
Each crag has up to 26 base samples from LOD2, the coarsest mesh.
The builder divides the negative-Z vertices into three height groups.
It takes four farthest-point samples per group for the rear checks.

The row uses a 400 m plane at 55 degrees.
Each joint overlaps by 15% of the narrower neighbour's projected width.
The builder rotates the check points with each crag before it measures burial.
The highest base check sits 0.15 m below the plane along its normal.
[Row placement measurements](../artifacts/rockkit/tripo2/row-placement.json)

The source review excluded four wall candidates.

| Source | Reason | Evidence |
|---|---|---|
| `tripo-crag2-05` | A broad smooth face wraps around the top. A deep shelf cuts across the opposite face. | [Crag source views](../artifacts/tripo-overnight/day2-crags-rocks.jpg) |
| `tripo-wall2-07` | The broad upper block stands on a narrow neck. The exposed outline resembles a pedestal. | [Wall source views](../artifacts/tripo-overnight/day2-walls.jpg) |
| `tripo-wall2-08` | Straight parallel columns form a narrow pillar with a level crown. | [Wall source views](../artifacts/tripo-overnight/day2-walls.jpg) |
| `tripo-crag-02` | Grass covers the low roof. A vertical cut exposes the end of the terrain patch. | [Earlier rock source views](../artifacts/tripo-overnight/rocks.jpg) |

Run these commands from the worktree root. The build needs the local Tripo GLBs and ffmpeg.

```sh
mkdir -p build-rockkit-cache/tmp
export TMPDIR="$PWD/build-rockkit-cache/tmp"
export BLENDER_USER_RESOURCES="$PWD/build-rockkit-cache/blender"
export XDG_CACHE_HOME="$PWD/build-rockkit-cache/cache"
kit() {
  /opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 \
    --python tools/build_rockkit.py -- "$@" </dev/null
}
walls=crag_a,crag_b,crag_c,crag_d,crag_e
kit --only "$walls"
kit --metadata --only "$walls"
kit --verify-only --only "$walls"
kit --render --only "$walls"
kit --row
shasum -a 256 -c artifacts/rockkit/tripo2/original-sha256.txt
```

SHA-256 matched all 42 original GLBs. The six earlier placement entries also matched.
Each new wall has 26 base points and 12 rear points from its LOD2 vertex buffer.
[Placement and hash audit](../artifacts/rockkit/tripo2/audit-walls.txt)

The first row exposed strips of the plane between all five faces.
I changed the order and yaw and moved the shoulders along the slope to meet their neighbours.
The second staging uses a grainy rock material on the same plane.
[Rejected 250 m row](../artifacts/rockkit/tripo2/rejected-row-250.png)

The close views show soft grain on the broad faces and sharp triangles along some fractures.
The left shoulder of `crag_e` has two dark pits visible from above.
Its forward face has a deep vertical joint. Its broken crown remains visible from above.

The revised row forms one band at 250 m. The overhead view shows the narrow saddles between shoulders.
At 80 m, perspective enlarges the nearest spur. The left edge of the test plane enters the frame.
[250 m](../artifacts/rockkit/tripo2/row-250.png) · [80 m](../artifacts/rockkit/tripo2/row-80.png) · [60 degrees above](../artifacts/rockkit/tripo2/row-top.png)

The file verifier reported the following results.

```text
VERIFY crag_a: PASS
VERIFY crag_b: PASS
VERIFY crag_c: PASS
VERIFY crag_d: PASS
VERIFY crag_e: PASS
VERIFY selection: PASS
selection mean linear albedo: (0.2268, 0.2027, 0.1753); worst piece deviation 2.87% (limit 8%)
  selected bytes: 39463016; directory size: 156.11 MB; original scan kit: 70.54 MB of 80 MB
```

[Full verification output](../artifacts/rockkit/tripo2/verify-walls.txt)
