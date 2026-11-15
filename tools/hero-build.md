# The hero dragon's static mesh

`tools/build_hero.py` builds `assets/embercrest-hero-static.glb` from three
Hunyuan sculpts. It was built and tested with Blender 5.3.0 on macOS (Metal).
The result is one unrigged dragon in six meshes and five materials. This file
records what the script does, the numbers, the decisions, the weak points, and
what the rigging step needs.

## Version 5b: the root panel by construction (2026-11-15)

Version 5b replaces version 5's root panel, the membrane between the body and the rib. The outer three panels are version 5's, built by the same code. The section below on version 5 still holds for them.

### State

- `assets/embercrest-hero-static.glb`: 25.6 MB, 86 of 86 checks pass (`artifacts/hero-build/verification-v5.txt`). Hide 104,665, membrane 7,992, horn 6,758 triangles: 119,415, under the budget of 120,000.
- `assets/embercrest-hero.glb`: 56 of 56 rig checks pass, none changed (`tools/hero-rig.md`, "Mesh version 5b").
- The membrane is one closed manifold: 4,000 positions, 11,988 edges each used by two triangles, and each position has one twin 19.8 mm away.
- No sheet edge folds past 60 degrees at rest, in the root patch or anywhere else. Version 5 had 16 a face past 100 degrees.
- `wing5` now runs after `align2`, because it reads body B's flank from `src2.blend`.

### The ruled patch (`W5Root`)

- B(t) is the wing's own edge: the upper arm's bone line from the shoulder to the elbow, resampled about every 0.17 m, then the outer sheet's own vertices along the rib to its tip. The two sheets share those 14 rib vertices, so they are welded. B is 3.77 m long. t is arc length along B.
- A(t) is the attachment line. It runs through the stations in `W5_ROOT_PATH`: (Y, Z) from (1.00, 3.40) to (-1.20, 3.25). Its X is body B's flank, read by rays from the side, smoothed six times, and moved 25 mm inward. A runs from (0.37, 1.00, 3.40) to (0.47, -1.20, 3.25).
  - The flank above Z 3.2 is smooth. Below Z 3.0 the haunch bulges to X 1.0 to 1.4 between Y -0.2 and -1.0, so A stays above it.
  - A was not taken from version 4's membrane. Version 4's membrane ends at X 1.13 on the old wing's root, not on the body.
- P(s, t) = A(t) + s (B(t) - A(t)), with a sag of 50 mm at s = 0.5 (a sine in s).
- The trailing edge from A(1) to the rib's tip is scalloped 0.35 m deep.
- The patch is triangulated in (u, v), with v the length along B and u = s |B - A|, at the same 0.17 m spacing as the other panels. The second face is the same offset copy as the rest.
- The sculpt (the bake source) is carried the same way. A sculpt point in the root panel takes the (s, t) of the nearest point of the same ruled patch in plan, between the sculpt's own arm-and-rib edge and the first 85% of its own inner edge.
- The exported membrane carries `_SHEET_S`: s on the root patch, -1 elsewhere. The rig reads it.

### Texture

The baked root texture streaked. Sculpt points beyond the plan patch clamp to its edge and collapse, so the bake rays hit folded source.
In the maps stage (`smooth_root5`), the membrane texels inside X 2.25 take each face's target colour, with a variation of plus or minus 10% in log colour that changes slowly over the wing. Their normal is flat. The replacement fades out by X 2.60, before the rib.
The root panel therefore has no veins. The outer panels keep the sculpt's.

### The arm's root

The arm's stump now ends 0.90 m inside the body past the shoulder (`W5_START_DEPTH`, was 0.55). It was not closed with a collar: the bones are open shells over the sheet. The arm still meets the back as an intersection. From `--inspect 300 6 10` the join reads as the arm going into the back, without a step.

### Engine pairs

`artifacts/hero-build/pairs-v5b-*.jpg`, version 4 on the left, in this worktree's engine at 1600 x 900, cropped and not scaled.

| File | View | What it shows |
|---|---|---|
| `pairs-v5b-glide-top.jpg` | `--studio 0 --frames 120 --inspect 90 14 85` | The root panel covers the flank beside the hips, as version 4 does. Ringed fingers and claws. |
| `pairs-v5b-rootTop.jpg` | `--inspect 90 7 85` | The root panel close: one smooth sheet from the arm to the flank. Plain, no veins. |
| `pairs-v5b-root.jpg` | `--inspect 300 6 10` | The far wing's root: smooth, attached to the flank. The near arm goes into the back. |
| `pairs-v5b-flap790-rear.jpg` to `-flap812-rear.jpg` | `--studio 1`, frames 790 to 812, from behind | The membrane is whole through the beat. |
| `pairs-v5b-flap790-side.jpg` to `-flap812-side.jpg` | the same, `--inspect 270 10 0` | The root panel is a smooth wall from the arm to the flank. |
| `pairs-v5b-dive-side.jpg` | `--studio 5 --frames 200 --inspect 270 10 0` | The folded wings lie along the back. Nothing pokes through the body or the hind leg. |
| `pairs-v5b-stand-side.jpg`, `-stand-rear.jpg` | `--studio 9 --frames 200 --inspect 270 11 4`, `0 12 6` | Folded on the ground. No tear, nothing through the body. |

### What is still weak

- The root panel has a smoothed colour and no veins or normal detail. Close up it reads as plain skin beside veined panels.
- The arm meets the back as an intersection, not a blend.
- The outer panel is 28% compressed across (see version 5).
- The fade from the smooth root colour to the baked texture leaves a few faint dark smears near the rib (`pairs-v5b-rootTop.jpg`).

## Version 5: wings from the skeleton-guided wing sculpt (2026-11-15, not delivered)

Version 5 replaces both wings of version 4 with wings from `assets/embercrest-cand-hero-wing2-hunyuan.glb`.
Its plates were drawn along a flat projection of the skeleton's wing bones.
The body and the head are version 4's. Version 4 stays the delivered mesh: the root panel of the new wing creases (below).

### State

- Build: `HERO_WING=v5 HERO_BODY=$PWD/assets/embercrest-cand-hero-body2-hunyuan.glb blender -b --factory-startup --python tools/build_hero.py`. The default run now runs the `wing5` stage when `HERO_WING=v5`.
- `assets/embercrest-hero-static.glb`: 25.7 MB, 86 of 86 checks pass (`artifacts/hero-build/verification-v5.txt`). Hide 104,533, membrane 6,320, horn 6,775 triangles: 117,628.
- `assets/embercrest-hero.glb`: 56 of 56 rig checks pass, with no check changed (`tools/hero-rig.md`, "Mesh version 5").
- Version 4's files are kept in `build-hero-cache/ref-v4/` (ignored by git).
- The first build with 9,000 bone triangles a wing failed one check: 121,454 triangles against the budget of 120,000. The wings now have 7,000 a wing.

### The panels against the skeleton

The sculpt's bone points were read off a thickness picture in plan and moved onto each bone's ridge.
With one scale of 7.205 m per sculpt unit (the skeleton's bone length over the sculpt's), the gap between the bone ends of each panel is:

