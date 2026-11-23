# Embercrest hero wingbeat

The hero has two AI-authored wingbeats. Each contains one loop with 61 rotation keys on 22 wing joints.
The studio plays each loop over 0.85 seconds. The GLB stores it over one second.
Both animations use the hero’s 77 original joints. The unweighted end of the elbow spur follows its parent.

## Motion and timing

The cruise shoulder starts the downstroke slowly, accelerates through the upper third, and turns upward at half a beat.
The wrist continues down for three more keys. All three fingertips continue down for five.
The hand pitches nose-down through the powered stroke. During recovery, the elbow bends, the fingertips drop, and the fingers draw together.
The joints open before the shoulder finishes rising.
The authoring script fits cubic curves through the poses and samples them 60 times per cycle.
Every keyed joint turns more than 0.001° between successive samples in both clips.

The climb uses the same sequence. Its shoulder travels farther, its elbow and wrist fold more, and its fingertips reverse later.
The body, neck, legs and tail follow the flight rig in both clips.

The table measures rotations and source fingertip positions from the exported keys.
It uses the levelled shoulder frame. Phase is the fraction of one beat.

| Measurement | Cruise | Climb |
|---|---:|---:|
| Arm high, phase 0 | +54.79° | +64.79° |
| Arm low, phase 0.50 | −40.09° | −60.09° |
| Wrist delay at both reversals | 0.050 | 0.067 |
| Fingertip delay at both reversals | 0.083 | 0.117 |
| Elbow peak and phase | 34.47° at 0.683 | 44.57° at 0.683 |
| Wrist peak and phase | 47.40° at 0.667 | 64.46° at 0.683 |
| Elbow and wrist fold at phase 0.95 | 0.72° and 0.96° | 0.86° and 1.22° |
| Forward shoulder sweep | 15.00° | 25.00° |
| Leading-edge pitch at mid-downstroke | −12.93° | −12.93° |
| Largest turn in one key interval | 6.56° | 8.62° |

## Blender axis measurements

The source hero faces negative Z. Its head tip lies at Z −4.30 m. Positive X points right and positive Y points up.
Blender rotated each joint by +5° about each source axis. The probe measured the first child’s tail, or the joint’s own tail for a leaf.
Each entry gives the displacement in millimetres along X, Y and Z.
Positive Z rotation raises a right wing. Positive Y rotation sweeps it toward the nose.
Positive X rotation lowers an aft-pointing rib.

| Joint | Probe point | +X | +Y | +Z |
|---|---|---:|---:|---:|
| `wing_root_l` | `wing_arm_l` | 0 / -34 / 173 | 35 / 0 / 198 | -166 / -206 / 0 |
| `wing_arm_l` | `wing_wrist_l` | 0 / 39 / 141 | -41 / 0 / 93 | -135 / -97 / 0 |
| `wing_wrist_l` | `wing_finger_1a_l` | 0 / -16 / -4 | 27 / 0 / 250 | 14 / -251 / 0 |
| `wing_finger_1a_l` | `wing_finger_1b_l` | 0 / -53 / -30 | 70 / 0 / 372 | 44 / -373 / 0 |
| `wing_finger_1b_l` | `wing_finger_1c_l` | 0 / -113 / -91 | 128 / 0 / 260 | 98 / -261 / 0 |
| `wing_finger_1c_l` | `wing_finger_1c_l` | 0 / -73 / -69 | 81 / 0 / 119 | 71 / -119 / 0 |
| `wing_finger_2a_l` | `wing_finger_2b_l` | 0 / -162 / -179 | 187 / 0 / 380 | 189 / -379 / 0 |
| `wing_finger_2b_l` | `wing_finger_2b_l` | 0 / -84 / -127 | 98 / 0 / 192 | 131 / -191 / 0 |
| `wing_finger_3a_l` | `wing_finger_3b_l` | 0 / -189 / -241 | 207 / 0 / 170 | 241 / -169 / 0 |
| `wing_finger_3b_l` | `wing_finger_3b_l` | 0 / -89 / -160 | 100 / 0 / 80 | 160 / -78 / 0 |
| `wing_spur_a_l` | `wing_spur_b_l` | 0 / -107 / -125 | 113 / 0 / 17 | 121 / -17 / 0 |
| `wing_spur_b_l` | `wing_spur_b_l` | 0 / -67 / -115 | 73 / 0 / 6 | 112 / -5 / 0 |
| `wing_root_r` | `wing_arm_r` | 0 / -34 / 173 | 17 / 0 / -200 | -183 / 191 / 0 |
| `wing_arm_r` | `wing_wrist_r` | 0 / 39 / 141 | -49 / 0 / -89 | -143 / 85 / 0 |
| `wing_wrist_r` | `wing_finger_1a_r` | 0 / -16 / -4 | 5 / 0 / -252 | -7 / 251 / 0 |
| `wing_finger_1a_r` | `wing_finger_1b_r` | 0 / -53 / -30 | 38 / 0 / -376 | 12 / 375 / 0 |
| `wing_finger_1b_r` | `wing_finger_1c_r` | 0 / -113 / -91 | 105 / 0 / -270 | 75 / 269 / 0 |
| `wing_finger_1c_r` | `wing_finger_1c_r` | 0 / -73 / -69 | 70 / 0 / -125 | 60 / 125 / 0 |
| `wing_finger_2a_r` | `wing_finger_2b_r` | 0 / -162 / -179 | 153 / 0 / -394 | 155 / 394 / 0 |
| `wing_finger_2b_r` | `wing_finger_2b_r` | 0 / -84 / -127 | 81 / 0 / -200 | 114 / 201 / 0 |
| `wing_finger_3a_r` | `wing_finger_3b_r` | 0 / -189 / -241 | 191 / 0 / -187 | 225 / 189 / 0 |
| `wing_finger_3b_r` | `wing_finger_3b_r` | 0 / -89 / -160 | 92 / 0 / -89 | 152 / 91 / 0 |
| `wing_spur_a_r` | `wing_spur_b_r` | 0 / -107 / -125 | 111 / 0 / -27 | 119 / 27 / 0 |
| `wing_spur_b_r` | `wing_spur_b_r` | 0 / -67 / -115 | 72 / 0 / -13 | 111 / 14 / 0 |

