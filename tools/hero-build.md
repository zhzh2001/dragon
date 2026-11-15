# The hero dragon's static mesh

`tools/build_hero.py` builds `assets/embercrest-hero-static.glb` from two
Hunyuan sculpts. It was built and tested with Blender 5.3.0 on macOS (Metal).
The result is one unrigged dragon in six meshes and five materials. This file
records what the script does, the numbers, the decisions, the weak points, and
what the rigging step needs.

Both sculpts are AI-generated (Hunyuan 3D). `AI_DISCLOSURE.md` must list the
file if it is published.

## Commands

```sh
blender -b --factory-startup --python tools/build_hero.py                    # every stage, then verify
blender -b --factory-startup --python tools/build_hero.py -- --stage maps,export
blender -b --factory-startup --python tools/build_hero.py -- --verify-only
blender -b --factory-startup --python tools/build_hero.py -- --render        # all seven sheets
blender -b --factory-startup --python tools/build_hero.py -- --render eye,mouth
```

Each stage reads the cache of the stage before it from `build-hero-cache/`
(1.8 GB, ignored by git) and writes its own. A change needs only its own
stage and the stages after it.

| Stage | What it does | Time |
|---|---|---:|
| `import` | Reads each sculpt once into a `.blend`. | 13 s |
| `align` | Puts both in the build frame, and tapers the head sculpt's neck. | 1 s |
| `low_parts` | Cuts the body sculpt's head off. Decimates each sculpt. | 65 s |
| `join` | Unions the two reduced sculpts into one closed surface. | 2 s |
| `head` | Cuts the lips, builds the lids, the eye sockets, the eyeballs, the mouth. | 3 s |
| `parts` | Cuts the joint loops. Splits hide, membrane and horn. Thins the membrane. | 15 s |
| `uv` | Separates the three parts and unwraps each. | 35 s |
| `bake` | Bakes the sculpts onto the three parts. Saves the raw bakes. | 160 s |
| `maps` | Grades the raw bakes and writes the maps. | 45 s |
| `export` | Paints the eye and the mouth. Writes the file. | 5 s |

All the stages ran in this order to make the delivered file. The run with no
arguments, all stages in one process, was not timed.

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
| `hide` | 100,773 | 4096 | 0.50 to 0.69, from the sculpt's own map |
| `membrane` | 6,340 | 2048 | 0.55 to 0.62 |
| `horn` | 7,803 | 1024 | 0.22 to 0.38 |
| `eye_l`, `eye_r` | 1,024 each | 512 | 0.05 on the cornea, 0.15 at the rim |
| `mouth` | 4,686 | 1024 | gums 0.35, tongue 0.45, teeth 0.25 |

Hide, membrane and horn together are 114,916 triangles. The head and its
collar have 35,937 of them (31.3%). The file is 21.8 MB.

## What the script does

Inside the script the axes are Blender's: +Z up, the nose at +Y. All numbers
in this section are in that frame. "For the rigging step" gives glTF numbers.

### 1. Join

- The body sculpt is scaled by 16.198 (19 m over its 1.173 unit span). The head
  sculpt is scaled by 3.0 m per unit. At that scale its four horns lie on the
  body sculpt's horns. A scale-free fit (ICP) drifted from 3.03 to 2.79 and was
  not used. The snout tips are matched: Y 4.495, Z 4.60.
- The necks did not agree. The head sculpt's neck is 1.5 m deep and 1.33 m wide
  at its lower end. The body's neck is 1.1 m by 0.95 m there. Below Z 4.40 the
  head sculpt is scaled about the neck axis, more with depth, to 0.62 at its
  lower rim (Z 3.58). The collar becomes a cone and its rim ends inside the
  body's neck.
- The body sculpt loses everything above Z 4.0 in the neck's column (its own
  head, 198,736 triangles). The cut is capped. The cap is inside the head
  sculpt's neck.
- Both sculpts are closed manifolds after a weld. The two reduced meshes are
  joined with an exact boolean union. The seam is the line where the head
  sculpt's lowest scales come out of the body's neck. It is a real
  intersection, not a bridged ring, so the surface is closed and has no
  hidden faces.
- Colour: the head sculpt's neck takes a gain toward the body's neck colour.
  The script measures one gain on the dark back and one on the pale front
  (measured 0.70, 0.70, 0.96 and 1.39, 1.45, 1.45; both hit the clamps of 0.7
  and 1.45). The gain is full at Z 4.0 and fades to 40% at Z 4.9.

### 2. Game mesh

- Body: 1,301,264 to 68,000 triangles. Head: 1,498,976 to 31,000. Blender's
  collapse decimation. No retopology.
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
    (two knuckles on the leading finger). 62 planes in all.
