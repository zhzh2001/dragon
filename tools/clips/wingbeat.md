# Tide2 and Storm3 wingbeats

Each model has two AI-authored loops. The engine plays `flap` in cruise and blends toward `flap_climb` as effort increases.
Each file contains the original joint hierarchy and 61 rotation samples per wing joint over one second.
The engine applies these rotations after its flight pose. The flight profile sets the duration of one beat.

The shoulder sets the pace. The wrist and fingertips reach their highest and lowest points later.
A short turn at the bottom gives way to an open recovery. The wings slow as they approach the top.
The generator mirrors the rotations in model space before converting them into each joint’s local axes.

## Tide2

The seven-rib fan stays spread at both reversals. Its shoulder reaches the bottom halfway through the beat.
The studio plays one beat in 0.85 seconds. The key measurements use the bind shoulder after the rig levels it.
The studio glide shoulder sits 15.32° above horizontal at frame 200.
Against that glide, the finished cruise shoulder travels from +33.42° to −29.94°. Climb travels from +43.32° to −39.92°.
I kept these arcs because the revised fan clears the legs through most of the beat and stays open over the back.

In the procedural side view, the near fan fell below the feet at frames 226, 232 and 238.
The first clip passed the clearance limits, but the rear hook still projected about 42 pixels below the toes at frame 232.
I raised the fingertip floor from −17° to −8° in cruise and from −23° to −13° in climb.
The lowest skin rose another 0.60 m in cruise and 0.63 m in climb.

In report 0035, the revised hook clears the hind toes by about 12 pixels at frame 232.
In the climb side view, the same hook projects about 10 pixels below the toes at frame 232. Both feet remain visible below the membrane.
The near wing hides most of the head at frames 200, 206 and 213. All eight cruise side views show the toes. The raised outer wing looks nearly edge-on at the bottom.

In report 0036, both fans keep their width through the turn at the top. The close camera cuts off the nearest hook in several frames.
The far spur overlaps the head in this camera’s projection. The elevated view shows the membrane remaining attached at the shoulder.
At frame 219, the hands still sit above the descending shoulders. At 238, the wrists rise ahead of the hooks.
The front view shows both wrist spurs clear of the feet at frames 226 to 238.
The flatter downstroke gives a weaker impression of thrust than the procedural beat.

At peak flexion, the posed skin retains 91.5% of its widest span in cruise and 89.7% in climb.
A surface check found zero intersections between the outer rib patches across 120 settled poses per loop.
It checked 10,349 triangles per pose. Each tested triangle lay beyond the first quarter of its rib. Every vertex on those triangles carried over 85% weight from that rib.
The reconstructed minima for all 14 ribs matched the probe to the printed millimetre in both loops.

[Cruise comparison](../../artifacts/wingbeat/tide2/cruise-before-after.jpg) · [Climb comparison](../../artifacts/wingbeat/tide2/climb-before-after.jpg) · [Report views](../../artifacts/wingbeat/tide2/report-views-before-after.jpg) · [All after views](../../artifacts/wingbeat/tide2/views.jpg)

At takeoff frame 52 and chase frame 313, the new beat shows the membranes across a broad V.
At takeoff frame 76, both fans extend almost flat above the trailing feet.
The banked turn keeps both wings extended and clear of the tail.
The frame-200 glide matched pixel for pixel inside a 775×564 box surrounding the dragon. Only 187 landscape pixels changed.
The existing engine played both clips without a rig or flight override.
At chase frame 339, a gap separates the hands from the feet. The model switch also shows the open low pose.
The close flap view puts the far hook behind the neck. The head, crest and jaw keep their procedural shapes.
The two engine passes, checks and 82 matched camera pairs took about 30 minutes.

### Clearance in the engine

The probe sampled the posed skin at 60 Hz after four seconds of settling. All distances are in game metres.
The last column measures distance from the centre plane for wing skin more than 0.5 m below the belly.

| Motion | Nearest rib to centre | Highest skin above back | Lowest skin from belly | Nearest under belly |
|---|---:|---:|---:|---:|
| Procedural cruise | +0.181 | +2.59 | -3.27 | +1.03 |
| Procedural climb | +0.181 | +2.59 | -3.27 | +1.03 |
| Clip cruise | +1.408 | +1.50 | -0.32 | No skin |
| Clip climb | +1.213 | +1.95 | -0.63 | +4.41 |

