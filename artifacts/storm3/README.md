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

## After the job (the main session, 2026-11-22)

The glide stood 35 degrees up: the bind keeps the sheet's raised wings. `wing_rest_deg -20` in the rig profile lowers the wing in flight.
The flight profile has no stroke keys, so the beat moved down with the rest. Renders: `artifacts/elemental-round3/`. The record: `docs/MODEL_GENERATION.md`, "The third round modelled".

## Second pass: postures and wingbeat

### 1. Glide

The standing bind pose carried the shoulders and neck upright in flight. The hip-to-shoulder line rose 71.6° at glide frame 200.
The rig has no flight-only trunk pitch control. I pitched the trunk forward 61° in Blender and carried the skin with the joints.
The three neck segments use absolute pitch changes of −60°, −55° and −47°. The skull turns up 13° to level the muzzle.
The tail straightens progressively toward the fluke. The new trunk rises 10.6° and the neck rises 14.0° from its base to the skull.
The glide now meets the 15° trunk target.

For a clean rebuild, complete the original rig, grade and material repair above first.
Copy the standing raw and repaired GLBs into a separate directory before baking the flight bind.
The script preserves the image data, UV coordinates, triangle indices and 55 joint names.

```sh
mkdir -p build-rig-cache/asset/standing
cp assets/storm3-raw.glb assets/storm3.glb build-rig-cache/asset/standing/
PYTHONDONTWRITEBYTECODE=1 /opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 \
  --python tools/storm3_flight_bind.py -- build-rig-cache/asset/standing assets
PYTHONDONTWRITEBYTECODE=1 /opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 \
  --python tools/storm3_flapcheck.py
```

### 2. Flight legs

One alternating tuck angle straightened the shin. The limb springs then let the feet swing through each reversal.
I set `leg_flight_pose` to 1 and the hip, knee and hock to 9°, 27° and 47°. Hip splay is −12°.
The foot now trails 2.33 m behind the hip and 0.35 m below it. The former glide put it 1.09 m below.
The first tuck put the foot slightly above the hip. Lowering the hip angle by 10° brought the claws alongside the tail.
Spring stiffness is 110, tip stiffness is 0.85, damping is 0.95 and angular range is 0.6°.
Across a complete 54-frame beat, maximum foot displacement fell from 0.455 m to 0.041 m. Total travel fell from 0.775 m to 0.183 m.
I measured the foot joint relative to the hip and removed the trunk rotation at each frame.

### 3. Wingbeat height

The leading tip already reached 37.8° above level and 31.2° below. The innermost hook stayed at least 5.7° below the shoulder.
I measured each angle from the shoulder to the same skin point throughout the beat.
The new leading tip reaches 41.8° above and 29.7° below. The innermost hook now reaches 19.6° above the shoulder.
The climb reaches 41.6° above and 36.2° below. The wing clears the hanging-leg position at the bottom.
The cruise still reaches 12.1° farther above level than below.

I set the shared wing leveling to 0.45 and the wing rest to −28°.
The upper stroke command is 51°. The lower commands are −49° in cruise and −64° in climb.
The powered stroke takes 48% of the cycle in both profiles. Peak force fell to 7500 to preserve the impulse.
The stroke plane tilts 12° forward.

### 4. Membrane and recovery

The inner web joined the moving wing to the fixed flank across a narrow strip.
The first rebuild spread the wing weights across 1,155 points. Rotating the body still rolled the inner web into a tube.
The second rebuild fitted both faces of the web between the arm and trailing edge. It moved 3,236 inner points.
In the bind pose, the rolled edge rose 1.18 m above its endpoints before the repair. The repair removed that rise.
At 45° of shoulder depression, the rise fell from 1.45 m to 0.34 m.

I reduced recovery flex to 4° at the elbow, 10° at the wrist and 8° at the fingers.
Recovery droop is 8°, upstroke fold is 15°, twist is 10° and phase delay is 0.035 cycles.
The hand starts reopening at phase 0.79. The flank stays still as the hand narrows and opens.
The wing-only check moved none of the 35,360 body vertices across 41 poses.

The large fold is gone. A small notch remains beside the innermost rib, visible in rear frames 221 and 235. Side frame 228 shows a bright strip along the folded edge.
The inner hook still travels 0.14 m across the centerline during cruise recovery. It previously crossed by 0.38 m.
The shared finger flex controls all four ribs. A separate fourth-rib angle, such as `recovery_finger4_deg`, would let it clear the spine.

### 5. Ground stance

The old stance held the hand at 55° and fanned the ribs upright. Its floor height also matched the standing bind.
I set the body pitch to 27°, hip to 14°, neck to 1° and tail to 48°. The floor offset is 3.09 m.
The upper arm aims −18° in sweep and −21° in elevation. The forearm aims −60° and −65°.
The hand sweeps back 118° and rises 25°. All four ribs share that direction.

Each wrist now stands 3.20 m ahead of its hind foot and 1.48 m farther out. The former distances were 2.74 m and 1.16 m.
On a level floor, the hind toes touch and the wrist pad sits 10 mm below them. The body clears the floor by 0.58 m.
The neck rises 39° and the muzzle faces forward. The tips still flare outward and the inner membrane stays broad across the flank. The neck sits lower than the concept.
The engine distributes ground neck pitch evenly. A separate `ground_head_pitch_deg` would allow a steeper neck with a level muzzle.
A separate `ground_wing_tip_sweep_deg` would turn the last finger segment inward. The ground solver currently aims only segments with a child.

### 6. Dive and turn

The old speed sweep opened the extended wing forward during the dive release. The inner hook crossed the spine by 0.07 m at full tuck.
I set the speed sweep to 14° aft, starting at 34 m/s. The turn uses −20° of forward sweep under load.
A 52° dive sweep brought the web through the legs. A 44° sweep cleared the shins at full tuck but caught them during release.
The final profile uses the original 40° sweep. The ribs converge 37° and droop 5°.
The nearest inner tip stays 1.61 m from the centerline at full tuck and 1.10 m away during release.
The leg intersection check found no wing vertices inside the shins at either frame. It still flagged contact at the chest attachment.

At turn frame 260, the leading tip extends 5.32 m sideways from the shoulder. It previously reached 6.58 m.
At dive frame 300, that distance fell from 5.50 m to 4.17 m. Both poses keep the tips on their own side.

### Captures and checks

The comparison pairs eight poses at one image pixel per captured pixel. [Before and after](pass2/before-after.jpg)
The contact sheet includes all 47 requested views. The two sets share the same nine Embercrest reference frames. [All final views](pass2/views.jpg)
The original front landing camera cuts off the near wrist. Some side cameras cut off the leading wingtip at stroke extremes.
The full frames retain the requested camera positions. [Before frames](pass2/before/) and [after frames](pass2/after/)

I checked the landing in flight and the switch from Embercrest to Storm3.
Both final GLBs retain 80,000 triangles, 55 named joints, the triangle indices, UV coordinates, maps and graded materials.
The glide, leg tuck, dive and turn met their targets. The cruise stroke, inner membrane and ground fold retain the differences described above.
The pass took about 29 minutes, including two asset rebuilds and the engine captures.