- Everything else is decimated: the frill, the neck, the body, the tail, the
  limbs between joints, the wing bones and the membrane. The loops are planar
  cuts through decimated triangles. They give each joint rings of vertices to
  weight. They do not give quads.
- The joint loops cost 13,000 triangles, so the body's decimation target is
  68,000, not 76,000.

### 3. Parts

- Eyes. Each eyeball is a sphere of radius 140 mm with a cornea bulge of 8 mm
  over the front 26 degrees. Its centre is 140 mm behind the sculpt's painted
  eye. The lid's inner edge lies on a sphere 12 mm wider than the ball, so the
  ball turns freely. The opening is 187 mm long and 78 mm high. A socket
  behind the lid's edge closes the view into the hollow head. The sculpt's
  painted eye is painted over with the colour of the skin beside it.
- The lid patch is laid out as seen from 19 degrees below the eye's normal.
  From the normal itself the brow ridge hides the upper lid.
- Mouth. The sculpt has an overbite: the upper lip ends in a shelf 15 to 45 mm
  wide. The cut runs 8 mm under the shelf on the lower jaw's wall. A cut on
  the shelf's own height grazed it and left loose pieces.
- The sculpt has two fangs a side that hang from the upper lip and are fused
  to the lower jaw's wall. Below the cut, whatever lies outside the wall
  between Y 4.262 and 4.398 is fang and stays on the upper jaw. The cut runs
  down each fang's seam. Each fang gets a flat back face and the wall gets a
  face of the same outline. Four seams were closed.
- The mouth interior is one mesh. Two gum sheets start on the lip's own
  vertices and dome into the head (up to 85 mm up, 55 mm down). A throat of
  three rings closes the back. 67 teeth, 23 to 76 mm long, stand 28 mm (upper)
  and 36 mm (lower) inside the lips. A tongue lies on the floor. Every part is
  kept inside the closed head, so nothing shows when the jaw is shut. A tooth
  that would show through the jaw is shortened and leaned in.
- Membrane. A wing vertex is membrane where the sculpt is thinner than 135 mm
  along its normal. The membrane measures 70 to 130 mm, the bones 150 mm and
  up. The histogram has no clean gap, so the border follows the threshold.
  Each side of the membrane then moves in by 30% of the thickness. The
  membrane is a closed two-sided sheet, about 30 to 50 mm thick.
- Horn. The four horns are found by colour: the sculpt's horns are grey-brown
  (saturation under 0.45) and its frill is red-brown. Each seed grows over the
  horn's smooth tube and stops at the crease where it meets the head. The
  body sculpt's claws have the hide's colour, so claws are found by position:
  the toe tips under Z 0.30, and five circles on each wing.

### 4. Maps

- UVs: Blender's smart projection at 76 degrees, packed with rotation. The
  hide has 5,752 islands and uses 38% of its atlas. The head's islands are
  scaled by 1.4 before the pack. The head then has 30% of the used area.
- The bake is Cycles, selected to active, one pass per map and per sculpt.
  A body face reads the body sculpt only, and a head face the head sculpt
  only. Cage 35 mm (membrane 70 mm, because it was thinned).
- Texels that miss (the lids, the sockets, the fang backs: 42,134 on the hide)
  take the nearest baked texel in space.
- Baked light. The script fits log luminance against the surface direction.
  It found (x, y, z) = (-0.03, 0.11, -0.63) on the body and (-0.01, 0.55,
  -0.16) on the head. The Z and Y terms are the pale belly and the pale
  throat, which are the design. A sun would also give an X term, and X is
  0.03. So the script removes the X term only. In effect it changes nothing:
  these sculpts carry no directional light to remove.
- Occlusion: the hide gets a Cycles occlusion bake of the game mesh at 2K,
  scaled to 0.35 to 1. The other materials have occlusion 1.
- Thickness map: 0.45 x (sculpt thickness / 160 mm) + 0.55 x exp(-distance to
  the nearest bone / 0.45 m). It is thin between the fingers (0.23) and thick
  at the bones (1.0).
- The eye is painted by the script: an amber iris with radial fibres, a
  vertical slit pupil, a dark rim. The UV centre is the pupil.
- The mouth is coloured from vertex attributes (gum, tongue or tooth, and
  depth). Its normal map is flat.

### 5. Check

`--verify-only` reads the file and runs 84 checks. It fails with a non-zero
exit and a list of the failed checks. It checks:

- file size, no Draco, no skin, six meshes, five materials, three maps per
  material at the brief's sizes, one packed map, no `baseColorFactor`;
- one indexed triangle primitive per mesh with tangents;
- the triangle budget (80,000 to 120,000 for hide, membrane and horn
  together) and the head's share (at least 25%);
