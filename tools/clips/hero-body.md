# Hero body loops

The five body actions target the 77 joints of the Embercrest hero. The separate flight file supplies the powered wingbeat.
The engine adds each body rotation after the procedural pose. A powered tail track replaces the procedural beat wave.
Gliding keeps the procedural tail spring and its existing lateral wave.

[Cruise and climb comparison](../../artifacts/hero-body/beat-before-after.jpg) · [Glide comparison](../../artifacts/hero-body/glide-before-after.jpg) · [Walk comparison](../../artifacts/hero-body/walk-before-after.jpg) · [Stand comparison](../../artifacts/hero-body/stand-before-after.jpg)

[All camera views](../../artifacts/hero-body/views.jpg) · [Joint paths](../../artifacts/hero-body/traces.jpg)

## Authoring and playback

The source model faces −Z, with +Y up and +X to its right. The skeleton file and posed Blender landmarks agree on that frame.
The generator copies the original joint names, hierarchy and rest transforms.
It keys the imported Blender armature with quaternions, which represent 3D rotations.
Cubic curves ease between authored poses. The export stores dense rotation samples with glTF linear interpolation between adjacent samples.
Each track shares its first and last sample. Matching the two adjacent rotation steps preserves the sampled velocity across the seam.

| Action | Playback clock | Duration in file | Samples per joint |
|---|---|---:|---:|
| `flap_body` | Cruise wingbeat phase | 1 s | 61 |
| `flap_climb_body` | Climb wingbeat phase | 1 s | 61 |
| `glide` | Elapsed time | 8 s | 241 |
| `walk` | Diagonal stride phase | 1 s | 61 |
| `stand` | Elapsed time | 10 s | 301 |

## Motion and revisions

Cruise. The first four tail joints together sweep 44.09° peak to peak. The outer four sweep 72.36°.
The dip travels from the first joint at phase 0.633 to the eighth at 0.917.
The faster down-flick gives the tip a different shape on its return.
The hips kick back 10°, the knees open 6.8°, and the hind toes curl 12° during recovery.
The elbows and wrists follow 0.15 cycle later at 5° and 7°. The fore toes curl 8°.
The chest arches 2.4°. The three neck joints reach 4.3°, 4.5° and 4.6° forward and down.
The head counters their combined rotation. The neck and head keys together change its direction by less than 0.001°.
The first pass carried the head 8.56 units through the beat, 2.50 times the baseline.
Bringing the chest arch in earlier and retaining some neck reach in recovery cut travel to 4.86 units.

Climb. The first four tail joints sweep 66.14° together and the outer four sweep 108.54°.
The hips kick back 15°, and the legs peak 0.033 cycle later than in cruise.
The neck reaches 6.45°, 6.75° and 6.90°. Holding part of the reach through recovery cut head travel from 5.59 to 4.80 units.
In frame 226, the tip rises nearly to horn height. The hind toes hang below the tail.

Glide. The chest takes two 1.5° breaths. The neck leads each glance from its base.
The head looks 7.96° to one side at 2.03 seconds. At 6.03 seconds it looks 5.98° the other way and dips 2.34°.
The slow tail sway reaches 15.18° across the joints. Its tip follows the base by 0.246 cycle and twitches once.
The hips drift 3.8° each, 0.317 cycle apart. Only the right hind toes flex, through 10°.
The final version shifts each foreleg once. The right elbow shifts 2.6° around 1.77 seconds, and the left shifts 3.2° around 5.60 seconds.
This raises the right wrist's path from 5.60 units in the first pass to 6.82 units.

Walk. The neck dips 4.20° at each diagonal landing. The two peaks fall at phases 0.017 and 0.500.
The head counters the dip. The neck sways 3.79° toward each planted forefoot.
The tail sways 20.12° against the hips. The tip trails the base by 0.150 cycle.
The folded wrists bounce 2.49° twice per stride. The top view shows the tail's free end crossing behind the folded wings.

Stand. Three breaths lift the neck base 1.45° and the folded shoulders 1.80°.
The head turns 12°, holds from three to four seconds, returns, then tilts 5° at seven seconds.
The tail sways 12.28°, with one 8.40° curl at its tip.
The front view separates the turned muzzle from the chest. The top view shows the tail behind the hips.

## Probe measurements

