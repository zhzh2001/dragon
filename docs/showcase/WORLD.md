# Showcase: the world

Changes to the valley made outside a critic round: the river, the walls,
the bushes, the scanned rock kit, and where a frame's time goes. Part of
the showcase record; `docs/SHOWCASE.md` is the index. The cliffs, the
braided river, the air and the grass tufts were built after critic rounds
and are in `CRITIC.md`.

## The river's streaks and whirls (2026-11-15, 11:55)

Reports 0008 and 0010. Close to the river on a bend, the whole surface
was straight streaks from the far bank to the camera, and from a distance
a patch of it churned. `artifacts/showcase-s8/water-close-before-after.jpg`
and `water-distant-before-after.jpg` have both from the reports' own
cameras.

The cause was in the wave trains, not the reflection. A first guess, that
the reflection's march sampled the forest at the same distances in every
pixel, was wrong: jittering it changed nothing. A flat normal removed the
streaks, and the normal drawn as colour showed them. Each train's
direction was built from the river's local flow, which turns a little
with every metre of a bend. Two kilometres from the map's origin, that
turn times the position times a wavenumber of forty ran the phase through
whole cycles across a metre. On a straight reach the flow is constant and
the chop was right, which is why it had passed.

- The trains now run in fixed world directions and are not carried by the
  flow. The current shows in the rough patches and the edge froth.
- The froth's streaks out in the current are gone. They were a noise
  contour in the same turned frame; in a sound frame they closed into
  loops metres across.
- The cost, with the river filling a 1080p frame: 31.0 ms a frame before
  and 27.1 after, of which everything but the water is 15.3. Eight trains
  for twelve, and 20 reflection steps for 40, each pixel starting its
  march at a different distance so the coarser steps do not band.

The water is still 12 ms of such a frame. S8 has the rest: a cheaper
reflection, and the look of the surface.

## Trees in a mountain's shadow: what does not work (2026-11-15, 12:05)

Report 0011: inside the mountain's shadow a tree casts nothing and stands
on the grass like a cut-out. The sun's term is zero there, so the shadow
map has nothing to dim.

Tried: where `terrain_sun` hides the sun, let the shadow map's answer dim
the ambient instead, as a soft shadow from the bright side of the sky. It
changed 1% of the picture. Drawn as colour, the map reads "shadowed"
across the whole shaded valley floor: the terrain is in the cascades too,
so the map cannot tell a tree's shade from the mountain's. The change was
reverted.

Routes left: a second, trees-only depth in the cascade (one more target),
or a blob of sky occlusion per tree drawn from the vegetation's own
instance list, which also works past the cascades. The canopy map is too
coarse up close (7 m texels paint blots; `terrain.hlsl` already says so).

## The valley walls: fewer, larger buttresses (2026-11-15, 12:17)

Report 0012. From its camera a wall was a row of like rounded cones: the
spurs, 380 m apart and 420 m high. Six sets of the terrain's dials are in
`artifacts/showcase-s6/terrain-shape-candidates.jpg`. The defaults are now
`spur_spacing` 700, `spur_height` 300, `ridge_height` 240 and
`strata_strength` 0.6: a wall is a few large buttresses with gullies
between. `terrain-shape-before-after.jpg` has a run valley from the chase
camera and from above, and the arena's wall, the old shape on the left.

The 26 suites pass, and the autopilot banked valley 1 of seed 3 and went
on into valley 2. The Metal goldens were captured again. Not done: the
surface is still smooth. Cliff bands and rock faces are S6's rock work,
and the rocks of report 0011 with them.

## Bushes of leaf cards, fuller tufts (2026-11-15, 12:27)

