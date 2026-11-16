# Showcase: other showcases, and the blind critic

What three other showcases do that this build did not, and the three
rounds of the blind critic (a subagent that sees renders only) with what
each round changed. Part of the showcase record; `docs/SHOWCASE.md` is the
index.

## What other showcases do that this build does not (2026-11-15, 12:55)

The user pointed at three sources: `../crimson-loong` (two serpent dragons
over a karst lake, a 90-second film in three.js), `../abyssal-garden` (an
underwater garden, WebGL) and `../awesome-fable-5.2` (an index of 288 works,
no prompt text). Three read-only agents studied them. Nothing was built or
run. The costs below are the agents' estimates from reading shader code, not
measurements.

### Licences

- Crimson Loong (github.com/ChetasLua/crimson-loong) has no licence file.
  Its code is all rights reserved. Ideas and constants can be reimplemented
  in our own code. No file, shader text or leaf-drawing routine can be
  copied.
- Abyssal Garden is MIT, "Copyright (c) 2026 Maksymilian Dendura". A port of
  its code needs the notice in `THIRD_PARTY_NOTICES.md`.
- Unsung Vale (github.com/Leonxlnx/unsung-vale) is MIT. Thousand Peaks
  (github.com/0xCodez/thousand-peaks) and Ryu (github.com/seeconever/ryu)
  were read for method only.

### What the engine already has

The check was a grep on 2026-11-15. The engine has the split tone and the
vignette (`post_composite.hlsl`), cloud shadows in every lit shader
(`cloud_shadow` in `scene_common.hlsl`), the terrain's shadow at any distance
(`terrain_sun.hlsl`) and in the air (`atmo_air_shadow`), a foliage wind sway,
a wind-up and a recoil on procedural strikes, ray-marched water reflections,
half-resolution ambient occlusion, adaptive resolution, and TAA.

The engine does not have light shafts, a dither or grain after the grade,
crown-centre normals on leaf cards, a spring answer to gusts, jointed rock,
detail that keeps its size on screen, a number for how much a frame moves,
or a critic that sees only renders.

### Method: the part all three agree on

1. A blind visual critic. Abyssal Garden's prompt requires "a separate
   subagent that sees ONLY the your renders, never your code or reasoning",
   six fixed camera poses plus a fly-through, and "Iterate until it reports
   nothing above 'minor'". Thousand Peaks compares each frame with a
   reference still and writes "the three biggest gaps", for "15+ rounds".
   This build's rounds were judged by the session that wrote the change,
   which knows what the change was meant to do. That is the largest gap in
   method.
2. One key frame approved before the rest (Thousand Peaks). `docs/concept/`
   holds targets, but no round ended with a side-by-side against one.
3. A motion number. Crimson Loong's `motion.py` freezes the camera, renders
   time T and T+DT, shrinks both eight times and reports the share of pixels
   that changed, whole frame and a 3 by 3 grid. A photograph scores 0. It
   turns "stiff" and "dead world" into numbers. `DRAGON_FREEZE_AT` freezes
   the world, not the camera alone, so this needs a small flag.
4. A stated noise floor for image differences (its `imgdiff.py`: the same
   frame captured twice differs by 0 to 0.6 mean, a real change is above 1).
5. Ryu's two lessons: "Stills lied about the render" and "Showing a draft
   to a person found what no agent had flagged".

### Techniques, ranked by what the playtester would see against the work

| # | Technique | Source | Answers |
|---|---|---|---|
| 1 | Light shafts: a half-resolution march through the shadow map, 12 to 34 jittered steps, phase `0.2 + 2 mu^3`, depth-aware upsample; dust particles lit by the same shadow map so a shaft reads as volume | Abyssal Garden; Crimson Loong marches 14 steps at quarter resolution and also samples its cloud field | the flat frame (S9) |
| 2 | Leaf-card normals blended toward the direction out of the crown centre (`0.5 card + 0.62 outward + 0.16 up`, card share 0.40 for a broadleaf and 0.12 for a pine), self-shade baked per spray from the spray area above it, a fill that is warm from below and sky from above, cards seen edge-on faded out | Crimson Loong, Unsung Vale, Harappa | toy-like trees (0009), part of the shade complaint (0011) |
| 3 | Strikes as impulses: draw back over 0.5 s, strike in 0.18 s, a recoil pulse (attack 0.16 s, release 0.38 s), each spine joint delayed by its distance from the head over a whip speed, with a Gaussian reach | Crimson Loong | lazy attacks (S5) |
| 4 | Rock: jointed blocks (Voronoi columns that lean with height, beds of varying thickness with a gentle dip) run once for displacement and again per pixel, crack lines at least a pixel wide, shade under overhangs, stains below them, a cool fill on the shadow side; boulders as noisy icospheres clipped by four planes, power-law sizes, sunk 30% | Crimson Loong, Abyssal Garden, Thousand Peaks | smooth mountains and unnatural rocks (0011, 0012) |
| 5 | Detail grain that keeps its size on screen: pick the octave from the pixel footprint, sample one map at two scales, blend and renormalise | Crimson Loong | terrain that is mush far away and wallpaper close |
| 6 | Plants as damped springs: a gust is a plane front crossing the valley, each plant overshoots, rings and settles (maple 4.3 rad/s damping 0.17, pine 5.2 and 0.20, bush 9 and 0.24, grass 15 and 0.42), leaves show a pale side in the gust, the shadow pass bends too; a dragon's pass is a push in a ring buffer | Crimson Loong | a dead world; the dragon has no effect on it |
| 7 | Head and neck: gaze target smoothed over five past samples, a neck bend that peaks one neck-length back, head turn capped at 62 degrees, idle drift, blinks every 4.7 s, a 1.8% breath swell | Crimson Loong | stiff motion (S4) |
| 8 | Water: a shore distance field jump-flooded once per valley, wavelets and a broken foam line along it, the bed seen through shallow water with per-channel absorption, ripples smaller than a pixel faded out of the reflection, foam drawn in travel-time coordinates along the flow | Crimson Loong | artificial water (0008, 0010) |
| 9 | Grade: a luminance-only S-curve, highlight desaturation, a triangular dither or grain of about 0.009 after the tonemap, weak bloom (6 to 8% mix) | both | banding in the sky, the amateur frame |
| 10 | Clustered planting with clearings, plants on walls, grass never thinner than a pixel | Abyssal Garden, Crimson Loong | S7 ground cover |
| 11 | Camera: look-at as a weighted blend of the player and the target, a framing offset of about 14 by 6 degrees off the subject, handheld drift of 0.004 rad, shake as event pulses | Crimson Loong | the chase camera in combat |