[Full probe output](../../artifacts/wingbeat/tide2/probe.txt)

### Measurements from the exported clip

The checker places each joint from its stored rotations and source offsets. The arm angles use the model’s neutral reference.
The clearance table above includes the engine’s complete flight pose.

| Measurement | Cruise | Climb |
|---|---:|---:|
| Arm high and frame | +40.00° at 0/60 | +50.00° at 0/60 |
| Arm low and frame | -22.30° at 30/60 | -32.36° at 30/60 |
| Wrist reversal delay (top, bottom) | 3/60, 3/60 | 4/60, 4/60 |
| Fingertip reversal delay (top, bottom) | 5/60, 5/60 | 7/60, 7/60 |
| Peak elbow flexion | 22.97° at 40/60 | 27.97° at 40/60 |
| Peak wrist flexion | 28.97° at 40/60 | 35.96° at 40/60 |
| Span at peak wrist flexion | 91.5% | 89.4% |
| Smallest adjacent fingertip angle | 6.75° | 6.57° |
| Largest single key interval | 4.84° | 7.69° |

The wrist and each fingertip peak on the same delayed frames on both sides. Span uses the widest horizontal joint span of that loop.

### Measured joint axes

Blender turned each joint +5° about model X, Y and Z. Each cell gives the resulting X/Y/Z displacement in thousandths of a source unit.
The point is the first child’s tail, or the joint’s own tail for a leaf. The JSON also records each axis in parent coordinates.

