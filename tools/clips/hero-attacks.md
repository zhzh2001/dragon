# Hero attack clips

## Result

Five AI-authored attack clips target the version 4 Embercrest hero rig.
The export contains 77 joint nodes, five animations, and rotation tracks only.
All 133 checks passed. Eight corrupted exports failed their corresponding checks.
All ten requested strips were rendered and inspected.
The preview also contains 109 full-size frames and five cropped rest/strike pairs.

The output is `assets/clips/embercrest-hero.attacks.glb`, 273,884 bytes.
`bite`, `claw_r`, and `claw_l` last 0.90 seconds. Both tail clips last 1.00 second.
Every track starts and ends at rest. The exporter copies the model's rest TRS fields exactly.
TRS means translation, rotation, and scale.

The work changed only the five path groups in the brief.
The source model was read through its supplied symlink. No other checkout received writes.
Blender ran as a foreground subprocess. No second worker was used.

## Motion

`bite` coils three neck joints by 42 degrees together, then throws them 28.5 degrees past rest.
The jaw opens 40 degrees and closes with a 2.5-degree overshoot.
The chest pitches, both shoulders reach, and the wings sweep back.
The tail lifts in a delayed wave. The head recoils 8 degrees before settling.

`claw_r` draws the shoulder back, closes the elbow, and then reaches across the chest.
The wrist flicks downward. The toes spread, then curl.
The head leads the body turn. The other foreleg braces, and the wings take different heights.
The tail swings left for balance. `claw_l` reflects these rotations and swaps paired joints.

`tail_r` coils left, then sends a short whip pulse from the root to the tip.
The eight local peaks increase from 35 to 42 degrees, one frame apart.
Their largest simultaneous sum is 126.79 degrees. The narrow pulses prevent all eight peaks from coinciding.
The hips turn with the whip. The neck counters that turn, and the right hind leg kicks back.
`tail_l` reflects these rotations and swaps paired joints.

The authoring curves use cubic smoothstep between pose knots.
The neck and tail start in sequence, with one frame between adjacent joints.
The recovery crosses rest with small counter-rotations. The explicit bite head recoil precedes its smaller final overshoot.
`artifacts/hero-clips/authoring-knots.json` records every source knot.

## Rebuild

Run these commands from this worktree:

```sh
/Applications/Blender.app/Contents/MacOS/Blender -b --python-exit-code 1 \
  --python tools/clips/make_hero_attacks.py -- --preview \
  > artifacts/hero-clips/build.log 2>&1
python3 -B tools/clips/check_hero_attacks.py \
  --report artifacts/hero-clips/checks.json \
  > artifacts/hero-clips/check-output.txt
python3 -B artifacts/hero-clips/checker-negative-tests.py
```

Blender 5.3.0 generated the delivered export and renders.
The sheet compositor uses Pillow through `/opt/homebrew/bin/python3.14` and the macOS Menlo font.
The builder imports the existing brake-bank helpers without changing them.
It uses the same joint-only GLB layout and exact-rest writer approach.

The builder reads the exported float32 keys back before creating Blender actions.
It assigns each action to the imported model's armature and keys every joint rotation.
It compares all posed joint positions with independent forward kinematics at every exported frame.
Forward kinematics composes transforms along each parent chain.
The largest Blender position error was 0.000003199 metres.

## Measured axes

The actual model faces glTF -Z, with +Y up and +X right.
The brief's +Z statement conflicts with both the source joint coordinates and `tools/skeletons/embercrest-hero.json`.
This authoring uses the measured model frame. No source transforms were changed.

The script posed every joint by positive five degrees around each model axis.
It measured the chosen child's source tail in Blender world space, then converted that displacement to glTF space.
A leaf uses its own source tail. Blender's invented leaf tips were not used.
The source tail comes from each original node's `extras.tail` field.
The script transports that point through the imported bone's rest and posed matrices.

The table gives the measured XYZ displacement in metres for each active axis.
X, Y, and Z mean model-frame axes, not Blender bone-local axes.
All source joint rest rotations are identity. Therefore these axes also equal the exported parent-frame axes.
`axis-probes.json` records all 231 probes, the sampled points, and the converted Blender bone-local axes.