Abyssal Garden also uses one fog function in every shader, with extinction
per channel and a forward-scatter lobe. This engine's atmosphere already does
that job.

### What does not transfer

- Crimson Loong is a film. Its wake, wetness and gust tables come from
  walking both dragons' rails through all 90 seconds at load. A game needs
  ring buffers of recent events.
- Its body is "the last 172 metres of the head's path". That suits a
  serpent. For a winged quadruped the idea survives for the neck and tail
  only.
- Its one 4096 shadow map covers a 300 by 340 metre set, and its planting
  follows two fixed lens positions. A valley is kilometres long and the
  camera is free.
- Its clouds are four fisheye layers marched once. They hold from one
  viewpoint. The volumetric clouds here are the better tool.
- Alpha-to-coverage needs MSAA. This engine resolves with FXAA or TAA, so a
  coverage fraction must be a dither.
- Neither source addresses large terrain form. Their rock is parametric
  pillars and SDF walls. Report 0012 stays a heightfield problem.

### Order

The method comes first because it is cheap and it changes what every later
round is judged by: the blind critic on six fixed views, then the motion
number. Then the techniques in the table's order, with rows 2, 4 and 10
after the rock worker's branch merges, because they touch the same files.

## The blind critic, round 1 (2026-11-15, 13:15)

Eight fixed 1080p views went to an agent that saw the renders and the two
slice targets and nothing else. The views, their flags and the whole report
are in `artifacts/showcase-critic/round1/`. Run the same eight views for
every later round, so rounds compare.

Its verdict: the build is "well short of target-floor", the dragon is "the
best asset in the game", and the frame reads as four styles (a semi-realistic
dragon, low-poly plants, "smooth procedural clay terrain", toy rings and
clouds). Its ten fixes, in its order:

| # | Fix | State |
|---|---|---|
| 1 | Rock and cliff surfaces on the valley walls: "one sandy noise texture", no strata, no gullies with scree | the rock worker's branch, then row 4 of the technique table |
| 2 | The fire frame: a flat orange disc over a third of the picture | not fire: a training target's hit flash, see below |
| 3 | Square edges on the spray | fixed, see below |
| 4 | Ground cover: "sparse spiky blades on a bare green plane" | open (S7) |
| 5 | Trees: low-poly, "lollipop" broadleaves, an even tree line with no clearings | open |
| 6 | Gate rings: the later gates show as grey rings inside the next one, and a near gate is a filled grey disc | open, a design call |
| 7 | White streaks across the sky and slopes, also when landed | they are the thermals' wind lines, see below |
| 8 | Water: no gravel bars, no depth colour, hard grass banks | open (S8, row 8) |
| 9 | HUD markers overlap and clip at the screen edge; the tutorial line reads as debug text | open (S9) |
| 10 | Exposure and haze differ from view to view; shade is too dark and cold | open (S9) |

It also said: from behind, the hero's head is "a shaggy spiky mop with no
readable skull or horns"; the landed wings hang "half-open like a cape"; the
wing membrane close up is "smooth, glossy and plastic"; the lone clouds are
"cotton-ball sprites"; the pebbles on the slopes are stickers with no contact
shadow.

What this session never flagged and the critic saw at once: the spray's
squares, the target's flat disc, the wind lines read as scratches, the gates
dominating four of eight frames, and the label pile-up in combat.

### Fixed in this round

- The spray (`artifacts/showcase-critic/spray-edges-before-after.jpg`). Two
  causes. A strong billow alone carried the smoke's body past the alpha
  threshold at the quad's edge, so the quad's corners showed; the smoke now
  closes by r = 1. And a billboard that crossed the river was cut by the
  depth test along a straight line; a particle now carries `floor`, the
  height of the surface it rises off, and the smoke fades toward it. The
  floor is the drawn plane, which stands 0.5 m over the water line on the
  modern tier. The first try used the water line and changed nothing.