| Wing joint | Probe point | +X | +Y | +Z |
|---|---|---:|---:|---:|
| `wing_root_l` | `wing_arm_l.tail` | 0.00 / -4.34 / 1.41 | 4.69 / 0.00 / 9.45 | -1.18 / -9.70 / -0.00 |
| `wing_arm_l` | `wing_wrist_l.tail` | -0.00 / -0.83 / 1.05 | 1.02 / 0.00 / 5.26 | -0.86 / -5.35 / 0.00 |
| `wing_wrist_l` | `wing_finger_1a_l.tail` | 0.00 / 10.28 / 0.54 | -10.27 / 0.00 / 0.71 | -0.08 / -0.27 / 0.00 |
| `wing_finger_1a_l` | `wing_finger_1b_l.tail` | 0.00 / 17.72 / 0.16 | -17.74 / 0.00 / -0.27 | 0.56 / 1.07 / 0.00 |
| `wing_finger_1b_l` | `wing_finger_1tip_l.tail` | 0.00 / 9.36 / -0.05 | -9.36 / 0.00 / -0.05 | 0.43 / 0.47 / -0.00 |
| `wing_finger_1tip_l` | `wing_finger_1tip_l.tail` | 0.00 / 0.36 / -0.00 | -0.36 / 0.00 / -0.00 | 0.02 / 0.02 / 0.00 |
| `wing_finger_2a_l` | `wing_finger_2b_l.tail` | 0.00 / 16.60 / -0.15 | -15.73 / -0.00 / 19.81 | 1.70 / -19.05 / 0.00 |
| `wing_finger_2b_l` | `wing_finger_2tip_l.tail` | 0.00 / 4.21 / -0.82 | -3.75 / 0.00 / 9.70 | 1.41 / -9.47 / 0.00 |
| `wing_finger_2tip_l` | `wing_finger_2tip_l.tail` | 0.00 / 0.16 / -0.03 | -0.14 / 0.00 / 0.37 | 0.05 / -0.36 / 0.00 |
| `wing_finger_3a_l` | `wing_finger_3b_l.tail` | 0.00 / 2.30 / -0.69 | -1.16 / 0.00 / 25.46 | 1.89 / -25.33 / -0.00 |
| `wing_finger_3b_l` | `wing_finger_3tip_l.tail` | -0.00 / 0.22 / -0.90 | 0.39 / 0.00 / 13.15 | 1.48 / -13.10 / -0.00 |
| `wing_finger_3tip_l` | `wing_finger_3tip_l.tail` | 0.00 / 0.01 / -0.03 | 0.02 / 0.00 / 0.51 | 0.06 / -0.50 / 0.00 |
| `wing_finger_4a_l` | `wing_finger_4b_l.tail` | 0.00 / -13.21 / -1.54 | 14.43 / 0.00 / 26.44 | 2.14 / -26.98 / 0.00 |
| `wing_finger_4b_l` | `wing_finger_4tip_l.tail` | 0.00 / -4.15 / -0.73 | 4.82 / 0.00 / 14.77 | 1.20 / -14.93 / 0.00 |
| `wing_finger_4tip_l` | `wing_finger_4tip_l.tail` | 0.00 / -0.16 / -0.03 | 0.19 / 0.00 / 0.57 | 0.05 / -0.57 / -0.00 |
| `wing_finger_5a_l` | `wing_finger_5b_l.tail` | 0.00 / -22.10 / -1.93 | 23.33 / 0.00 / 26.31 | 2.15 / -27.24 / -0.00 |
| `wing_finger_5b_l` | `wing_finger_5tip_l.tail` | 0.00 / -6.70 / -0.38 | 7.34 / -0.00 / 14.30 | 0.73 / -14.59 / 0.00 |
| `wing_finger_5tip_l` | `wing_finger_5tip_l.tail` | 0.00 / -0.26 / -0.01 | 0.28 / 0.00 / 0.55 | 0.03 / -0.56 / 0.00 |
| `wing_finger_6a_l` | `wing_finger_6b_l.tail` | 0.00 / -32.30 / -2.28 | 33.40 / 0.00 / 23.08 | 1.94 / -24.45 / -0.00 |
| `wing_finger_6b_l` | `wing_finger_6tip_l.tail` | 0.00 / -10.27 / 0.19 | 10.74 / 0.00 / 10.97 | -0.14 / -11.45 / 0.00 |
| `wing_finger_6tip_l` | `wing_finger_6tip_l.tail` | 0.00 / -0.40 / 0.01 | 0.41 / 0.00 / 0.42 | -0.01 / -0.44 / 0.00 |
| `wing_finger_7a_l` | `wing_finger_7b_l.tail` | 0.00 / -40.59 / -2.30 | 41.63 / 0.00 / 21.41 | 1.54 / -23.16 / 0.00 |
| `wing_finger_7b_l` | `wing_finger_7tip_l.tail` | 0.00 / -13.80 / -0.06 | 14.02 / 0.00 / 4.93 | -0.30 / -5.55 / 0.00 |
| `wing_finger_7tip_l` | `wing_finger_7tip_l.tail` | 0.00 / -0.53 / -0.00 | 0.54 / 0.00 / 0.19 | -0.01 / -0.21 / 0.00 |
| `wing_root_r` | `wing_arm_r.tail` | 0.00 / -4.34 / 1.41 | 3.85 / 0.00 / -9.82 | -2.02 / 9.57 / 0.00 |
| `wing_arm_r` | `wing_wrist_r.tail` | 0.00 / -0.83 / 1.05 | 0.55 / 0.00 / -5.33 | -1.32 / 5.25 / 0.00 |
| `wing_wrist_r` | `wing_finger_1a_r.tail` | -0.00 / 10.28 / 0.54 | -10.30 / 0.00 / 0.19 | -0.10 / 0.26 / 0.00 |
| `wing_finger_1a_r` | `wing_finger_1b_r.tail` | -0.00 / 17.72 / 0.16 | -17.65 / 0.00 / 1.82 | 0.66 / -1.02 / 0.00 |
| `wing_finger_1b_r` | `wing_finger_1tip_r.tail` | 0.00 / 9.36 / -0.05 | -9.32 / 0.00 / 0.86 | 0.47 / -0.43 / 0.00 |
| `wing_finger_1tip_r` | `wing_finger_1tip_r.tail` | 0.00 / 0.36 / -0.00 | -0.36 / 0.00 / 0.03 | 0.02 / -0.02 / 0.00 |
| `wing_finger_2a_r` | `wing_finger_2b_r.tail` | 0.00 / 16.60 / -0.15 | -17.39 / 0.00 / -18.36 | 0.04 / 19.13 / 0.00 |
| `wing_finger_2b_r` | `wing_finger_2tip_r.tail` | 0.00 / 4.21 / -0.82 | -4.59 / 0.00 / -9.34 | 0.58 / 9.56 / 0.00 |
| `wing_finger_2tip_r` | `wing_finger_2tip_r.tail` | 0.00 / 0.16 / -0.03 | -0.18 / 0.00 / -0.36 | 0.02 / 0.37 / 0.00 |
| `wing_finger_3a_r` | `wing_finger_3b_r.tail` | -0.00 / 2.30 / -0.69 | -3.37 / 0.00 / -25.26 | -0.32 / 25.40 / -0.00 |
| `wing_finger_3b_r` | `wing_finger_3tip_r.tail` | 0.00 / 0.22 / -0.90 | -0.76 / 0.00 / -13.14 | 0.33 / 13.18 / 0.00 |
| `wing_finger_3tip_r` | `wing_finger_3tip_r.tail` | 0.00 / 0.01 / -0.03 | -0.03 / 0.00 / -0.51 | 0.01 / 0.51 / 0.00 |
| `wing_finger_4a_r` | `wing_finger_4b_r.tail` | 0.00 / -13.21 / -1.54 | 12.07 / 0.00 / -27.60 | -0.22 / 27.06 / 0.00 |
| `wing_finger_4b_r` | `wing_finger_4tip_r.tail` | 0.00 / -4.15 / -0.73 | 3.52 / 0.00 / -15.14 | -0.11 / 14.98 / 0.00 |
| `wing_finger_4tip_r` | `wing_finger_4tip_r.tail` | 0.00 / -0.16 / -0.03 | 0.14 / 0.00 / -0.58 | -0.00 / 0.58 / 0.00 |
| `wing_finger_5a_r` | `wing_finger_5b_r.tail` | 0.00 / -22.10 / -1.93 | 20.95 / 0.00 / -28.25 | -0.23 / 27.32 / 0.00 |
| `wing_finger_5b_r` | `wing_finger_5tip_r.tail` | 0.00 / -6.70 / -0.38 | 6.07 / 0.00 / -14.89 | -0.55 / 14.60 / 0.00 |
| `wing_finger_5tip_r` | `wing_finger_5tip_r.tail` | 0.00 / -0.26 / -0.01 | 0.23 / 0.00 / -0.57 | -0.02 / 0.56 / 0.00 |
| `wing_finger_6a_r` | `wing_finger_6b_r.tail` | 0.00 / -32.30 / -2.28 | 31.27 / -0.00 / -25.90 | -0.20 / 24.53 / -0.00 |
| `wing_finger_6b_r` | `wing_finger_6tip_r.tail` | 0.00 / -10.27 / 0.19 | 9.74 / 0.00 / -11.87 | -1.13 / 11.39 / 0.00 |
| `wing_finger_6tip_r` | `wing_finger_6tip_r.tail` | 0.00 / -0.40 / 0.01 | 0.37 / 0.00 / -0.46 | -0.04 / 0.44 / 0.00 |
| `wing_finger_7a_r` | `wing_finger_7b_r.tail` | 0.00 / -40.59 / -2.30 | 39.60 / -0.00 / -24.96 | -0.49 / 23.21 / 0.00 |
| `wing_finger_7b_r` | `wing_finger_7tip_r.tail` | 0.00 / -13.80 / -0.06 | 13.54 / 0.00 / -6.13 | -0.79 / 5.51 / 0.00 |
| `wing_finger_7tip_r` | `wing_finger_7tip_r.tail` | 0.00 / -0.53 / -0.00 | 0.52 / 0.00 / -0.24 | -0.03 / 0.21 / 0.00 |