| Panel | Sculpt | Skeleton | Difference |
|---|---:|---:|---:|
| finger 1 to finger 2 | 2.06 m | 1.61 m | +28% |
| finger 2 to finger 3 | 2.46 m | 2.51 m | -2% |
| finger 3 to rib | 2.90 m | 2.90 m | 0% |

The outer panel is 28% wider at the trailing edge and its angle at the hub is 22 degrees against 15. The brief's limit was about 15%.
The build went on, because the sheet is rebuilt from a smooth map and is not the sculpt's billow squeezed. The panel's texture is 22 to 30% shorter across the panel than along it.
Version 3's outer panel was 3.5 times too wide.

### The `wing5` stage

`--stage wing5` reads `wing5_src_raw.blend` (written by `import`) and nothing from the body. It writes `wing5.blend` and `wing5_plan.npz`. It takes about 35 seconds.

1. The sculpt turns into its own plane (the principal axes of its vertices). Plan is an 800 px picture over it; a raster of 1,200 cells gives the upper and lower surfaces.
2. `W5_CHAINS` holds each bone line in plan with the skeleton point that each knot lands on. Skeleton points come from `tools/skeletons/embercrest-hero.json`.
3. One stub was found: a rib 1 m long from the wrist knob between the forearm and finger 3 (`W5_STUB`). It is flattened into the membrane.
4. The membrane thins to the sheet's 20 mm about its middle. Bones keep their thickness.
5. `W5Map` carries plan to the build frame. Four panels (root, rib to finger 3, finger 3 to 2, finger 2 to 1) are polygons of their bounding bone lines and trailing edge. Mean value coordinates carry each polygon onto the same bones in 3D. Within a reach of each bone (2.2 times its half width) a point moves rigidly with the bone, and the two maps blend. The panel keeps 60% of the sculpt's billow.
6. The root panel's inner edge runs from the rib's end to `flank_b`, then along the flank to `flank_a` and the arm's stump, inside body B.
7. The sheet is a constrained Delaunay triangulation in plan: the outline, the bone lines of fingers 2 and 3 and the rib as constraints, and a triangular grid 16 px apart. It is mapped, then copied 10 mm each way along the vertex normals, and a rim joins the two faces. Edges: median 0.17 m, 95% under 0.29 m.
8. The bones are the sculpt with every face thinner than 0.008 units removed, reduced to 7,000 triangles a wing. Their open rims sink to the sheet's middle, inside the sheet. Faces past each finger's end and the two thumb claws are `horn`.
9. The finger 1 claw ended at X 9.68. Points past X 7.6 move in along X so that the tip is at 9.50.
10. The left wing is the mirror.

In the exported file the membrane is one closed manifold: 3,164 positions, 9,480 edges, each used by two triangles. Each position has exactly one twin, 19.8 mm away.
The later stages leave the sheet alone: no long-edge split, no joint loops and no merge on membrane faces. The wings are joined, not unioned.

### Colour

The sculpt's membrane is (0.64, 0.09, 0.003) in linear, on both faces. Each part moves by one log gain to version 4's geometric mean, measured on the reference file.
The membrane's detail gets 1.15 times its contrast.

| Part | Target, linear |
|---|---|
| membrane, upper face | 0.529, 0.112, 0.029 |
| membrane, lower face | 0.492, 0.108, 0.030 |
| wing bones (`hide`) | 0.138, 0.027, 0.010 |
| wing claws (`horn`) | 0.107, 0.031, 0.015 |

### What is wrong with version 5

- The root panel creases at rest. Between the body and the rib, 16 sheet edges a face fold past 100 degrees, along a line from (0.61, 0.93, 3.58) to (2.40, -0.80, 3.24) in the build frame. In the engine the panel looks crumpled and torn from above and mid-beat (`pairs-v5-rootTop.jpg`, `pairs-v5-glide-top.jpg`, `pairs-v5-flap805-side.jpg`). This is version 3's failure in one panel.
- Three changes were tried, and none removed the crease: a narrower rigid zone for the upper arm (42 folds a wing), no forced upward panel normal (42), and the shoulder knot moved to its place by length (44). The cause is the root panel's mean value map. Its target polygon is not convex: the inner edge turns back along the flank under the arm. Mean value coordinates are not one-to-one on such a polygon. The next try is a harmonic map of that panel, or a root panel whose inner edge stays the sculpt's free edge and does not attach to the flank.
- The other three panels have no fold at rest, and in the engine they stay whole in the glide, the beat, the dive and the stand.
- The arm meets the back as an intersection, not a union. The bones are open shells over the sheet, so the exact boolean cannot take them.
- The outer panel is 28% compressed across.
- The finger 1 claw end moved in by up to 0.18 m. The finger's bone ends 0.15 m inside the claw.

### Engine pairs

Old is version 4 (`build-hero-cache/ref-v4/embercrest-hero.glb` with version 4's sidecars), new is version 5, in this worktree's engine at 1600 x 900. Each pair is cropped and not scaled.

| File | View | What it shows |
|---|---|---|
| `pairs-v5-glide-top.jpg` | `--studio 0 --frames 120 --inspect 90 14 85` | New: ringed fingers, veins, claws at each tip. The root panel beside the hips is crumpled. |
| `pairs-v5-rootTop.jpg` | `--inspect 90 7 85` | The root panel close from above: jagged and bunched, on both sides. |
| `pairs-v5-glide-side.jpg`, `-glide-rear.jpg` | `--inspect 270 10 0`, `0 12 12` | The spread wing is whole from the side and from behind. |
| `pairs-v5-flap790-rear.jpg` to `-flap812-rear.jpg` | `--studio 1`, frames 790, 797, 805, 812, from behind | The outer membrane stays whole through the beat. No pale side shows through. |
| `pairs-v5-flap790-side.jpg` to `-flap812-side.jpg` | the same frames, `--inspect 270 10 0` | The outer membrane is whole. Below the arm the root panel bunches. |
| `pairs-v5-dive-side.jpg` | `--studio 5 --frames 200 --inspect 270 10 0` | The folded wings lie along the back. No tear. |
| `pairs-v5-stand-side.jpg`, `-stand-rear.jpg` | `--studio 9 --frames 200 --inspect 270 11 4`, `0 12 6` | The folded wings on the ground. No tear. |
| `pairs-v5-root.jpg` | `--studio 0 --frames 120 --inspect 300 6 10` | The far wing's root: the crumpled root panel shows pale. |

## Version 4: the bent-leg body under version 2's wings (2026-11-15)

Version 4 is the delivered mesh. It takes the body from a new sculpt and keeps version 2's wings and head.
The sections below describe versions 3 and 2. They hold except where this section says otherwise.

### State

- `assets/embercrest-hero-static.glb`: 24.3 MB, 86 of 86 checks pass (`artifacts/hero-build/verification-v4.txt`). Hide 104,162, membrane 4,444, horn 7,488 triangles: 116,094.
- `assets/embercrest-hero.glb`: 27.1 MB, 56 of 56 rig checks pass, with two checks changed (`tools/hero-rig.md`, "Mesh version 4").
- Build command: `HERO_BODY=$PWD/assets/embercrest-cand-hero-body2-hunyuan.glb blender -b --factory-startup --python tools/build_hero.py`.

### Two switches

- `HERO_WING` is `v2` (the default) or `v3`. `v2` cuts both wings out of the first whole-body sculpt and unions them with the body. `v3` runs the `wing` stage of "Version 3". The default run skips the `wing` stage unless `HERO_WING=v3`.
- `HERO_BODY` names the body sculpt. `BODY_FIT` in the script must have an entry for the file's name. In this file "body A" is `embercrest-cand-hero-body-hunyuan.glb` (version 2's) and "body B" is `embercrest-cand-hero-body2-hunyuan.glb`.
- With body A and `HERO_WING=v2` the `parts` stage gives hide 104,619, membrane 4,429, horn 7,337 triangles. Version 2 had 104,616, 4,429 and 7,337.

