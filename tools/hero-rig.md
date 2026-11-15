# Embercrest hero rig attempt

## Status

The rig failed acceptance after three full passes on 2026-11-14.
Pass 3 reported 47 passing gates and seven failing gates.
The process exited with status 1 after writing all seven renders.
No pass qualified for a commit. No files were staged or committed.

Do not use this GLB in the game. Eight hide vertices contain invalid weights.
The failure also affects mirrored weights, wing planarity, and right-wing intersections.
The source symlink and its target remained unchanged.

## Files and commands

`tools/rig_hero.py` builds the skin, checks the exported GLB, and renders the poses.
`tools/skeletons/embercrest-hero.json` contains 77 bone records, measured anchors, eye frames, and measurement notes.
`assets/embercrest-hero.glb` contains the failed output, preserved for inspection.
The output has 110,350 vertices and 121,650 triangles across six meshes.
Its size is 24,418,268 bytes.
The two hero sidecars are unchanged copies of the original Embercrest sidecars.

The following commands run from this worktree:

```sh
/opt/homebrew/bin/blender -b --factory-startup --python tools/rig_hero.py
/opt/homebrew/bin/blender -b --factory-startup --python tools/rig_hero.py -- --verify-only
/opt/homebrew/bin/blender -b --factory-startup --python tools/rig_hero.py -- --render
```

The default command builds, checks, and renders.
`--verify-only` checks the existing exported asset without building or rendering.
`--render` checks and renders the existing exported asset without rebuilding.
Both inspection modes use the same verification function as the default command.
The three full passes exercised the default command.
The standalone inspection flags were not run separately within this pass budget.

Failed gates return status 1 after the requested work finishes.
Unexpected exceptions return status 2 with a traceback.
The script uses an explicit process exit because Blender otherwise returns zero for Python exceptions.

The cache directory holds temporary files, Python caches, probe scripts, and intermediate renders.
`.gitignore` changed only to ignore `build-hero-rig-cache/`.
The GLB remains ignored. It was not force-added.
No game, CMake build, interactive Blender session, MCP tool, or additional agent ran.

## Source preservation

The builder reads the source GLB directly.
An accessor describes one typed array in a GLB buffer.
The builder appends weight accessors, joint accessors, joint nodes, and inverse bind matrices.
An inverse bind matrix maps model coordinates into a resting joint.

The original 21,741,832 buffer bytes remain identical, including all embedded image bytes.
All positions, normals, tangents, texture coordinates, jaw masks, and triangle indices remain identical.
Material, texture, and image records remain identical.
The builder does not decimate, weld, remesh, or re-export the source through Blender.

The verifier imports the output once to count Blender armatures and obtain its materials.
It then reconstructs evaluation meshes from the output accessors without changing their vertex order.
It reads the output's weights, hierarchy, and inverse bind matrices for these meshes.
Blender evaluates linear skinning. Preserve Volume is disabled because the engine uses linear skinning.

Blender texture coordinates reverse the GLB V coordinate during inspection only.
Pass 1 omitted this conversion and rendered incorrect texture patches.
Pass 2 corrected the inspection conversion. The exported texture coordinates never changed.

## Measurement method

All recorded coordinates use metres, +Y up, and forward -Z.
The measurements came from the actual static GLB, not the published landmark table alone.

The preparation probes measured bounds, connected components, local surface sections, and transverse ray intersections.
A connected component is a group of connected vertices.
Position duplicates were merged only in temporary measurement arrays.
This merge did not change the exported asset.

The published joint planes supplied initial search locations.
Surface measurements supplied missing heights and lateral centres.
The shoulder and hip surfaces merge into the torso.
Their internal joint centres remain anatomical estimates, not directly observable mesh features.

Three source renders checked the side, top, and head measurements.
They were preparation probes, not build passes.
Later probes examined boundary caps, existing intersections, and invalid exported weights.
Those probes did not rebuild the asset or generate additional pose sheets.

## Measured landmarks

The complete numeric bone table is in `embercrest-hero.json`.
The table below records the main anchors.
Positive X marks the right side. Limb and wing anchors mirror across X=0.
The head has its own measured centre near X=-0.01.