[Poses and parent axes](wingbeat/tide2.json)

<details>
<summary>Posed wing measurements</summary>

```text
Cruise studio frames 240 to 359
right rib minima in metres 1.408 2.484 3.106 3.234 3.353 3.210 3.544
left rib minima in metres 1.439 2.565 3.187 3.379 3.585 3.270 3.591
span at nearest sampled peak fold / widest 0.915
right shoulder elevation in frame 200 glide +15.32 deg
right shoulder elevation against horizontal +48.74 to -14.63 deg
right shoulder excursion from frame 200 glide +33.42 to -29.94 deg
right shoulder high and low phases 0.000 0.490
wing_wrist_r high and low phases 0.039 0.549
wing_finger_1tip_r high and low phases 0.078 0.588
wing_finger_2tip_r high and low phases 0.078 0.588
wing_finger_3tip_r high and low phases 0.078 0.588
wing_finger_4tip_r high and low phases 0.078 0.588
wing_finger_5tip_r high and low phases 0.078 0.569
wing_finger_6tip_r high and low phases 0.078 0.569
wing_finger_7tip_r high and low phases 0.078 0.569
Climb studio frames 240 to 359
right rib minima in metres 1.213 2.208 2.794 2.912 3.013 2.863 3.213
left rib minima in metres 1.248 2.302 2.903 3.077 3.272 2.953 3.292
span at nearest sampled peak fold / widest 0.897
right shoulder elevation in frame 200 glide +15.32 deg
right shoulder elevation against horizontal +58.64 to -24.61 deg
right shoulder excursion from frame 200 glide +43.32 to -39.92 deg
right shoulder high and low phases 0.000 0.490
wing_wrist_r high and low phases 0.059 0.569
wing_finger_1tip_r high and low phases 0.118 0.608
wing_finger_2tip_r high and low phases 0.118 0.608
wing_finger_3tip_r high and low phases 0.118 0.608
wing_finger_4tip_r high and low phases 0.118 0.608
wing_finger_5tip_r high and low phases 0.118 0.608
wing_finger_6tip_r high and low phases 0.118 0.608
wing_finger_7tip_r high and low phases 0.118 0.608
```