### Why a new body

Body A's plates asked for four straight legs. Its legs are columns with no elbow, knee or hock, and the rig bent them at invented places.
Body B stands like a quadruped. The fore leg has a shoulder, an elbow that points back, a wrist and a paw. The hind leg has a haunch, a knee that points forward, a raised hock and a long foot.

### How body B goes into the frame

Body B stands lower and carries its neck forward, so its feet cannot set its scale as body A's did. `BODY_FIT` holds these numbers. They were fitted on side silhouettes of both bodies in the build frame.

| Number | Value | Reason |
|---|---|---|
| scale, Y shift | 13.037, 0.0 | Body A's scale and the generator's own Y. The two torsos then coincide in plan. |
| Z scale | 1.07 | Body B's back was 0.25 m lower. After the scale its back is within 0.07 m of body A's from Y 0 to 2 (3.78 against 3.84 at Y 0), and its belly within 0.1 m. |
| neck turn | 30 degrees about (Y 2.3, Z 3.1), band 1.4 m, then 0.2 m forward | The neck leaned 45 degrees forward. The head sculpt's collar stands at 75. |
| hind widen | 0 | Body B is already wider than the first sculpt. Body A's 0.15 m move was a box that ended at Y -1.7 with no fade, which left a step across the tail root. |
| hind narrow | 0.35 m at the ground | The hind feet stood 1.7 m from the centre plane under hips at 0.9. Each leg leans in below Z 2.4, by (1 - Z/2.4)^1.5, only outside half width 0.55 to 0.85. The foot's centre is now at X 1.36. |
| tail | Y scale 0.951 behind Y -1.5, lift -0.07 to 0.56 m | As for body A. The lift starts at Y -1.8, behind the hind feet. |
| neck cut box | back to Y 0.8 | The turned head's horns reach back to Y 1.4. With the box at 1.5 their tips stayed, and the body was open. |

Length, wingspan and head placement are version 2's: tail tip at Z 9.73, wingspan 18.994 m, eyes, lips and jaw hinge unchanged. The bounds centre is 3.009 m over the soles, so `ground_offset` 3.01 stands.

### Numbers measured again for body B

| Number | Body A | Body B |
|---|---|---|
| `LEG_JOINTS` | eight planes across vertical columns | eight planes, each normal the bisector of the two segments at the joint (`LEG_JOINTS_B`) |
| `TOE_CLAWS` Y ranges | (2.50, 9.0), (-0.42, 0.25) | (2.98, 9.0), (-0.62, 0.25) |
| `TOE_CLAW_Z` | 0.36 | 0.40 |
| `OLD_FEET_Y`, the soles' centres | 2.334, -0.538 | 2.828, -0.875 |

The paws stand 0.49 m further forward and the hind feet 0.34 m further back than version 2's. The verifier's foot check now compares the file with these numbers.
`tools/hero-rig.md` has the joint positions.

### Two changes the new body forced

- The exact boolean returns one wing alone when it joins body B to the wing piece. `stage_join` detects the lost body, collapses the wing piece's ten edges that have four faces, and uses the manifold solver. That solver does not carry the operand's material, so a face is the wing's where the wing piece's surface is nearer than the body's. Body A still takes the exact path.
- The palate came 5.2 mm inside the left eyeball's reach. `build_mouth` now pushes mouth vertices out to 3 mm past the reach (5 vertices moved).

### What is weak in version 4

- The hind legs still splay. From above in the glide the knees show beside the body (`pairs-v4-glide-top.jpg`).
- The fore paw has four toes and the rig has three toe bones. The two inner claws follow one bone.
- The outer fore claws are small pieces: the claw range starts at Y 2.98 and the outer toes are shorter.
- The wing root was not re-tuned. `graft.png` shows it on the new back. The stretched band of version 2 is still there.
- The neck's plates are turned 30 degrees over 1.4 m. No stretch shows in the renders. The texture is the sculpt's, baked after the turn.
- The Z scale makes body B 7% taller than its sculpt.
- `seam.png`, `head.png`, `eye.png`, `mouth.png`, `wire.png` and `compare.png` were not rendered again. `turnaround.png`, `graft.png` and `body.png` are version 4.
- The `--landmarks` table was not run for body B.

### Engine pairs

Old is version 2's rigged file, new is version 4, both in this worktree's engine build at 1600 x 900. Each pair is cropped to the dragon and not scaled.

| File | View | What it shows |
|---|---|---|
| `pairs-v4-glide-side.jpg` | `--studio 0 --frames 300 --inspect 270 6.5 0` | Old: both legs hang as columns, bent at mid-shin. New: the hind leg is a folded Z under the hip and the fore leg lies back under the chest. |
| `pairs-v4-glide-top.jpg` | `--inspect 90 14 85` | The same span and length. The new hind knees show beside the body. |
| `pairs-v4-flap-a.jpg`, `-b.jpg` | `--studio 1`, frames 790 and 805 | Two phases of the beat. The legs keep their fold. In `-b` a pale wedge of belly skin shows at the fore armpit. |
| `pairs-v4-attack.jpg` | `--studio 8 --frames 440` | The legs in the bite. Old: columns swung down. New: folded. |
| `pairs-v4-ground-side.jpg` | `--studio 9 --frames 200 --inspect 90 14 6` | Old stands on four posts. New stands with elbow, knee and hock. |
| `pairs-v4-ground-feet.jpg` | `--inspect 60 11 3` | All four feet on the ground with their shadows attached. |
| `pairs-v4-walk-a.jpg`, `-b.jpg` | `--studio 13`, frames 200 and 220 | The stride. The new fore leg reaches further. |
| `pairs-v4-landed.jpg` | `--input -0.25,0,0,0,0,0 --frames 900 --inspect 90 14 6` | Landed in the game. The feet are on the grass. |
| `v4-leg-candidates-round2.jpg` | glide, `--inspect 270 11 0` | Four of the eight tuck and trail candidates. D is the profile. |

## Version 3: the wings (2026-11-15)

Version 3 replaces both wings. The body, the head and every other part are version 2's.
The sections below this one describe version 2 and still hold, except where this section says otherwise.
The main session cut the task to the wings while it ran. The flank, hip and shoulder marks were not started.

### State

- `assets/embercrest-hero-static.glb`: 27.7 MB, 86 of 86 checks pass. Hide 98,442, membrane 9,322, horn 6,429 triangles: 114,193.
- `assets/embercrest-hero.glb`: 30.7 MB, 51 of 56 rig checks pass. The five failures are all on the new wing. See "What is weak".
- In the engine the new wing is better in one view and worse in three. Read "The marks" before you use it.

