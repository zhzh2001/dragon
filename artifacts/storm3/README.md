# Storm3

Storm3 has 80,000 triangles and 55 joints. Storm3 and Stormsail printed the same line without a joint warning.

```text
mapped rig: neck 3, tail 11, wing root 3/3, fingers 4/4, legs 3/3, front legs 0/0, feet 2, jaw jaw
```

## Measurements and binding

I measured the source mesh with +Y forward and +Z up. The central side projection isolates the body within |X| 0.12.
I traced the hind legs from below and matched each wing rib across front, side and overhead projections.
The wrist hub sits at (0.240, 0.121, 0.547). The final shoulder pivot sits at |X| 0.063 inside the upper arm.
The bind keeps the sculpted shoulder, elbow and wrist angles. [Measured joints](landmarks.jpg)

The inner web meets the flank above the thigh. I followed connected edges from each wingtip toward the flank.
Below Z 0.265 the cut lies at |X| 0.080. It slopes inward to |X| 0.04685 at Z 0.350.
Wing weight increases across 0.030 units outside the cut. The disconnected thigh surface stays with the leg.
The wing-only test recovers the same boundary from 162 texture landmarks.
Across 41 poses, all 35,360 body vertices moved 0.00000000 m. The test includes the flank, crest, horns, hind legs and rudder.

The last tail bone carries the whole rudder behind Y −0.484. Its weight blends into the previous bone by Y −0.460.
All 5,947 exported vertices behind Y −0.490 have full weight on `tail_11`.
At Y 0.319 the mandible ends at Z 0.5013 and the palate starts at Z 0.5073.
The first jaw cut included a bridge between the lips. The final cut separates the connected surfaces beyond Y 0.319.
At Y 0.334 the lower lip follows the jaw and the upper lip follows the skull.

## Size and motion

The 19 m default span gave a body length of 17.57 m. I set `model_span_m` to 14.65 and `ground_offset` to 3.9577 m.
Storm3 measures 13.55 m from nose to rudder, or 95.2% of the 14.24 m hero. [Hero at the same camera](../frost3/hero.jpg)

I copied all three Stormsail profiles and kept its wingbeat and storm breath.
The initial wing stroke crossed the centerline by 0.364 m. I moved the shoulder pivot outward and kept the membrane's sculpted tilt in flight.
The open wing sweeps forward with speed, starting at 22 m/s and reaching 14° at 75 m/s.
The dive retains Stormsail's 40° rearward tuck. The closest finger cleared the centerline by 0.026 m over two beats.
At five seconds in the dive, the probe found zero wing vertices inside the body.

The grounded hip bends 24°, the knee bends −49° and the upper wing arm reaches down at −24°.
The tail angle is −23° and the ground lift is 0.164 m.
The wrists sit 2.7 m ahead of the hind feet and 1.2 m farther out on either side.
On level ground, the lowest wrist and hind toe differ in height by 0.002 m.
The rudder clears the floor. The resting jaw angle is −19° and the breath adds 30°. [Standing feet](feet.jpg)

## Likeness and materials

At equal body length, the near wing extends less far behind the shoulder and hides more of the flank.
The lower web descends closer to the thigh. From the front, the neck and legs look longer relative to the wing span.
The generated wing hooks and crown horns are thicker. The tail keeps its diamond-shaped rudder, with heavier ribs and a broader base.
The wing veins have less contrast than the silver branching lines on the sheet. [Sheet and bind pose](likeness.jpg)

The first engine side view rendered the membranes charcoal. I lifted the base colour before the material repair.
For normalized sRGB, the red, green and blue powers are 0.58, 0.51 and 0.48.
The lift increases from zero at luminance 0.13 to full strength at 0.49. A separate colour mask protects the red mouth.
The grade lifted the vertex-sampled median from RGB (94, 102, 114) to (130, 145, 158).
Under the specified sun, the underside still reads darker and warmer than the sheet.
The bake assigned 39.0% to membrane, 3.5% to hard tips, 10.1% to belly and 0.3% to mouth.
The broad wing fans dominate the membrane mask. The repair preserved the base colour, joints and mesh buffers.

## Rebuild

Run these commands from the worktree root. The raw and repaired models use identical profiles.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/storm3_rig.py
mv assets/storm3-raw.glb build-rig-cache/storm3-ungraded.glb
build-rig-cache/venv/bin/python tools/storm3_grade.py build-rig-cache/storm3-ungraded.glb assets/storm3-raw.glb
build-rig-cache/venv/bin/python tools/repair_model_materials.py assets/storm3-raw.glb -o assets/storm3.glb --bake-size 1024
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/storm3_flapcheck.py
```

I inspected all 18 requested frames at 1920 × 1080. The upper muzzle holds its shape when the jaw opens.
The rudder keeps its outline in the dive and on the ground.
A small step remains along the inner trailing edge during recovery, especially in rear frames 208 and 216.
The planted wrist draws the membrane into a broad, angular pleat. [Engine poses](views.jpg)

Three builds, colour correction and engine inspection took about 11 minutes.