| Joint | Axis | Child-tail displacement for +5 degrees (X, Y, Z), m |
|---|---|---|
| `neck_01` | X | (+0.00000, +0.06622, +0.08323) |
| `neck_01` | Y | (-0.06972, +0.00000, +0.00304) |
| `neck_02` | X | (+0.00000, +0.05197, +0.08959) |
| `neck_02` | Y | (-0.05574, +0.00000, +0.00331) |
| `neck_03` | X | (+0.00000, +0.11223, +0.06952) |
| `neck_03` | Y | (-0.11501, +0.00000, +0.00589) |
| `head` | X | (+0.00000, +0.09595, +0.00244) |
| `head` | Y | (-0.09587, +0.00000, +0.00426) |
| `jaw` | X | (+0.00000, +0.08663, -0.00626) |
| `root` | X | (+0.00000, +0.20557, +0.05176) |
| `root` | Y | (-0.20743, +0.00000, +0.00906) |
| `chest` | X | (+0.00000, +0.15081, +0.06596) |
| `chest` | Y | (-0.15339, +0.00000, +0.00670) |
| `wing_root_r` | Z | (-0.18299, +0.19110, +0.00000) |
| `wing_root_r` | Y | (+0.01747, +0.00000, -0.19986) |
| `upper_arm_r` | X | (+0.00000, -0.00565, -0.17052) |
| `upper_arm_r` | Z | (+0.17037, -0.00217, +0.00000) |
| `forearm_r` | X | (+0.00000, +0.06880, -0.13584) |
| `toe_fore_1_r` | X | (+0.00000, +0.04411, -0.01030) |
| `toe_fore_1_r` | Y | (-0.04335, +0.00000, +0.00713) |
| `toe_fore_2_r` | X | (+0.00000, +0.04411, -0.01030) |
| `toe_fore_2_r` | Y | (-0.04381, +0.00000, -0.00333) |
| `toe_fore_3_r` | X | (+0.00000, +0.03278, -0.01079) |
| `toe_fore_3_r` | Y | (-0.03286, +0.00000, -0.01254) |
| `wing_root_l` | Z | (-0.16564, -0.20633, +0.00000) |
| `wing_root_l` | Y | (+0.03482, +0.00000, +0.19757) |
| `upper_arm_l` | X | (+0.00000, -0.00565, -0.17052) |
| `upper_arm_l` | Z | (+0.16954, +0.01701, +0.00000) |
| `forearm_l` | X | (+0.00000, +0.06880, -0.13584) |
| `toe_fore_1_l` | X | (+0.00000, +0.04411, -0.01030) |
| `toe_fore_1_l` | Y | (-0.04381, +0.00000, -0.00333) |
| `toe_fore_2_l` | X | (+0.00000, +0.04411, -0.01030) |
| `toe_fore_2_l` | Y | (-0.04335, +0.00000, +0.00713) |
| `toe_fore_3_l` | X | (+0.00000, +0.03278, -0.01079) |
| `toe_fore_3_l` | Y | (-0.03164, +0.00000, +0.01535) |
| `tail_01` | X | (+0.00000, -0.21176, -0.04994) |
| `tail_01` | Y | (+0.21353, +0.00000, -0.00932) |
| `tail_02` | X | (+0.00000, -0.20651, -0.07014) |
| `tail_02` | Y | (+0.20917, +0.00000, -0.00913) |
| `tail_03` | X | (+0.00000, -0.20896, -0.01410) |
| `tail_03` | Y | (+0.20917, +0.00000, -0.00913) |
| `tail_04` | X | (-0.00000, -0.20987, +0.00682) |
| `tail_04` | Y | (+0.20917, +0.00000, -0.00913) |
| `tail_05` | X | (+0.00000, -0.21014, +0.01309) |
| `tail_05` | Y | (+0.20917, +0.00000, -0.00913) |
| `tail_06` | X | (+0.00000, -0.20026, -0.01328) |
| `tail_06` | Y | (+0.20046, +0.00000, -0.00875) |
| `tail_07` | X | (+0.00000, -0.17232, -0.09327) |
| `tail_07` | Y | (+0.17605, +0.00000, -0.00769) |
| `tail_08` | X | (+0.00000, -0.07725, -0.07078) |
| `tail_08` | Y | (+0.08018, +0.00000, -0.00350) |
| `wrist_r` | X | (+0.00000, +0.07231, -0.05622) |
| `wrist_l` | X | (+0.00000, +0.07231, -0.05622) |
| `thigh_r` | X | (+0.00000, -0.06345, -0.14685) |
| `thigh_l` | X | (+0.00000, -0.06345, -0.14685) |

The measured signs determine the authored directions:

| Action | Chosen axis and sign | Measurement used |
|---|---|---|
| Neck coil | +X | Child tails rise and move aft. |
| Neck throw and chest load | -X | Child tails move down and forward. |
| Jaw opening | -X | +X raises the jaw tip. |
| Foreleg reach | +X | The wrist moves forward under the shoulder. |
| Elbow closing | +X | The child tail rises and moves forward. |
| Elbow opening and wrist rake | -X | The child tail moves down and aft. |
| Right foreleg crosses centre | -Z | The child tail moves toward negative X. |
| Right wing lift | +Z | The child tail rises. |
| Left wing dip | +Z | The child tail falls. |
| Right wing sweeps back | -Y | The child tail moves aft. |
| Left wing sweeps back | +Y | The child tail moves aft. |
| Rightward head turn | -Y | The front-pointing child moves toward positive X. |
| Rightward tail whip | +Y | The aft-pointing child moves toward positive X. |
| Tail lift | -X | The child tail rises. |
| Hind leg kicks back | -X | The child tail moves aft. |
| Right outer toe spreads | -Y | The claw moves toward positive X. |
| Right inner toe spreads | +Y | The claw moves toward negative X. |
| Toe curls | -X | The source toe tip moves down. |

Left clips mirror axial rotation vectors as `(x, -y, -z)` and swap `_r` with `_l`.
This operation preserves each target node's original rest transform.
The mirror check compares these model-frame deltas at every key. Its maximum error is zero degrees.

## Peak measurements

These values come from the exported keys, not the authoring knots.
Directional measurements use rotation-vector components in the measured model frame.
A rotation vector stores an axis multiplied by its angle.
The shoulder's pitch and cross-body roll coexist, so its full quaternion angle exceeds either component alone.
`checks.json` also records every joint's full quaternion peak, including all rest-only joints.

Times in the table are seconds. Both mirrored versions have the same angular measurements.

| Clip | Measurement | Peak, degrees | First peak time |
|---|---|---:|---:|
| `bite` | neck coil total | 42.0000 | 0.1667 |
| `bite` | neck throw total | 28.5000 | 0.3000 |
| `bite` | jaw opens | 40.0000 | 0.1667 |
| `bite` | jaw closed overshoot | 2.5000 | 0.2667 |
| `bite` | head recoil | 8.0000 | 0.4667 |
| `bite` | root pitch | 6.0000 | 0.2333 |
| `bite` | chest pitch | 8.0000 | 0.2667 |
| `bite` | wing_root_r sweep | 15.0000 | 0.2667 |
| `bite` | upper_arm_r reach | 25.0000 | 0.2667 |
| `bite` | wing_root_l sweep | 15.0000 | 0.2667 |
| `bite` | upper_arm_l reach | 25.0000 | 0.2667 |
| `bite` | tail root lift | 12.0000 | 0.2667 |
| `claw_r` | shoulder wind-up | 29.5306 | 0.1667 |
| `claw_r` | shoulder forward travel | 59.5306 | 0.3000 |
| `claw_r` | elbow closed | 29.5306 | 0.2000 |
| `claw_r` | elbow opening travel | 49.5306 | 0.3333 |
| `claw_r` | wrist flick | 40.0000 | 0.3667 |
| `claw_r` | opposite foreleg brace | 15.0000 | 0.3000 |
| `claw_r` | spine twist | 15.0000 | 0.3333 |
| `claw_r` | neck twist total | 15.0000 | 0.3333 |
| `claw_r` | head lead | 15.0000 | 0.2333 |
| `claw_r` | striking wing lift | 15.0000 | 0.3000 |
| `claw_r` | opposite wing dip | 10.0000 | 0.3333 |
| `claw_r` | tail opposite swing | 22.0000 | 0.3000 |
| `claw_r` | toe 1 curl | 28.0000 | 0.4333 |
| `claw_r` | toe 2 curl | 28.0000 | 0.4333 |
| `claw_r` | toe 3 curl | 28.0000 | 0.4333 |
| `claw_r` | outer toe spread | 16.0000 | 0.2333 |
| `claw_r` | inner toe spread | 16.0000 | 0.2333 |
| `tail_r` | root left coil | 25.0000 | 0.2000 |
| `tail_r` | root right whip | 35.0000 | 0.3333 |
| `tail_r` | tail_01 whip | 35.0000 | 0.3333 |
| `tail_r` | tail_02 whip | 36.0000 | 0.3667 |
| `tail_r` | tail_03 whip | 37.0000 | 0.4000 |
| `tail_r` | tail_04 whip | 38.0000 | 0.4333 |
| `tail_r` | tail_05 whip | 39.0000 | 0.4667 |
| `tail_r` | tail_06 whip | 40.0000 | 0.5000 |
| `tail_r` | tail_07 whip | 41.0000 | 0.5333 |
| `tail_r` | tail_08 whip | 42.0000 | 0.5667 |
| `tail_r` | tip cumulative bend | 126.7907 | 0.5000 |
| `tail_r` | root hip/spine twist | 12.0000 | 0.3333 |
| `tail_r` | chest hip/spine twist | 10.0000 | 0.3667 |
| `tail_r` | neck counter-swing | 15.0000 | 0.4333 |
| `tail_r` | head counter-swing | 15.0000 | 0.4667 |
| `tail_r` | hind kick | 15.0000 | 0.3667 |

