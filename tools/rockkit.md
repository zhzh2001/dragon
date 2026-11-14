# Rock and cliff kit

`tools/build_rockkit.py` builds the kit from Poly Haven scans. It was tested
with Blender 5.3.0. The kit is 24 files in `assets/props/rockkit/`: eight
pieces at three levels of detail (LOD). All sizes below are metres in glTF
axes (X width, Y up, Z depth).

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
else is in the kit. The scans were not made by an AI tool.

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

The default run downloads into `build-rockkit-cache/` (about 470 MB with its work files, ignored by
git), builds all eight pieces and verifies. A full build takes about 4
minutes on this Mac with the downloads cached. `--only` builds the named
pieces and does not verify. A failed check prints the reason and
`ROCKKIT FAILED`, and Blender exits with code 1.

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
- the directory holds the 24 files and nothing else, and is at most 80 MB.

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