| Landmark | X | Y | Z | Basis |
|---|---:|---:|---:|---|
| Root | 0 | 2.55 | 0.20 | Torso estimate |
| Chest | 0 | 2.80 | -0.90 | Torso estimate |
| Neck 01 | 0 | 3.04 | -2.18 | Neck surface sections |
| Neck 02 | 0 | 3.48 | -2.66 | Neck surface sections |
| Neck 03 | 0 | 3.96 | -2.98 | Neck surface sections |
| Head | -0.01 | 4.48 | -3.30 | Skull base estimate |
| Jaw hinge | -0.0108 | 4.575033 | -3.441072 | Rear jaw-mask support |
| Jaw tip | -0.0108 | 4.46 | -4.40 | Measured front jaw extent |
| Left eyeball | -0.224210 | 4.796591 | -3.644584 | Rear sphere fit |
| Right eyeball | 0.204210 | 4.796591 | -3.644584 | Rear sphere fit |
| Fore shoulder | 0.72 | 2.30 | -1.84 | Torso intersection estimate |
| Fore elbow | 0.76 | 1.92 | -1.72 | Torso intersection estimate |
| Fore wrist | 0.599 | 0.98 | -2.04 | Ray interval midpoint |
| Fore foot | 0.582 | 0.40 | -2.25 | Ray interval midpoint |
| Hind hip | 0.85 | 2.25 | 0.05 | Torso intersection estimate |
| Hind knee | 0.903 | 1.83 | 0 | Ray interval midpoint |
| Hind hock | 0.957 | 1.43 | 0.96 | Ray interval midpoint |
| Hind foot | 1.048 | 0.45 | 0.84 | Ray interval midpoint |
| Wing shoulder | 1.25 | 3.78 | -1.10 | Local hide section |
| Wing elbow | 2.64 | 4.56 | -0.45 | Local hide section |
| Wing wrist | 3.43 | 5.68 | -0.71 | Local hide section |
| Finger fan origin | 3.66 | 5.72 | -0.65 | Fan junction estimate |
| Finger 1 knuckle A | 6.31 | 5.64 | -0.40 | Local hide section |
| Finger 1 knuckle B | 7.95 | 5.40 | 0.07 | Local hide section |
| Finger 1 tip | 9.35 | 4.65 | 0.94 | Claw component |
| Finger 2 knuckle | 5.85 | 5.16 | 0.37 | Local hide section |
| Finger 2 tip | 8.10 | 3.75 | 1.40 | Claw component |
| Finger 3 knuckle | 4.74 | 4.84 | 0.63 | Local hide section |
| Finger 3 tip | 5.71 | 3.05 | 1.73 | Claw component |
| Finger 4 knuckle | 2.76 | 4.02 | 0.43 | Local hide section |
| Finger 4 tip | 2.87 | 2.74 | 1.26 | Claw component |

The actual span is 18.993751 m.
The lowest source vertex is Y=-0.001826 m.
The source's wings are spread, but their surfaces are not horizontal or coplanar.

The jaw hinge uses 79 rear-wall vertices with mask values between 0.02 and 0.15.
Their Y bounds are 4.502058 to 4.682943 m.
Their Z bounds are -3.515665 to -3.394357 m.
The hinge uses their median Y and Z coordinates.
Its position differs from the published preview hinge by about 68 mm.
The static mesh contains no internal jaw joint. This estimate is not a measured anatomical pivot.

The eye fit excludes the cornea and uses the rear hemisphere.
Each fitted sphere has a radius of approximately 0.140000 m.
Each inner lid ring contains 36 unique positions at radius 0.152 m.
Each socket contains 253 unique positions at radius 0.156 m.
Topology traversal recovers five more rings outside the inner ring.
Each eye therefore has exactly six rings of 36 positions.
Texture seams duplicate some positions in the exported vertex arrays.

The tongue forms a separate 106-position component.
Its anchors follow four measured cross-sections:

| Anchor | X | Y | Z |
|---|---:|---:|---:|
| Tongue 01 | -0.010783 | 4.551916 | -3.600000 |
| Tongue 02 | -0.010783 | 4.522171 | -3.850344 |
| Tongue 03 | -0.010783 | 4.492452 | -4.100688 |
| Tongue tip | -0.010783 | 4.474186 | -4.351032 |