- Report 0011, read again. The mark is on a tree in the mountain's shadow.
  The plants there were warm and bright on dark blue ground. Cause: a
  plant's cards stand upright, so half their ambient is the ground bounce,
  and the bounce was the sun's full colour whether or not the ground there
  was in sun. `ambient_light_at` (scene_common.hlsl) cuts nine tenths of
  the bounce where `ground_sun` says the ground in that column is shaded by
  the terrain or a cloud. Every lit world shader calls it. In the report's
  view the mean changed by 0.4% of full scale and the spruces went from
  olive to blue-green (`artifacts/showcase-critic/shade-bounce-before-after.jpg`).
  It is a moderate change, not the whole answer.
- A training target held in the breath flashed at full emissive, which was
  tuned to be seen at 500 m. Within 40 m the flash is now 0.35 of that. The
  target is still a plain ball. It needs a prop.

### The valley walls: grass with grey rock, not one tan (13:22)

The critic's first fix, and the playtest's report 0012. Pairs:
`artifacts/showcase-critic/walls-{1-chase,5-mountains,8-run}-before-after.jpg`.

A render of the shader's masks as colours showed the cause. Scree covered
nearly the whole wall. The erosion's thermal passes move material wherever a
slope is over the talus angle, and at `talus` 0.9 (42 degrees) that was every
wall, so every wall slid to one even slope and the shader painted all of it
with the scree colour, a pale tan, at 85%. Rock by height added 60% rock
above 200 m whatever the slope. The result was one tan from the valley floor
to the snow.

Four changes, the first three on the modern tier only:

- Scree shows where the talus layer is over 0.3 of its scale (was 0.08), and
  its colour is the rock's with 18% sand (was 35%).
- Rock by height starts at 0.7 of the snow line and counts for 0.35 (was 0.5
  and 0.6). `rock_slope` is 0.48, rock from about 50 degrees (was 0.62).
  The grass takes 35% of the dry colour as it climbs.
- The rock palette is grey, 0.36 0.35 0.345 and 0.19 0.19 0.195 (was the
  shared brown-grey 0.26 0.23 0.20 and 0.15 0.13 0.12). Set in `App::init`
  beside the modern tier's sun, so the retro tiers keep the table's.
- `talus` is 1.5 (56 degrees) on every tier. Clefts and faces stay steeper
  than the fans under them. Every valley's shape changes a little.

What it does not do: the walls are still smooth. There is no cliff band, no
jointing and no ledge anywhere, because a heightfield at 6 m cells over a
12 m erosion grid cannot hold one. That is the rock kit's job (mesh cliffs on
the steep bands) and row 4 of the technique table.

### The eye opens in shade (13:31)

The critic's frame 6, a meadow in a mountain's shadow, was "a dark green-navy
murk", a blocker. One exposure served the whole valley, so a view in shade
sat at about a third of the sunlit level. `App::update_shade_exposure` casts
rays to the sun from the camera and from six points on the ground ahead of
it, against the terrain, and smooths the shaded share over a second. From a
quarter in shade the exposure rises, to `shade lift` stops (1.0, the Grade
panel, `--grade shade_lift=`) by 85%. A sunlit view is unchanged: seven of
the eight goldens did not move. The morning valley of a run is 88% in shade
and gains 0.97 stops (`artifacts/showcase-critic/shade-exposure-before-after.jpg`).

It is measured by place and not from the picture. A histogram would also
answer to fire and snow, and it needs a mip chain or a readback.

### The claws on the wing (13:31)

The critic saw "bead-like spheres on the bone" of the wing. They are the
thumb claws. The horn coat reflected the sky at the full Fresnel, so a claw
seen edge-on against the sun's haze took all of it and read as glass. The
horn's grazing reflection now stops at 0.46, what a surface of its roughness
returns. The cornea keeps the full term. The change is small
(`artifacts/showcase-critic/horn-coat-before-after.jpg`): the claws are
darker and still pale, because their texture is pale.

### World labels keep clear of each other (13:36)

The critic: a hoard's label "overlaps another label and is garbled", three
ranges on one spot in combat, a marker's name "clipped at the right edge".
`ui::Hud::world_label` keeps a label on the screen and moves it down a line
at a time until it is clear of every world label placed before it in the
frame. The run's markers, the herd's, and a target's range and status go
through it (`artifacts/showcase-critic/hud-labels-before-after.jpg`). The
first placed keeps its spot: combat targets, then the run's markers.

Not done: the tutorial line under the run's top bar still reads as debug
text, and the bottom plate still covers the dragon's hind legs in the chase
view.

### The sward: short grass edge to edge near the camera (13:43)

The critic's ground-cover blocker: "sparse, spiky low-poly blades and paper
bushes spaced out on bare flat green". The tufts are clumps 1.5 m tall on a
2.2 m grid, and between them was the terrain's texture alone.
`GrassKind::Sward` is a patch of 200 one-triangle blades, 16 to 60 cm tall,
over a square 2.6 m across. `Vegetation::grass_around` sets one a cell on a
1.7 m grid of its own within 46 m of the camera, where the ground is grass
and above the gravel line. The blades take the terrain's own grass colour
from the palette, and each patch takes the terrain's dry-patch noise as its
warm push, so a patch has no outline on the ground under it. The modern tier
only. Pair: `artifacts/showcase-critic/sward-before-after.jpg`.

- The first blades were 20 to 75 cm, up to 6.5 cm wide, laid over by up to
  half their height, with tips at 1.75 of the ground's colour. They read as
  cut straw. Upright, under 4 cm wide and tips at 1.3 read as turf.