### The marks

| Mark | What it was in the mesh | What was done | Pair | Fixed |
|---|---|---|---|---|
| 0004 mark 3, "wings are worse" | The first sculpt's wing: a smooth arm and fingers with no rings, and a membrane with no veins | Both wings replaced with the wing sculpt, fitted to version 2's skeleton, graded to the plates | `pairs-top.jpg`, `pairs-rear.jpg`, `pairs-flank.jpg`, `pairs-beat790.jpg`, `pairs-beat805.jpg`, `pairs-stand.jpg` | No. From above in the glide the bones have rings and the membrane has veins and the plate's orange-red. In the two mid-beat frames and folded on the ground the membrane looks torn, and version 2 looks cleaner. |
| 0004 mark 1, 0003, 0001 mark 1, 0002 | Not examined | Not started: the body sculpt is being replaced | none | No |

One finding for the next body, not acted on: `stage_align2` moves the hind legs out by 0.15 m inside a box that ends at Y -1.7 with no fade.
The tail root is wider than 0.3 m there, so the box's rear face leaves a 0.15 m step across the tail root beside the thigh.
That step is the likeliest cause of report 0001's "flat plate" and seam. Fade the move in Y, or drop it for a body that needs no widening.

### Which option, and why

Option (a) of the brief: the sculpt itself, with the stub bones cut away and the colours graded to the plates.
The sculpt's arm, ringed fingers and claws are geometry that version 2's wing does not have, so a bake onto version 2's shape would lose them.

### The `wing` stage

`--stage wing` reads `wing2_src_raw.blend` and writes `wing2.blend` (both wings in full detail as `wing_src`, the reduced right and left wings as `wing_low` and `wing_low_left`) and `wing2_plan.npz` (masks for the maps stage). It takes about 90 seconds.
No later stage writes these files, and the stage reads nothing from the body.

All work on the sculpt is in "plan": a 1600 px square over the flat sculpt seen from above.
`W2_CHAINS` holds each bone line in plan px and the skeleton points it maps to.

1. Rays from above and below give the sculpt's upper and lower surface as two rasters.
2. The rasters are edited: three stub bones drop to the membrane's level, a bone is swept across the gap in finger 2, the panel between fingers 1 and 2 is laid flatter, and the membrane is thinned from 64 mm to 41 mm.
3. Each sculpt vertex moves from the old surfaces to the edited ones.
4. A copy is reduced to 4,400 triangles while still flat. The stage cuts it along the edge of every bone strip and marks each face hide, membrane or horn. The border between membrane and bone is therefore a cut edge, not a row of triangle corners.
5. `WingWarp` moves both meshes into the build frame. Each bone line maps to a line of version 2's skeleton. A vertex on a bone moves rigidly with it. A membrane vertex blends the bones' motions by 1 / distance^4.
6. The left wing is the mirror.

Which sculpt finger maps to which chain:

| Skeleton chain | Sculpt bone | Note |
|---|---|---|
| upper arm and forearm | the scaled arm and the forearm | The arm bends down after the shoulder joint and ends inside the body at (0.30, 2.75, -1.12) glTF |
| finger 1 (3 segments) | the leading-edge finger | Its claw ends at version 2's wing tip. The finger is 0.06 m shorter for it |
| finger 2 | the second finger from the wrist to where it stops, then a swept bone 1.3 m long, then the lower part of the third finger with its claw | The swept part has no rings |
| finger 3 | the finger that runs straight back from the wrist | Its first knuckle is on the skeleton's knuckle |
| spur | the rib from the elbow to the trailing edge | |
| none | the short finger near the tip | Its bone is flattened and its point is cut off the trailing edge. Its line still holds the trailing edge between fingers 1 and 2 |
| none | two thumb claws at the wrist and one claw on finger 1's first knuckle | Kept. Each rides on the bone under it |

The stubs that were removed: the cross bar on finger 2, the upper 2 m of the third finger, and the short finger's bone.

Landmarks: every bone line ends on its skeleton point by construction. The stage logs each line's ends.
The three fingers start 0.08 to 0.13 m from the fan origin, on the wrist knob, because the knob is rigid. The brief asked for 0.10 m.

### Join, parts, maps

- The wings are not unioned with the body. The reduced membrane's two faces touch in about 1,000 places, and the exact boolean drops such a mesh. With self-intersection on, it tears the sheet into 2,200 pieces. Each wing is its own closed surface. The arm and the root of the membrane end inside the body.
- `stage_parts` takes the wing's hide, membrane and horn from the wing stage's marks. It merges wing vertices within 6 mm after the joint loops.
- The joint loops are at the rig's joints now (`WING_JOINTS`).
- The bake records where each wing texel lies in plan. `grade_wing` uses it to remove the stub bones' colour and to give the swept bone a bone's colour.
- Colour: the upper face is graded to (0.60, 0.105, 0.022) linear with bones at (0.19, 0.045, 0.020), the lower face to (0.68, 0.27, 0.17) with bones at (0.46, 0.17, 0.10). A texel takes the upper colour by how far its surface faces up at bind.

### Swapping the body

The body sculpt is `BODY2_IN`. Set `HERO_BODY=/path/to/body.glb` to use another.
Then run every stage but `wing`: `--stage import,align,low_parts,align2,low2,join,head,parts,uv,bake,maps,export`. `import` reads the wing sculpt again, which does not change `wing2.blend`.
These numbers belong to the present body and must be measured again: the tail fit and `HIND_WIDEN` in `stage_align2`, `LEG_JOINTS`, `TOE_CLAWS`, `OLD_FEET_Y`, and the flank points `flank_a`, `flank_b` and `root` in `W2_TARGET`. Changing those three points does need the `wing` stage again.

### What is weak in version 3

- The wing reads as torn when it flaps or folds. Three causes, in order:
  1. The panel between fingers 1 and 2 is 5.6 m wide in the sculpt and 1.6 m in the skeleton. The warp squeezes it into folds.
  2. The reduced membrane has triangles 0.3 to 0.6 m long and two faces 40 mm apart. Skinning moves the faces through each other, and the pale underside shows.
  3. The rig's wing solve does not converge on this mesh.
- Rig checks that fail: membrane stretch on both wings (longest edge 4.08 of rest, limit 1.6, on the web in front of the forearm), one new membrane intersection a side, and the membrane's mirror check (0.028, limit 0.02).
- What would fix it: a membrane built as one sheet with one triangulation and an offset copy for the second face, a skeleton whose finger fan matches the sculpt's (which the brief ruled out), and a rig pass on the wing weights.
- 80 membrane triangles a side cut the body at rest. They are the membrane's root inside the flank.
- The root shows a hard line where the arm enters the back. It is an intersection, not a blend.
- The membrane's atlas has 1,104 islands.
- `tools/build_hero.py` still has version 2's unused wing code: `wing_region`, `wing_deform`, `WING_TRIS`, `WING_CLAWS` and the `WING_SINK` and `WING_TUCK` constants.
- `body.png`, `seam.png`, `head.png`, `eye.png`, `mouth.png` and `compare.png` are version 2's renders. Only `turnaround.png`, `graft.png` and `wire.png` were made again.