</details>

<details>
<summary>Outer rib surface test</summary>

```text
PASS flap 120 frames, 14 outer rib patches, 10349 triangles, zero intersecting pairs
PASS flap_climb 120 frames, 14 outer rib patches, 10349 triangles, zero intersecting pairs
Blender 5.3.0 (hash 02f0899d78e3 built 2026-11-10 01:31:21)

Blender quit
```

</details>

<details>
<summary>Checker output</summary>

```text
PASS 1 96 joint nodes, source hierarchy and two named loops
PASS 2 source rest translations, rotations and scales
PASS 1 flap 61 unit rotations from 0 to 1 second
PASS 2 flap paired wing rotation channels only
PASS 1 flap_climb 61 unit rotations from 0 to 1 second
PASS 2 flap_climb paired wing rotation channels only
PASS 3 flap loop seam 0.00000 deg
PASS 4 flap mirror error in model axes 0.00000 deg
PASS 5 flap arm relative to neutral +40.00 at 0/60, -22.30 deg at 30/60
PASS 6 flap wrist height lags 3/60 and 3/60
PASS 6 flap rib 1 height lags 5/60 and 5/60
PASS 6 flap rib 2 height lags 5/60 and 5/60
PASS 6 flap rib 3 height lags 5/60 and 5/60
PASS 6 flap rib 4 height lags 5/60 and 5/60
PASS 6 flap rib 5 height lags 5/60 and 5/60
PASS 6 flap rib 6 height lags 5/60 and 5/60
PASS 6 flap rib 7 height lags 5/60 and 5/60
PASS 7 flap elbow fold 22.97 deg at 40/60, 0.41 at 57/60
PASS 7 flap wrist fold 28.97 deg at 40/60, 0.52 at 57/60
PASS 8 flap largest step 4.84 deg on wing_arm_r
PASS 9 flap arm stroke +40.00 to -22.30 deg
PASS 9 flap peak elbow fold 22.97 deg
PASS 9 flap peak wrist fold 28.97 deg
PASS 9 flap span at peak wrist fold 0.915 of widest
PASS 9 flap smallest adjacent tip separation 6.75 deg, largest closure 1.33 deg
PASS 9 flap fan narrows 7.23 deg at peak wrist fold
PASS 3 flap_climb loop seam 0.00000 deg
PASS 4 flap_climb mirror error in model axes 0.00000 deg
PASS 5 flap_climb arm relative to neutral +50.00 at 0/60, -32.36 deg at 30/60
PASS 6 flap_climb wrist height lags 4/60 and 4/60
PASS 6 flap_climb rib 1 height lags 7/60 and 7/60
PASS 6 flap_climb rib 2 height lags 7/60 and 7/60
PASS 6 flap_climb rib 3 height lags 7/60 and 7/60
PASS 6 flap_climb rib 4 height lags 7/60 and 7/60
PASS 6 flap_climb rib 5 height lags 7/60 and 7/60
PASS 6 flap_climb rib 6 height lags 7/60 and 7/60
PASS 6 flap_climb rib 7 height lags 7/60 and 7/60
PASS 7 flap_climb elbow fold 27.97 deg at 40/60, 0.50 at 57/60
PASS 7 flap_climb wrist fold 35.96 deg at 40/60, 0.65 at 57/60
PASS 8 flap_climb largest step 7.69 deg on wing_arm_r
PASS 9 flap_climb arm stroke +50.00 to -32.36 deg
PASS 9 flap_climb peak elbow fold 27.97 deg
PASS 9 flap_climb peak wrist fold 35.96 deg
PASS 9 flap_climb span at peak wrist fold 0.894 of widest
PASS 9 flap_climb smallest adjacent tip separation 6.57 deg, largest closure 2.38 deg
PASS 9 flap_climb fan narrows 8.97 deg at peak wrist fold
PASS 9 climb exceeds cruise at both reversals and both hinges
```