Tail anchors use surface samples within 120 mm of each Z station.
They use the central 300 mm strip and its 40th-percentile Y height.
This reduces bias from dorsal spikes, but remains a surface-based estimate.

## Bone contracts

Every original `winged-quadruped.json` name remains present.
The rig retains `root`, `chest`, three neck bones, `head`, `jaw`, `jaw_tip`, and eight tail bones.
It retains both forelegs, both hind legs, their feet, and their three-toe branches.
It retains every original wing root, arm, wrist, and finger name.

Each wing adds a fourth two-bone finger and a third segment on finger 1.
The face adds `eye_l`, `eye_r`, four eyelid bones, and `tongue_01` through `tongue_03`.
All eye and eyelid bones parent to `head`.
The tongue chain parents to `jaw`.
`jaw_tip` remains available for the engine's forward jaw probe.

The mapper skips `_end`, `ik`, `pole`, `cont`, `target`, `chain`, `roll`, `fly`, `muscle`, and `kneecap` in relevant searches.
The new face names contain none of those substrings.
They also avoid neck, tail, wing, shoulder, head, limb, foot, and chest aliases.
`eyelid_upper_l` does not contain the foreleg alias `upper_arm`.

The mapper selects sides from joint positions and descendants.
The wing chain branches at `wing_wrist_l` or `wing_wrist_r`.
The four finger branches preserve the mapper's anatomical hierarchy.
No engine source changed. The engine mapper itself was not executed.

## Weight decisions

The body uses distance to measured bone segments.
The leg field blends adjacent segments within 150 mm on either side of a joint.
It blends toward toe bones below Y=0.35 m.
The torso blend ends at Y=2.65 m.

The wing field copies the candidate rigger's inverse-distance formula and continuous four-influence cutoff.
The cutoff subtracts the fifth-largest score before normalizing the four remaining scores.
The distance radius is 0.408 m, scaled from the candidate's canonical radius.
The wrist share fades between 0.24 and 0.98 m from its anchor.
The field applies beyond X=0.85 m, above Y=2.65 m, and behind Z=-1.75 m.
It reaches full strength at X=1.5 m.
Membrane and adjacent hide use the same field and body transition.
The builder does not level the already spread wings.

This vectorized field missed the candidate function's fallback for tied scores.
Five wing segments share a fan junction.
Eight exported hide vertices encounter tied scores and produce division by zero.
Their weights become NaN, which means an invalid numeric value.
This is an implementation defect. The source geometry did not require invalid weights.

The source `hide._JAW` mask becomes the exported jaw weight exactly.
The remaining share belongs to `head`.
This preserves the authored corner falloff and skull-side fang assignments.
No additional cheek vertices receive jaw weight.

The mouth begins with its source jaw mask.
Each tooth component becomes rigid under `head` or `jaw` according to its original mask.
The tongue receives only tongue-chain weights.
The three tongue bones remain children of the jaw.

Each eye mesh receives weight 1 on its own eye bone.
The socket receives weight 1 on `head`.
The six lid rings receive strengths 1.0, 0.97, 0.84, 0.58, 0.25, and 0.035.
Their remaining weight belongs to `head`.
The inner ring's measured principal axis supplies the lid's long axis.
The fitted eye direction supplies its outward frame.

Each eyelid bone uses that measured frame in its bind rotation.
The closed pose scales its local transverse axes to 0.001.
It translates outward by 0.164 m and overlaps the opposite lid by 0.8 mm.
This is a flattened closure, not a spherical sliding animation.
It passes sampled coverage, but the closed skin looks stretched.
The geometry remains unchanged in the resting GLB.

Every horn component receives one rigid owner.
Skull horns follow `head`.
Foot claws follow their nearest toe segment.
Wing claws follow their nearest wing segment.
The final verifier checks all 20 horn components.

The posterior head region follows `head` rigidly to protect its dense frill.
Its bounds are Y>3.48 m, -3.50<Z<-1.78 m, and absolute X<1.32 m for the length check.
Zero-mask vertices in that region receive no jaw movement.
This conservative choice also makes part of the collar follow the head.
The collar does not have separately fitted plate ownership along the neck.

## Pose checks and limits