## Version 2

This is version 2. Version 1 took the body and the wings from one sculpt. Its
body had soft, smeared scales. Version 2 takes each part from the sculpt that
makes it best:

| Part | Sculpt | Name in the script |
|---|---|---|
| Torso, neck, legs, feet, tail | `assets/embercrest-cand-hero-body-hunyuan.glb`, generated without wings | `body2_src` |
| Wing arms, fingers, membranes | `assets/embercrest-cand-hero-hunyuan.glb`, the first whole dragon | `body_src`, then `wing_src` |
| Head and collar | `assets/embercrest-cand-hero-head-hunyuan.glb` | `head_src` |

In this file "the old body" is the first whole-dragon sculpt, and "the new
body" is the wingless sculpt.

All three sculpts are AI-generated (Hunyuan 3D). `AI_DISCLOSURE.md` must list
the file if it is published.

## Commands

```sh
blender -b --factory-startup --python tools/build_hero.py                    # every stage, then verify
blender -b --factory-startup --python tools/build_hero.py -- --stage maps,export
blender -b --factory-startup --python tools/build_hero.py -- --verify-only
blender -b --factory-startup --python tools/build_hero.py -- --render        # all nine sheets
blender -b --factory-startup --python tools/build_hero.py -- --render seam,body
blender -b --factory-startup --python tools/build_hero.py -- --landmarks     # the table in "Alignment"
```

Each stage reads the cache of the stage before it from `build-hero-cache/`
(3.7 GB, ignored by git) and writes its own. A change needs only its own
stage and the stages after it.

| Stage | What it does | Time |
|---|---|---:|
| `import` | Reads each sculpt once into a `.blend`. | 8 s |
| `align` | Puts the old body and the head sculpt in the build frame, as version 1 did. | 1 s |
| `low_parts` | Decimates the head sculpt. | 11 s |
| `align2` | Fits the new body to the old body's frame. Moves the head's collar. Cuts the wings out of the old body. | 3 s |
| `low2` | Cuts the new body's head off. Decimates the new body and the wings. | 20 s |
| `join` | Unions body, wings and head into one closed surface. Cuts off slivers. Smooths the wing seam. | 2 s |
| `head` | Cuts the lips, builds the lids, the eye sockets, the eyeballs, the mouth. | 2 s |
| `parts` | Splits long wing edges. Cuts the joint loops. Splits hide, membrane and horn. Thins the membrane. | 5 s |
| `uv` | Separates the three parts and unwraps each. | 4 s |
| `bake` | Bakes the three sculpts onto the three parts. Saves the raw bakes. | 189 s |
| `maps` | Grades the raw bakes and writes the maps. | 37 s |
| `export` | Paints the eye. Bakes the mouth's relief. Writes the file. | 4 s |

The run with no arguments made the delivered file. It ran every stage from
the three inputs and the verifier in 4 minutes 47 seconds.

## File contract

- glTF binary. +Y up, forward at -Z, metres. Wingspan 18.994 m, feet at Y = 0.
- Six nodes with no transform: `hide`, `membrane`, `horn`, `eye_l`, `eye_r`,
  `mouth`. Each has one mesh with one indexed triangle primitive.
- Five materials: `hide`, `membrane`, `horn`, `eye`, `mouth`. Both eyes use `eye`.
- Attributes: `POSITION`, `NORMAL`, `TANGENT`, `TEXCOORD_0`. `hide` and `mouth`
  also carry `_JAW`, a float per vertex (see "For the rigging step").
- Each material has three embedded JPEG maps: base colour (sRGB), tangent-space
  normal (OpenGL convention), and one packed map with occlusion in R, roughness
  in G and metallic 0 in B. The occlusion texture and the metallic-roughness
  texture are the same texture index. No `baseColorFactor`.
- No skin, no bone, no Draco. The engine's loader rejects a file with no skin
  (`docs/MODEL_GENERATION.md`). The rigging step adds it.
- `assets/embercrest-hero-membrane-thickness.png`: 2048 x 2048, one channel, in
  the membrane's UV layout. White is thick.

| Mesh | Triangles | Map size | Roughness |
|---|---:|---:|---|
| `hide` | 104,616 | 4096 | 0.50 to 0.70, from each sculpt's own map |
| `membrane` | 4,429 | 2048 | 0.55 to 0.62 |
| `horn` | 7,337 | 1024 | 0.22 to 0.35 |
| `eye_l`, `eye_r` | 1,024 each | 512 | 0.05 on the cornea, 0.15 at the rim |
| `mouth` | 4,476 | 1024 | gums 0.16, tongue 0.24, teeth 0.20; up to 0.10 more in a crease |

Hide, membrane and horn together are 116,382 triangles. The head and its
collar have 36,088 of them (31.0%). The file is 25.5 MB.

## What the script does

Inside the script the axes are Blender's: +Z up, the nose at +Y. All numbers
in this section are in that frame, unless a table says glTF. "For the rigging
step" gives glTF numbers.

### 1. Alignment

The old body sets the frame, because the rig is being fitted to version 1.

- The old body is scaled by 16.198 (19 m over its 1.173 unit span), as in
  version 1.
- The new body is scaled by 13.037 and shifted 0.010 m in Y. The script
  computes both from the feet: the distance from the fore feet to the hind
  feet is the same on both bodies (2.87 m), and the fore feet have the same Y.
- With that one scale the two torsos agree. The belly, the back and the flanks
  lie within 0.1 m of each other. The same picture guided both generations.
- The new tail was 0.55 m longer and up to 0.57 m lower. Behind Y -1.5 the
  script scales the tail's Y by 0.937, so the tips agree. It then lifts the
  tail until its belly line lies on the old belly line.
- The new hind legs stood 0.15 m closer together. Below Z 2.5 the script moves
  them out by 0.15 m. The move fades to nothing between Z 1.9 and 2.5.
- The wings are not moved at all. The old body is the frame, so every wing
  landmark keeps its version 1 position.
- The head sculpt keeps its version 1 scale and place. The eyes, the lips and
  the jaw hinge keep their version 1 numbers.

The table compares landmarks on the old body with the same landmarks in
version 2. Coordinates are the file's: glTF, +Y up, -Z forward, metres, right
side. The leg joints are the centres of the joint loops. They were read off
side renders by eye on each body, and X is the middle of the limb there. The
other rows are measured by the script (`--landmarks`).

