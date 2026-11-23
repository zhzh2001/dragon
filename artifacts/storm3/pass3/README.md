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