- Cost, paired on one frozen frame at 1080p (`DRAGON_ABLATE_ALTERNATE=sward`):
  +0.64 and +0.83 ms GPU in the landed view, +0.78 ms in the meadow view.
  The record is in gpu-hist, `20261115-1844-mac-m5-dragon-sward-cost`. Two
  single runs before it had read 10.3 and 11.7 ms; the pair is the number.
  About 2,300 patches, 460,000 triangles. None from higher than 46 m over
  the ground, so flight pays nothing.
- The motion number of the meadow view (`tools/showcase/motion.py`) went
  from 9.5% to 17.6% of the frame in a second.
- Open: the sward stops at a visible edge at its radius, and the tufts above
  it are still the large pale clumps.

### Crowns lit as masses, and no more lollipops (13:49)

The critic on the trees: "low-poly", "a lollipop or palm shape on a bare
pole". Three changes in `gfx/foliage.cpp`, one in `foliage.hlsl`
(`artifacts/showcase-critic/crowns-before-after.jpg`):

- A crown's card is lit by `crown_normal`: half its own face, half the
  direction out of its crown, a little up. The shader no longer turns a
  crown card's normal toward the viewer. Before, each card took the sun at
  its own angle, and a crown was a pile of bright and dark cards. Now a
  crown has a lit side, a shaded side and a dark underside.
- A card's brightness falls toward the underside and the middle of its
  crown (0.8 to 1.12 by height, 0.9 to 1.02 by depth). The first try ran
  0.68 to 1.0 with the normal at 0.62 outward, and the whole forest went a
  murky olive.
- The broadleaf's crown is four lobes, one at the end of each limb (45
  cards, was 34 on one ellipsoid). The pine's is three flat boughs at
  different heights (28 cards, was 22 on one disc).

The trees' paired cost in the run view did not move: +2.19 and +2.14 ms
(`DRAGON_ABLATE_ALTERNATE=trees`, two runs, logs not kept), against 2.3 ms
before.

The forest is darker than it was on its shaded side. That is the point of
the change, and it is also the first thing a playtester may dislike.

### Tried and dropped: more haze

`--atmosphere aerial=7.5,haze=0.8` against the defaults (5 and 1.2), for the
critic's "no aerial perspective" in the chase view. In two valleys of a run
the difference was a slightly bluer far wall. Not plain, so not kept.

### Shade under the crowns, from the sky (14:59)

Report 0011's mark is on a tree in the mountain's shadow with nothing under
it. No tree casts a shadow there, so it stood on evenly lit ground and
floated. `Vegetation::build_crown_shade` splats each tree onto a 2048 grid
over the playable extent (2.4 m a texel): flat across 1.6 crown radii,
falling off to the edge, 0.9 deep, overlapping crowns multiplied. The
terrain shader reads it as a texture (slot 10) within 600 m and takes 70% of
the ambient and 50% of the sun at its centre. The grass and the sward read
the same grid on the CPU at their roots and lose up to 40% of their colour.
`DRAGON_ABLATE=crown_shade` turns it off. Pair:
`artifacts/showcase-critic/crown-shade-before-after.jpg`.

What went wrong on the way, each of which rendered as "no change":

- The first version took the sky's share only. The report's ground is in
  the shadow's penumbra and still grazed by the sun, so a cut in the ambient
  alone moved it by 10 levels of 255.
- The falloff was squared, dark only at the trunk, which the crown hides
  from any view above it.
- `core::smoothstep(1.0, 0.3, x)` returns a step, not a falling curve: the
  edges must ascend. That zeroed the whole grid for one build.

A debug render of the grid as a colour settled the first two in one look.

The bark was a warm brown, 0.20 0.14 0.09, that the low sun turned red (the
critic: "the tree trunks at the left are saturated red"). On the modern tier
it is 0.15 0.125 0.105, set beside the rock's in `App::init`
(`artifacts/showcase-critic/bark-before-after.jpg`).

### Not changed, because they are design calls

- The white streaks are the thermals' wind lines, made a metre wide on
  purpose so a pilot can pick a column from 400 m. To a viewer who does not
  know that, they are scratches on the picture.
- The course's gates are in every arena frame. The later gates draw grey
  through the next one.

## The blind critic, round 2 (2026-11-15, 15:05)

The same eight views on commit e0d707d, to a critic that had not seen round
1, with a score a frame: 5 is the floor target, 10 the ceiling. Views and
report: `artifacts/showcase-critic/round2/`.

| Frame | Score |
|---|---|
| 1-chase | 3 |
| 2-hero-close | 3.5 |
| 3-landed | 2 |
| 4-river | 2 |
| 5-mountains | 1.5 |
| 6-forest | 1 |
| 7-fire | 1.5 |
| 8-run | 3.5 |

No frame reaches the floor. Round 1 gave no scores, so these are the
baseline and not a measure of this round's changes. What it no longer
reports: square edges on the spray, plants glowing in shade, the labels in
the run view as "garbled", the trunks as red. What it still reports, in its
order: no cliff structure on the walls and the kit's rocks as "stickers";
no blue shift on the far ranges outside the run view; trees and bushes in a
different art style; the valley floor a flat green plane from the air; the
water without visible reflection or shallows; the gates; the particles; the
wind lines; shade; labels over the dragon.