Each cell gives the procedural distance followed by the distance with the clip. Each path sums three-dimensional distances between successive samples at 60 Hz.
Length is the mean distance from the head joint to the last tail joint. The supplied probe scales both length and travel by 13.6773.
The tail landmarks are joint origins. The probe labels the tracked right hind ankle `leg2` and the right fore wrist `arm2`.
These are the probe's foot and hand landmarks. It does not track the toe tips.

| Loop | Head to tail | Tail tip | Tail middle | Head | Hind ankle | Fore wrist |
|---|---:|---:|---:|---:|---:|---:|
| beat | 164.51 → 164.09 | 112.15 → 168.06 (1.50×) | 45.98 → 62.29 (1.35×) | 3.42 → 4.86 (1.42×) | 2.06 → 32.26 (15.67×) | 3.01 → 4.81 (1.60×) |
| climb | 164.51 → 161.28 | 112.15 → 247.84 (2.21×) | 45.98 → 90.75 (1.97×) | 3.42 → 4.80 (1.41×) | 2.06 → 47.13 (22.90×) | 3.01 → 7.16 (2.38×) |
| glide | 165.91 → 165.73 | 182.87 → 182.93 (1.00×) | 47.45 → 50.35 (1.06×) | 7.82 → 7.36 (0.94×) | 8.61 → 11.32 (1.31×) | 5.10 → 6.82 (1.34×) |
| walk | 169.74 → 169.53 | 0.65 → 75.08 (115.26×) | 0.24 → 27.10 (115.05×) | 0.00 → 7.65 | 0.001 → 0.001 | 0.001 → 0.001 |
| stand | 169.77 → 169.28 | 0.00 → 47.99 | 0.00 → 15.35 | 0.00 → 7.03 | 0.000 → 0.000 | 0.000 → 0.000 |

The retained engine reports an alignment scale of 1.0004. The rig converts body heave from metres using that scale.
The first kick passed the supplied probe at 13.6773, but body heave largely canceled it at 1.0004.
The final kick deepens the forward gather to 17° in cruise. The hip also swings laterally about a tilted axis.
The outward swing follows the aft swing by 0.24 beat. The lateral rotation ranges from −7.7° to +20.3° in cruise.
At frame 238, ankle separation grows from 1.99 m procedurally to 3.03 m in cruise and 3.62 m in climb.
The climb multiplies the hip swing by 1.5.

| At game scale | Tail tip, m | Head, m | Hind ankle, m |
|---|---:|---:|---:|
| Procedural beat | 7.203 | 0.935 | 0.677 |
| Final cruise | 11.802 (1.64×) | 0.990 (1.06×) | 2.145 (3.17×) |
| Final climb | 17.609 (2.44×) | 0.976 (1.04×) | 3.224 (4.76×) |

Use `--scale 1.0004` with the measurement script's `before`, `after` and `report` commands to reproduce the second table.
Both beats use frames 204 through 255, including the closing sample. The glide uses frames 480 through 960.
The walk uses frames 200 through 248. Successive tail maxima in the longer walk trace occurred at frames 204, 252, 300, 348 and 396.
The stand uses frames 600 through 1200.
For the climb, the probe reads an isolated copy of the flight tuning with `flap_effort_bias 1` beside a scratch model symlink.
The probe reads the wingbeat from the same flight clip in both runs.

During the glide, the existing tail wave already travels farther than the head-to-tail length.
The new wave moves the tip up to 13.58 units from the matching baseline frame, despite the nearly equal total path lengths.
The head travels 4.44% of body length in the glide, 4.51% in the walk and 4.15% in the stand.
The ground clips leave both leg chains untouched. Their recorded endpoints match the baseline at every sample.
The probe omits the engine's terrain query. The engine foot views show the gait on the ground.

## Export checks

The checker printed 168 passes. All five loops closed within 0.05°. Their incoming and outgoing sampled velocities matched within 20%.
Both powered loops mirrored within 0.00001°. The largest step was 3.50° at the eighth tail joint during the climb.
Fifteen damaged exports each failed the intended check with exit status 1.
Reversing the incoming rotation step kept the seam pose identical and produced a 200% velocity error.
Moving the walk dip displaced its peaks to phases 0.300 and 0.817. Reversing its sway aimed the neck over the airborne foreleg.