| Landmark (right side) | Old X, Y, Z | New X, Y, Z | Difference |
|---|---|---|---|
| fore shoulder joint | 0.79, 2.30, -1.84 | 0.63, 2.30, -1.85 | -0.16, +0.00, -0.01 |
| fore elbow joint | 0.67, 1.92, -1.72 | 0.62, 1.90, -1.82 | -0.05, -0.02, -0.10 |
| fore wrist joint | 0.63, 0.98, -2.04 | 0.52, 0.95, -2.04 | -0.11, -0.03, +0.00 |
| fore foot joint | 0.57, 0.40, -2.25 | 0.53, 0.40, -2.20 | -0.05, +0.00, +0.05 |
| hind hip joint | 0.82, 2.25, 0.05 | 0.74, 2.30, 0.50 | -0.08, +0.05, +0.45 |
| hind knee joint | 0.92, 1.83, -0.00 | 0.97, 1.85, 0.33 | +0.05, +0.02, +0.33 |
| hind hock joint | 0.96, 1.43, 0.96 | 1.02, 1.10, 0.98 | +0.06, -0.33, +0.02 |
| hind foot joint | 1.06, 0.45, 0.84 | 1.03, 0.45, 0.78 | -0.02, +0.00, -0.06 |
| fore foot, centre of the sole | 0.59, 0.13, -2.34 | 0.51, 0.13, -2.33 | -0.08, +0.01, +0.00 |
| hind foot, centre of the sole | 1.07, 0.12, 0.54 | 1.10, 0.13, 0.53 | +0.02, +0.02, -0.01 |
| wing shoulder | 1.25, 3.61, -1.10 | 1.25, 3.61, -1.10 | +0.00, +0.00, +0.00 |
| wing elbow | 2.64, 4.49, -0.45 | 2.64, 4.49, -0.45 | +0.00, +0.00, +0.00 |
| wing wrist | 3.43, 5.73, -0.71 | 3.43, 5.73, -0.71 | +0.00, +0.00, +0.00 |
| wing tip | 9.50, 4.52, 1.02 | 9.50, 4.52, 1.02 | +0.00, +0.00, +0.00 |
| tail root, belly | 0.00, 1.82, 1.50 | 0.00, 1.91, 1.50 | +0.00, +0.10, +0.00 |
| tail middle, belly | 0.00, 1.66, 5.50 | 0.00, 1.65, 5.50 | +0.00, -0.01, +0.00 |
| tail tip | 0.02, 1.11, 9.73 | 0.01, 1.22, 9.73 | -0.02, +0.11, +0.00 |
| belly between the legs | 0.00, 1.95, -0.70 | 0.00, 1.90, -0.70 | +0.00, -0.05, +0.00 |
| eyes, jaw hinge, lip line | as version 1 | as version 1 | 0 |

Three rows need a refit larger than 0.2 m. All three are the hind leg. The
new hind leg stands more upright. Its thigh is 0.9 m wide where the old thigh
was 1.5 m, and it sits further back. So the hip is 0.45 m and the knee 0.33 m
further back, and the hock is 0.33 m lower. The feet are in the same place.
The script does not shear the thigh to hide this. A shear of 0.45 m over the
0.7 m between the hip and the flank would stretch the flank's plates.

### 2. Join

- Neck. The new body's neck is thicker than the old one. It matches the head
  sculpt's own neck: both throats are at Y 3.25 to 3.29. The head sculpt's
  collar still tapers to 0.62 at its lower rim (Z 3.58), but about an axis
  0.27 m further forward (Y 2.67, was 2.40). The rim then sits inside the new
  neck on every side.
- The script decimates the head in version 1's taper and then moves the
  reduced collar. So the head's triangles are the same in both versions. Only
  points below Z 4.40 move. The eyes, lids, lips and jaw do not.
- The new body's neck narrows to 0.80 between Z 3.55 and 4.0. Its cut at Z 4.0
  then stays inside the head sculpt. The new body loses its own head above
  the cut (about 278,000 triangles).
- Wings. The wing piece is the old body outside X 0.30, above Z 2.70 and
  between Y -1.30 and 1.95. Outside X 1.75 everything is wing. Four planes
  make the cut, and the cut is capped. All caps end inside the new body.
- The old body's shoulder is 0.25 to 0.35 m bigger than the new one. A wing
  piece that kept it would leave a hump of smeared scales and an open ledge.
  So the script deforms the root before the union:
  - Sink. Inside X 0.90 the root's top slopes down toward the spine, by up to
    0.45 m at X 0.40. The top of the root then enters the new back at about
    X 0.47.
  - Tuck. Under the root, the old flank is drawn in toward X 0.30. The pull
    is full 0.35 m below a line that rises from Z 3.05 at the membrane's root
    to Z 3.60 at the front of the shoulder. The root's underside becomes a
    ramp into the new flank.
- The wing arm leaves the new body at X 0.5 to 0.65, as in the old sculpt. The
  membrane's inner edge meets the flank at Z 3.15.
- The three reduced meshes are closed manifolds. Two exact boolean unions
  join them. Each seam is a real intersection line. The surface is closed and
  has no hidden faces.
- Where two sculpts cross at a shallow angle, small islands of one poke
  through the other. Ten islands under 150 faces, at the neck and at one wing
  root, are cut off and the holes closed.
- The wing seam has 886 vertices. They and two rings round them are smoothed
  four times.

### 3. Game mesh

- New body: 1,222,142 to 52,000 triangles. Wings: 666,440 to 20,000. Head:
  1,498,976 to 31,000. Blender's collapse decimation. No retopology.
- The decimation left the flat membrane with edges up to 1.17 m. The script
  splits every wing edge longer than 0.45 m (327 edges, three passes).
- Regions with real loops:
  - The lip line. The cut itself is an edge loop from corner to corner round
    the snout. Two more loops run 18 mm and 45 mm above it, and two below.
    A loop round the muzzle marks the mouth corner (Y 3.60).
  - The eyelids. Each eye has a patch of 6 rings of 36 vertices: the lid's
    inner edge, the lid's rim, and four rings out to an oval of 330 by 182 mm.
    Behind the lid's edge a socket of 7 more rings wraps the eyeball.
  - The joints. Three parallel loops, 100 mm apart, at each shoulder, elbow,
    wrist, hip, knee, hock and foot joint, and at the wing's shoulder, elbow
    and wrist. Two loops, 100 mm apart, at one knuckle on each wing finger
    (two knuckles on the leading finger). 62 planes in all. The leg planes
    are at the new legs' joints.
- Everything else is decimated: the frill, the neck, the body, the tail, the
  limbs between joints, the wing bones and the membrane. The loops are planar
  cuts through decimated triangles. They give each joint rings of vertices to
  weight. They do not give quads.

### 4. Parts

The head's parts are built as in version 1, by the same code on the same
triangles.

- Eyes. Each eyeball is a sphere of radius 140 mm with a cornea bulge of 8 mm
  over the front 26 degrees. Its centre is 140 mm behind the sculpt's painted
  eye. The lid's inner edge lies on a sphere 12 mm wider than the ball, so the
  ball turns freely. The opening is 187 mm long and 78 mm high. A socket
  behind the lid's edge closes the view into the hollow head. The sculpt's
  painted eye is painted over with the colour of the skin beside it.
- Mouth. The sculpt has an overbite: the upper lip ends in a shelf 15 to 45 mm
  wide. The cut runs 8 mm under the shelf on the lower jaw's wall.
- The sculpt has two fangs a side that hang from the upper lip and are fused
  to the lower jaw's wall. They stay on the upper jaw. The cut runs down each
  fang's seam. Four seams were closed.
- The mouth interior is one mesh: two gum sheets, a throat of three rings, 62
  teeth of 23 to 76 mm, and a tongue. Every part is kept inside the closed
  head, so nothing shows when the jaw is shut.