It read the sward as "stubble" and the tall tufts as "sprinkled evenly at one
spacing": the sward is a gain it did not credit, and the tufts on their grid
are the next thing wrong with the meadow.

One reading was wrong and useful. The "grey fill inside the ring" in frame 5
is a cloud behind the ring. A cloud at that range is a flat grey shape with
no structure, which is the real defect.

### Fixed after round 2

- The bushes, "the worst object in the set": cards over a solid dark core,
  which showed between them as black holes, and every card lit flat, because
  `finish_grass` recomputed the bush's normals and pulled them upright. The
  bush is now three clumps of leaf cards, the inner one dark, and keeps the
  crown's normals (`artifacts/showcase-critic/bushes-before-after.jpg`).
- The spray, "opaque cotton wool" over the dragon's tail. Smoke has a second
  shape, mist (`gfx::Particle::soft`, carried in the fraction of the kind):
  dense in the middle, gone by the rim, no edge. The spray uses it at 0.55
  to 0.95 m and throws a second, lower drop a puff
  (`artifacts/showcase-critic/spray-mist-before-after.jpg`). Half the puffs
  stayed hard for three renders. They came from a second emitter: the
  touchdown ring, which fires when the skimming dragon brushes the water. A
  count of hard and soft smoke in the pool, with each group's brightness,
  named it in one run.

### Cliff bands on the walls (15:25)

The critic's first item in both rounds: "no cliff structure". A wall was one
even slope from the floor to the crest, and the rock on it was a tint.

Tried first and dropped: hard and soft beds inside the erosion. A hard bed
eroded at 0.3 times the rate and held a talus angle of 3.6 against 1.5. Four
variants of the bed's thickness and hardness showed almost nothing in the
mountains view. Erosion only removes material along its channels, and a
face between two gullies never meets it.

What was built: the finished height is remapped (`Terrain::cliff_bands`,
after the erosion). Each 110 m of height is one bed. Its top share is hard
and stands as a cliff. The rest is soft and lies back as a bench at a
quarter of the wall's own slope. The cliff takes up the height the bench
gave away, so the function is continuous and rising and a wall keeps its
height. In numbers, on a 40 degree wall: benches at 12 degrees, cliffs at 60
to 78 degrees and 86 to 94 m tall, and the ground lowered by up to 71 m.
The dials are `cliff_period`, `cliff_share`, `cliff_bench` and
`cliff_strength` in `TerrainSettings`, the Terrain panel's four "cliff"
sliders, and `--terrain cliffs=,cliff_share=,cliff_bench=,cliff_strength=`
(`cliffs=0` is the wall as it was).

Three things kept it from reading as a staircase:

- The beds dip and wander. The bed's height is offset by a plane (5 m in
  100 east, 3 m in 100 north), a broad noise (70 m over 900 m) and a ripple
  (26 m over 240 m), so a band climbs and falls along a wall.
- Each bed has its own hard share, half to one and a half times the dial,
  from an integer hash of its number. A sine hash differs in its last bits
  between machines, and the same seed must build the same valley on each.
- Broad patches keep 80 to 100% of the strength. At a floor of 35% the
  mountains view showed no bands at all, and at 60% soft ones.

The crests needed a fourth. A remap of height flattens any summit that
falls in the soft part of its bed, and every crest of the run's first
valley became a mesa. The erosion grid now carries how far each point lies
below the highest ground within 150 m, and the bands fade in over the first
110 m of that (`ErosionGrid::cliff_keep`). Past the grid, where no crest is
known, the ground keeps 60% of the bands.

The material followed without a change: rock by slope puts rock on the
cliffs and grass on the benches, and the trees found the benches.

The pairs: `artifacts/showcase-critic/cliffs-5-mountains-before-after.jpg`,
`cliffs-1-chase-before-after.jpg` and `cliffs-8-run-before-after.jpg`.

Two suites failed with the bands in, and both were real:

- The ten-minute dogfight (`test_bot`) ended on a face at 48 s. A cliff 94 m
  tall, first seen three seconds out at 52 m/s, is more than a bot climbs.
  `Terrain::ground_ahead` now also looks four and five seconds out and 240
  and 300 m ahead, less the 20 m a second a flier climbs on the way, so a
  low rise far off does not make a pilot shy of a slope.
- With that it ended at 509 s instead. The bot hung at 20 m/s with the stick
  back for eight seconds and drifted onto a 56 degree wall: its ground
  recovery climbs straight ahead, which is a climb at the face. The pilot
  now takes `Terrain::downhill` (the broad slope over 60 m each way), and on
  ground steeper than 27 degrees the recovery turns that way, all the way by
  45 degrees. Zero grounded frames after it.
- `test_vegetation` holds the mesh within a limit of the analytic height.
  The limit went from 6 m to 10 m: a bench meets a cliff at a corner, and a
  6 m cell cuts it by up to 7.1 m.

Two soaks with the bands in: `--bots 3 --match --autopilot --frames 7200`
ended with no grounding, 3 kills and 5 bites swung, and `--run 7 --autopilot
--frames 8700` banked the first valley at 112 s and lost the second in a
fight 370 m over the ground. Neither pilot touched a wall.

### A braided river (15:45)

The critic on the river frame: "a flat navy plane with tiny sparkles", "no
colour change toward the shallows", "a hard edge where grass meets water,
with no gravel". The slice target's river is blue-green water winding
between pale gravel bars.

