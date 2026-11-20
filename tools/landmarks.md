# Valley landmarks

The kit contains five landmarks from the November 16 paid Tripo run.
Each landmark has three levels of detail (LODs).
LOD0 is the near mesh. LOD2 is the mesh in each 250 m render.

## Accepted pieces

| Piece | Tripo source | Size X × Y × Z (m) | Triangles LOD0 / LOD1 / LOD2 | Mean linear RGB |
|---|---|---|---|---|
| `tower_ruin` | `tripo-ruin-tower-02` | 15.91 × 23.00 × 16.19 | 23,472 / 5,868 / 1,467 | 0.094, 0.087, 0.070 |
| `arch_ruin` | `tripo-arch-04` | 13.87 × 14.00 × 7.86 | 23,504 / 5,876 / 1,469 | 0.158, 0.134, 0.101 |
| `stone_circle` | `tripo-standing-stones-05` | 22.39 × 16.10 × 27.00 | 23,638 / 5,909 / 1,462 | 0.124, 0.120, 0.087 |
| `monolith` | `tripo-monolith-06` | 5.72 × 12.00 × 5.44 | 11,809 / 2,952 / 738 | 0.132, 0.135, 0.104 |
| `cairn` | `tripo-cairn-15` | 4.26 × 6.00 × 5.04 | 11,796 / 2,947 / 736 | 0.098, 0.095, 0.073 |

Mean linear RGB measures surface colour before lighting. The measurement averages seven samples per triangle and weights each triangle by area.

- `tower_ruin`. The builder replaced the blue interior disc with dark stone colour. The broken rim and window read at 250 m. Close views show dark pits on some blocks.
- `arch_ruin`. The builder kept the open arch and its rubble feet. The block faces show some uneven shading.
- `stone_circle`. Seven stones stand as separate islands in one mesh, with open ground between them.
- `monolith`. The broad face points toward +Z. The cut marks and chipped outline remain visible in the near views.
- `cairn`. The stones keep their stacked profile. At 250 m, the stacked outline reads more clearly than individual stone faces.

## Rejected pieces

| Piece | Tripo source | Size X × Y × Z (m) | Triangles LOD0 / LOD1 / LOD2 | Mean linear RGB |
|---|---|---|---|---|
| `keep_ruin` | `tripo-ruin-keep-01` | 25.00 × 21.50 × 24.74 | 23,360 / 5,840 / 1,460 | 0.254, 0.253, 0.231 |
| `bridge` | `tripo-bridge-03` | 40.00 × 12.82 × 9.51 | 23,601 / 5,906 / 1,473 | 0.305, 0.304, 0.268 |
| `dragon_bones` | `tripo-bones-16` | 33.00 × 7.03 × 11.86 | 21,232 / 5,308 / 1,327 | 0.159, 0.132, 0.085 |
| `snag` | `tripo-snag-01` | 4.03 × 11.00 × 4.84 | 11,184 / 2,796 / 699 | 0.186, 0.195, 0.193 |
| `log` | `tripo-log-01` | 10.00 × 2.06 × 2.41 | 11,392 / 2,848 / 712 | 0.158, 0.128, 0.105 |

- `keep_ruin`. Both passes kept the rubble skirt. The first produced noisy wall shading at 250 m. The second distorted the window lintel and lost part of the rear arch.
- `bridge`. The first pass left a foot above ground at LOD2. The second lowered both abutments, but stretched the stone courses into bright strips at 250 m.
- `dragon_bones`. The builder cut away the sandy slab. The first distant view lost the rib cage. The second kept the rib gaps but turned limbs, horn tips and vertebrae into pointed sheets.
- `snag`. The first pass read as a dead tree, but LOD1 lost branch coverage. The second preserved silhouette overlap, but two branch clumps floated beside the trunk.
- `log`. The builder tilted the trunk 5.5 degrees to seat its ends. The first LOD2 retained 80% silhouette overlap. The second turned rounded branch stubs into thin spikes.

Both bridge ends reached Y = 0 or below at every LOD. Its deck ran from (−19.654, 4.792, 0) to (19.620, 4.554, 0) m.
The highest deck point on that line reached Y = 12.118 m.