Report 0009. A bush was two faceted lumps, a green boulder on the grass.
It is now two clumps of leaf cards over a dark core, built by the same
`card_crown` as the broadleaf's crown; the grass pass already ran the
shader that cuts the cards. A tuft was eight thin blades and read as a
few spikes; it has eight short, wider blades around them as a skirt.
`artifacts/showcase-s7/bush-and-tuft-before-after.jpg` has the report's
view and a lower one, before on the left. The frame costs the same within
0.3 ms (7.9 against 8.2 at 1080p there).

The lawn is still clumps on a painted ground. Cover dense enough to hide
it, in short grass, is S7's ground work.

## Where a frame's time goes (2026-11-15, 12:45)

Report 0008 also marked the frame rate: 37 fps beside the river, and 46
to 68 in the other reports. A paired profile at 1080p, each pass switched
on and off inside one run of a frozen frame, is recorded in
`~/src/gpu-hist/research/dragon-m5/runs/20261115-1730-mac-m5-dragon-pass-ablation-showcase/`.
The Mac was loaded by two workers' builds and renders throughout, so the
plain medians moved between 11 and 26 ms and only the paired differences
count.

| Pass | chase in a run | river filling the view | meadow |
|---|---:|---:|---:|
| water | 0.1 ms | 4.3 ms | 0.3 ms |
| trees | 2.3 | within noise | within noise |
| terrain, near | 0.8 | 1.7 | 1.0 |
| tree and terrain shadows | 0.5 | 0.9 | 1.0 |
| mist | 0.6 | within noise | within noise |
| every listed pass together | 7.8 of 12.0 | | |

- No one pass is the frame except the water when it fills the view, and
  that is 4.3 ms after the trim against about 16 before it.
- With every listed pass off, 4 ms remains at 1080p and 2.8 at 540p. On a
  loaded machine part of that is waiting, not work; it needs a quiet
  machine to split.
- The playtest's frame rates were read while two workers rendered in the
  background. A fair reading needs the workers stopped. With vsync on a
  99 Hz display, a frame over 10.1 ms waits for the next refresh, so 12 ms
  of work shows as 49.5 fps.

## S6, first part: the scanned rock kit in the world (2026-11-15, 13:30)

Report 0011 said "rocks look unnatural". The modern tier now draws the eight
scanned pieces of `assets/props/rockkit/` instead of the six untextured
shapes. `docs/WORLD.md`, "The scanned rock kit", has the design; this is
what was built, measured and left open.

Built:

- `gfx::RockKit` and `shaders/rock.hlsl` / `rock_depth.hlsl`: base colour,
  normal and occlusion-roughness maps, the shared lighting path with
  `ambient_light_at` and `terrain_sky`, three levels of detail by distance,
  24 instanced draws a frame after a CPU cull. The retro tiers keep the old
  rocks; an `--tier sm2` frame was byte-identical to the build before.
- The colour: each piece's measured mean albedo scaled to the palette's
  rock (grey since the wall pass), the terrain's broken-rock drift, moss on
  up-facing parts near the floor, a soil band and contact shade over the
  last metre above the ground.
- `Vegetation::place_kit`: boulder groups, cliff pieces on the steep faces,
  every piece sunk until check points from its LOD2 are under the terrain
  (`test_vegetation` checks every point of every piece: 0 floating).
- Sliders under Terrain > Vegetation > Rocks.

Measured:

- Textures: the 1024 maps, 134 MB uncompressed with mips; the 2048 maps
  would be 537 MB and were not loaded. The kit loads in about 230 ms.
- Cost, `gpu: median` at 1920x1080, three runs each, interleaved, old rocks
  (`DRAGON_NO_ROCK_KIT=1`) against the kit: the report's camera 14.5, 12.9,
  15.0 ms against 13.3, 15.1, 12.3 ms; `--run 3` 16.3, 16.6, 15.5 ms against
  16.2, 17.3, 15.7 ms. Another session shared the GPU, so the spread is
  2 to 3 ms; no difference shows above it.
- Count: about 5,200 pieces in the arena (530 cliff pieces), against 4,900
  old rocks at the same seed.