The checks read the exported GLB again before posing.
The jaw rotates -35 degrees about local X.
Both eyes rotate 25 degrees about local Y.
Both wing elbows and wrists rotate 80 degrees about local Y, with mirrored signs.
Both forearm bones rotate 60 degrees about local X.
Both shin bones rotate -60 degrees about local X.
`neck_02` rotates 30 degrees about local X.

The blink check measures inner-edge separation against the unchanged 5 mm limit.
It casts 10,517 coverage rays per eye across nine viewing directions.
The directions span approximately 20 degrees around each eye's normal.
No ray exposes the ball in the final blink.
This sampled check is not a proof for every possible viewing direction or intermediate blink pose.

The limb check welds texture seams in temporary measurement arrays.
It finds each boundary loop and caps that loop separately.
A cap closes the measurement surface without changing the asset.
All four tested limb regions have three simple boundary loops.
Each boundary vertex has two neighbours, and no measured edge has more than two incident triangles.
The same vertex correspondence and cap rule apply before and after deformation.

The earlier single-centroid cap combined unrelated boundaries and gave misleading volume changes.
Pass 3 replaced that method with separate boundary caps.
The 15% threshold did not change.
The final changes range from 3.9066% to 8.8792%.

The frill check measures 50,673 edges longer than 0.1 mm.
It also checks rigid head ownership for 28,490 exported vertices.
The maximum measured length change is 0.137055%.
Very short edges are numerically unstable at float precision.
Rigid ownership is the stronger check for those excluded short edges.
The frill is connected to the head, so the check covers its region rather than naming individual spikes.

Mirroring compares actual exported weights at nearest reflected surface positions.
It swaps left and right joint names before comparing weights.
The threshold remains two percentage points per influence.
The mouth uses X=-0.01 as its reflection plane. Other meshes use X=0.

The source is asymmetric and has different triangulation on opposite sides.
Nearest positions can pair different teeth or upper and lower surfaces.
The final mouth example pairs a head-owned tooth with a jaw-owned tooth only 10.031 mm away.
This correspondence limitation remains a failed gate, not a declared symmetry pass.
The membrane's maximum influence difference is 0.373582, well above 0.02.
Its worst pair lies 77.694 mm apart after reflection.
The hide's invalid weights prevent a finite symmetry result.

The membrane check fits one plane to the posed finger anchors and measures every membrane vertex against it.
The unchanged limit is 0.05 m.
The maximum distances are 2.994477 m on the left and 3.080295 m on the right.
The finger anchors themselves deviate by up to 0.257998 m from that fitted plane.

Four right-wing anchors form a tetrahedron with volume 0.366381 cubic metres in the rest pose.
Those anchors are the fan origin, finger 1 knuckle A, finger 2 knuckle, and finger 4 knuckle.
A nonzero tetrahedron volume proves that those four anchors are not coplanar.
A literal plane through all measured finger anchors therefore does not exist.
Flattening the membrane or moving the measured anchors would change this geometric contract.
The plane gate remains failed.

The intersection check compares membrane triangles with arm-dominated hide triangles.
The left wing has no measured intersections.
The right wing has seven intersecting triangle pairs before deformation and eight after folding.
The source overlap lies near X=2.17 to 2.56 m, Y=4.10 to 4.45 m, and Z=-0.65 to -0.47 m.
The source-preserving build retains that overlap.
The folded intersection gate remains failed.

## Pass history and render inspection

| Pass | Result | Changes and observations |
|---|---|---|
| 1 | 37 pass, 13 fail | Initial skin, full render set, incorrect inspection UV conversion, failed blink frame |
| 2 | 38 pass, 12 fail | Correct inspection UVs, measured lid bind axes, adjacent limb weights, improved framing |
| 3 | 47 pass, 7 fail | Lid overlap, nine-direction coverage, corrected volume caps, component and hierarchy checks |

Each pass built, checked, rendered, and received full-resolution inspection.
No fourth build pass ran.
The three complete process outputs are preserved in `artifacts/hero-rig/pass-1-output.txt` through `pass-3-output.txt`.
The first two asset and render snapshots remain in the ignored cache.

All seven final sheets are 1920 by 1080 pixels:

| File | Inspection result |
|---|---|
| `bones.png` | Side and top skeleton overlays show the measured limb and finger layout. |
| `jaw.png` | The jaw opens clearly. Lower teeth clear the upper lip. The mouth floor looks pale and flat. |
| `blink.png` | The ball disappears. The lid looks stretched, and the lower patch retains small dark defects. |
| `wingfold.png` | The folded fan is not planar. Invalid wrist vertices leave thin protruding lines. |
| `crouch.png` | Four legs bend. The pose is a deformation test, not a planted crouch. |
| `neck.png` | The frill stays rigid. The collar moves with the head more than a flexible neck requires. |
| `weights-head.png` | Red shows jaw weight. Green and blue show upper and lower lid weight. |

Blender ignores the invalid influence entries when constructing inspection vertex groups.
Those eight vertices therefore remain unweighted in the inspection scene.
The renders do not establish usable runtime deformation at those vertices.
The explicit exported-weight gate fails before rendering, and every final sheet displays the failed status.

## Exact final verification output

The following block is copied from `artifacts/hero-rig/verification.txt` without editing.
The NaN case requires the additional diagnostic immediately after this block.

```text
PASS bone names: 77 unique joints; all original and requested names exist
PASS armature contract: 77 joints, 1 skin, 1 imported armature
PASS new names avoid mapper aliases: []
PASS six meshes: eye_l, eye_r, hide, horn, membrane, mouth
PASS material records unchanged: materials, image records, and texture records compared
PASS source buffer unchanged: 21741832 original binary bytes compared, including every embedded image
PASS rig.cfg unchanged: byte comparison
PASS flight.cfg unchanged: byte comparison
PASS inverse binds reconcile: maximum matrix error 2.43390027e-07
PASS measured skeleton hierarchy: maximum anchor error 4.4408921e-16 m; wrong parents []
PASS eye_l source geometry: 1024 triangles; all source attributes and indices compared
PASS eye_l influences: max sum error 0; unweighted 0; maximum 1
PASS eye_r source geometry: 1024 triangles; all source attributes and indices compared
PASS eye_r influences: max sum error 0; unweighted 0; maximum 1
PASS hide source geometry: 100773 triangles; all source attributes and indices compared
FAIL hide influences: max sum error nan; unweighted 0; maximum 4
PASS horn source geometry: 7803 triangles; all source attributes and indices compared
PASS horn influences: max sum error 0; unweighted 0; maximum 1
PASS membrane source geometry: 6340 triangles; all source attributes and indices compared
PASS membrane influences: max sum error 1.78813934e-07; unweighted 0; maximum 4
PASS mouth source geometry: 4686 triangles; all source attributes and indices compared
PASS mouth influences: max sum error 1.1920929e-07; unweighted 0; maximum 3
PASS hide jaw mask retained: maximum exported weight difference 0; source corner falloff retained
PASS horn component ownership: 20 components checked; incorrect components []
PASS mouth component ownership: 68 components checked; incorrect components []
PASS eye_l rigid: 546 vertices at weight 1
PASS lid_l topology: six rings of 36 unique positions
PASS socket_l fixed: 369 exported vertices follow head
PASS eye_r rigid: 546 vertices at weight 1
PASS lid_r topology: six rings of 36 unique positions
PASS socket_r fixed: 377 exported vertices follow head
FAIL hide mirrored weights: max nan; RMS nan; nearest surface distance max 0.207003 m; 24548 pairs
FAIL membrane mirrored weights: max 0.373582; RMS 0.030359; nearest surface distance max 0.193837 m; 1910 pairs
PASS horn mirrored weights: max 0.000000; RMS 0.000000; nearest surface distance max 0.109653 m; 2110 pairs
FAIL mouth mirrored weights: max 1.000000; RMS 0.338856; nearest surface distance max 0.065520 m; 1277 pairs
PASS Blender bind pose: max vertex displacement 2.81154547e-07 m
PASS jaw fixed skull: 35 degrees; 89140 distant zero-mask vertices; max motion 0 m
PASS lower teeth clear upper lip: 1542 vertices; nearest fixed hide 0.196240 m; minimum vertical clearance 0.128151 m
PASS blink_l closure: inner-edge height spread 0.001599765 m; threshold 0.005 m
PASS blink_l ball coverage: 0/10517 rays exposed across nine directions; minimum hide distance 0.010729 m
PASS blink_r closure: inner-edge height spread 0.001599879 m; threshold 0.005 m
PASS blink_r ball coverage: 0/10517 rays exposed across nine directions; minimum hide distance 0.010729 m
PASS eye_l independence: 25 degrees; max lid motion 0 m; eye motion 0.062647 m
PASS eye_r independence: 25 degrees; max lid motion 0 m; eye motion 0.062647 m
FAIL wing_l membrane plane: elbow 80 and wrist 80 degrees; max 2.994477 m; RMS 0.468634 m; finger nonplanarity 0.257998 m
PASS wing_l arm intersections: 0 folded intersecting triangle pairs; 0 already intersect at rest; 2097 arm triangles
FAIL wing_r membrane plane: elbow 80 and wrist 80 degrees; max 3.080295 m; RMS 0.579947 m; finger nonplanarity 0.257998 m
FAIL wing_r arm intersections: 8 folded intersecting triangle pairs; 7 already intersect at rest; 2110 arm triangles
PASS fore_l volume: 60-degree elbow/knee; rest 0.631979 m3; posed 0.575865 m3; change 8.8792%; simple boundary loops [93, 39, 37]; valid True
PASS hind_l volume: 60-degree elbow/knee; rest 0.900040 m3; posed 0.864879 m3; change 3.9066%; simple boundary loops [28, 31, 151]; valid True
PASS fore_r volume: 60-degree elbow/knee; rest 0.631848 m3; posed 0.578212 m3; change 8.4887%; simple boundary loops [43, 90, 40]; valid True
PASS hind_r volume: 60-degree elbow/knee; rest 0.911286 m3; posed 0.864028 m3; change 5.1859%; simple boundary loops [141, 32, 30]; valid True
PASS frill edge lengths: neck 30 degrees; 50673 edges over 0.1 mm; maximum length change 0.137055%
PASS frill rigid ownership: 28490 vertices in measured posterior head region
```