## Meshes, textures and placement

Each GLB contains one mesh in one untransformed node. Each mesh uses one material and one triangle primitive. Positions, normals, tangents and texture coordinates use the rock kit contract.
Each level embeds three maps. All three measure 2048 pixels per side at LOD0, 1024 at LOD1 and 512 at LOD2.
Each level uses the same atlas layout, which assigns parts of one image to mesh surfaces.
The base colour uses sRGB JPEG. The PNG normal map stores OpenGL tangent-space directions.
One JPEG serves the occlusion and metallic-roughness slots. Red stores full visibility, green stores source roughness, and blue stores zero metal.

Blender fits directional lighting against the source surface normals.
It removes the fitted light from the source colours and restores their area-weighted mean.
The standing stones, monolith and cairn retain their source texture coordinates.
For the tower and arch, Blender unwraps the coarse mesh and subdivides it twice.
It fits new vertices to the source and projects source colour and roughness into the shared layout.
Three normal projections use different ray distances to recover recessed surfaces.

The glTF axes use Y up and +Z forward. The origin lies at the horizontal centre of each base.
The builder buries the lowest 3% of each piece. The skeleton trial used a 0.30 m burial after cutting away the sand.
Ground hides the open cuts. Each placement record contains at most 26 actual LOD2 vertices near the base.
Keep Y vertical when placing these landmarks. Rotate around Y and lower the mesh until the check points reach terrain.

## Rebuild

Run these commands from the worktree root on macOS with Blender 5.3.
The builder uses the meshoptimizer library bundled with Blender.
The build reads the original GLBs through `assets/tripo-raw/`.

```sh
mkdir -p build-landmarks-cache/tmp
export TMPDIR="$PWD/build-landmarks-cache/tmp"
landmarks() {
    /opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/build_landmarks.py -- "$@"
}
landmarks
landmarks --verify-only
landmarks --render
```

To inspect a rejected source with the current converter, select one piece explicitly. The rebuild writes its geometry in the cache.
The bridge and keep review images predate the final colour-decoding correction. Their table entries describe the reviewed files.

```sh
landmarks --only bridge
landmarks --render --only bridge
```

## Verification and render review

The check reads the exported GLBs and decodes every map.
Silhouette overlap divides the shared projected area by the combined area.
The report gives the lowest overlap across three views at LOD1 and LOD2.
The minimum ratios are 0.95 and 0.90. The hole count includes projected openings and two depth probes into the tower windows at LOD1.

```text
VERIFY tower_ruin: PASS; triangles [23472, 5868, 1467]; size [15.913, 23.0, 16.189]; albedo [0.0943, 0.0875, 0.0698]; silhouettes [0.985, 0.973]; holes 2
VERIFY arch_ruin: PASS; triangles [23504, 5876, 1469]; size [13.869, 14.0, 7.856]; albedo [0.1585, 0.1337, 0.1011]; silhouettes [0.987, 0.973]; holes 2
VERIFY stone_circle: PASS; triangles [23638, 5909, 1462]; size [22.39, 16.097, 27.0]; albedo [0.1236, 0.1199, 0.0874]; silhouettes [0.978, 0.909]; holes 0
VERIFY monolith: PASS; triangles [11809, 2952, 738]; size [5.72, 12.0, 5.443]; albedo [0.1316, 0.1348, 0.1037]; silhouettes [0.991, 0.983]; holes 0
VERIFY cairn: PASS; triangles [11796, 2947, 736]; size [4.264, 6.0, 5.041]; albedo [0.0977, 0.0948, 0.0735]; silhouettes [0.983, 0.93]; holes 0
VERIFY directory: 33323106 bytes / 110000000; 0 failures
```

The review used two near views and a 250 m view per candidate at 1920 × 1080.
Each scene used a 20-degree sun, sky light and neutral green-brown ground.
The [LOD sheet](../artifacts/landmarks/lods.png) shows the keep and arch with a fixed camera across each row.
The [render directory](../artifacts/landmarks/) also contains first-pass evidence for rejected pieces.
