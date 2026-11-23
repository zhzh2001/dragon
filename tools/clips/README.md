# Keyframed clips

Every clip the engine plays over the procedural rig is built here by
script in Blender and checked by script. A clip file holds the armature
alone, with the model's joint names and rest pose, and rotation keys.
The engine reads keys as deltas from the rest pose
(`docs/ANIMATION.md`: "A keyframed wingbeat", "Keyframed body loops").

| Clip file in `assets/clips/` | What it is | Generator | Checker | Record |
|---|---|---|---|---|
| `embercrest.brake_bank.glb` | The first trial, 2026-11-04: a small overlay on the air brake. It did not read | `make_brake_bank.py` | `check_clip.py` | Below |
| `embercrest-hero.attacks.glb` | The hero's bite, claw and tail strikes | `make_hero_attacks.py` | `check_hero_attacks.py` | `hero-attacks.md` |
| `embercrest-hero.flight.glb` | The hero's wingbeat: `flap`, `flap_climb` | `make_hero_flight.py` | `check_hero_flight.py` | `hero-flight.md` |
| `tide2.flight.glb`, `storm3.flight.glb` | Wingbeats for two more dragons | `make_wingbeat.py <model>`, poses in `wingbeat/<model>.json` | `check_wingbeat.py <model>` | `wingbeat.md` |
| `embercrest-hero.body.glb` | The hero's neck, tail and legs: `flap_body`, `flap_climb_body`, `glide`, `walk`, `stand` | `make_hero_body.py` | `check_hero_body.py` | `hero-body.md` |

The `*-brief.md` files are the briefs the Codex jobs were given. The
`*_render.py` and `*_sheets.py` helpers capture engine frames and build
the contact sheets in `artifacts/`. They write single frames into
`before/` and `after/` folders, which are ignored by git: commit the
sheets, not the frames.

A wingbeat for another dragon: copy `wingbeat/tide2.json`, measure the
model's wing axes (`wingbeat.md` says how), and set its limits with
`tools/rig_probes/rib_centre_probe`, which plays a clip and prints how
near the wing comes to the back and the belly.

## Embercrest brake-bank clip trial


This clip adds small, overlapping adjustments to a held procedural air-brake pose.
It contains one authored loop and its mirrored version.
The left clip treats the dragon's left wing as the inside wing.

The motion uses hand-authored layered curves, baked into quaternion keys.
Shoulders lead wrists and fingers. The neck absorbs body motion while the head keeps a small inward aim.
The tail carries a travelling S wave. Hind legs paddle with delayed knees, ankles, and toes.
Forelegs move less. The jaw adds a small breathing motion to its already-open bind pose.

The clip and preview work are AI-generated with Codex.
The preview mesh is the project's AI-generated Tencent Hunyuan Embercrest asset.
The source asset remains read-only. The clip contains no mesh data or material data.
See the repository's existing `ATTRIBUTION.md` for the source asset record.

## Files

| Path | Contents |
| --- | --- |
| `assets/clips/embercrest.brake_bank.glb` | Exactly 62 joint nodes and two animations |
| `tools/clips/make_brake_bank.py` | Blender builder, bone probes, export, and optional renders |
| `tools/clips/check_clip.py` | Independent GLB checks using only the Python standard library |
| `tools/clips/preview_report.py` | Pillow contact sheets and angle traces |
| `artifacts/clip-trial/*.contact.png` | Eight times from each of three views, for each clip |
| `artifacts/clip-trial/*.traces.png` | Offset angles for every joint, grouped by function |
| `artifacts/clip-trial/frames/` | Full-size renders, extra inspection views, and bind references (on the `clip-trial` branch only: 43 MB) |
| `artifacts/clip-trial/axis-probes.json` | Measured child movement and parent/bone axes for every joint |
| `artifacts/clip-trial/check.txt` | Per-joint check table |
| `artifacts/clip-trial/checks.json` | Machine-readable check results and traces |
| `artifacts/clip-trial/preview-pose-check.json` | Blender joint positions compared with exported transforms |
| `artifacts/clip-trial/preview-manifest.json` | Render times, paths, source hash, and clip hash |

## Rebuild

Run these commands from the worktree root:

```sh
/opt/homebrew/bin/blender -b -P tools/clips/make_brake_bank.py
python3 tools/clips/check_clip.py
```

To rebuild the preview evidence, add `--preview`:

```sh
/opt/homebrew/bin/blender -b -P tools/clips/make_brake_bank.py -- --preview
python3 tools/clips/check_clip.py > artifacts/clip-trial/check.txt
```

Blender 5.2.3 and `/opt/homebrew/bin/python3.14` with Pillow produced the previews.
The checker needs only `python3`. The builder does not need Pillow unless preview assembly runs.

To measure bone axes without exporting a clip, run:

```sh
/opt/homebrew/bin/blender -b -P tools/clips/make_brake_bank.py -- --probe-only
```

The scripts resolve paths from their own location. They write only within the three trial directories.
No external animation, saved Blender scene, random seed, or interactive input is needed.

## Engine contract

The animations are named `brake_bank_l` and `brake_bank_r`.
Each lasts 2.4 seconds at 30 fps, with 73 keys from 0 through 2.4 seconds.
The final key exactly duplicates the first key. All samplers use `LINEAR` interpolation.