[Complete checker output](../../artifacts/hero-body/checks.txt) · [Complete corruption proof](../../artifacts/hero-body/negative.txt)

## Engine review

Each comparison keeps native pixels and a fixed crop for each camera. Each procedural row sits above its keyed row.
The beat sheet includes the cruise side, cruise underside and climb side. The overview includes every delivered after frame.
The full file loaded in the retained engine with this line.

```text
body clips: 5 in ~/src/game-claude-wt/r11-body/assets/clips/embercrest-hero.body.glb (flap_body, flap_climb_body, glide, walk, stand)
```

The first underside series looked down on the dragon. The engine option parser treats a leading minus as another flag.
Passing the elevation as `' -30'` selected the requested view beneath the belly. Both versions use the corrected camera.

Cruise frames 207 and 213 gather the feet close to the tail. Frames 226 through 238 show the feet spreading again.
The rear view projects the rising tail over a leg. The matching side and underside views separate those silhouettes.
The moving wing briefly hides the forepaw in both versions. Its toes are easiest to see late in recovery.

In the climb, frames 238 and 245 leave a visible gap between the toes and the trailing tail.
The higher flick and wider hind-foot swing give the climb a stronger kick. The steeper tail curl also looks more emphatic than the procedural one.

In the glide, the overhead view shows the snout turning separately from the tail. The rear view shows the tail reaching both sides.
The procedural tail already sways strongly. The slower added sway chiefly changes where the tip goes.
The small forepaw shifts remain hard to distinguish from the side.

The walking tail clears both folded membranes at its widest swing. From the front, each forefoot lifts on its own half of the stride.
The standing turn reads most clearly from the front. Around frame 300, the side view catches the tail tip straightening upward.
The folded wrist looks blocky in close head views across the standing cycle. The standing clip preserves that underlying shape.
The field grass and one shrub hide some toe contacts. Separate ground-contact frames remove the vegetation.
At walk frames 212 and 236, the raised forefoot clears the terrain and the planted forefoot loses some claw tips below it.
At stand frame 225, the terrain cuts a flat edge across both near feet in both versions.
The lowest visible pixel in each selected foot region matched its baseline in all three views.
The clips add no visible slide. Correcting the existing toe burial requires a change to procedural foot placement.

From the chase camera, the tail hangs well below the legs at frame 300. At 313 it curls across the projected foot outline.
That overlap briefly makes the foot harder to distinguish. The procedural chase frame also shows an overlap there.
At frame 339, the feet separate either side of the falling tail. The camera cuts off its tip in both versions.
All four landing runs reported grounded at 0.6 m/s. The low ground speed blends the walk into the stand.
The rear camera crops the tail tip in those frames. The studio views show the whole standing tail.
Switching from Rimefang to Embercrest restored the hero and its moving body in the rear camera.

Exact target lock has an engine limit. The rig aims the head before it applies the body loops.
The powered neck and head keys cancel each other's turn. The glide and stand glances still play during aim.
Suppressing those glances during aim requires an engine weight or a final aim correction. The clips have no target information.

## Time and capture count

The engine produced 124 required pairs at 1920 × 1080. Three extra pairs show bare ground, and thirteen extra views show the final clips.
The review covered all 267 source images. The overview contains all 140 final images.

| Loop | Approximate authoring and revision minutes | Final capture minutes | Final and procedural images |
|---|---:|---:|---:|
| Cruise | 7 plus 6 shared | 18.9 | 58 |
| Climb | 3 plus the same 6 | 6.9 | 20 |
| Glide | 4 | 18.1 | 50 |
| Walk | 2.5 | 21.7 | 62 |
| Stand, including landings | 2.3 | 37.1 | 68 |
| Chase and model switch | | 5.8 | 9 |

The six shared minutes cover the final kick revision for both powered loops.
Capture minutes sum the elapsed engine times for the delivered images. Several captures ran concurrently.

## Measured axes

Each cell gives the XYZ displacement in metres after a positive five-degree rotation.
Blender tracked the source tip of the named descendant. Leaf rows use their own source tip.