- Membrane. A wing vertex is membrane where the sculpt is thinner than 135 mm
  along its normal. Only faces from the wing piece can be membrane. Each side
  of the membrane then moves in by 30% of the thickness. The membrane is a
  closed two-sided sheet, about 30 to 50 mm thick.
- Horn. The four horns are found by colour on the head sculpt, as in version 1.
  The wing claws are five circles on each wing. The toe claws are the new
  body's faces under Z 0.36 with Y over 2.50 (fore) or between -0.42 and 0.25
  (hind).

### 5. Maps

- UVs: Blender's smart projection at 76 degrees, packed with rotation. The
  hide has 8,889 islands. The crisp plates make more islands than version 1's
  smooth body did (5,752).
- The pack now uses an added margin of 0.0004 (1.6 texels) in place of a
  margin that is a fraction of the islands' size. The hide uses 46.7% of its
  atlas. With version 1's setting the islands used about 37%.
- Before the pack the head's islands are scaled by 1.43 and the wing's by 0.8.
  Of the hide's texels, the new body has 50%, the head 34% and the wing arms
  and fingers 16%.
- The bake is Cycles, selected to active, one pass per map and per sculpt.
  A face reads only the sculpt it came from. The wing sculpt gets the same
  sink and tuck as the game mesh. Cage 35 mm (membrane 70 mm).
- Texels that miss (the lids, the sockets, the fang backs, the smoothed seam:
  58,554 on the hide) take the nearest baked texel in space.
- Baked light. The script fits log luminance against the surface direction,
  for each sculpt. The X terms are 0.003 (new body), 0.000 (head) and -0.043
  (wing). A sun would give an X term. The script removes the X term only. The
  Y and Z terms are the pale belly and throat, which are the design.
- Neck colour, first step, as in version 1: the head sculpt's neck takes a
  gain toward the body's, one for the back and one for the front (0.70, 0.70,
  0.87 and 1.44, 1.45, 1.45; the clamps are 0.7 and 1.45). The gain is full at
  Z 4.0 and fades to 40% at Z 4.9.
- Neck colour, second step, new: the script measures what difference is left
  in six directions round the neck, from the throat to the nape. The body is
  darker than the collar by a factor of 0.63 to 0.95 in red. Each side moves
  half way to the other. The head's share fades out between Z 3.9 and 4.5,
  the body's between Z 3.5 and 2.9.
- Wing colour. The script measures one gain where the wing's root meets the
  body: 0.645, 0.635, 0.641. The old sculpt's arm is lighter and browner than
  the new body's dark back plates. The wing's hide takes the full gain inside
  X 1.6 and 60% of it from X 4.0 out. The membrane takes the full gain at the
  root and none from X 4.0 out.
- Roughness. Each sculpt's texels are centred on the material's mean before
  the clamp. So no step in gloss shows where two sculpts meet.
- Occlusion: the hide gets a Cycles occlusion bake of the game mesh at 2K,
  scaled to 0.35 to 1. The other materials have occlusion 1.
- Thickness map: 0.45 x (sculpt thickness / 160 mm) + 0.55 x exp(-distance to
  the nearest bone / 0.45 m). It is thin between the fingers (0.25) and thick
  at the bones (1.0).
- The eye is painted by the script: an amber iris with radial fibres, a
  vertical slit pupil, a dark rim. The UV centre is the pupil.
- The mouth is coloured from vertex attributes (gum, tongue or tooth, and
  depth). New: a height field gives it relief. The gums and palate have
  ridges across the mouth, 5 mm deep, with fine noise. The tongue has
  papillae. The teeth have faint grain. Cycles bakes the height's bump into
  the tangent-space normal map. The creases are about half as bright as the
  crests.
  The mouth is wet: roughness 0.16 to 0.34.

### 6. Check

`--verify-only` reads the file and runs 86 checks. It fails with a non-zero
exit and a list of the failed checks. It checks:

- file size, no Draco, no skin, six meshes, five materials, three maps per
  material at the brief's sizes, one packed map, no `baseColorFactor`;
- one indexed triangle primitive per mesh with tangents;
- the triangle budget (80,000 to 120,000 for hide, membrane and horn
  together) and the head's share (at least 25%);
- wingspan, feet height, forward direction;
- new: the feet and the tail tip are within 0.1 m of the old body's in Z;
- each eyeball is a sphere within 8 mm, no vertex of any other mesh is within
  the ball's reach, the ball stays clear of the lids when turned 25 degrees
  each way about two axes (4.7 mm), and the lids are open over the pupil;
- with the jaw shut, no interior vertex of the mouth can be seen from 59
  directions. With the jaw open 35 degrees by `_JAW`, the interior shows and
  the chin drops 0.55 m.

### 7. Renders

`artifacts/hero-build/`, 1920 x 1080, a sun at 30 degrees and a sky light:
`turnaround`, `head`, `eye`, `mouth`, `seam`, `wire`, `compare`, and two new
sheets. `body.png` shows the back from above, a flank with a fore leg, and the
tail. `graft.png` shows one wing root from above, from the front and from
behind.

## For the rigging step

All positions here are in the file's coordinates: glTF, +Y up, -Z forward,
metres.

- Unchanged from version 1: the jaw hinge, the mouth corners, the lip line,
  `_JAW`, the eye centres, the lids, and every wing joint.
- Changed: the leg joints (the table below and the table in "Alignment"), the
  neck's girth, and the tail's surface. The tail's centre line is the old one.
- Jaw hinge: the axis is parallel to X through Y 4.640, Z -3.420. The renders
  open the jaw 35 degrees about it.
- Mouth corners: Z -3.600, Y 4.580, X -0.372 and +0.350. The slit ends there.
- Lip line (Z, Y): (-3.60, 4.580), (-3.76, 4.578), (-3.85, 4.552),
  (-3.94, 4.528), (-4.00, 4.520), (-4.12, 4.508), (-4.27, 4.508),
  (-4.365, 4.495), (-4.43, 4.465). The chin's front is at Z -4.431.
- `_JAW` on `hide` and `mouth`: 1 on the lower jaw, 0 on the skull, 0.5 on
  both lips at the corner, with a ramp of 0.25 m either side of the corner.
  The fangs are 0. It is a preview weight for the renders and the verifier.
  Use it to tell the lower lip's vertices from the upper lip's: they sit on
  the same positions.
- Eye centres: `eye_r` (0.204, 4.797, -3.645), `eye_l` (-0.224, 4.797,
  -3.645). Radius 0.140 m plus an 8 mm cornea. The gaze axis is (0.784, 0.282,
  -0.553) for the right eye, X mirrored for the left. The pupil's long axis
  is world up projected across the gaze. The head's centre plane is at
  X -0.0115, not 0.
- The lids: the lid's inner edge is 36 vertices on a sphere of radius 0.152 m
  about the eye centre. To close a lid, slide its vertices over that sphere.
  The socket vertices (radius 0.156 m) must stay with the skull.