The checker bounds adjacent quaternion changes, not only Euler components.
Bite peaks at 23.12000 degrees per frame. Claw peaks at 24.86708 degrees per frame.
Tail peaks at 20.62500 degrees per frame. The limit is 25 degrees.

The neck coil peaks on frames 3, 4, and 5. The neck throw peaks on frames 7, 8, and 9.
The bite tail lift peaks on frames 8 through 15. The claw tail swing peaks on frames 9 through 16.
The tail whip peaks on frames 10 through 17.

## Landmark movement

These distances include ancestor rotations and compare each landmark with its rest position.
They measure joint anchors, not mesh vertices or hitboxes.
The tail landmark is the `tail_08` anchor. It is not the mesh's extreme tip.

| Clip | Landmark | Maximum distance from rest, m | Time, s |
|---|---|---:|---:|
| `bite` | `head` | 1.42402 | 0.3000 |
| `bite` | `jaw_tip` | 2.08516 | 0.3333 |
| `bite` | `tail_08` | 3.85034 | 0.3667 |
| `claw_r` | `head` | 1.52077 | 0.3333 |
| `claw_r` | `jaw_tip` | 2.55166 | 0.3333 |
| `claw_r` | `hand_r` | 1.11984 | 0.2667 |
| `claw_r` | `tail_08` | 6.95509 | 0.4000 |
| `claw_l` | `head` | 1.51917 | 0.3333 |
| `claw_l` | `jaw_tip` | 2.55126 | 0.3333 |
| `claw_l` | `hand_l` | 1.11984 | 0.2667 |
| `claw_l` | `tail_08` | 6.95509 | 0.4000 |
| `tail_r` | `head` | 0.98742 | 0.3667 |
| `tail_r` | `jaw_tip` | 1.17406 | 0.3333 |
| `tail_r` | `hand_r` | 0.84223 | 0.3667 |
| `tail_r` | `tail_08` | 11.77739 | 0.3667 |
| `tail_l` | `head` | 0.98719 | 0.3667 |
| `tail_l` | `jaw_tip` | 1.17394 | 0.3333 |
| `tail_l` | `hand_l` | 0.84223 | 0.3667 |
| `tail_l` | `tail_08` | 11.77739 | 0.3667 |

At frame 5, the bite head anchor moves 0.355 m up and 0.915 m back.
At frame 9, it moves 1.266 m down and 0.653 m forward from rest.
The claw hand crosses the chest's centre plane by 0.982 m in both versions.
The source head is slightly off-centre, so mirrored angular tracks produce slightly different head distances.

## Visual inspection

The ten strips contain eight frames each, arranged as two rows of four.
Every cell retains its 960 by 640 source render. The strips are 3840 by 1424 pixels.
The cameras remain fixed within each strip. The left side camera views the dragon's left side.
The top camera is identical for both sides. Model +X appears on the left of that image.

| Strip pair | Observed result |
|---|---|
| `bite-side.jpg`, `bite-top.jpg` | The side strip shows coil, downward lunge, and recoil. The top strip shows the wing sweep. |
| `claw_r-side.jpg`, `claw_r-top.jpg` | The right arm draws back, reaches, and curls. The body turn and tail balance are clear above. |
| `claw_l-side.jpg`, `claw_l-top.jpg` | The left-side action reverses the right-side sequence. The mesh retains its original asymmetry. |
| `tail_r-side.jpg`, `tail_r-top.jpg` | The top strip shows left coil, right sweep, and a curl travelling to the tip. |
| `tail_l-side.jpg`, `tail_l-top.jpg` | The top strip shows the mirrored sweep. The side strip shows foreshortening during the whip. |

The five `*-rest-strike.jpg` pairs crop both images to one shared rectangle without resizing.
The pairs show the creature at its original render scale.
The full-size side, front, rear, top, and head views were also inspected at representative attack frames.
The head views include bite rest, open jaw, closed overshoot, hold, and recoil.
The last head camera follows the head's turn because a fixed opposite-side camera hid the head behind a wing.

The strikes read at a glance in their main motion planes.
The tail's travelling curl is clear from above and poorly resolved from the side.
The foreleg reads from the striking side and front. The wings partly hide the foreleg from above.
The rigid neck plates remain visible as separate plates in the bite close views.
No gross tear or flipped limb was visible in the inspected frames.
These observations do not establish collision-free skinning between sampled frames.