The engine must match tracks to model joints by name.
The output contains exactly the source skin's 62 joints, including the `jaw_tip` marker.
TRS means translation, rotation, and scale. The joint hierarchy and rest TRS values match the source.
The source's identity armature wrapper is omitted because it is not a joint.

A quaternion represents a rotation. The GLB stores its components as `[x, y, z, w]`.
For each key, the engine must calculate:

```text
delta = key * inverse(rest)
weighted_delta = slerp(identity, delta, blend_weight)
result_rotation = weighted_delta * procedural_rotation
```

Slerp interpolates rotations along the shortest arc.
The parent frame means the coordinate axes of a joint's parent.
The deltas use that frame. A key equal to the rest rotation causes no change.

The clip must overlay the procedural pose that already supplies the flare, nose-up pitch, forward legs, and bank.
It has rotation channels only. It contains no translation channels, scale channels, meshes, skins, or materials.
The engine must retain the model's hierarchy and rest translations and scales.

| Joint group | Peak offset limit | Held mean limit |
| --- | ---: | ---: |
| Root and chest | 6 degrees | 3 degrees |
| Neck and head | 10 degrees | 5 degrees |
| Jaw and jaw_tip | 8 degrees | 4 degrees |
| Wing roots and arms | 12 degrees | 6 degrees |
| Wing wrists and fingers | 16 degrees | 8 degrees |
| Each tail joint | 15 degrees | 7.5 degrees |
| Forelimbs and hind limbs | 20 degrees | 10 degrees |
| Toes | 25 degrees | 12.5 degrees |

Every joint has a 120 degrees/second angular speed limit.
The checker measures means from average rotation vectors over the 72 unique samples.
It measures each quaternion's shortest angular offset from rest.
Its speed calculation also bounds normalized linear interpolation between adjacent keys.

## Authoring and export

The source frame has X pointing right, Y pointing up, and negative Z pointing forward.
The left wing occupies negative X.
Positive five-degree probes moved actual Blender bones and measured their children.
Leaf probes used the imported bone tip. The script records the measured directions and full results.

Positive X raises the neck but lowers aft-pointing wing fingers.
Negative Z raises the left wing. Positive Y moves the aft tail toward the outside of a left turn.
Negative X opens the jaw. The builder converts these directions through each joint's parent rest rotation.

Each curve layer specifies an axis, mean angle, and sine terms with amplitude, cycle count, and phase.
The curve periods are 2.4, 1.2, and 0.8 seconds. All layers close exactly after 2.4 seconds.
Different phases and harmonic mixtures avoid a single shared pumping rhythm.
The short loop still repeats exactly; it contains no random variation.

The right clip swaps `_l` and `_r` tracks and reflects rotations across the model's X symmetry plane.
Centerline joints also reflect. The checker compares mirrored deltas rather than copied absolute rotations.
The `jaw_tip` marker keeps an identity delta and follows its parent.

The builder writes the GLB directly after calculating the animation in Blender.
It copies source rest TRS numbers instead of exporting decomposed Blender bone transforms.
The checker compares JSON values, property presence, and packed float32 bits for every rest component.

Preview deformation uses the exported float32 keys.
The builder converts each delta into the imported Blender bone basis without changing its rest pose.
It also compares every joint position against transforms composed from the exported keys at all 73 frames.

## Verification on 2026-11-04

The standard-library checker passed for both clips.
Seven deliberately invalid clips also produced nonzero checker exits.
Those cases changed rest values, channel types, loop endpoints, offset limits, mean limits, speed limits, and mesh payloads.
It checked names, hierarchy, exact rest TRS, channel types, frame times, endpoint equality, quaternion normalization, mirror symmetry, offsets, means, and speeds.

| Measurement | Result |
| --- | ---: |
| Joint nodes | 62 |
| Rotation channels per clip | 62 |
| Keys per channel | 73 |
| Largest joint offset | 5.884 degrees |
| Largest held mean | 2.100 degrees |
| Largest angular speed | 14.920 degrees/second |
| Largest head offset | 1.150 degrees |
| Largest neck offset | 2.959 degrees |
| Mirror delta error | Less than 0.000001 degrees |
| Blender/export joint position difference | Less than 0.0000007 source units |

I inspected both contact sheets and the joint traces.
I also inspected full-size side, front, below, overhead, rear, and head views.
The sheets show times 0.00, 0.30, 0.60, 0.90, 1.20, 1.50, 1.80, and 2.10 seconds.

The wings and feet adjust at different times. The tail changes curvature while the head stays comparatively quiet.
I found no obvious new mesh crossings or abrupt pose changes in the sampled views.
The head close-up retains the source's open mouth. The added jaw motion does not drive the teeth through the upper jaw.
The source membrane retains its visible folds and surface irregularities.
I widened the below and overhead cameras after the first inspection because the snout and tail tip crowded the frame edges.

These Workbench renders show the overlay on the bind pose, without the procedural flare or bank.
They verify the clip's isolated deformation. They do not establish its appearance over the engine's final procedural pose.
No engine code changed, and no engine integration test ran.