The river was a canal: one channel 52 m wide and 7 m deep from head to
pass, so the water was 80 m across at the line and near black everywhere.
Now (`TerrainSettings::river_braid`, `--terrain braid=0` for the canal):

- The canal's footprint is a gravel bed, 104 m wide, 0.3 to 1.5 m over the
  water, with low swells in it.
- The water runs in it as a main thread, 40 to 55 m wide, that swings from
  bank to bank every 560 m. Its bed is 1.3 m under the water on the riffles
  and 5 m in the pools.
- A second thread, 18 m wide and under 1.4 m deep, is there for a few
  hundred metres at a time. Where the two part there is a bar between them.
- `Terrain::river_thread_x` is the middle of the main thread. The skim spawn
  and the reed test use it: the bed's centre line is not always wet.
- On the gravel one plant in five grows, a reed. Trees, rocks and the sward
  already kept off ground that low.
- A grid coarser than 8 m keeps the canal (`App::tier_terrain`): it draws
  threads this narrow as a string of ponds.

The modern tier's colours changed with it. Its gravel is pale grey cobble
(`Sand` 0.50, 0.49, 0.46) where the table's is a beach's sand. Its water is
blue-green (`WaterDeep` 0.03, 0.12, 0.15), and the light through it loses
its red in the first metre (`exp(-depth * (1.5, 0.5, 0.42))`), so a riffle
over pale gravel is turquoise and a pool is the water's own colour. The
terrain shader keeps a bar gravel to its top: with grass blending in from
0.3 m by the patch noise, every bar was blotched.

Two things tried on the way. A thread 32 m wide in an 88 m bed read as
puddles on a gravel flat from skim height. And 0.9 m of relief over 19 m on
the bars threw a blotch of shadow from every swell under the 19 degree sun.
The darker patches that remain on a bar are the bank trees' shadows.

The pairs: `artifacts/showcase-critic/river-above-before-after.jpg` and
`river-chase-before-after.jpg`, and the skim view as it is now,
`river-skim-after.jpg`.

The demo pilot on both grounds, 7.5 minutes each from one seed
(`--demo --seed 11 --frames 27000`, and the same with `--terrain
cliffs=0,braid=0`): two runs each, three of the four lost to rivals in the
second valley and one, on the new ground, in the first. No run ended on a
wall or in the river.

Not done: no froth line where a riffle breaks, the gravel has no cobbles at
close range, and the spray reads as round balls of light.

### The ward stone (16:00)

The flying sentinel and the training target were a plain sphere 7 m in
radius, a placeholder since combat was first built. In the fire frame it
filled a third of the picture, and the critic scored that frame "without
the placeholder ball". The design call was listed for the user; with
"continue autonomously" it was taken here, and it is one prop to swap back.

`assets/props/ward_stone.glb` (`tools/props.md`, built by
`tools/build_props.py`; gpt-6-astra on the work account, medium, 76 k
tokens, one unattended pass in a worktree): seven basalt shards with rune
grooves, held by two studded iron bands around an empty socket, a long
shard down as a keel. 3,108 triangles. The socket is clear to 4.38 m, and
52% of rays from its centre leave between the shards.

In the engine (`App::ward_stone_matrix` and the sentinel loop of the world
pass):

- The stone turns about its own axis, 20 degrees a second, each from its
  own angle, and casts a shadow.
- The core in the socket is the health readout the inner sphere was: it
  shrinks with damage. It takes the sentinel's element colour, and it is
  what flashes on a hit. The cage warms on a hit and rimes when frozen, as
  a tower does.
- An unlit mesh now has a hot middle and a limb in its own colour
  (`mesh.hlsl`: 0.45 at the limb to 2.1 facing the eye, a mean near one).
  One flat colour across a sphere was a paper disc. This reaches the
  brazier fires and the fireballs as well.
- At 2.6 times its colour the core threw two vertical pillars of glare
  beside the stone. It is 1.7.

The pair, from 26 m: `artifacts/showcase-critic/ward-stone-before-after.jpg`.
`--training` now logs where its line starts, for a `--cam` on it.

### The breath has a core and a fringe (16:05)

The critic on the fire frame: "a grey-white jet like steam from a hose. It
has no orange core, no heat glow". Two things were wrong, and the first was
the frame. `--attack` holds the boost, the bite and the fireballs with the
breath: the grey jet is the boost's trail, the dragon is 90 m ahead of the
camera, and the flame is under four reticles. `--breathe` holds the breath
alone, and `--breathe --inspect 70 26 6` is the view the breath is judged
from now.

The second was the flame. Seen alone it was one cream from the jaws to the
tip, (255, 215, 142) at the jaws and half way down the jet alike, with
mustard blotches. The
shader multiplied the breath's colour by up to six where it was hot. The
tonemap keeps a bright colour's hue and scales its peak channel back to
one, so every hot pixel came out as the same colour and no pixel as a
brighter one. Brightness cannot shape a flame in this pipeline. Hue and
darkness can.

So the flame is three colours by its heat, each from the breath's own
(`particles.hlsl`, the modern branch):

- The fringe: the hue made deeper (each channel over the peak, to the power
  2.2) and dim enough to stay under the tonemap's ceiling.