## Deviations and limits

- The source faces -Z rather than the brief's stated +Z. The axis probes, motions, and checker use the actual source frame.
- The checker allows one 30 fps frame around stated timing windows. The phase boundaries do not all fall on exported frames.
- The bite's full neck coil and jaw opening peak at 0.1667 s, 0.0067 s after the stated wind-up boundary.
- The jaw reaches its closed overshoot at 0.2667 s. The last neck joint finishes its throw at 0.3000 s, during the hold.
- The claw elbow opens at 0.3333 s and wrist flicks at 0.3667 s, during follow-through. This preserves the requested overlapping chain action.
- The last tail joint peaks at 0.5667 s, 0.0167 s after the stated follow-through boundary. The root peaks within the whip window.
- The sampled claw shoulder travel is 59.5306 degrees. The authored travel is 60 degrees; sampling misses the exact wind-up extremum.
- Dense exports use `LINEAR` interpolation, as the earlier clip does. Eased authoring changes speed across frames; each exported interval remains interpolated between samples.
- The body and neck angles accumulate through the hierarchy. The checker reports local deltas and chain sums separately from landmark displacement.
- The mirror requirement covers rotation deltas. The asymmetric source mesh and off-centre head are preserved, so rendered pixels do not mirror exactly.
- The requested 2.5-degree jaw overshoot passes the closed rest pose. Tooth and lip intersections were not measured.
- The previews apply clips over rest in Blender. No engine flight overlay, procedural pose, gameplay hit timing, or model-switch test ran.
- These are flight overlays without planted feet. The bite lowers the hand below the rest ground plane; grounded blending needs separate engine review.
- The renders use grey Workbench materials. They do not assess textures, game lighting, or performance.
- No mesh collision, membrane stretch, volume, or between-frame skinning test ran. The complete checker covers the brief's animation data requirements.
- A finite difference check rejects constant-speed attack tracks. It does not prove continuous easing between the baked keys.
- The eight-frame strips and supplementary stills do not replace playback at game speed. The parent and user must judge that motion after integration.

## Provenance

OpenAI Codex authored these curves, scripts, measurements, and this record.
Blender rendered the project's existing AI-generated Embercrest hero mesh.
No outside animation, model, or reference image was imported for this task.
`tools/hero-build.md` records the Hunyuan sculpt source; `AI_DISCLOSURE.md` records the original hero concept.
The repository's `ATTRIBUTION.md` permits labelled original AI renders and script-authored assets.
The GLB metadata, manifest, and strip titles label this work as AI-authored.

The five-path restriction excludes edits to `AI_DISCLOSURE.md`.
The parent must add this clip set to that file before publication.
The existing disclosure already lists Codex-authored keyframed animation clips as a category.

## Reproduction identity

```text
source SHA256  32655f936dfd5a26ab718a0b2382a6e0bb93899073b249b9b48911c92f776138
clip SHA256    6104ce3f8563e71fb3df83b1f1d9d7b16be91bc6e6461bb627bded1f412e1a99
```

`preview-manifest.json` records these same hashes, the Blender version, and every rendered frame.
`preview-pose-check.json` records the Blender/export position comparison for all five clips.

## Complete checker output

The checker exited zero. This is its complete output:

```text
PASS GLB loads: 273884 bytes
PASS 77 unique model joints
PASS armature alone: no mesh, skin, material, image, or required extension
PASS embedded animation buffer
PASS model hierarchy
PASS joint scene roots
PASS exact rest TRS: rotation 0.000000000 deg; translation 0.000000000 m; exact fields True
PASS five named animations
PASS bite rotation-only channels: one rotation channel per joint; no translation or scale keys
PASS bite duration and 30 fps sampling: 0.90 s; 28 unit-quaternion keys per joint
PASS bite rest endpoints: max 0.00000000 deg
PASS bite no pop: 23.12000 deg; jaw; frames 6 to 7
PASS claw_r rotation-only channels: one rotation channel per joint; no translation or scale keys
PASS claw_r duration and 30 fps sampling: 0.90 s; 28 unit-quaternion keys per joint
PASS claw_r rest endpoints: max 0.00000000 deg
PASS claw_r no pop: 24.86708 deg; upper_arm_r; frames 7 to 8
PASS claw_l rotation-only channels: one rotation channel per joint; no translation or scale keys
PASS claw_l duration and 30 fps sampling: 0.90 s; 28 unit-quaternion keys per joint
PASS claw_l rest endpoints: max 0.00000000 deg
PASS claw_l no pop: 24.86708 deg; upper_arm_l; frames 7 to 8
PASS tail_r rotation-only channels: one rotation channel per joint; no translation or scale keys
PASS tail_r duration and 30 fps sampling: 1.00 s; 31 unit-quaternion keys per joint
PASS tail_r rest endpoints: max 0.00000000 deg
PASS tail_r no pop: 20.62500 deg; tail_01; frames 8 to 9
PASS tail_l rotation-only channels: one rotation channel per joint; no translation or scale keys
PASS tail_l duration and 30 fps sampling: 1.00 s; 31 unit-quaternion keys per joint
PASS tail_l rest endpoints: max 0.00000000 deg
PASS tail_l no pop: 20.62500 deg; tail_01; frames 8 to 9
PASS claw exact mirrored deltas: max 0.000000000 deg over every joint and key
PASS tail exact mirrored deltas: max 0.000000000 deg over every joint and key
PASS bite neck coil total: 42.0000 deg at 0.1667 s; range (35, 45); window (0, 0.16)
PASS bite neck throw total: 28.5000 deg at 0.3000 s; range (25, 30); window (0.16, 0.36)
PASS bite jaw opens: 40.0000 deg at 0.1667 s; range (32, 48); window (0, 0.16)
PASS bite jaw closed overshoot: 2.5000 deg at 0.2667 s; range (2, 3); window (0.24, 0.28)
PASS bite head recoil: 8.0000 deg at 0.4667 s; range (6.4, 9.6); window (0.36, 0.65)
PASS bite root pitch: 6.0000 deg at 0.2333 s; range (6, 10); window (0.16, 0.36)
PASS bite chest pitch: 8.0000 deg at 0.2667 s; range (6, 10); window (0.16, 0.36)
PASS bite wing_root_r sweep: 15.0000 deg at 0.2667 s; range (12, 18); window (0.16, 0.36)
PASS bite upper_arm_r reach: 25.0000 deg at 0.2667 s; range (20, 30); window (0.16, 0.36)
PASS bite wing_root_l sweep: 15.0000 deg at 0.2667 s; range (12, 18); window (0.16, 0.36)
PASS bite upper_arm_l reach: 25.0000 deg at 0.2667 s; range (20, 30); window (0.16, 0.36)
PASS bite tail root lift: 12.0000 deg at 0.2667 s; range (10, 15); window (0.16, 0.36)
PASS claw_r shoulder wind-up: 29.5306 deg at 0.1667 s; range (24, 36); window (0, 0.18)
PASS claw_r shoulder forward travel: 59.5306 deg at 0.3000 s; range (48.0, 84.0); window (0.18, 0.3)
PASS claw_r elbow closed: 29.5306 deg at 0.2000 s; range (24, 36); window (0, 0.22)
PASS claw_r elbow opening travel: 49.5306 deg at 0.3333 s; range (40, 60); window (0.18, 0.42)
PASS claw_r wrist flick: 40.0000 deg at 0.3667 s; range (32, 48); window (0.18, 0.42)
PASS claw_r opposite foreleg brace: 15.0000 deg at 0.3000 s; range (12, 18); window (0.18, 0.42)
PASS claw_r spine twist: 15.0000 deg at 0.3333 s; range (12, 18); window (0.18, 0.42)
PASS claw_r neck twist total: 15.0000 deg at 0.3333 s; range (12, 18); window (0.18, 0.42)
PASS claw_r head lead: 15.0000 deg at 0.2333 s; range (12, 18); window (0.18, 0.3)
PASS claw_r striking wing lift: 15.0000 deg at 0.3000 s; range (12, 18); window (0.18, 0.42)
PASS claw_r opposite wing dip: 10.0000 deg at 0.3333 s; range (8, 12); window (0.18, 0.42)
PASS claw_r tail opposite swing: 22.0000 deg at 0.3000 s; range (20, 25); window (0.18, 0.42)
PASS claw_r paw crosses chest centre: min signed hand X -0.98196 m
PASS claw_r toe 1 curl: 28.0000 deg at 0.4333 s; range (20, 34); window (0.3, 0.48)
PASS claw_r toe 2 curl: 28.0000 deg at 0.4333 s; range (20, 34); window (0.3, 0.48)
PASS claw_r toe 3 curl: 28.0000 deg at 0.4333 s; range (20, 34); window (0.3, 0.48)
PASS claw_r outer toe spread: 16.0000 deg at 0.2333 s; range (10, 20); window (0.18, 0.34)
PASS claw_r inner toe spread: 16.0000 deg at 0.2333 s; range (10, 20); window (0.18, 0.34)
PASS claw_l shoulder wind-up: 29.5306 deg at 0.1667 s; range (24, 36); window (0, 0.18)
PASS claw_l shoulder forward travel: 59.5306 deg at 0.3000 s; range (48.0, 84.0); window (0.18, 0.3)
PASS claw_l elbow closed: 29.5306 deg at 0.2000 s; range (24, 36); window (0, 0.22)
PASS claw_l elbow opening travel: 49.5306 deg at 0.3333 s; range (40, 60); window (0.18, 0.42)
PASS claw_l wrist flick: 40.0000 deg at 0.3667 s; range (32, 48); window (0.18, 0.42)
PASS claw_l opposite foreleg brace: 15.0000 deg at 0.3000 s; range (12, 18); window (0.18, 0.42)
PASS claw_l spine twist: 15.0000 deg at 0.3333 s; range (12, 18); window (0.18, 0.42)
PASS claw_l neck twist total: 15.0000 deg at 0.3333 s; range (12, 18); window (0.18, 0.42)
PASS claw_l head lead: 15.0000 deg at 0.2333 s; range (12, 18); window (0.18, 0.3)
PASS claw_l striking wing lift: 15.0000 deg at 0.3000 s; range (12, 18); window (0.18, 0.42)
PASS claw_l opposite wing dip: 10.0000 deg at 0.3333 s; range (8, 12); window (0.18, 0.42)
PASS claw_l tail opposite swing: 22.0000 deg at 0.3000 s; range (20, 25); window (0.18, 0.42)
PASS claw_l paw crosses chest centre: min signed hand X -0.98196 m
PASS claw_l toe 1 curl: 28.0000 deg at 0.4333 s; range (20, 34); window (0.3, 0.48)
PASS claw_l toe 2 curl: 28.0000 deg at 0.4333 s; range (20, 34); window (0.3, 0.48)
PASS claw_l toe 3 curl: 28.0000 deg at 0.4333 s; range (20, 34); window (0.3, 0.48)
PASS claw_l outer toe spread: 16.0000 deg at 0.2333 s; range (10, 20); window (0.18, 0.34)
PASS claw_l inner toe spread: 16.0000 deg at 0.2333 s; range (10, 20); window (0.18, 0.34)
PASS tail_r root left coil: 25.0000 deg at 0.2000 s; range (20, 30); window (0, 0.22)
PASS tail_r root right whip: 35.0000 deg at 0.3333 s; range (28, 42); window (0.22, 0.38)
PASS tail_r tail_01 whip: 35.0000 deg at 0.3333 s; range (28, 42); window (0.22, 0.55)
PASS tail_r tail_02 whip: 36.0000 deg at 0.3667 s; range (29, 43); window (0.22, 0.55)
PASS tail_r tail_03 whip: 37.0000 deg at 0.4000 s; range (30, 44); window (0.22, 0.55)
PASS tail_r tail_04 whip: 38.0000 deg at 0.4333 s; range (31, 45); window (0.22, 0.55)
PASS tail_r tail_05 whip: 39.0000 deg at 0.4667 s; range (32, 46); window (0.22, 0.55)
PASS tail_r tail_06 whip: 40.0000 deg at 0.5000 s; range (33, 47); window (0.22, 0.55)
PASS tail_r tail_07 whip: 41.0000 deg at 0.5333 s; range (34, 48); window (0.22, 0.55)
PASS tail_r tail_08 whip: 42.0000 deg at 0.5667 s; range (35, 49); window (0.22, 0.55)
PASS tail_r larger distal peaks: [35.0, 36.0, 37.0, 38.0, 39.0, 40.0, 41.0, 42.0]
PASS tail_r tip cumulative bend: 126.7907 deg at 0.5000 s; range (70, 180); window (0.22, 0.55)
PASS tail_r root hip/spine twist: 12.0000 deg at 0.3333 s; range (10, 15); window (0.22, 0.55)
PASS tail_r chest hip/spine twist: 10.0000 deg at 0.3667 s; range (10, 15); window (0.22, 0.55)
PASS tail_r neck counter-swing: 15.0000 deg at 0.4333 s; range (12, 18); window (0.22, 0.55)
PASS tail_r head counter-swing: 15.0000 deg at 0.4667 s; range (12, 18); window (0.22, 0.55)
PASS tail_r hind kick: 15.0000 deg at 0.3667 s; range (12, 18); window (0.22, 0.55)
PASS tail_l root left coil: 25.0000 deg at 0.2000 s; range (20, 30); window (0, 0.22)
PASS tail_l root right whip: 35.0000 deg at 0.3333 s; range (28, 42); window (0.22, 0.38)
PASS tail_l tail_01 whip: 35.0000 deg at 0.3333 s; range (28, 42); window (0.22, 0.55)
PASS tail_l tail_02 whip: 36.0000 deg at 0.3667 s; range (29, 43); window (0.22, 0.55)
PASS tail_l tail_03 whip: 37.0000 deg at 0.4000 s; range (30, 44); window (0.22, 0.55)
PASS tail_l tail_04 whip: 38.0000 deg at 0.4333 s; range (31, 45); window (0.22, 0.55)
PASS tail_l tail_05 whip: 39.0000 deg at 0.4667 s; range (32, 46); window (0.22, 0.55)
PASS tail_l tail_06 whip: 40.0000 deg at 0.5000 s; range (33, 47); window (0.22, 0.55)
PASS tail_l tail_07 whip: 41.0000 deg at 0.5333 s; range (34, 48); window (0.22, 0.55)
PASS tail_l tail_08 whip: 42.0000 deg at 0.5667 s; range (35, 49); window (0.22, 0.55)
PASS tail_l larger distal peaks: [35.0, 36.0, 37.0, 38.0, 39.0, 40.0, 41.0, 42.0]
PASS tail_l tip cumulative bend: 126.7907 deg at 0.5000 s; range (70, 180); window (0.22, 0.55)
PASS tail_l root hip/spine twist: 12.0000 deg at 0.3333 s; range (10, 15); window (0.22, 0.55)
PASS tail_l chest hip/spine twist: 10.0000 deg at 0.3667 s; range (10, 15); window (0.22, 0.55)
PASS tail_l neck counter-swing: 15.0000 deg at 0.4333 s; range (12, 18); window (0.22, 0.55)
PASS tail_l head counter-swing: 15.0000 deg at 0.4667 s; range (12, 18); window (0.22, 0.55)
PASS tail_l hind kick: 15.0000 deg at 0.3667 s; range (12, 18); window (0.22, 0.55)
PASS bite neck coil overlap: peak frames [3, 4, 5]; 1 to 3 frames per joint
PASS bite neck coil delayed starts: first moving frames [1, 2, 3]
PASS bite neck throw overlap: peak frames [7, 8, 9]; 1 to 3 frames per joint
PASS bite neck throw delayed starts: first moving frames [1, 2, 3]
PASS bite tail lift overlap: peak frames [8, 9, 10, 11, 12, 13, 14, 15]; 1 to 3 frames per joint
PASS bite tail lift delayed starts: first moving frames [4, 5, 6, 7, 8, 9, 10, 11]
PASS tail_r tail whip overlap: peak frames [10, 11, 12, 13, 14, 15, 16, 17]; 1 to 3 frames per joint
PASS tail_r tail whip delayed starts: first moving frames [1, 2, 3, 4, 5, 6, 7, 8]
PASS claw_r tail swing overlap: peak frames [9, 10, 11, 12, 13, 14, 15, 16]; 1 to 3 frames per joint
PASS claw_r tail swing delayed starts: first moving frames [1, 2, 3, 4, 5, 6, 7, 8]
PASS bite eased strike samples: constant-speed active tracks: []
PASS claw_r eased strike samples: constant-speed active tracks: []
PASS claw_l eased strike samples: constant-speed active tracks: []
PASS tail_r eased strike samples: constant-speed active tracks: []
PASS tail_l eased strike samples: constant-speed active tracks: []
PASS bite recovery overshoot: []
PASS claw_r recovery overshoot: []
PASS claw_l recovery overshoot: []
PASS tail_r recovery overshoot: []
PASS tail_l recovery overshoot: []
PASS bite head moves up/back then down/forward: coil XYZ (-7.281343947923569e-10, 0.354655980662959, 0.9153218824693714); hit XYZ (-5.876230179530095e-10, -1.2655605455038597, -0.6528214805714132)
PASS complete checker: 133/133 checks
```

## Checker failure tests

The tests modify temporary export copies inside `artifacts/hero-clips/` and delete each copy afterward.
They exercise wrong names, rest transforms, non-rotation tracks, endpoints, mirrors, amplitudes, overlap, and frame jumps.
The valid export remains unchanged.

```text
PASS rejected joint_names: KeyError: 'wrong_root'
PASS rejected rest: exact rest TRS
PASS rejected translation_keys: bite rotation-only channels
PASS rejected endpoints: bite rest endpoints, bite head recoil
PASS rejected mirror: claw exact mirrored deltas
PASS rejected amplitude: bite jaw opens, bite jaw closed overshoot
PASS rejected overlap: tail exact mirrored deltas, tail_r larger distal peaks, tail_r tail whip overlap, tail_r tail whip delayed starts
PASS rejected pop: bite no pop, bite head recoil
```