- Joint loops, right side (mirror X for the left). Each is a plane through the
  point with the normal given. Wing joints are in (X, Z), the planes are
  vertical. They are version 1's:

  | Joint | X | Z | Normal (X, Z) |
  |---|---:|---:|---|
  | wing shoulder | 1.25 | -1.10 | (0.90, 0.45) |
  | wing elbow | 2.64 | -0.45 | (1, 0) |
  | wing wrist | 3.43 | -0.71 | (1, 0) |
  | finger 1, knuckle a | 6.31 | -0.45 | (0.97, 0.20) |
  | finger 1, knuckle b | 7.95 | 0.07 | (0.92, 0.38) |
  | finger 2 | 5.85 | 0.37 | (0.90, 0.44) |
  | finger 3 | 4.74 | 0.63 | (0.60, 0.80) |
  | finger 4 | 2.74 | 0.43 | (0.10, 1.00) |

  Leg joints are in (Y, Z), the planes contain the X axis. They are new. The
  fore leg is at X 0.52 to 0.63 and the hind leg at X 0.74 to 1.03:

  | Joint | Y | Z | Normal (Y, Z) |
  |---|---:|---:|---|
  | hind hip | 2.30 | 0.50 | (-1.00, -0.30) |
  | hind knee | 1.85 | 0.33 | (-0.87, 0.50) |
  | hind hock | 1.10 | 0.98 | (-0.86, 0.50) |
  | hind foot | 0.45 | 0.78 | (-1, 0) |
  | fore shoulder | 2.30 | -1.85 | (-1.00, 0.20) |
  | fore elbow | 1.90 | -1.82 | (-1.00, -0.10) |
  | fore wrist | 0.95 | -2.04 | (-1.00, -0.30) |
  | fore foot | 0.40 | -2.20 | (-1, 0) |

  The leg joint positions were read off orthographic renders by eye. Expect
  an error of 50 to 100 mm. The loops are 100 mm apart, so a joint still
  lands between loops.
- The wing's root. The wing piece meets the body along a line round X 0.5 to
  0.65, from about Z -1.75 (front) to Z 0.2 (the membrane's rear end), at
  Y 3.1 to 3.6. Inside X 0.9 the root's surface is not the old sculpt's: it slopes
  into the back. Weight the root between the wing shoulder bone and the
  spine. The wing shoulder joint itself (X 1.25) did not move.
- The neck seam is at Y 3.6 to 4.0. The head sculpt's collar is rigid scales
  over the neck. Weight the collar to the neck bones under it.
- The wing claws and toe claws are in `horn`. The neck, tail and spine have
  no loops.

## What is weak

- The neck seam is better and not gone. Both sides now have crisp plates of
  about the same size, and the colours meet. From the 3 m views in `seam.png`
  the seam reads as the lower edge of the frill. A close look still finds it:
  the head sculpt's plates are flat with sharp edges, and the new body's are
  rounder with dark creases. The brief asks for a seam that cannot be found.
  This build is close to that from 3 m and does not reach it from 1 m.
- The second neck colour step compares each direction as a whole. In the
  direction next to the throat the head has orange scales where the body has
  pale plates, and the blue gain hits its clamp (1.65). Both sides shift
  toward each other there by more than the scales alone would need.
- The wing arms and membranes are still the old sculpt's soft surface. The
  graft is clean in shape (`graft.png`): no step, no ledge, and the colour
  matches the dark back. The surface quality changes at the root, from crisp
  body plates to the smeared arm. The line where the root's underside meets
  the flank shows as a dark crease from the front.
- The sink and tuck stretch the old sculpt's texture over the root's slopes.
  The stretched band is about 0.4 m wide on each side of each root.
- The hind hip and knee are 0.33 to 0.45 m behind their version 1 positions
  and the hock is 0.33 m lower (see "Alignment"). The rig's hind leg needs
  more than a small refit. Everything else is within 0.16 m.
- The leg joint centres in the comparison were read by eye on both bodies.
- The topology is decimated triangles with loops cut in. It is not a
  retopology. The frill and the back plates are dense thin spikes, and
  skinning them will need care.
- The hide atlas is 46.7% used, in 8,889 islands. A hand-made unwrap would
  still do much better. The margin between islands is 3 texels, so the lowest
  mip levels will bleed a little between islands.
- The membrane has 4,429 triangles, fewer than version 1's 6,340. Its edges
  are at most 0.45 m. The border between membrane and bone follows decimated
  triangles and is jagged. The thickness map is a model, not a measurement.
- The lids are one flat colour where the sculpt's painted eye was. From close
  they read as a smooth band (`eye.png`). A step in the cheek below the right
  eye shows as a small dark notch.
- The teeth are small, because a tooth must hide inside the shut jaw. The
  mouth's relief is a pattern, not anatomy. The fangs have flat backs.
- The number of teeth changes by a few between builds (62 now, 67 in version
  1). The teeth are fitted with rays against the whole joined mesh, and the
  neck's triangles changed. The teeth's rule did not change.
- The mouth's palate comes within 2 mm of the right eyeball's reach. Both are
  hidden. A rig that moves the palate must keep it clear.
- The hide has 3 non-manifold edges and the slit along the lips. Both come
  from the lip cut near the fangs.
- The toe claws are found by a height and two Y ranges. They include a little
  of each toe. The new body also has a small rear claw on each foot. It stays
  in `hide`.
- The new body has a spike that hangs from the chest between the fore legs
  (X 0, Y 0.17 to 1.4, Z -1.9). It is the sculpt's. It will swing with the
  chest bone.
- The normal maps are JPEG at quality 95. Occlusion is baked for the hide
  only.
- The membrane's thinning takes 6 mm off the span: 18.994 m, not 19.
- The eye and mouth checks use the build's own constants for the eye centres
  and the hinge. They check the file against the build, not against the
  design.

## Decisions

- The old body is the frame, and the wings do not move. The two bodies agree
  after one scale, so the wings need no placement. The rig's wing bones stay
  valid.
- The new body's tail is fitted to the old tail's length and belly line. The
  change is a 6% squeeze and a lift of at most 0.57 m over 8 m. It keeps the
  rig's tail chain valid, and it cannot be seen on the plates.
- The hind legs are moved out, not sheared back. The first is a 0.15 m shift
  with a gentle fade. The second would distort the flank.
- The wing root is deformed, not cut at a ring and bridged. The old arm
  leaves the body in a broad flare, not a tube. A ring cut at the first clean
  section (X 1.25) would leave 0.6 m to bridge with made-up surface.
- Union, not a bridged ring, at the neck too. Both sides are spiky. A union
  of two closed meshes has one answer.
- The head's collar moves forward after the decimation, so the head's
  triangles, eyes, lids and mouth are the ones version 1 made.
- The fangs stay in the hide and on the upper jaw, as in the design's inset.
- The eyeball's clearance is a sphere, not a swept test.
- The membrane is thinned in the game mesh. The brief asks for a thin
  membrane, and the sculpt's is 70 to 130 mm.
- The atlas fix is the pack's margin only, and the mouth fix is a baked
  procedural relief. Both were cheap. A better unwrap was not.
- Version 1 is not kept. `stage_align` and `stage_low_parts` still run,
  because the frame and the head's triangles come from them.
- `assets/*.glb` is ignored by this repository. The file is not committed.
  It stays in the worktree's `assets/` for the main session to collect.