| Joint | Tracked tip | Pitch (+X) | Yaw (+Y) | Roll (+Z) |
|---|---|---|---|---|
| `root` | `chest` | +0.0000 / +0.2056 / +0.0518 | -0.2074 / +0.0000 / +0.0091 | -0.0427 / -0.0019 / +0.0000 |
| `chest` | `neck_01` | +0.0000 / +0.1508 / +0.0660 | -0.1534 / +0.0000 / +0.0067 | -0.0593 / -0.0026 / +0.0000 |
| `neck_01` | `neck_02` | +0.0000 / +0.0662 / +0.0832 | -0.0697 / +0.0000 / +0.0030 | -0.0802 / -0.0035 / +0.0000 |
| `neck_02` | `neck_03` | +0.0000 / +0.0520 / +0.0896 | -0.0557 / +0.0000 / +0.0033 | -0.0871 / -0.0047 / +0.0000 |
| `neck_03` | `head` | +0.0000 / +0.1122 / +0.0695 | -0.1150 / +0.0000 / +0.0059 | -0.0645 / -0.0037 / +0.0000 |
| `head` | `jaw` | +0.0000 / +0.0959 / +0.0024 | -0.0959 / +0.0000 / +0.0043 | +0.0017 / +0.0000 / +0.0000 |
| `jaw` | `jaw_tip` | +0.0000 / +0.0866 / -0.0063 | -0.0862 / +0.0000 / +0.0038 | +0.0100 / +0.0004 / +0.0000 |
| `jaw_tip` | `jaw_tip` | +0.0000 / +0.0026 / +0.0001 | -0.0026 / +0.0000 / +0.0001 | -0.0000 / +0.0000 / +0.0000 |
| `tail_01` | `tail_02` | +0.0000 / -0.2118 / -0.0499 | +0.2135 / +0.0000 / -0.0093 | +0.0406 / +0.0018 / +0.0000 |
| `tail_02` | `tail_03` | +0.0000 / -0.2065 / -0.0701 | +0.2092 / +0.0000 / -0.0091 | +0.0610 / +0.0027 / +0.0000 |
| `tail_03` | `tail_04` | +0.0000 / -0.2090 / -0.0141 | +0.2092 / +0.0000 / -0.0091 | +0.0050 / +0.0002 / +0.0000 |
| `tail_04` | `tail_05` | -0.0000 / -0.2099 / +0.0068 | +0.2092 / +0.0000 / -0.0091 | -0.0159 / -0.0007 / +0.0000 |
| `tail_05` | `tail_06` | +0.0000 / -0.2101 / +0.0131 | +0.2092 / +0.0000 / -0.0091 | -0.0222 / -0.0010 / +0.0000 |
| `tail_06` | `tail_07` | +0.0000 / -0.2003 / -0.0133 | +0.2005 / +0.0000 / -0.0088 | +0.0045 / +0.0002 / +0.0000 |
| `tail_07` | `tail_08` | +0.0000 / -0.1723 / -0.0933 | +0.1761 / +0.0000 / -0.0077 | +0.0856 / +0.0037 / +0.0000 |
| `tail_08` | `tail_08` | +0.0000 / -0.0772 / -0.0708 | +0.0802 / +0.0000 / -0.0035 | +0.0673 / +0.0029 / +0.0000 |
| `upper_arm_l` | `forearm_l` | +0.0000 / -0.0057 / -0.1705 | +0.0127 / +0.0000 / -0.0102 | +0.1695 / +0.0170 / +0.0000 |
| `forearm_l` | `wrist_l` | +0.0000 / +0.0688 / -0.1358 | -0.0639 / +0.0000 / -0.0225 | +0.1375 / +0.0313 / +0.0000 |
| `wrist_l` | `hand_l` | +0.0000 / +0.0723 / -0.0562 | -0.0698 / +0.0000 / +0.0022 | +0.0592 / +0.0035 / +0.0000 |
| `hand_l` | `toe_fore_1_l` | +0.0000 / +0.0734 / -0.0212 | -0.0733 / +0.0000 / -0.0195 | +0.0234 / +0.0237 / +0.0000 |
| `thigh_l` | `shin_l` | +0.0000 / -0.0634 / -0.1469 | +0.0707 / +0.0000 / +0.0187 | +0.1448 / -0.0155 / +0.0000 |
| `shin_l` | `ankle_l` | +0.0000 / -0.0686 / -0.1287 | +0.0745 / +0.0000 / +0.0072 | +0.1260 / -0.0050 / +0.0000 |
| `ankle_l` | `foot_l` | +0.0000 / +0.0827 / -0.0951 | -0.0776 / +0.0000 / +0.0235 | +0.0994 / -0.0157 / +0.0000 |
| `foot_l` | `toe_hind_1_l` | +0.0000 / +0.0725 / -0.0213 | -0.0722 / +0.0000 / -0.0126 | +0.0237 / +0.0168 / +0.0000 |
| `toe_fore_1_l` | `toe_fore_1_l` | +0.0000 / +0.0441 / -0.0103 | -0.0438 / +0.0000 / -0.0033 | +0.0120 / +0.0058 / +0.0000 |
| `toe_fore_2_l` | `toe_fore_2_l` | +0.0000 / +0.0441 / -0.0103 | -0.0433 / +0.0000 / +0.0071 | +0.0124 / -0.0047 / +0.0000 |
| `toe_fore_3_l` | `toe_fore_3_l` | +0.0000 / +0.0328 / -0.0108 | -0.0316 / +0.0000 / +0.0154 | +0.0128 / -0.0134 / +0.0000 |
| `toe_hind_1_l` | `toe_hind_1_l` | +0.0000 / +0.0415 / -0.0104 | -0.0408 / +0.0000 / +0.0053 | +0.0124 / -0.0030 / +0.0000 |
| `toe_hind_2_l` | `toe_hind_2_l` | +0.0000 / +0.0467 / -0.0102 | -0.0457 / +0.0000 / +0.0125 | +0.0127 / -0.0099 / +0.0000 |
| `toe_hind_3_l` | `toe_hind_3_l` | +0.0000 / +0.0337 / -0.0108 | -0.0322 / +0.0000 / +0.0215 | +0.0131 / -0.0195 / +0.0000 |
| `wing_root_l` | `wing_arm_l` | +0.0000 / -0.0338 / +0.1732 | +0.0348 / +0.0000 / +0.1976 | -0.1656 / -0.2063 / +0.0000 |
| `wing_arm_l` | `wing_wrist_l` | +0.0000 / +0.0392 / +0.1414 | -0.0414 / +0.0000 / +0.0926 | -0.1355 / -0.0967 / +0.0000 |
| `wing_wrist_l` | `wing_finger_1a_l` | +0.0000 / -0.0155 / -0.0042 | +0.0266 / +0.0000 / +0.2503 | +0.0144 / -0.2509 / +0.0000 |
| `wing_finger_1a_l` | `wing_finger_1b_l` | +0.0000 / -0.0528 / -0.0302 | +0.0704 / +0.0000 / +0.3715 | +0.0442 / -0.3727 / +0.0000 |
| `wing_finger_1b_l` | `wing_finger_1c_l` | +0.0000 / -0.1130 / -0.0914 | +0.1284 / +0.0000 / +0.2599 | +0.0979 / -0.2612 / +0.0000 |
| `wing_finger_1c_l` | `wing_finger_1c_l` | +0.0000 / -0.0730 / -0.0687 | +0.0812 / +0.0000 / +0.1187 | +0.0707 / -0.1192 / +0.0000 |
| `wing_finger_2a_l` | `wing_finger_2b_l` | +0.0000 / -0.1625 / -0.1791 | +0.1868 / +0.0000 / +0.3796 | +0.1886 / -0.3795 / +0.0000 |
| `wing_finger_2b_l` | `wing_finger_2b_l` | +0.0000 / -0.0844 / -0.1268 | +0.0983 / +0.0000 / +0.1922 | +0.1315 / -0.1907 / +0.0000 |
| `wing_finger_3a_l` | `wing_finger_3b_l` | +0.0000 / -0.1886 / -0.2414 | +0.2065 / +0.0000 / +0.1700 | +0.2405 / -0.1685 / +0.0000 |
| `wing_finger_3b_l` | `wing_finger_3b_l` | +0.0000 / -0.0891 / -0.1602 | +0.0996 / +0.0000 / +0.0804 | +0.1597 / -0.0777 / +0.0000 |
| `wing_spur_a_l` | `wing_spur_b_l` | +0.0000 / -0.1072 / -0.1252 | +0.1134 / +0.0000 / +0.0169 | +0.1212 / -0.0165 / +0.0000 |
| `wing_spur_b_l` | `wing_spur_b_l` | +0.0000 / -0.0675 / -0.1147 | +0.0728 / +0.0000 / +0.0064 | +0.1120 / -0.0047 / +0.0000 |
| `upper_arm_r` | `forearm_r` | +0.0000 / -0.0057 / -0.1705 | +0.0135 / +0.0000 / +0.0090 | +0.1704 / -0.0022 / +0.0000 |
| `forearm_r` | `wrist_r` | +0.0000 / +0.0688 / -0.1358 | -0.0616 / +0.0000 / +0.0280 | +0.1397 / -0.0192 / +0.0000 |
| `wrist_r` | `hand_r` | +0.0000 / +0.0723 / -0.0562 | -0.0697 / +0.0000 / +0.0039 | +0.0593 / +0.0017 / +0.0000 |
| `hand_r` | `toe_fore_1_r` | +0.0000 / +0.0734 / -0.0212 | -0.0713 / +0.0000 / +0.0258 | +0.0254 / -0.0216 / +0.0000 |
| `thigh_r` | `shin_r` | +0.0000 / -0.0634 / -0.1469 | +0.0688 / +0.0000 / -0.0248 | +0.1429 / +0.0281 / +0.0000 |
| `shin_r` | `ankle_r` | +0.0000 / -0.0686 / -0.1287 | +0.0736 / +0.0000 / -0.0137 | +0.1250 / +0.0159 / +0.0000 |
| `ankle_r` | `foot_r` | +0.0000 / +0.0827 / -0.0951 | -0.0793 / +0.0000 / -0.0166 | +0.0976 / +0.0243 / +0.0000 |
| `foot_r` | `toe_hind_1_r` | +0.0000 / +0.0725 / -0.0213 | -0.0708 / +0.0000 / +0.0188 | +0.0251 / -0.0146 / +0.0000 |
| `toe_fore_1_r` | `toe_fore_1_r` | +0.0000 / +0.0441 / -0.0103 | -0.0433 / +0.0000 / +0.0071 | +0.0124 / -0.0047 / +0.0000 |
| `toe_fore_2_r` | `toe_fore_2_r` | +0.0000 / +0.0441 / -0.0103 | -0.0438 / +0.0000 / -0.0033 | +0.0120 / +0.0058 / +0.0000 |
| `toe_fore_3_r` | `toe_fore_3_r` | +0.0000 / +0.0328 / -0.0108 | -0.0329 / +0.0000 / -0.0125 | +0.0116 / +0.0145 / +0.0000 |
| `toe_hind_1_r` | `toe_hind_1_r` | +0.0000 / +0.0415 / -0.0104 | -0.0411 / +0.0000 / -0.0017 | +0.0120 / +0.0040 / +0.0000 |
| `toe_hind_2_r` | `toe_hind_2_r` | +0.0000 / +0.0467 / -0.0102 | -0.0466 / +0.0000 / -0.0084 | +0.0117 / +0.0110 / +0.0000 |
| `toe_hind_3_r` | `toe_hind_3_r` | +0.0000 / +0.0337 / -0.0108 | -0.0340 / +0.0000 / -0.0186 | +0.0113 / +0.0206 / +0.0000 |
| `wing_root_r` | `wing_arm_r` | +0.0000 / -0.0338 / +0.1732 | +0.0175 / +0.0000 / -0.1999 | -0.1830 / +0.1911 / +0.0000 |
| `wing_arm_r` | `wing_wrist_r` | +0.0000 / +0.0392 / +0.1414 | -0.0493 / +0.0000 / -0.0887 | -0.1434 / +0.0846 / +0.0000 |
| `wing_wrist_r` | `wing_finger_1a_r` | +0.0000 / -0.0155 / -0.0042 | +0.0047 / +0.0000 / -0.2517 | -0.0075 / +0.2512 / +0.0000 |
| `wing_finger_1a_r` | `wing_finger_1b_r` | +0.0000 / -0.0528 / -0.0302 | +0.0377 / +0.0000 / -0.3763 | +0.0116 / +0.3751 / -0.0000 |
| `wing_finger_1b_r` | `wing_finger_1c_r` | +0.0000 / -0.1130 / -0.0914 | +0.1052 / +0.0000 / -0.2701 | +0.0747 / +0.2687 / -0.0000 |
| `wing_finger_1c_r` | `wing_finger_1c_r` | +0.0000 / -0.0730 / -0.0687 | +0.0705 / +0.0000 / -0.1253 | +0.0600 / +0.1249 / +0.0000 |
| `wing_finger_2a_r` | `wing_finger_2b_r` | +0.0000 / -0.1625 / -0.1791 | +0.1531 / +0.0000 / -0.3944 | +0.1548 / +0.3945 / +0.0000 |
| `wing_finger_2b_r` | `wing_finger_2b_r` | +0.0000 / -0.0844 / -0.1268 | +0.0812 / +0.0000 / -0.2000 | +0.1143 / +0.2015 / +0.0000 |
| `wing_finger_3a_r` | `wing_finger_3b_r` | +0.0000 / -0.1886 / -0.2414 | +0.1909 / +0.0000 / -0.1873 | +0.2249 / +0.1888 / -0.0000 |
| `wing_finger_3b_r` | `wing_finger_3b_r` | +0.0000 / -0.0891 / -0.1602 | +0.0922 / +0.0000 / -0.0887 | +0.1523 / +0.0914 / +0.0000 |
| `wing_spur_a_r` | `wing_spur_b_r` | +0.0000 / -0.1072 / -0.1252 | +0.1115 / +0.0000 / -0.0267 | +0.1193 / +0.0270 / +0.0000 |
| `wing_spur_b_r` | `wing_spur_b_r` | +0.0000 / -0.0675 / -0.1147 | +0.0719 / +0.0000 / -0.0127 | +0.1111 / +0.0145 / +0.0000 |
| `eye_l` | `eye_l` | +0.0000 / +0.0066 / +0.0037 | -0.0063 / +0.0000 / +0.0099 | -0.0030 / -0.0097 / +0.0000 |
| `eyelid_upper_l` | `eyelid_upper_l` | +0.0000 / -0.0073 / +0.0012 | +0.0075 / +0.0000 / +0.0043 | -0.0013 / -0.0046 / +0.0000 |
| `eyelid_lower_l` | `eyelid_lower_l` | +0.0000 / -0.0073 / +0.0012 | +0.0075 / +0.0000 / +0.0043 | -0.0013 / -0.0046 / +0.0000 |
| `eye_r` | `eye_r` | +0.0000 / +0.0066 / +0.0037 | -0.0072 / +0.0000 / -0.0093 | -0.0039 / +0.0094 / +0.0000 |
| `eyelid_upper_r` | `eyelid_upper_r` | +0.0000 / +0.0073 / -0.0012 | -0.0071 / +0.0000 / +0.0049 | +0.0017 / -0.0045 / +0.0000 |
| `eyelid_lower_r` | `eyelid_lower_r` | +0.0000 / +0.0073 / -0.0012 | -0.0071 / +0.0000 / +0.0049 | +0.0017 / -0.0045 / +0.0000 |
| `tongue_01` | `tongue_02` | +0.0000 / +0.0439 / -0.0033 | -0.0436 / +0.0000 / +0.0019 | +0.0052 / +0.0002 / +0.0000 |
| `tongue_02` | `tongue_03` | +0.0000 / +0.0438 / -0.0023 | -0.0436 / +0.0000 / +0.0019 | +0.0042 / +0.0002 / +0.0000 |
| `tongue_03` | `tongue_03` | +0.0000 / +0.0219 / -0.0006 | -0.0218 / +0.0000 / +0.0010 | +0.0016 / +0.0001 / +0.0000 |

## Reproduce

Run these commands from the worktree root. The capture and chart scripts use Python with Pillow and NumPy.

```sh
/opt/homebrew/bin/blender -b --factory-startup --python-exit-code 1 --python tools/clips/make_hero_body.py -- --measure-axes
python3 -B tools/clips/check_hero_body.py
python3 -B tools/clips/hero_body_negative.py
/opt/homebrew/bin/python3.14 -B tools/clips/hero_body_measure.py before
/opt/homebrew/bin/python3.14 -B tools/clips/hero_body_measure.py after
/opt/homebrew/bin/python3.14 -B tools/clips/hero_body_measure.py charts
/opt/homebrew/bin/python3.14 -B tools/clips/hero_body_render.py before
/opt/homebrew/bin/python3.14 -B tools/clips/hero_body_render.py after
/opt/homebrew/bin/python3.14 -B tools/clips/hero_body_sheets.py
```

The renderer skips existing JPEGs. Pass `--replace` to capture an existing view after changing its action.