The axis file also gives the rotation axes in each parent frame and Blender bone frame. [Full probe](../../artifacts/hero-flight/wing-axes.json)

## Measurements in the engine pose

The probe ran the studio rig at 60 updates per second with the hero’s flight and rig profiles.
It measured frames 204 through 255. Both procedural effort settings produced the same measurements.
The table uses the right leading fingertip and measures height from its shoulder.
Span measures the horizontal distance between the leading fingertips. Wrist travel measures its full forward and backward range relative to the shoulder.
Values between frames use linear interpolation.

| Measurement in metres | Procedural | Cruise | Climb |
|---|---:|---:|---:|
| Tip height at phase 0.00 | +4.00 | +5.96 | +6.32 |
| Tip height at phase 0.25 | +0.64 | +2.34 | +2.32 |
| Tip height at phase 0.50 | −4.69 | −4.26 | −4.55 |
| Tip height at phase 0.75 | −0.73 | −1.27 | −1.30 |
| Lowest tip height | −6.15 | −5.42 | −6.34 |
| Span at phase 40/60, peak cruise wrist fold | 11.97 | 11.88 | 10.05 |
| Span at phase 41/60, peak climb wrist fold | 12.41 | 11.57 | 9.55 |
| Widest span | 19.43 | 20.09 | 19.34 |
| Wrist travel | 2.24 | 0.95 | 1.42 |

At peak wrist fold, cruise retained 59% of its widest span. Climb retained 49%.
The finished arm ranged from +55.97° to −38.92° in cruise and +65.95° to −58.40° in climb.
The rig adds body motion and load to the keys.
At 60 Hz, all three cruise fingertips peaked at phase 0.078 and bottomed at 0.588.
The climb fingertips peaked at 0.118 and bottomed at 0.608. [Procedural samples](../../artifacts/hero-flight/procedural-landmarks.csv), [cruise samples](../../artifacts/hero-flight/cruise-landmarks.csv), [climb samples](../../artifacts/hero-flight/climb-landmarks.csv)

## Engine review

The work took about 25 minutes. Each clip took two engine passes.
The review covered 63 matched camera frames and six extra views. The first cruise downstroke left the outer wing nearly level as the shoulder turned upward.
Lowering the three fingertip keys brought the leading tip from 4.25 m to 5.42 m below the shoulder.
The first climb folded tighter but its tip reached only 5.44 m below the shoulder.
Lowering its three fingertips brought the leading tip to 6.34 m. Frame 232 shows the steeper outer wing from behind.

Frame 213 shows the shoulders driving down while the hands are still high.
By frame 238, the arms have started lifting and the fingertips still hang below the body.
The shoulders and outer wings now reverse in a clear sequence. The climb shows the strongest fold.
The procedural beat sweeps farther fore and aft. Its cruise downstroke also carries the fingertips lower.
The authored cruise shoulder sweeps back about 22° from its forward extreme. The brief asked for about 12°.
I kept the extra sweep because the finished wrist already covered less than half the procedural distance.
A bright crease remains near the wrist in the side view. The outer ribs also make a sharp corner at some angles.
The close side camera clips the upper tips. The rear and wider side views show their separation.

The sampled frames showed continuous membrane at the root and separation above the back and below the belly.
A triangle check found zero intersections between the outer membrane and the torso, legs or tail in 52 studio poses per clip.
It also found zero intersections between the three outer finger cores.
The test excluded shared attachment surfaces and the skin between adjacent fingers.

The chase view showed a broad V at frame 313. By frame 339 the arms rose while the outer wings still pointed down.
During takeoff, frames 64 and 88 showed the opposite bends of the loaded and recovering wings.
The glide kept its wing outline. The 187 changed image pixels fell in the landscape outside the dragon.
The banked turn also kept its extended wings.
Both head closeups showed the closed jaw and the same dark shoulder joint behind the neck.
The model switch from Rimefang showed Embercrest using the wingbeat at frame 238.