</details>

<details>
<summary>Damaged export results</summary>

```text
PASS name, exit 1, FAIL 1 96 joint nodes, source hierarchy and two named loops
PASS samples, exit 1, FAIL 1 flap 61 unit rotations from 0 to 1 second
PASS rest, exit 1, FAIL 2 source rest translations, rotations and scales
PASS channel, exit 1, FAIL 2 flap paired wing rotation channels only
PASS body, exit 1, FAIL 2 flap paired wing rotation channels only
PASS seam, exit 1, FAIL 3 flap loop seam 24.70190 deg
PASS mirror, exit 1, FAIL 4 flap mirror error in model axes 180.00000 deg
PASS phase, exit 1, FAIL 5 flap arm relative to neutral +40.00 at 45/60, -22.30 deg at 15/60
PASS lag, exit 1, FAIL 6 flap rib 1 height lags 0/60 and 0/60
PASS fold, exit 1, FAIL 7 flap wrist fold 28.70 deg at 25/60, 0.54 at 57/60
PASS step, exit 1, FAIL 8 flap largest step 179.84 deg on wing_arm_r
PASS amplitude, exit 1, FAIL 9 flap_climb arm stroke +40.00 to -22.30 deg
```

</details>

<details>
<summary>Raw probe output</summary>

```text
procedural cruise ./build/rib_probe-r11 assets/tide2.glb --no-clip
beat: procedural
side 0 rib 0: nearest the centre plane +0.234 m at phase 0.51
side 0 rib 1: nearest the centre plane +1.169 m at phase 0.55
side 0 rib 2: nearest the centre plane +1.905 m at phase 0.08
side 0 rib 3: nearest the centre plane +1.553 m at phase 0.06
side 0 rib 4: nearest the centre plane +1.268 m at phase 0.06
side 0 rib 5: nearest the centre plane +0.901 m at phase 0.06
side 0 rib 6: nearest the centre plane +0.599 m at phase 0.04
side 1 rib 0: nearest the centre plane +0.181 m at phase 0.51
side 1 rib 1: nearest the centre plane +1.030 m at phase 0.53
side 1 rib 2: nearest the centre plane +1.883 m at phase 0.55
side 1 rib 3: nearest the centre plane +1.674 m at phase 0.04
side 1 rib 4: nearest the centre plane +1.446 m at phase 0.04
side 1 rib 5: nearest the centre plane +0.967 m at phase 0.04
side 1 rib 6: nearest the centre plane +0.626 m at phase 0.04
wing's highest skin: +2.59 m above the back at phase 0.10
wing's lowest skin: -3.27 m from the belly at phase 0.55 (negative is below it)
under the belly: nearest the centre plane +1.03 m at phase 0.53

procedural climb ./build/rib_probe-r11 assets/tide2.glb --no-clip --climb
beat: procedural
side 0 rib 0: nearest the centre plane +0.234 m at phase 0.51
side 0 rib 1: nearest the centre plane +1.169 m at phase 0.55
side 0 rib 2: nearest the centre plane +1.905 m at phase 0.08
side 0 rib 3: nearest the centre plane +1.553 m at phase 0.06
side 0 rib 4: nearest the centre plane +1.268 m at phase 0.06
side 0 rib 5: nearest the centre plane +0.901 m at phase 0.06
side 0 rib 6: nearest the centre plane +0.599 m at phase 0.04
side 1 rib 0: nearest the centre plane +0.181 m at phase 0.51
side 1 rib 1: nearest the centre plane +1.030 m at phase 0.53
side 1 rib 2: nearest the centre plane +1.883 m at phase 0.55
side 1 rib 3: nearest the centre plane +1.674 m at phase 0.04
side 1 rib 4: nearest the centre plane +1.446 m at phase 0.04
side 1 rib 5: nearest the centre plane +0.967 m at phase 0.04
side 1 rib 6: nearest the centre plane +0.626 m at phase 0.04
wing's highest skin: +2.59 m above the back at phase 0.10
wing's lowest skin: -3.27 m from the belly at phase 0.55 (negative is below it)
under the belly: nearest the centre plane +1.03 m at phase 0.53

clip cruise ./build/rib_probe-r11 assets/tide2.glb --clip ~/src/game-claude-wt/r11-wingbeat/assets/clips/tide2.flight.glb
beat: keyframed, ~/src/game-claude-wt/r11-wingbeat/assets/clips/tide2.flight.glb
side 0 rib 0: nearest the centre plane +1.408 m at phase 0.02
side 0 rib 1: nearest the centre plane +2.484 m at phase 0.04
side 0 rib 2: nearest the centre plane +3.106 m at phase 0.06
side 0 rib 3: nearest the centre plane +3.234 m at phase 0.04
side 0 rib 4: nearest the centre plane +3.353 m at phase 0.04
side 0 rib 5: nearest the centre plane +3.210 m at phase 0.04
side 0 rib 6: nearest the centre plane +3.544 m at phase 0.04
side 1 rib 0: nearest the centre plane +1.439 m at phase 0.02
side 1 rib 1: nearest the centre plane +2.565 m at phase 0.04
side 1 rib 2: nearest the centre plane +3.187 m at phase 0.04
side 1 rib 3: nearest the centre plane +3.379 m at phase 0.04
side 1 rib 4: nearest the centre plane +3.585 m at phase 0.04
side 1 rib 5: nearest the centre plane +3.270 m at phase 0.04
side 1 rib 6: nearest the centre plane +3.591 m at phase 0.04
wing's highest skin: +1.50 m above the back at phase 0.08
wing's lowest skin: -0.32 m from the belly at phase 0.57 (negative is below it)
under the belly: no wing skin more than 0.5 m below it

clip climb ./build/rib_probe-r11 assets/tide2.glb --clip ~/src/game-claude-wt/r11-wingbeat/assets/clips/tide2.flight.glb --climb
beat: keyframed, ~/src/game-claude-wt/r11-wingbeat/assets/clips/tide2.flight.glb
side 0 rib 0: nearest the centre plane +1.213 m at phase 0.04
side 0 rib 1: nearest the centre plane +2.208 m at phase 0.06
side 0 rib 2: nearest the centre plane +2.794 m at phase 0.08
side 0 rib 3: nearest the centre plane +2.912 m at phase 0.08
side 0 rib 4: nearest the centre plane +3.013 m at phase 0.08
side 0 rib 5: nearest the centre plane +2.863 m at phase 0.08
side 0 rib 6: nearest the centre plane +3.213 m at phase 0.08
side 1 rib 0: nearest the centre plane +1.248 m at phase 0.04
side 1 rib 1: nearest the centre plane +2.302 m at phase 0.04
side 1 rib 2: nearest the centre plane +2.903 m at phase 0.04
side 1 rib 3: nearest the centre plane +3.077 m at phase 0.04
side 1 rib 4: nearest the centre plane +3.272 m at phase 0.04
side 1 rib 5: nearest the centre plane +2.953 m at phase 0.04
side 1 rib 6: nearest the centre plane +3.292 m at phase 0.04
wing's highest skin: +1.95 m above the back at phase 0.10
wing's lowest skin: -0.63 m from the belly at phase 0.59 (negative is below it)
under the belly: nearest the centre plane +4.41 m at phase 0.63
```

</details>

## Reproduce the checks

Run these commands from this worktree. Replace `tide2` with `storm3` for the wyvern.
The generator uses Blender. The sheet tools use Python with Pillow and NumPy.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/clips/make_wingbeat.py -- tide2 --measure
python3 -B tools/clips/check_wingbeat.py tide2
python3 -B tools/clips/wingbeat_negative.py tide2
/opt/homebrew/bin/python3.14 -B tools/clips/wingbeat_studio.py tide2
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/clips/wingbeat_ribs.py -- tide2
```

The raw probe output lists the arguments for each run. Start `./build/rib_probe-r11` in the main checkout.

The render helper selects the released engine, 1920×1080 resolution and each report camera.
A plate is a background image without the dragon. The sheet helper uses it to find the silhouette for each native-size crop.

```sh
/opt/homebrew/bin/python3.14 -B tools/clips/wingbeat_render.py tide2 before
/opt/homebrew/bin/python3.14 -B tools/clips/wingbeat_render.py tide2 after
/opt/homebrew/bin/python3.14 -B tools/clips/wingbeat_render.py tide2 plate
/opt/homebrew/bin/python3.14 -B tools/clips/wingbeat_sheets.py tide2
```