The literal `unweighted 0` field counts zero sums only. It does not count NaN sums.
A separate exported-array diagnostic found eight invalid rows with zero finite weight mass.
The finite-weight condition correctly failed the influence gate. The zero-sum count alone is incomplete.

```text
INVALID_WEIGHTS eye_l 0 finite mass zero 0
INVALID_WEIGHTS eye_r 0 finite mass zero 0
INVALID_WEIGHTS hide 8 finite mass zero 8
BOUND [-3.661107301712036, 5.915647983551025, -0.6388640999794006] [3.6576881408691406, 5.9357147216796875, -0.49701619148254395]
EXAMPLES [[-3.661107301712036, 5.915647983551025, -0.6388640999794006], [-3.661107301712036, 5.915647983551025, -0.6388640999794006], [-3.647160053253174, 5.9301910400390625, -0.6041232347488403], [-3.6446645259857178, 5.925394535064697, -0.5424110889434814], [-3.6132802963256836, 5.9265618324279785, -0.5034734010696411], [3.6576881408691406, 5.9292216300964355, -0.5975627303123474], [3.63157320022583, 5.9357147216796875, -0.5580158829689026], [3.612229585647583, 5.92567777633667, -0.49701619148254395]]
INVALID_WEIGHTS horn 0 finite mass zero 0
INVALID_WEIGHTS membrane 0 finite mass zero 0
INVALID_WEIGHTS mouth 0 finite mass zero 0
GLB_BYTES 24418268
Blender 5.3.0 (hash 02f0899d78e3 built 2026-11-10 01:31:21)

Blender quit
```

## Reproduction identity

Source SHA-256:

```text
6edb94f950cd149e44089c5412e123b03bb2f76999406e6f229e6ecfe159c401
```

Output SHA-256:

```text
c8ec367162bf260ef68226eb8abafb59bb6f0b2e2e0b791c3d93aae295d28468
```

The generator and asset remain in the final failed state. No unverified fix was applied after the third pass.
The first repair must restore the tied-score fallback before another exported asset can meet the influence contract.
The remaining geometry and symmetry gates also need resolution before acceptance.
