# Storm3 inner wing panels

## Diagnosis before repair

The upper and lower skins crossed during the wingbeat. I checked 9,506 triangles across both inner panels and their borders.
The bind pose had zero intersections between triangles that shared no vertex.
In a reconstruction of cruise frames 240, 247, 254, 261, 268, 275, 282 and 289, the counts were 43, 84, 123, 233, 244, 199, 73 and 61.
At frame 268, 244 intersecting pairs involved 228 triangles.

The two inner faces had median sRGB values of (209, 221, 237) and (206, 218, 232) between absolute X coordinates 0.14 and 0.18.
An initial comparison included dark wing ribs and suggested a paint mismatch.
A second sample used only membrane between the third and fourth ribs.
At model height 0.26, 21 and 19 triangles gave median colours of (211, 223, 238) and (208, 221, 234).
The two membranes already had similar paint.

Of 8,231 sampled vertices, 2,302 carried both body and wing weight.
In the bind pose, 11 of 9,506 triangles had a geometric normal opposite their average shading normal.

At frame 200, the triangle near report pixel (740, 428) faced the camera, but its shading normal pointed away.
Their dot products with the camera direction were +0.55 and −0.26. A positive value means the surface faces the camera.
Near (740, 449), both pointed forward at +0.81 and +0.14.
The two pixels sampled base colours of (210, 222, 236) and (214, 225, 239).
The shader reverses a normal that points away from the viewer. On this panel, that put a dark triangle beside a pale one.
These pixel locations use projected rig geometry before the shader bows the membrane.

## Cameras

The 0.9-second cruise beat lasts 54 frames at 60 frames per second.
The eight sample frames are 200, 207, 214, 220, 227, 234, 240 and 247.
The procedural views use frames 200, 214, 227 and 240.

The engine skips an inspect argument that starts with a minus sign.
The playtester's command therefore renders at the default elevation of +14.5°.
The report rows reproduce the pictured angle. The report-low rows use the requested −1°.
The capture script passes a leading space before negative elevations. SDL reads the signed number.
The below-panel and below-glide views use −35° and −60° respectively.

## Round 1

The first fit curved the hem and spread the fourth rib's influence across both skins.
It adjusted 3,422 distinct wing points. The greatest movement was 0.040799 model units, or 3.62% of the wingspan.
I fitted surface normals to the moving triangles across 14 cruise poses.
The large dark island disappeared at frame 240. A thin black line remained beside the arm and another beside the fourth rib.
The underside also showed a narrow light edge. The folded wing kept its outline from behind.
The wing-only test measured zero motion across 35,360 body vertices.

## Round 2

I fitted the skins around a common middle surface across the keyframed and procedural beats.
I also checked the fit against the glide, turn, dive and standing poses.
I smoothed the positions and bone weights at remaining surface contacts. The largest movement reached 3.63% of the wingspan.
Across all 54 cruise frames, the keyframed beat fell from 43–248 intersecting pairs to 0–8.
The procedural beat fell from 49–581 pairs to 0–46.
These counts use the rig's triangles before the shader bows the membrane.

At frame 240, the large dark patch disappeared, but a black strip still ended abruptly beside the upper arm.
Near pixel (591, 510), triangle 11387 faced the viewer at +0.33.
Its three shading normals pointed away at −0.14 to −0.22.
The thin line also showed above the wing at frame 247. The underside showed a narrow bright rim along the curved hem.

## Round 3

I extended the normal fit up to three mesh edges beyond the panel and included the pictured report angle.
At frame 240, the three normals on triangle 11387 now faced the viewer at +0.23 to +0.29.
The squared black stripe disappeared beside the arm in the report and above views.
The wider fit added shallow triangular shadows near the flank at frames 220 and 247.
I kept the geometry and compared a narrower normal correction for the final round.

## Original models

The scratch directory `build-rig-cache/before/` contains both original flight-bind models.
Their SHA-256 digests identify the inputs to the panel repair.

| Model | SHA-256 before repair |
| --- | --- |
| storm3-raw.glb | `ce20be39d3d076751d8a4b74aaca05835a89a484778532b777695ce6599f7275` |
| storm3.glb | `a31f7beb11afdb2738e337f4efbe1ebf051ef5f2b378c5ac50cb155cca3a7e23` |

## Rebuild

Run these commands from this worktree. Use Blender 5.3 and Python 3 with NumPy and Pillow.
The original pipeline builds the rig, web, grade and materials, then applies the flight bind. [Original build record](../README.md)
For this repair, use the two original flight-bind files in `build-rig-cache/before/`. Check their hashes against the table.

The pose sampler compiles a measurement tool with Clang, SDL3 and headers from the main checkout's build.
It writes the joint transforms for both beats and four other poses under `build-rig-cache/`.

```sh
python3 -B tools/storm3_panel_poses.py
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 \
  --python tools/storm3_panel_fit.py -- build-rig-cache/before build-rig-cache/rebuilt
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 \
  --python tools/storm3_panel_check.py -- build-rig-cache/before build-rig-cache/rebuilt/storm3.glb
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 \
  --python tools/storm3_flapcheck.py -- build-rig-cache/rebuilt/storm3.glb
shasum -a 256 build-rig-cache/rebuilt/storm3*.glb
```

The capture script uses the model currently in `assets/storm3.glb`.
With the original model installed, run the first command.
With the rebuilt model installed, run the last two commands.

```sh
python3 -B tools/storm3_panel_captures.py before
python3 -B tools/storm3_panel_captures.py after
python3 -B tools/storm3_panel_sheets.py
```
