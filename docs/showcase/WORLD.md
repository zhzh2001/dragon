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