- The body: the hue a little deeper (power 1.3), at full strength.
- The core: 42% of the way to white. Only near the jaws: it fades out by
  0.42 of a puff's life, and from 0.3 of it the puff turns to the fringe's
  colour. With pale cores all along it the jet was a camouflage pattern.

It is from the breath's own colour, so frost has a white core and a deep
blue fringe and blight a pale one and a dark green, without a table. The
bloom reaches the flame as before: at the default 0.35 its halo is slight.

The pairs, fire, frost and blight: `artifacts/showcase-critic/
breath-before-after.jpg`. From the chase camera:
`breath-chase-after.jpg`.

Not done: the flame lights nothing around it that a capture shows, it has
no smoke at its tip, and the SM2 and fixed-function flames are as they
were.

### Spray as wisps and dashes (16:10)

On the new water the spray read as round balls of light. Two causes. The
mist puff was a round falloff that the billow only shaded, so each was a
disc with a bright middle. It is now `pow(core, 1.5) * (0.1 + 1.5 *
smoothstep(0.3, 0.8, billow))`: the billow decides where there is any, and
a puff is a few shreds. And the drops were soft discs. They are streaks now,
0.028 s of their own flight long (a 12 m/s drop is a 0.34 m dash) and a
third as wide. The pair: `artifacts/showcase-critic/
spray-wisps-before-after.jpg`.

## The blind critic, round 3 (2026-11-15, 16:15)

The same eight views on commit 55300c8, and a ninth for the breath
(`--frames 90 --breathe --inspect 70 26 6 --hide-ui`). The renders and the
report as written are in `artifacts/showcase-critic/round3/`. The critic
did not see rounds 1 and 2.

| Frame | Round 2 | Round 3 |
|---|---|---|
| 1-chase | 3 | 3.5 |
| 2-hero-close | 3.5 | 3.5 |
| 3-landed | 2 | 2.5 |
| 4-river | 2 | 3 |
| 5-mountains | 1.5 | 2.5 |
| 6-forest | 1 | 2.5 |
| 7-fire | 1.5 | 2 |
| 8-run | 3.5 | 3.5 |
| 9-breath | | 3 |

The eight shared frames average 2.25 and then 2.88. No frame reaches 5. A
different agent reads each round, and how far two readers differ on one
frame is unknown, so half a point is not a result. The four frames that
moved a point or more are the ones this round's work was in: the river,
the mountains, the forest view (its bushes and its foreground), the fire.

What the critic says now, in its order:

1. Rock and cliff surfaces, still first. The walls have structure now and
   the critic names it: "stepped terraces", "the most convincing wall in the
   set" (frame 8). But "a row of identical stepped battlements" (1), "a
   stepped pyramid" (4), and in frame 5 "melted wax". Two gaps: the steps
   are too regular, and a cliff's face is a smooth smudge with no crags,
   cracks or scree.
2. Trees and ground cover: card crowns of one height, "paper" bushes, single
   grass cards.
3. No aerial haze between the near forest and the far wall (1, 7, 8), and
   too much milky wash toward the sun (2).
4. Marker clutter in combat: brackets, rings and labels stacked on one
   target (7).
5. Fire and spray: "cel-shaded" blobs with hard edges, no smoke, no glow on
   the dragon or the ground. The spray is "cotton wool and rain".
6. The ward stone up close: "flat untextured brown polygons".
7. The dragon back-lit and dark from the chase camera, and three wing
   poses: the far wing's root in the glide (2), the fold on the ground (3),
   the near wing over the jaw while it breathes (9).
8. Gravel "a grey noise smear with no stones", water with no reflection.
9. A course ring cutting a frame that is not about the course (3, 5).
10. The valley's end seen as "the end of the map" (2).

Read against the work: the cliffs and the river moved their frames and are
not finished. The breath went from "steam" to "cel-shaded", which is the
puffs' hard alpha edge and was there before the colours. The ward stone's
lift (a tint of 1.35, as the towers have) washes its dark basalt to brown
at 15 m.

### After round 3: unlike beds, and a face with strata (16:20)

The critic's first item again, in two parts: the steps are "a row of
identical stepped battlements", and a face is "smooth and smudged".

The steps (`Terrain::cliff_bands`):

- The beds are of unlike thickness. The count of beds runs fast and slow by
  turns up the wall (`t = e + 0.3 sin(2.3 e + 1.7)`, which only rises), so
  one bed is a third as thick as the dial and another five thirds.
- One bed in four has no cliff. The wall runs through it at its own slope,
  and the bands either side of it are that much further apart.

