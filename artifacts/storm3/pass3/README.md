# Storm3 inner wing panels

[Wing panels at capture resolution](panel-before-after.jpg) · [All final views](views.jpg) · [Before frames](before/) · [Final frames](after/)

The four repair rounds took about 34 minutes. I reviewed 58 engine captures before and after, plus the intermediate views.

## Diagnosis before repair

The upper and lower skins crossed during the wingbeat. I checked 9,506 triangles across both inner panels and their borders.
The bind pose had zero intersections between triangles that shared no vertex.
I reconstructed cruise frames 240, 247, 254, 261, 268, 275, 282 and 289.
The counts were 43, 84, 123, 233, 244, 199, 73 and 61 intersecting pairs.
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
The four procedural report views use frames 200, 214, 227 and 240.
Two additional views show the procedural hem from above and below at frame 252.

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

## Round 4

I kept the round 3 correction along the arm and fourth rib. Across the middle, I blended back to the round 2 normals.
A sample of 914 central vertices matched round 2. A sample of 296 border vertices matched round 3.
I measured the curved hem at 57 points between absolute X coordinates 0.086 and 0.198 on each wing.
In the bind pose, the right hem bowed 45.6 cm from the line between its endpoints. The original hem bowed 6.5 cm.
The left hem increased from 6.5 to 45.4 cm.
At frame 240, the midpoint offset increased from 32.8 to 41.9 cm on the right and 32.3 to 41.7 cm on the left.

The final geometry moved 3,270 distinct wing points. The greatest displacement was 0.040915 model units across a 1.125801-unit wingspan.
Both models retain 80,000 triangles and 55 joints.
The indices, joint names, rest transforms, UV coordinates, materials and image data matched their original values.
Only vertex positions, bone weights and joint assignments, shading normals and their corresponding tangents changed.
All changes fell inside the two panels and their borders.

## Remaining contacts and shading

The table counts pairs of intersecting triangles across both inner panels and their borders.
It excludes triangles that share a vertex. The two beat rows cover all 54 frames before the shader bows the membrane.

| Pose | Original pairs | Final pairs |
| --- | ---: | ---: |
| Keyframed beat | 43–248 | 0–8 |
| Procedural beat | 49–581 | 0–46 |
| Glide at 200 | 93 | 1 |
| Turn at 260 | 351 | 1 |
| Dive at 300 | 254 | 4 |
| Standing at 200 | 733 | 717 |

At keyframed frame 213, all eight contacts occurred beside the shoulder pivot, between absolute X coordinates 0.050 and 0.063.
At procedural frame 252, 46 pairs involved 48 triangles along the trailing hem.
Their centres lay between 92.76% and 99.98% of the distance from arm to hem.

At frame 240, I measured the same 7,686 pixels across the original dark patch in the report view.
Originally, 3,212 pixels fell below 90 on an 8-bit brightness scale. The final frame had zero.
The brightness weights for red, green and blue were 0.2126, 0.7152 and 0.0722.
The measurement polygon in image pixels was `[(611,481),(639,463),(729,432),(742,431),(727,480),(724,519),(699,538),(653,515)]`.

At the pictured report angle, the broad dark island and squared mark beside the arm disappeared.
At the true −1° angle, a small dark sliver remains across the far panel during recovery. It shows most clearly at frame 240. [Low report view](after/report-low-6.jpg)
At procedural frame 240, the lowered far wing still shows a dark fold above the shoulder. [Procedural report view](after/procedural-report-3.jpg)
At procedural frame 252, the former pale underside patch shrank to a light edge. [Above](after/procedural-hem-above.jpg) · [Below](after/procedural-hem-below.jpg)

The fourth rib retains a fine dark line at some angles. Its outer ridge has dark paint and faces the camera nearly edge-on.
The underside has a thin light rim along the curved edge.
From behind, the hems still form a corner near the flanks at the top of the stroke.
In the glide and turn, the deeper curve creates a sharper indentation near the body.
The standing views show a tradeoff. The rear view lost the original protruding pale folds.
From the side, the folded wing still covers the flank, and the dark crease under the back spines grew wider.
In the rectangle from (712, 552) through (770, 590), 527 pixels fell below brightness 45. The original had 88.
The side view falls short of the standing requirement. [Original stance](before/stand-side.jpg) · [Final stance](after/stand-side.jpg)

## Wing-only flap check

The check rotates 22 wing bones. It measures body travel in metres.

```
storm3: 35360 body vertices, 30854 wing vertices, 22 posed bones, 13.01295 m/unit
body: 0.00000000 m, vertex 0, XYZ (-0.0001, 0.388972, 0.276458), weights {'neck_03': 0.991308, 'head': 0.008692}, 0 over 2 cm
crest and horns: 0.00000000 m, vertex 0, XYZ (-0.0001, 0.388972, 0.276458), weights {'neck_03': 0.991308, 'head': 0.008692}, 0 over 2 cm
hind legs: 0.00000000 m, vertex 14283, XYZ (-0.049255, 0.104074, 0.130426), weights {'thigh_l': 0.911044, 'shin_l': 0.088956}, 0 over 2 cm
flank at the web: 0.00000000 m, vertex 13701, XYZ (-0.04683, 0.169011, 0.237857), weights {'chest': 0.719665, 'root': 0.213425, 'thigh_l': 0.06691}, 0 over 2 cm
tail and rudder: 0.00000000 m, vertex 1, XYZ (-3.9e-05, -0.582762, 0.237756), weights {'tail_11': 1.0}, 0 over 2 cm
```

## Model hashes

The original flight-bind models are in `build-rig-cache/before/`. The installed outputs are in `assets/`.

| Model | SHA-256 before repair | SHA-256 after repair |
| --- | --- | --- |
| storm3-raw.glb | `ce20be39d3d076751d8a4b74aaca05835a89a484778532b777695ce6599f7275` | `5b34a2209140d6e26ee7bb47501128ace2a59c77369a51290e7b41a828d58f0c` |
| storm3.glb | `a31f7beb11afdb2738e337f4efbe1ebf051ef5f2b378c5ac50cb155cca3a7e23` | `34b3cb45a6c2ea5b17f5908335b10ce2a1870439e330b888258f24a13efb9f8a` |

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