The trial used the existing engine. Its load-dependent finger curl still acts underneath the clip.
The extra curl changes with flight conditions. Fixed keys can compensate for one load at a time.

[Native-size cruise comparison](../../artifacts/hero-flight/cruise-before-after.jpg), [native-size climb comparison](../../artifacts/hero-flight/climb-before-after.jpg), [all camera views](../../artifacts/hero-flight/views.jpg)

## Checks and reproduction

The checker printed 48 passes across all nine groups. It measured zero seam and mirror error in both clips.
Twelve damaged copies each produced exit status 1 and failed the expected group.
The damage included a body key, a shifted finger reversal, an early wrist fold and a single-frame jump.
[Checker output](../../artifacts/hero-flight/checks.txt), [damaged-file output](../../artifacts/hero-flight/negative.txt)

<details>
<summary>Full checker output</summary>

```text
PASS 1 77 joint nodes, hierarchy, scene roots and two named animations
PASS 2 model rest translation, rotation and scale
PASS 1 flap 61 unit rotations from 0 to 1 second
PASS 2 flap paired wing rotation channels only
PASS 3 flap seam 0.00000 deg
PASS 4 flap mirror 0.00000 deg
PASS 5 flap arm high 54.79 at 0/60, low -40.09 at 30/60
PASS 6 flap wrist height reversal lags 3/60 and 3/60
PASS 6 flap tip 1 height reversal lags 5/60 and 5/60
PASS 6 flap tip 2 height reversal lags 5/60 and 5/60
PASS 6 flap tip 3 height reversal lags 5/60 and 5/60
PASS 7 flap elbow fold 34.47 deg at 41/60, 0.72 at 57/60
PASS 7 flap wrist fold 47.40 deg at 40/60, 0.96 at 57/60
PASS 8 flap largest step 6.56 deg on wing_arm_r
PASS 9 flap arm high 54.79 deg
PASS 9 flap arm depth 40.09 deg
PASS 9 flap elbow fold 34.47 deg
PASS 9 flap wrist fold 47.40 deg
PASS 9 flap forward sweep 15.00 deg
PASS 9 flap pitch down 12.93 deg
PASS 9 flap finger closure 15.63 deg
PASS 9 flap loaded outer wing 9.63 deg above arm at 15/60
PASS 9 flap span at peak wrist fold 0.564 of maximum
PASS 9 flap recovery pitch 11.86, hand-to-tip elevation -61.91 deg
PASS 9 flap wrist moves 0.797 m toward nose during downstroke
PASS 1 flap_climb 61 unit rotations from 0 to 1 second
PASS 2 flap_climb paired wing rotation channels only
PASS 3 flap_climb seam 0.00000 deg
PASS 4 flap_climb mirror 0.00000 deg
PASS 5 flap_climb arm high 64.79 at 0/60, low -60.09 at 30/60
PASS 6 flap_climb wrist height reversal lags 4/60 and 4/60
PASS 6 flap_climb tip 1 height reversal lags 7/60 and 7/60
PASS 6 flap_climb tip 2 height reversal lags 7/60 and 7/60
PASS 6 flap_climb tip 3 height reversal lags 7/60 and 7/60
PASS 7 flap_climb elbow fold 44.57 deg at 41/60, 0.86 at 57/60
PASS 7 flap_climb wrist fold 64.46 deg at 41/60, 1.22 at 57/60
PASS 8 flap_climb largest step 8.62 deg on wing_finger_3a_r
PASS 9 flap_climb arm high 64.79 deg
PASS 9 flap_climb arm depth 60.09 deg
PASS 9 flap_climb elbow fold 44.57 deg
PASS 9 flap_climb wrist fold 64.46 deg
PASS 9 flap_climb forward sweep 25.00 deg
PASS 9 flap_climb pitch down 12.93 deg
PASS 9 flap_climb finger closure 16.08 deg
PASS 9 flap_climb loaded outer wing 18.85 deg above arm at 15/60
PASS 9 flap_climb recovery pitch 11.99, hand-to-tip elevation -73.97 deg
PASS 9 flap_climb wrist moves 1.223 m toward nose during downstroke
PASS 9 climb exceeds cruise at both reversals and both hinges
```

</details>

Run these commands from the worktree root. The render and sheet scripts use Python with Pillow and NumPy.
The measurement script compiles a standalone probe against the retained libraries.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/clips/make_hero_flight.py -- --probe-only
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/clips/make_hero_flight.py
python3 -B tools/clips/check_hero_flight.py
python3 -B tools/clips/hero_flight_negative.py
python3 -B tools/clips/hero_flight_measure.py
/opt/homebrew/bin/python3.14 -B tools/clips/hero_flight_render.py before
/opt/homebrew/bin/python3.14 -B tools/clips/hero_flight_render.py after
/opt/homebrew/bin/python3.14 -B tools/clips/hero_flight_render.py details
/opt/homebrew/bin/python3.14 -B tools/clips/hero_flight_sheets.py
```