The face (`cliff_face` in `terrain.hlsl`, modern tier, not at the Terrain
setting's Low): on the mesh's own normal from 55 degrees, to 6 km, on the
two upright planes.

- Strata 2 to 8 m thick, each its own grey, a little wavy.
- Streaks 11 m wide that run down the face, as water stains do.
- Seams: a dark line every 13 m or so along the face, pushed about by a
  slow noise, each present for a stretch of the face.

Three versions of the face were wrong first, and each is a trap:

- Beds cut into blocks at joints, each block its own tone: a masonry wall
  on every face. With the benches between them the mountain was a ziggurat.
- Cracks as the level lines of a stretched noise: they close into loops,
  scribbles on every near face. The river's froth lines did the same
  (feedback report 0010). A line that must not close is a coordinate plus a
  push too gentle to turn it back.
- The same lines at 0.3 m with a hard edge: pen strokes. They are 1.3 m
  wide with a soft edge and take 30% of the light, not 50%.

The pairs against round 3's renders: `artifacts/showcase-critic/
cliff-faces-run-before-after.jpg`, `cliff-faces-river-before-after.jpg`
and `cliff-faces-near-before-after.jpg`. The face's GPU cost is not
measured: a pair of runs differs by more than it can cost
(`gpu-hist`, `20261115-2112-mac-m5-dragon-terrain-shape-cost`).

### After round 3: quiet marks, a darker ward stone (16:30)

- A mark with no role, within 150 frame units of the locked target on the
  screen, is a bracket and nothing more: no range, no status word. In the
  training line six targets wrote over one another, "340 m", "92 m", "241
  m" and "BURNING" three times; the fire frame now has two ranges and two
  status words. `T` moves the lock and the words with it. A run's rivals,
  towers and hunters have a role and keep their words.
- The ward stone has no lift inside 60 m (a tint of 0.85) and the towers'
  1.35 from 400 m. Lifted at every distance its basalt was a flat brown at
  15 m.

The pair: `artifacts/showcase-critic/marks-quiet-before-after.jpg`.

### After round 3: more air, less aerosol (16:25)

Three rounds said it: "no blue shift on the far ranges", "the depth reads
flat", and toward the sun "a milky haze wash". A wall 3 km off had the
contrast and the colour of one at 300 m.

`AtmosphereSettings::aerial_scale` is 11 (was 5) and `mie` is 0.55 (was
1.2): more than twice the air between the eye and a far wall, with under
half the aerosol in it. The far wall goes blue and pale, a shaded wall
goes blue-grey, and the veil toward the sun thins. The sky is a deeper
blue. `--atmosphere aerial=5,haze=1.2` is the air as it was.

In round 1 `aerial=7.5,haze=0.8` was tried and dropped as too subtle to
see. It was. This is twice that step, and it is plain in every view with a
far wall: `artifacts/showcase-critic/air-run-before-after.jpg` and
`air-hero-before-after.jpg`. Checked too in the landed view, the shaded
meadow and a run's evening valley.

The cost: a sunlit far wall is less warm and less contrasty than it was.
That is the trade the target makes.

### After round 3: fine tufts, reeds at the water (16:35)

The critic on the landed frame: "large flat grass cards", "hard polygon
edges", "a low-poly asset pack". The tuft was sixteen straight blades 9 and
13 cm wide, pale against the lawn.

- The modern tier's tuft (`make_grass_mesh(kind, fine)`) is 26 thin blades
  from a disc 0.6 m across, each bent over at the middle, 0.35 to 1.25 m
  tall and 2 to 3.4 cm wide at the root, in the ground's green. 78
  triangles where there were 16. The retro tiers keep the broad one.
- Reeds grew within 14 m of the water's height. Beside the canal's long
  banks that was the waterside. A braided river's floor starts 3 m up, and
  14 m took in the whole meadow: one plant in nine there was a tall yellow
  reed. Within 5 m now, where the river is braided.

The pairs against round 3's renders: `artifacts/showcase-critic/
tufts-landed-before-after.jpg` and `tufts-meadow-before-after.jpg`.

Cost: all grass is 1.59 and 1.83 ms of the landed frame with the fine
tuft, 1.47 and 2.00 ms with the broad one, paired. No cost shows
(`gpu-hist`, `20261115-2132-mac-m5-dragon-fine-tuft-cost`).

Not done: the bushes are still faceted, and nothing grows in drifts or
flowers.

### After round 3, tried and dropped: a softer flame (16:38)

The critic on the breath: "flat cel-shaded orange and cream blobs with hard
edges". Two changes to `particles.hlsl` were rendered and neither is kept.

- The puff's cover coming in over 0.38 of its density, not 0.12. No plain
  difference: the outline is the fringe's dark colour, not the alpha's
  edge.
- The body as a gradient under the tonemap's ceiling (0.35 to 2.0 times
  the hue), a fringe that is thin and not dark, the core from a lower
  heat. The outlines went, and so did the orange: the jet was pale cream
  again, because the cores of forty stacked puffs sum and the grade is
  20% per-channel.

What this leaves for the next attempt: the flame's look is decided by how
stacked puffs sum, more than by one puff's colours. A test of one puff
alone, then five, then forty, would show where the orange is lost. The
smoke at the flame's tip exists (`smoke_for`) and was out of the critic's
frame; `--breathe --inspect 100 60 8` shows it.

## The blind critic, round 4 (2026-11-15, 20:25)

Twelve new views about the dragon in motion and the fire, on 45aa30e. The
abridged report and the frames are in
`artifacts/showcase-critic/round4/`. The mean is 4.1 and no frame reaches
5; the views are new, so this does not compare with round 3's 2.88.

What it ranks first:

1. The wing membranes: "flat, opaque boards with straight rod bones and
   no sag, translucency or bend". The critic calls this "the main reason
   the dragon looks like a model and not an animal". It outranks every
   pose and timing change of the day.
2. The fire: "drawn, not hot". A hard outline (softened after the
   renders), a dither pattern in the pale core, no light on what is
   around it, a jet eight times the dragon's length, a fireball that is a
   smear and not a ball, and a jet that leaves the mouth in a turn.
3. The light: a flat haze, no low warm key, no rim, no strong shadows.

The world items (floor, walls, forest, water) repeat rounds 1 to 3.