- wingspan, feet height, forward direction;
- each eyeball is a sphere within 8 mm, no vertex of any other mesh is within
  the ball's reach, the ball stays clear of the lids when turned 25 degrees
  each way about two axes (4.7 mm), and the lids are open over the pupil;
- with the jaw shut, no interior vertex of the mouth can be seen from 59
  directions. With the jaw open 35 degrees by `_JAW`, the interior shows and
  the chin drops 0.55 m.

## For the rigging step

All positions here are in the file's coordinates: glTF, +Y up, -Z forward,
metres.

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
  vertical:

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

  Leg joints are in (Y, Z), the planes contain the X axis. The legs are at
  X about 0.75 (fore) and 0.85 (hind):

  | Joint | Y | Z | Normal (Y, Z) |
  |---|---:|---:|---|
  | hind hip | 2.25 | 0.05 | (-1.00, -0.30) |
  | hind knee | 1.83 | 0.00 | (-0.87, 0.50) |
  | hind hock | 1.43 | 0.96 | (-0.86, 0.50) |
  | hind foot | 0.45 | 0.84 | (-1, 0) |
  | fore shoulder | 2.30 | -1.84 | (-1.00, 0.20) |
  | fore elbow | 1.92 | -1.72 | (-1.00, -0.10) |
  | fore wrist | 0.98 | -2.04 | (-1.00, -0.30) |
  | fore foot | 0.40 | -2.25 | (-1, 0) |

  The joint positions were read off orthographic renders by eye. Expect an
  error of 50 to 100 mm. The loops are 100 mm apart, so a joint still lands
  between loops.
- The neck seam is at Y 3.6 to 4.2. The head sculpt's collar is rigid scales
  over the neck. Weight the collar to the neck bones under it.
- The wing claws and toe claws are in `horn`. The neck, tail and spine have
  no loops.

## What is weak

- The seam can be found. The geometry is closed and the colours are close, but
  the head sculpt has crisp separate plates and the body sculpt has soft,
  smeared scales. The change of detail shows at 3 m (`seam.png`). The brief
  asked for a seam that cannot be found. This build does not reach that. The
  body sculpt's neck would need the same quality as the head, for example a
  neck generated on its own.
- The topology is decimated triangles with loops cut in. It is not a
  retopology. The frill is dense thin spikes, and skinning it will need care.
- The hide atlas is 38% used, in 5,752 islands. The head has 30% of the used
  area, which is 11.5% of the whole atlas. A hand-made unwrap would double the
  texel density.
- The lids are one flat colour where the sculpt's painted eye was. From close
  they read as a smooth band (`eye.png`). The gap between lid edge and ball is
  12 mm, which the eye needs to turn 25 degrees. A step in the cheek below
  the right eye shows as a small dark notch where the lid patch meets the
  decimated mesh.
- The teeth are small. The lower jaw is 70 to 100 mm deep at the front, and a
  tooth must hide inside it when the jaw is shut. The mouth's colours are
  ramps, not painted detail, and its normal map is flat. The fangs have flat
  backs.
- The mouth's palate comes within 2 mm of the right eyeball's reach. Both are
  hidden. A rig that moves the palate must keep it clear.
- The hide has 3 non-manifold edges and the slit along the lips. Both come
  from the lip cut near the fangs. `wire.png` shows a few long thin lines:
  sliver triangles that the plane cuts left.
- The border between membrane and bone follows decimated triangles and is
  jagged. The membrane's unwrap has 740 islands. The thickness map is a
  model, not a measurement.
- The toe claws are found by a height and two Y ranges. They include a little
  of each toe.
- No baked light was removed, because the fit found none. If a later sculpt
  has a baked sun on its centre plane (from the front or from above), this
  fit cannot tell it from the pale belly.
- The normal maps are JPEG at quality 95. Occlusion is baked for the hide
  only.
- The membrane's thinning takes 6 mm off the span: 18.994 m, not 19.
- The eye and mouth checks use the build's own constants for the eye centres
  and the hinge. They check the file against the build, not against the
  design.

## Decisions

- Union, not a bridged ring. The head sculpt's collar is wider than the body's
  neck, and both are spiky. A ring cut through spikes makes many loops. A
  union of two closed meshes has one answer.
- The head sculpt is tapered, not the body widened. The body's proportions
  match the design.
- The fangs stay in the hide and on the upper jaw, as in the design's inset.
  The mouth mesh adds teeth behind them.
- The eyeball's clearance is a sphere, not a swept test. A lid that is outside
  the ball's bounding sphere clears every rotation, not just 25 degrees.
- The membrane is thinned in the game mesh. The brief asks for a thin
  membrane, and the sculpt's is 70 to 130 mm.
- `assets/*.glb` is ignored by this repository. The file is not committed.
  It stays in the worktree's `assets/` for the main session to collect.