- Goldens: every difference was a rock on the slopes; the Metal set was
  captured again after merging main.

Pairs, old on the left, in `artifacts/showcase-s6/`: `rocks-report.jpg`
(the report's camera), `rocks-floor.jpg` (a slab on the valley floor),
`rocks-outcrop.jpg` (cliff pieces on a steep face, about 250 m out),
`rocks-run.jpg` (a run valley from the chase camera), `rocks-high.jpg`
(from high up).

Left open:

- Cliff pieces are outcrops, not walls. Each shell's face stands only 2 to
  8 m out of its own rim, so once its foot and rim are under the ground on
  a 45 to 62 degree face, 25 to 55% of it shows. At scale 1.7 they read
  from altitude as grey rock bands; up close on a face they are patches of
  rock a few metres proud. Walls need S6's mesh cliffs.
- Fewer boulders stand on the steep faces than before: a tilted base on a
  steep, curved slope needs more than 60% of the rock's height to bury,
  and such a rock is left out.
- `slab_a` is not placed (its hollow reads as a black cave).
- The contact shade uses one ground plane per piece; on a large cliff piece
  over curved ground the band is wrong by a metre or two in places.
- Per-piece CPU culling is fine at 5,000 pieces; ten times that would want
  the foliage cells.

## A meadow in drifts (2026-11-15, 22:00)

"The ground plane is a flat green sheet" is the first item of the blind
critic's fifth round, and some form of it has led every round
(`CRITIC.md`). The meadow's colour was two greens blended by one noise 180
m across, under the ground scan's grain. From the air nothing sat between
the scan's grain and the valley's width.

`terrain_albedo` (`terrain.hlsl`) now has a second field for the grass,
on the modern tier's near mesh: two value noises at about 50 m and 20 m,
their sample point pushed up to 35 m by the first noise so the edges do
not run with the grid.

- Below 0.30 to 0.46 of the field: lush hollows, the grass darker and
  bluer (0.60, 0.80, 0.56 of its colour), three quarters of the way.
- Above 0.56 to 0.72: drifts gone to seed, 60% toward the dry grass and
  20% brighter.
- Worn tracks, tried and removed the same evening: the ridge of a third
  noise, a line about a metre wide. Whole, they were rings; cut into
  lengths by the drift field, the blind critic's sixth round still read
  them as "paths or stains with no purpose".

The grass on the walls takes the same field, which breaks their sheets
of one green too. `artifacts/meadow-drifts/before-after.jpg` has the
floor from 60 m and from 120 m. The landed view was checked: the blades
are one green over both tones and the seam does not show.

The far mesh (`TERRAIN_LITE`) and the retro tiers do not have it. Every
Metal golden moved, on the grass only, and was captured again; the
Vulkan, D3D12 and D3D11 sets are stale.

Cost at 1080p on the Mac, from separate runs with and without the field:
+0.22 and +0.40 ms looking down on the floor (10.9 to 11.2 ms), and
nothing measurable facing a wall (one pair +1.24 ms, the other -0.06, on
a busy machine). The record is
`~/src/gpu-hist/research/dragon-m5/runs/20261116-0300-mac-m5-dragon-meadow-drifts-cost`.

Still a sheet in other ways: no rocks or shrubs out in the open meadow,
no height in the grass past the blades' range, and the scan's ripple
repeats visibly from 60 m.

## Tripo crags, scree and boulders in the world (2026-11-16, 01:58)

The rock kit has six Tripo pieces since this night (`tools/rockkit.md`,
"Tripo pieces"; Codex built them from the raw files in
`assets/tripo-raw/`). `gfx/rock_piece.h` lists them after the scans, and
`Vegetation::place_kit` places them:

- `boulder_d`, `boulder_e` and `boulder_f` join the boulder groups. A group's
  large one and its small ones now draw from six boulders, not three.
- `tor_a` and `ridge_a` are crags. One candidate stands in each 130 m cell,
  kept at 0.7, on ground with a slope from 0.1 to 0.45, at least 30 m over
  the valley floor and at least two radii from any cliff piece. Each stands
  upright, turns about the vertical, and sinks 12% of its height and then
  until its base outline is under the ground.
- `scree_a` lies where gentler ground (slope 0.04 to 0.3) has a steep face
  15 to 60 m uphill. It leans 80% of the way to the slope.
- Neither crags nor scree go above 370 m. The renderer's snow line is 400 m,
  and loose rock on clean snow read as pasted on.

Every dial is under Terrain > Vegetation > Rocks. The arena at the default
seed has 3,898 kit pieces: 238 cliff pieces, 38 crags, 57 scree cones.
The log names the first of each kind with its position, for a `--cam` on it.

What it is and is not: rock variety on the slopes and the floor. It is not
the valley wall the critic asked for. Codex dropped the three Tripo walls
(cavities, a masonry pattern on one face, flat caps). Tonight's later
batches hold more wall candidates (`tripo-wall-06`, `-08`, `-09` to `-11`).

A long shadow under a crag or a scree cone can show as a dark
parallelogram in a `--cam` view far from the player. The shadow map follows
the player, so a far piece falls in the coarse cascade. With `--sun 60` it
is gone.

The Metal goldens pass without a new capture; the largest tile difference
was 4.1.

## Wall crags in rows (2026-11-20, 18:18)

The valley walls were the open gap after two Tripo spends: long wall
pieces failed three times, and the second spend found that one massive
lopsided crag is the shape Tripo makes well (`MODEL_GENERATION.md`, "The
second spend"). Codex (`gpt-6.2-astra`, medium) built five of them into
the kit from `tripo-crag2-02`, `-06`, `-07`, `-08` and `tripo-wall2-05`
(`tools/rockkit.md`, "Wall crags and more rocks"; the brief is
`tools/showcase/crags-brief.md`). They are `crag_a` to `crag_e`, 47 to
58 m high, 24,000 triangles at most.

`Vegetation::place_kit` stands them in rows, before everything else:

- A row may start in each 300 m cell, kept at 0.7, where the ground is
  steeper than 0.3 (1 minus the normal's y, about 46 degrees), at least
  25 m over the valley floor and under the snow line's 370 m.
- It runs along the contour for 5 to 12 crags, a crag every 0.6 of its
  width, so neighbours overlap. It steps over a gap in the face twice
  and then ends.
- A crag stands with its face (+Z) down the fall line, turned up to 18
  degrees either way, leaning 0.3 of the way back to the slope's normal,
  at 1.2 to 2 times its size (60 to 115 m high). It sinks until its base
  outline is under the ground. On such a face that puts its back half
  inside the slope and leaves its face standing over the ground at its
  foot.
- Cliff shells, crags, scree and boulders keep out of the rows.

Run seed 3 has 78 wall crags in 16 rows; the arena has 115 in 19. The log
names the first with its position and facing, for a `--cam` on it.
`DRAGON_NO_WALL_CRAGS=1` leaves them out, and the dials are under Terrain >
Vegetation > Rocks ("wall crags in rows" and the seven below it).

Three things were wrong on the way, each found on a render:

1. At the kit's own scale, rows of 3 to 7, upright: two rocks on a
   hillside, not a band. Rows are now longer, the crags 1.6 times the
   size, and they lean back.
2. A face 100 m wide on a 1024 map is smooth clay from 90 m. A large
   piece now takes a detail layer (`rock.hlsl`, `RockKit::detail`): the
   terrain's scanned rock on the three world planes, at 4 m and 15 m a
   repeat, its relief deepened 2.6 times, bending the mapped normal and
   carrying its own grain. A piece takes it by its size: none under 14 m
   across, all of it from 44 m. It is gone by 450 m.
3. The detail layer did nothing at first. Its samples sat inside a
   branch, where a sampler has no screen gradients and takes the smallest
   mip. The gradients are now taken outside the branch and handed in.

Cost, `gpu: median` at 1920x1080 on run seed 3, three runs each,
interleaved, with two Blender jobs sharing the Mac. As first built: 20.4,
18.5 and 19.0 ms with the crags against 18.6, 17.0 and 16.3 ms without,
about 2 ms. A level of detail was chosen by distance in the piece's own
radii, so a crag with a radius of 70 m drew its 24,000 triangles out to
1.5 km. The levels now also have limits in metres (320 and 1,000;
`RockKit::lod1_metres`, `lod2_metres`): 17.0, 18.1 and 16.9 ms with the
crags against 17.0, 17.3 and 17.0 ms without. No difference shows above
the spread. The first three pairs were not logged. The second three are
in `~/src/gpu-hist`,
`research/dragon-m5/runs/20261120-2309-mac-m5-dragon-wall-crags-cost`.

The pairs, without the crags on the left, cropped and not scaled:
`artifacts/showcase-walls/crag-rows-before-after.jpg` (the far wall and
the left wall from the chase camera on run seed 3, and a row from 240 m),
and `crag-detail-before-after.jpg` (a face from 90 m without and with the
detail layer). The Metal goldens were captured again: every scene has a
slope in it.

What is weak:

- The rows cover little of a wall 600 m high. They stop at the snow line,
  and one row is one band 60 to 100 m high.
- Two crags that meet at a wide angle leave a dark slot between them.
- Five shapes repeat. From the air two rows can be told as the same set.
- The crags are greyer and darker than the sunlit heightfield behind
  them on a wall that faces the sun.
- A crag has no collision. `Terrain::height_at` reads the heightfield,
  so the dragon flies through a buttress.

## Landmarks, and five more rocks (2026-11-20, 18:32)

The valley had rock, trees and a river, and nothing built or old in it but
the run's two towers. Codex (`gpt-6.2-astra`, high, 477 k tokens, 45
minutes) converted five Tripo landmarks into `assets/props/landmarks/`
(`tools/landmarks.md`; the brief is `tools/showcase/landmarks-brief.md`):
a ruined round tower, a ruined arch, a circle of standing stones, a
monolith and a cairn. It rejected five more after two passes each: the
keep (it lost part of its rear arch), the bridge (its masonry smeared at
250 m), the dragon skeleton (limbs collapsed to sheets), the snag and the
log. Its third try at the hoard pile also failed, and the old pile stays
(`tools/props.md`).

The kit draws the landmarks (`gfx/rock_piece.h`, `landmark`): the same
files, levels of detail and shader, from their own folder and in their own
colours. They are not pulled to the terrain's rock and take none of its
warm and cool drift. `RockKit::landmark_brightness` is 1.2.

`Vegetation::place_kit` stands one in each 450 m cell, kept at 0.8, on the
first of 60 tries in the cell whose ground suits the kind picked for it:

| Landmark | Steepest ground (1 - normal y) | Over the valley floor |
|---|---|---|
| `tower_ruin` | 0.14 | 25 to 280 m |
| `arch_ruin` | 0.12 | 2 to 160 m |
| `stone_circle` | 0.07 | 2 to 130 m |
| `monolith` | 0.20 | 2 to 300 m |
| `cairn` | 0.26 | 50 to 340 m |

The slope is read at the centre and at four points 0.7 of the radius out.
A landmark stands upright at 1.5 to 2.1 times its file's size: the tower
35 to 48 m high, the arch 21 to 29 m, the circle 34 to 47 m across. A
dragon is 12 m long. Trees keep 1.6 radii clear around a landmark and
0.8 of a wall crag's radius (`Vegetation::clearings`). Run seed 3 has 20
landmarks and the arena 17. At 800 m a cell and 16 tries the arena had 5:
of 1,610 tries 1,429 were on ground outside the kind's height band.

Five more Tripo rocks from the same Codex job as the wall crags
(`tools/rockkit.md`): `pinnacle_a`, `outcrop_a` and `outcrop_b` join the
crags, `talus_a` alternates with the scree cone, and `slab_c` is the
second slab. `bank_a`, a riverbank strip, is in the folder and not yet in
the piece table: it needs a placement along the river.

The kit is now 29 pieces and 87 maps at 1024. It loads in 0.9 s (0.23 s
for the first eight). Its maps are not compressed: about 490 MB with mips
by the first eight's 134 MB. That wants block compression before more
pieces go in.

`artifacts/showcase-walls/landmarks.jpg`: the arch, the circle, the tower
and the monolith in run seed 3, cropped and not scaled. The Metal goldens
pass without a new capture; the largest tile difference was 4.9.

What is weak:

- A landmark has no collision, as a crag has none.
- The stone is dark in a mountain's shadow, and the detail layer's grain
  is strong on the arch's blocks.
- A landmark's place is not known to the run. A cache or a tower can
  stand beside one, or inside the circle.
- The tower stands alone on its bench. A keep and a bridge were the
  pieces that would have made a place of it, and both failed conversion.

## Scree and rubble at the feet (2026-11-20, 19:20)

The blind critic's seventh round put the rock kit first: the crags are
"objects set on a slope", "the grass runs right up to them", and the arch
stands on "a smooth slope with no rubble" (`CRITIC.md`). `place_kit` now
puts three things at the feet:

- A talus mound where each wall crag's face meets the ground, and a
  second at 0.7 (`talus_a` or `scree_a`, 1.5 to 3 times its size, leaning
  0.85 of the way to the slope). The foot is the downhill edge of the
  crag's base, which the placement buries to the ground's level.
- Ten boulders under each wall crag, spilled down the fall line, large
  near the face and smaller the further they rolled.
- Ten small blocks about each landmark's base, most of them close in.

Run seed 3 goes from 3,823 kit pieces to 4,904: 133 more mounds and
about 950 blocks. The dials are under Terrain > Vegetation > Rocks
("scree at a wall crag's foot" and the three below it).

`artifacts/showcase-walls/foot-scree-before-after.jpg`: the crag row and
the arch, the critic's frames on the left, cropped and not scaled. The
change is plain at the crags' feet and small at the arch. The Metal
goldens were captured again.

The detail layer now starts at 45 m across and is whole from 75 m (it
was 14 and 44): a talus mound took it and did not need it.

Cost, `gpu: median` at 1920x1080 on run seed 3, three interleaved pairs
against `DRAGON_NO_WALL_CRAGS=1` (which takes the scree and rubble with
the crags): 19.2, 18.5 and 18.0 ms with them against 18.3, 17.5 and
17.5 ms without, 0.8 ms on average (`~/src/gpu-hist`,
`research/dragon-m5/runs/20261121-0049-mac-m5-dragon-wall-crags-scree-cost`).
The Mac was shared, at a load average of 18 to 58. The crags alone were
0.2 ms. An earlier try, under a Codex job's Blender renders at a load
average of 87, wandered by 13 ms between pairs and was dropped.

What is weak: the mounds are heaps beside the crags, not a fan that runs
up their faces. The grass still meets rock at a line between them. A
band of the terrain's own rock colour under each row needs a mask the
terrain shader can read.

## Wild herds (2026-11-20, 19:50)

A run's herds were one animal, the Mossback grazer. Codex
(`gpt-6.2-astra`, high, 418 k tokens, 67 minutes) rigged five Tripo
animals to the grazer's contract (`tools/prey.md`; the brief is
`tools/showcase/prey-brief.md`): an elk, a bighorn ram, a boar, a chamois
and a goat. Each has 28 bones, under 8,900 triangles, a 1024 colour map,
and the three clips `graze`, `walk` and `run` at the grazer's lengths.

The engine fields them on the modern tier (`App::PreyKind`). A herd is
one kind. A valley's herds take the kinds in turn, starting from one its
seed picks, so two valleys do not field the same animals. Each wild kind
is drawn at a size that puts its body where the grazer's is, because the
herd's hit volumes are the grazer's: the elk 2.2 times its file's size
(5.3 m long), the bighorn and the boar 2.6 (4.4 and 4.2 m), the chamois 3
(3.6 m), the goat 2.8 (3.9 m). They are great beasts beside a 12 m
dragon, not their real size. "wild prey size" in the Combat panel's
"prey (live)" scales them. The retro tiers keep the grazer alone.

`artifacts/prey/wild-herds-in-game.jpg`: the elk in its forest, then the
bighorn, the boar, the chamois and the goat with the trees hidden
(`--hide-vegetation`), each frame at half size. Find a herd from the
log's `herd of 6 at x,y,z` and put a `--cam` 20 m from it. The Metal
goldens pass without a new capture: no golden scene has a herd in view.

What is weak:

- Codex's own note: the ram and the chamois fold their forelegs sharply
  in the deepest graze, and some shoulder creases are angular up close.
- The bighorn's run reads as a scramble from above.
- A carcass lies on its side at the bind pose, as the grazer's does.
- Nothing hunts yet: the wolf, the eagle and the raven are raw meshes.
- Not played. The swoop, the bite and the burn on a small animal want
  the playtester.


## Four herds, and every kind in a run (2026-11-20, 23:38)

The playtester asked for different prey in the same run. A valley had two
herds, and a flight often met one. `RunSettings::herds` is 4 now (the
Run panel's "herds"). The herds take the kinds in turn, and each valley
of a run goes on where the last one stopped, so three valleys field all
six kinds. The run's seed picks the first.

## The run's frame at 1080p (2026-11-20, 23:38)

The playtester: "fps also drops down to 30-40 further into run". Their
display is 1920x1080, fullscreen, vsync off, dynamic resolution off (it
is on by default only over 1300 lines; `docs/FIDELITY.md`).

Measured headless at 1920x1080, `--run 3 --autopilot`, 12,000 frames,
on an M5 that Safari and other sessions were loading (load average 20 to
46), so every number is high:

- The GPU's time was 15 to 18 ms for the first 2,400 frames and 20 to
  28 ms from there to frame 9,600. That is 36 to 50 fps.
- Frozen at frame 1500 the frame cost 12.8 ms, at frame 3200 19.0 ms.
- At 1280x720 the frame at 3200 cost 13.2 ms against 20.6: the cost
  follows the pixel count.
- No part explains it alone. Switched off one at a time at frame 3200
  (`DRAGON_ABLATE_ALTERNATE`): the player 1.5 ms, the terrain 1.4,
  water 0.9, grass 0.7, clouds 0.7, mist 0.5, the atmosphere 0.5, the
  shadows 0.7, the breath 0.5, ambient occlusion 0.3, the rock kit 0.02,
  trees 0.03. Together 7 ms. With all of them off the frame fell by
  12.6 ms, to 6.4 ms: what is left is the post stack and the passes with
  no switch.

So the frame is bound by shading over the whole screen, a part hidden is
replaced by the part behind it, and nothing added on 2026-11-20 is the
cause. `rocks` and `rock_shadows` are new `DRAGON_ABLATE` terms.

Not done: the same measurement on a quiet machine, and against the
build of 2026-11-14, when the run's frame was recorded at 11 ms
(`docs/GAPS.md`, "The suspected performance regression"). Until then it
is not known whether the frame grew or the machine was busy. Dynamic
resolution with its 900-line floor would hold 60 fps at 1080p by drawing
the world at 83%; the playtester turned it off once, at 60%, so turning
it on is their call.
