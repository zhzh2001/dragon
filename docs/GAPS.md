# Gaps: the build against other dragon and flight games

Written 2026-10-21. This file compares the modern build and the retro tiers
against Century: Age of Ashes, other dragon and creature-flight games, and
period games on the retro tiers' hardware. It ranks the gaps in the picture,
in feel and in play, and says which fixes reach D3D9 and the fixed-function
tier. `FIDELITY.md` stays the record of the modern techniques and their
phases. `DIRECTION.md` stays the plan for the game's shape. This file feeds
both.

Method:

- Our frames came from the current `main` (`efe3468`), headless, at each
  tier's default preset, on the M5.
- The targets are new. `concept/valley-target.png` was a test of the
  concept-art skill, not a direction, so it no longer sets the bar. Five
  new targets are in `concept/gaps/` (below).
- Research came from reviews, developer talks, wikis, Steam threads and
  GPU Gems-era papers. The sources are listed at the end. A claim marked
  "(inferred)" is judgment, not a source.
- No other game's screenshot is in this repository. The comparisons are
  against our own generated targets. The two period-style prompts named
  Far Cry, Half-Life 2, Oblivion, Drakan and Panzer Dragoon as a style;
  the images contain no character, logo or asset from them.

## The targets

| File | Backend | What it sets |
|---|---|---|
| `concept/gaps/run-target.png` | GPT Image 2 | The run's chase view: cliff bands, gullies, scree fans, ranges in haze behind the pass, a braided river with gravel bars, dark forest masses |
| `concept/gaps/breath-target.png` | GPT Image 2 | The breath: a white core at the jaws, turbulent orange flame, grey smoke at the far end, the fire lighting the dragon's own head and wings |
| `concept/gaps/landing-target.png` | GPT Image 2 | Weight: the downwash flattens a ring of grass, dust and pebbles fly, the legs flex |
| `concept/gaps/retro-sm2-target.png` | Nano Banana 2 | A 2004 D3D9 frame: normal-mapped dragon, rock by slope, baked ridge shadows, a painted cloudy sky, layered haze |
| `concept/gaps/retro-ff-target.png` | Nano Banana 2 | A 2000 DX7 frame: low-polygon dragon, a base texture times a detail texture, crossed-quad trees, a painted sky dome, a blob shadow |

The pairs, our frame on the left, are in `artifacts/gap-study/`.

## 1. The picture

Read off the pairs, worst first. Each row says which tiers the fix reaches.

### 1.1 The mountains are the biggest gap, on every tier

Our walls are broad ramps. The generator lifts 900 m of ridged noise on a
2.3 km scale over a 950 m falloff from the valley floor
(`game/terrain.h`). The ridge octave is 60 m, and erosion combs the faces
into parallel gullies. From the chase camera the result reads as smooth
cones with stripes. Every target has cliff bands, sharp crests, scree fans
under the cliffs and more ranges behind the pass.

This is not a shader gap. The period-style targets are generated images,
not real games, but they show what real games of that era did (Drakan,
Far Cry, Oblivion): their mountains are sharper and rockier than our modern
frame. They get that from three things, and all three are content:

1. Relief at 50-300 m: crests, spurs and steps in the faces. That is the
   generator's job (inferred: a stronger ridge octave, and terracing that
   survives erosion on slopes above about 40 degrees).
2. Rock coloured by slope. The modern tier has scanned rock to 2.5 km
   (F3), and past that only the crags' normals and grey blotches. On every
   retro tier a face is tinted ground with no rock in it. A colour map
   baked from slope, the erosion's flow and its scree would carry rock to
   the horizon on every tier.
3. Ranges past the playable square. The skirt carries the same noise to
   three times the extent, so the pass opens onto more of the same slope. A
   ring of taller, sharper backdrop ranges (geometry only, never flown to)
   gives the receding layers that every target uses for depth.

Fix reaches: all four tiers. It changes `height_at`, so the foot-plant
renders (`--inspect 60 11 3`) and the run's gameplay ground must be checked
again.

### 1.2 The river is a canal

The river is 52 m wide everywhere with steep banks (`river_half_width`).
Every target has a braided bed: channels that split and join, pale gravel
bars, and width that changes along the course. F2 already writes a sediment
map, and F8 already reads the bed's depth. The missing part is the course:
width, braids and bars from the generator. Fix reaches: all tiers, because
the bed is geometry and the bars are colour.

### 1.3 The breath is a row of soft balls

`artifacts/gap-study/breath-vs-target.png`. Our flame is additive round
puffs of one orange. It has no core, no structure, no smoke and no light on
anything. The target has a white-hot core at the jaws, flame tongues, smoke
that rolls off the end, and the fire lighting the dragon's own head, neck
and wing undersides orange. Missing pieces, and the tiers each reaches:

| Piece | Modern | SM3 | SM2 | Fixed function |
|---|---|---|---|---|
| A flipbook flame texture instead of one round puff | yes | yes | yes | yes |
| A hot core: a short bright stretched sprite at the jaws | yes | yes | yes | yes |
| Smoke: alpha-blended grey puffs at the end of each flame's life | yes | yes | yes | yes, sorted on the CPU |
| Soft particles: fade where a puff meets the ground | yes | yes, with depth | no depth texture on the GMA 3150 | no |
| The breath lights its source: one point light from the flame into the dragon's and the target's shaders | yes | yes | per vertex | a D3D9 point light |
| Heat haze: screen distortion behind the flame | yes | yes | yes, a screen-copy offset (GPU Gems 2 ch.19) | no |
| Scorch on the ground, burning trees | yes | yes | yes | yes, decal |

The same holds for the other five elements: each breath wants its own
flipbook, its own smoke or vapour and its own light colour.

### 1.4 Nothing has weight on contact

`artifacts/gap-study/landing-vs-target.png`. A landing in our build is a
dragon that stops. The grass is still, no dust rises and the camera holds.
Grass is already placed around the camera every frame, so a downwash
(a radial push in the grass's wind input, from the wings' height and
beat) costs little. Dust and pebbles are particles. A camera kick scaled by
the impact speed and the growth stage sells size. The same kit serves a
low pass over water (spray and a ring of ripples) and a dive past trees.
Fix reaches: all tiers. The grass bend is a vertex-shader input on the
shader tiers and a CPU offset on fixed function.

### 1.5 The creature sits on the scene, not in it

- The wing membranes show no light through them. Every dragon target and
  every reference game with membranes shows the sun through the wing when
  it is behind it. It is a wrap and back-light term in `skinned.hlsl` with
  a thickness from the hide's colour (Valve's half-Lambert family). Fix
  reaches: modern, SM3 and SM2 cheaply; fixed function only as a baked
  brighter texture.
- No speed cue but the boost's field-of-view surge. A dive at 80 m/s and a
  glide at 30 m/s look alike. Camera-velocity motion blur needs depth (modern
  and SM3). Wind streaks, a field of view and a synthesized wind pitch tied
  to airspeed reach every tier.

### 1.6 Ground level is unfinished

The grass is sparse blades and the bushes are flat green blobs
(`landing-vs-target.png`). The flier sees this only when landed, which the
run now asks for at every cache. Denser grass with a clumped meadow texture
under it, and bushes built from the same leaf cards as the crowns, close
most of it (inferred).

### 1.7 Light and time of day

The targets are all low sun, backlit, with warm and cool sides. The modern
tier's sun is at 19 degrees, which is close, and it has the long shadows.
What is missing is a reason to vary it: a run could deal its valley a time
of day from the seed, from dawn to dusk, now that every sky and shadow
term is physical.

### 1.8 The retro tiers lack everything the atmosphere work added

`artifacts/gap-study/vista-four-tiers.png` (modern, sm3, sm2, ff). The
retro tiers have no clouds, no long ridge shadows, no aerial blue and no
sky occlusion. Section 4 shows that most of this can be baked once per
valley. Two faults showed up in the captures:

- On sm2 the tree shadows are thin streaks several times longer than the
  trees (`sm2-vs-target.png`). The 14-degree retro sun and a 1024 map
  draw them as noise. A higher retro sun or a contact-only shadow length
  would read better (inferred).
- On ff at Low the run's start has almost no trees (`ff-vs-target.png`).
  The 2000 target has fewer trees than any modern tier, but each one is
  visible. Fewer, larger, nearer trees would read more like a forest than
  the current far, sparse spread (inferred).

### 1.9 What the other games' frames add

Read off the Steam store screenshots of Century, Day of Dragons, Dragon's
Dogma 2, Elden Ring, Monster Hunter Wilds, Ace Combat 7, Horizon Forbidden
West and Skyrim (2026-10-21). They are marketing frames, so they flatter,
but they agree on these, and our modern frame has none of them:

- Air with layers. Every one of them puts mist in the valleys, fog banks
  below the viewer and shafts of light through them (Elden Ring's
  panoramas, Horizon's cloud sea, Skyrim's forest). Our air is clear to
  the far wall; the aerial perspective is uniform and the valley fog is
  thin. Height fog that pools in the valley floor, cloud banks below the
  peaks, and light shafts from the terrain-sun map's shadowed air (F4
  already darkens the froxels) are the pieces.
- The sun in frame. Century and Ace Combat put the sun in the shot: glare,
  a bloom halo and streaks. Our sun is mostly behind the camera, and the
  bloom is subtle by choice. A lens glare at the sun and the chase camera
  framing toward the light at moments would use the low sun we have.
- Membranes that glow. Century's red wings and Day of Dragons' orange ones
  glow where the sun is behind them, and Century's breath lights the
  underside of the wing (1.3, 1.5).
- Speed. Century smears the frame radially at boost and adds white
  streaks. Reviewers said it was too much; a lighter version is the cue
  we lack (1.5).
- Snow and rock by orientation. Century's rock is dark, with snow on every
  upward face. Ours has snow by altitude only.
- Ground cover. Day of Dragons, Skyrim and Monster Hunter fill the ground
  with grass, shrubs and stones; the ground is never bare at a standing
  camera (1.6).
- A strong grade. Warm sun and cool shade, saturated near the sun. Ours is
  even and neutral. The grade's sliders exist; the targets want a stronger
  split.

### G1, first part, as built (2026-10-21): the shape

`game/terrain.h`, four new terms in `base_height_at`, each a slider under
Terrain and a key of the new `--terrain key=value,...` capture flag. Renders
in `artifacts/gap-study/g1-*`.

- Warp (450 m at an 1800 m scale): the mountain noise is read at a
  position pushed about by a broad fbm, so crests curve and branch.
- Spurs (420 m, 380 m apart): ridged noise stretched three to one across
  the valley, so its crests run down the walls. This is what broke the far
  wall into buttresses and gullies. Squared, it showed only at the crests.
  It is centred on the noise's mean, so gullies sink as far as spurs rise,
  and weighted to the upper wall.
- Wall profile (0.8): the ramp from the floor raised to a power below one,
  so the lower wall climbs sooner. At 0.55 the walls closed on the vista
  camera at 260 m.
- Backdrop (0.8): from 1.4 to 2.4 extents out the mountains grow by up
  to 80%, so the pass and the high views show jagged ranges behind.
- Also: the ridge octave is 160 m at 450 m (was 60 m at 700 m), the
  mountains 780 m (was 900 m). Strata at 45 m and 0.6 made no visible
  difference at flight distance and stay as they were.

Things learned:

- The near walls' smoothness was not the erosion. With erosion off they
  were as smooth; the erosion is what puts gullies on them.
- Spurs this steep grounded a bot. In the ten-minute bot test it touched a
  spur at 24 m/s, where its three-second lookahead was 72 m, and then
  stayed down for nine minutes: take-off needs a fresh press of flap
  (`FlightModel::step`), and a recovering bot held it down. The lookahead
  is now one function for the bots, the demo pilot and the test
  (`Terrain::ground_ahead`), with samples at 60, 120 and 180 m along the
  heading as well, and a grounded bot pulses its flap. A test sets a bot
  on the floor and checks that it is airborne within four seconds; it
  fails without the pulse.
- The first version darkened every frame, because the low sun's shadows
  grew with the walls. Mean brightness of the golden frames, of 255:
  valley 90.6 before G1, 65.1 at first, 90.5 now; the run 96.2, 74.3,
  91.0. Two causes, found by removing one term at a time:
  - The backdrop started at the playable edge, and its ranges, 80% taller,
    threw shadows kilometres into the valley. It starts 1.4 extents out
    now.
  - The spurs were added on top. Ridged noise is never negative, so they
    raised every wall by about half their height. Centred on the mean,
    they keep the wall's height.

  The run's start is still darker on the floor (a spur's shadow lies
  across the first 300 m), which the next playtest should judge.
- Erosion takes longer on the steeper walls: 1.1 s and 2.0 s for the
  arena and the run's valley on the M5, against 1.3 s and 0.8 s.

### G1, second part (2026-10-21): the material, first look

- Forest climbs the lower walls: trees stand on slopes to a normal of
  0.68 (was 0.78) and to 480 m over the floor (was 330 m). 56,005 trees
  against 44,002, about 0.6 ms a frame at 1080p on the M5 (the run's
  autopilot, 600 frames, 13.6 ms against 13.0 median).
- The walls' "clay" is not their colour. A grey rock palette and a darker
  one moved the sunlit far wall from (171, 153, 140) to (151, 140, 135)
  and changed how it reads very little; less haze changed it less. The
  target's cliffs average (102, 85, 66). The wall is bright because a
  face that turns to a low sun at 3 km is evenly lit: the crags bend its
  normals but cast nothing, and the mesh's 6 m cells carry no ledges.
  What makes a cliff read at that range is small-scale self-shadow, and
  that is F3's CDLOD mesh or a baked detail map of the eroded heightfield
  with its own shadow, not a palette. The palette is unchanged.

Next for G1: that detail at distance, and snow that lies on the upward
faces near the crests rather than above a contour.

### G2, first part, as built (2026-10-21): the breath

Renders in `artifacts/gap-study/g2-*`. The breath was a row of round orange
puffs; it is a jet from the jaws that billows into flame and ends in smoke,
and it lights what is round it. On the shader tiers:

- Three kinds of particle (`gfx::ParticleKind`): the old soft disc for
  embers and flashes, a flame tongue, and smoke. A per-particle seed and
  age reach the shader in a new attribute; fixed function's vertex layout
  is unchanged and draws every light as the disc, and no smoke.
- The flame tongue (`particles.hlsl`): a broad and a fine billow eat the
  disc from its edge in, climbing as the puff ages, closed inside the quad.
  Hottest where young and dense, so the core is at the jaws. sm2 takes one
  octave, to stay inside `ps_2_0`.
- Each flame puff is stretched along its motion across the screen, so the
  young fast ones form a jet and the slow old ones billow.
- Light is drawn premultiplied (the new `rhi::Blend::Premultiplied`, on
  every backend) and sorted back to front. Each flame puff is mostly opaque
  with a sharp edge, so stacked puffs converge on the front one's colour.
  Three things had been wrong, found in that order:
  - Added orange over a sunlit beige slope reads as peach.
  - The composite's `hue_preserve` is 0.8: a fifth of the curve is
    per-channel Reinhard, which turns a few hundred stacked puffs white.
  - Faint, soft puffs a few hundred deep average every billow away.
- Smoke (`App::smoke_for`, per element: soot, vapour, dust; none for
  storm) launches with a share of the flame puffs on the same path, lives
  2.2 times as long and fades in only where the flame dies, alpha-blended
  before the light.
- Puffs launch from the jaws (the head joint plus 2.5 m), narrow there, and
  spread over the frame's interval: the update moved each new puff two
  metres before it was drawn, so the flame began a gap past the mouth.
- The breath lights what is round it (`local_light` in
  `scene_common.hlsl`, the modern tier): one point light a third of the way
  down the flame, in the breath's hot colour, flickering, windowed inverse
  square, added to the direct light of the terrain, plants, meshes and
  creatures. The player's breath, else the hostile one nearest the camera.
  Lighting panel, "breath light".
- The world hit marker is gone: an opaque sphere growing to 12 m over the
  target, reset every frame a breath touched it, a balloon over anything
  being burned. Impacts flash and shed embers, and the HUD marks the hit.

Cost: about 0.6 ms at 1080p with the breath across much of the frame. Not
yet: heat haze, the fireball (still an emissive mesh), scorch on the
ground, the light on the retro tiers.

After the playtest (the same day): "the fog effect is probably too much;
with blight the dragon can be in fog for several seconds, tanking fps". Three
causes, and the murk was not the smoke:

- Old flame puffs are large and dim, and they still covered what was behind
  them, so a blight cloud (13 m puffs living 1.9 s) laid a dark green veil
  over the view. Cover now falls away with age faster than the light does.
- A breath that is a gas covers less: `cover` in the breath profile (fire
  0.9, stone 0.8, storm 0.7, frost and tide 0.55, blight 0.4). Every
  species' `.breath.cfg` that is not fire carries its own, since a partial
  file inherits fire's.
- Flame and smoke fade as the camera comes within two of their sizes and
  are not drawn inside one, so flying into a cloud no longer puts hundreds
  of screen-sized quads over the frame. Blight's particles went from 3.5 ms
  to 1.3 ms at 1080p in the chase view; fire's are 0.7 ms.
- Smoke is lighter: a tenth to a sixth of the flame puffs leave one (a third
  before), smaller and shorter-lived, and blight leaves the least.

### G4, first part, as built (2026-10-21): light through the wings

Renders in `artifacts/gap-study/g4-membranes-*`. A wing with the sun behind
it was a dark silhouette; it now passes the sun's light, coloured by the
hide, with the veins still dark.

- Nothing marked a membrane, so the load marks it (`App::mark_membranes`):
  each vertex's share of skin weight on a wing finger chain or below one,
  where the membrane spans; the arm's bones are thick and take none. A
  quarter to a third of each roster creature's vertices. It travels in the
  tangent's |w| past 1, the sign still the handedness, so no vertex format
  changed (`skinned.hlsl` reads the sign alone now).
- `skinned.hlsl`, the modern tier: where the sun is behind the face the
  viewer sees, the membrane adds the hide's colour filtered once more by its
  hue, strongest looking toward the sun. Its shadow is looked up from the
  sunward face; from the viewer's, the membrane shadows itself.
  At the hue alone a grey wing went white and lost its veins.
- Lighting panel, "membrane glow" (1.2).

### G4, second part (2026-10-21): landing weight

- Touchdown (`App::on_touchdown`): a ring of dust thrown outward, and a
  jolt through the chase camera, both growing with the sink speed and the
  growth stage. The dust is small, off the ground and rising: a
  camera-facing quad that cut into the ground showed as a hard-edged bar,
  and large dense puffs read as one flat sheet over the dragon.
- Downwash (`App::update_downwash`, `gfx::Foliage::downwash`,
  `foliage.hlsl`, the modern tier): under a dragon low and slow, the grass
  within 16 m (times the growth scale) lies away from it, flattening toward
  the middle and fluttering. In fast, out over about a second, so the air
  settles after the dragon does. Only the grass; trees do not bend.
- Dials: Dragon panel, "Landing weight" (downwash, jolt, dust colour).
- Found on the way, not fixed: the scripted dive
  (`--input -0.25,0,0,0,0,0`) arrives at 50 m/s straight down and stops
  dead with no damage (`STATUS.md`, `safe_landing_speed` is unread). A
  hard landing reads as weight now, but it costs nothing.

### G4, third part (2026-10-21): speed

The field of view already widened with airspeed and the wind's sound
followed it; nothing was seen to rush past. Now streaks of air
(`App::emit_speed_streak`, `gfx::ParticleKind::Streak`): thin pale lines at
rest in the air in a ring about the camera's view, 22-32 m ahead, never
through its middle, each as long as a tenth of a second of flight, so the
camera flies past them. Past 55 m/s, up to 120 a second 40 m/s faster;
always while boosting (the boost's streaks were round dots 30-50 m out,
thinner than a pixel, never seen). Each is a ribbon along its world line
turned to the eye, so in perspective they radiate from the heading:
projected onto the camera plane, a line along the view lost its length
and every streak fell vertical, like rain. Dials: Camera, "speed streaks
from" and "a second". Light by design: Century's radial blur was the
most criticised effect in its reviews.

### After the playtest of G4 (2026-10-21, later)

"Walking on uneven surface causes the landing animation. Diving from high
keeps the dragon spinning. Taking off is kind of tricky without boost."
All three were in the flight model (`game/flight.cpp`), and each now has a
check in `tests/test_flight.cpp` that fails on the old model.

- **Walking left the ground.** Downhill or over a crest each step left the
  ground a little under the body, which counted as airborne for a frame;
  every touchdown after it played the landing (the rig's pose, and since
  G4 the dust and the jolt), 45 times a minute walking at a slope.
  `resolve_ground` now keeps a grounded body that is walking, or slow and
  not rising, on the ground as far as a step on a steep slope drops; a
  walker going uphill rises along the slope, so it is not asked about its
  climb. The touchdown effects also need a third of a second in the air.
- **The spin.** Holding the nose down carried it past vertical into an
  outside loop: the body hung inverted, auto-level rolled it upright, the
  held stick pushed it over again, and three assists that steer by the
  bank angle (turn coordination, auto-level, the bank limit) read a
  meaningless 180 degrees near vertical and rolled at 3.5 rad/s. A push
  now stops at vertical (pulling through a loop, and the scripted flip,
  are untouched), and the three assists fade out as the nose nears
  vertical; turn coordination also acts only upright.
- **Take-off.** The flap's thrust is along the body, a third of the
  dragon's weight, and the wings lift nothing at walking speed, so after
  the leap a held flap fell back and sat there; only boost or mashing
  flap got a dragon up. Near the ground and slow, a wingbeat now also
  lifts 1.3 times the weight and pushes forward, fading out by 22 m/s and
  25 m up (`FlightTuning::takeoff_lift`, Flight panel, Propulsion). Held
  flap from standing: 17 m up at 20 m/s after four seconds. Pulling the
  nose full up still climbs into a stall.

### G5, first part, as built (2026-10-21): the shot announced

- **Incoming fire, before it lands.** For each hostile shot, its closest
  approach to the player over the next three seconds (gravity included);
  one passing within three times the hit distance draws an arc at the edge
  of the view toward it, red if it will hit, pale if it will pass close,
  thicker and wider as it nears. The old marker said only where the last
  hit came from, after it landed. Combat panel, "incoming-fire warning".
- **The bot's tell.** A rival holds a fireball for `fire_windup` (0.6 s
  rookie, 0.45 veteran, 0.3 ace; Combat panel, "bot shot tell") with its
  jaws opening and a glow swelling in them in its element's colour, then
  re-solves the shot and fires, or drops it if the target has left the
  range or a cone half again as wide. `test_bot` pins both.
- **Bots that the tell sent into the ground.** The tell moved the ten-minute
  bot test's path, and two old faults showed. The pull-out reflex assumed 12
  m/s^2 of pull and fell 2 m short from a 75 m/s dive at G1's steeper walls;
  it assumes 9 now. And the shared steering (`steer_toward`) climbed toward
  a target overhead until the bot hung at 7 m/s and stalled onto a slope;
  below 18 m/s with the nose up it now pushes the nose down, for bots, the
  course autopilot and the demo pilot alike.

- **The towers' tell.** The last `defence_tell` seconds (0.7) before a
  tower's bolt, its brazier flares, up to four times the plume and twice
  its size (`Sentinel::tell`). `test_combat` checks every bolt is told.
- **The punish window.** A rival's bite or strike that misses leaves it
  stunned for `whiff_stun` (0.9 s): weapons cold, controls slack, STUNNED
  on its marker, and the low snap of empty jaws. A rival biting at a
  dodging player was free before; now a roll that makes it miss opens it
  to a counter. The combat telemetry line counts the misses. Dials:
  Combat, "tower tell", "rival miss stun".

Not yet: a committed dive attack for the rivals with its own recovery,
and frame-level poses for the tells (the jaws use the breath's opening).

### G7, first part, as built (2026-10-21): rising air

`game/thermals.h`. Century's updrafts and the paraglider's thermals: lift
as a resource the valley offers, read from the sky.

- **Placement**: nine columns a valley, from its seed, deterministic, over
  dry ground that faces the sun, gentle, and low -- the meadows and lower
  slopes along the corridor, at least 550 m apart. A first version scored
  sun and slope alone and stood a column on a peak at the map's edge.
- **The air moves**: `FlightModel::wind`, and every aerodynamic force is
  computed on the velocity through the air. A glider in 6 m/s of rising
  air climbs 3.4 m/s where it sinks 2.6 in still air, with no special case
  (`test_thermals`). Each column is Gaussian across, about 85 m to 1/e,
  4.9-8.1 m/s at its axis, fading in over 40 m above the ground and out
  under its top (1350 m, under the cloud base), breathing over a minute.
  The player and every bot fly through it.
- **Seen from afar**: a cumulus caps every column (`thermal_caps` in
  `scene_common.hlsl`, used by the sky, the cloud shadows and the
  volumetric clouds' weather map, which had its own copy of the coverage
  formula and showed no caps at first). The cap is 4.5 column radii wide:
  at 2.6 it was two weather-map texels across, a faceted lump.
- **Felt close**: pale motes carried up and round each column near the
  camera, and "RISING AIR +x.x" over the speed while the player is in it.
- Dials: Flight, "Rising air" (count, strength, radius, top, motes).

Not yet: ridge lift on windward slopes, thermals in the bots' and the demo
pilot's plans (they fly through them by chance), and caps less round.

### G8 and G9, first part, as built (2026-10-24): the hour, the mist, the lens

A batch for one playtest, chosen to change the picture and not the play.
Modern tier only. Renders in `artifacts/gap-study/g9-*` and `g4-spray.png`.

- A time of day for each valley (`game/time_of_day.h`, 1.7). A run starts
  in the morning (sun 12-16 degrees, ahead and to the right). The second
  valley is the afternoon (19-26 degrees, near the tuned bearing), and the
  last is the evening (12-15 degrees, ahead and to the left). Each
  valley's seed jitters the hour. The thermals are placed after the sun is
  set, so they stand on the ground that faces it. The arena keeps its
  afternoon. `--sun` turns the run's hours off. Atmosphere panel, "time of
  day from the seed".
- The sun's colour follows its height. `sun_colour_ratio` integrates the
  Rayleigh and aerosol optical depth along the sun's ray on the sphere and
  divides by the 19-degree path. The tuned colour is the 19-degree colour
  (Sky & light, "sun colour at 19 deg"). At 10 degrees it is
  (0.88, 0.76, 0.52) of that, at 26 degrees (1.04, 1.09, 1.23).
  `test_time_of_day` checks both, and the order of the hours.
- Valley mist (`valley_mist` in `scene_common.hlsl`, 1.9). An exponential
  layer over the valley floor (the water level), 50 m to 1/e, integrated in
  closed form along each ray. Two drifting noises cut it into banks with
  gaps. The sun lights it with forward scattering (Henyey-Greenstein,
  g 0.45) except where the terrain-sun map shades the ground under it, and
  the sky lights it everywhere. The density is 0.0045 per metre at the
  floor in the morning, 0.4 of that in the evening, and zero in the
  afternoon. `--atmosphere mist=,mist_height=,mist_patches=`. Found on the
  way:
  - At 0.0035 nothing showed, and a layer without patches read as blue
    smog.
  - Lit by the sky alone, the shaded mist was grey-blue. A mist scatters
    many times over, so it now takes twice the sky light a surface does,
    and a twentieth of the sun even in shade.
  - At 0.006 and 60 m the floor under a dragon 180 m up was half hidden,
    the herds and caches with it. At 0.0045 and 50 m it lets through two
    thirds there, and a tenth 2 km out.
  - An afternoon mist of 0.12 washed the far floor milky from any raised
    camera, because 2-3 km of thin layer adds up. The `dragon-side` golden
    moved by a mean of 20/255 with it and by 1.9 without it.
- The lens (`post_composite.hlsl`, 1.9). Ghosts of the bloom are mirrored
  through the centre at four scales, with a halo ring (Chapman's
  screen-space flare). Their source is the bloom above 0.5: the clouds'
  own bloom made a purple disc in the middle of the sky. When the sun is
  in the frame, a glare surrounds it: twelve rays, a halo and a faint
  veil. The glare is as bright as five taps of the scene about the sun
  disc show, so a ridge or a cloud in front of the sun puts it out. A veil
  spread over the whole frame greyed every shot with the sun in it. Grade
  & bloom, "lens flare" (0.06) and "sun glare" (0.4).
- Snow on the upward faces (`terrain.hlsl`, 1.9). From 200 m under the
  ragged snow line to 100 m over it, the flatness that holds snow falls
  from 0.93 to 0.55. Low down only the ledges keep it, which are the bent
  normal's flattest pixels. Near the crests all but the cliffs keep it.
  Faces turned from the sun hold snow 0.07 steeper. The retro tiers keep
  the contour.
- Spray off the river (`App::update_spray`, 1.4). Low and fast over the
  water, the wake throws a sheet of spray and drops out to the sides. Low
  and slow, the wingbeat blows it off all round. A touchdown on the river
  throws spray, not dust. Smoke particles are drawn unlit, so sunlit spray
  needs a colour over 1 (1.25, 1.32, 1.4); at 0.65 it read as grey
  cotton. Dragon panel, Landing weight, "spray off water". `--skim`
  spawns the dragon six metres over the river for a capture.
- The thermals' caps are less round. A cap's coverage stops at 0.72, so
  the volumetric march's billows cut into it, and it carries lobes a few
  hundred metres across. From below, each cap was one smooth white ball.
  It is still soft; the march's shape noise is the next step.

Found, not fixed: skimming the river, the water shows harsh white streaks
toward the far bank, where every wave tips the near-mirror reflection
between sky and bank. Flattening the waves edge-on did not change it. In
the evening valley the whole floor can lie in the walls' shade.

### The suspected performance regression (2026-10-24)

No recent change costs more than about 0.6 ms of GPU time at 1080p on the
M5. What looked like a regression was the machine's other load.

- Eight commits from `e5d388b` to `a7bd866`, built in worktrees and run in
  commit order on four scenes, showed the GPU time climbing by commit. On
  the frozen run frame it was 10.7 ms at `f8c7307`, 12.5 at `c0a9608` and
  14.0 at `a7bd866`. Interleaved and repeated, the same binaries gave
  16-21 ms for the same frame in any order. With no game running, the GPU
  sat at about 20% utilization, with bursts to 90% (other agents' jobs,
  WindowServer, Defender scanning the new worktrees).
- So the measure became paired. `DRAGON_ABLATE_ALTERNATE=term` with
  `DRAGON_FREEZE_AT` draws the frozen frame 30 times with the term and 30
  times without it, alternating, and logs each half's median. Both halves
  see the same contention. Three runs of 1800 frames each:

  | Term | Vista | Run, frame 240 |
  |---|---|---|
  | `thermal_caps`, the cumulus over the thermals (`a7bd866`) | +0.13 to +0.60 ms | -0.16 to +0.61 ms |
  | `particles`, which holds the speed streaks (`c0a9608`) and the motes | not run | -1.8 to -0.2 ms (none) |
  | `thermal_caps` after the fix below | -0.39 to +0.05 ms | -0.01 to +0.60 ms |
  | `mist`, this batch's, in valley 1's morning | not drawn (the arena's afternoon has none) | +0.27 to +0.61 ms |
  | `lens`, this batch's, the sun in the frame | +0.10 to -0.13 ms (the sun at the corner) | +0.07 to +0.19 ms |

- The thermal caps cost every world pixel a loop over twelve columns in
  its cloud shadow, and a second four-octave cloud field under a cap. The
  loop now skips a column more than three cap-widths away before its
  `exp`, and the cap reuses the field the coverage already read.
- The mist runs per fragment, under the forest's overdraw too. It returns
  before its noise when the layer along the ray is too thin to show, and
  the glare returns before its taps past 0.6 of the frame from the sun:
  0.45 ms and 0.4-0.7 ms before those two changes.
- One change a playtester can feel as a regression: since `6e5b221`,
  dynamic resolution defaults on only for displays over 1300 lines. On a
  1080p window a heavy frame now shows as a lower frame rate, not as a
  softer picture. The Graphics panel's "dynamic resolution" turns it on.

### Where the GPU's time goes (2026-10-25)

The playtest found the run under 60 fps on a 99 Hz display, with
stutter. With vsync each frame waits for a refresh, so a frame just over
10.1 ms is shown at 20.2 ms (49.5 fps) and a slower one at 30.3 ms (33).
The run's frame at Ultra, 1080p, costs about 11 ms of GPU time on a quiet
M5: always just over the interval.

The frozen run frame (`--run 7 --autopilot`, frame 240) taken apart, with
OBS loading the GPU, so every figure is about twice a quiet one:

| Part | How measured | Cost |
|---|---|---|
| The main pass, all of it | `DRAGON_GPU_PASS_PROFILE` | 12.2 ms of 20.9 |
| The crowns (`trees`) | paired ablation | 2.6-3.2 ms |
| The terrain (`terrain_near`; it was not ablatable on the modern tier before) | paired ablation | 1.5 ms |
| The shadow pass | pass profile | 3.3 ms; its crowns (`tree_shadows`) 0.6-0.9 |
| The water, the mist | paired ablation | 0.5 ms each |
| Clouds (weather, march, resolve, apply) | pass profile | 1.3 ms |
| Post (SSAO, bloom, composite) | pass profile | 2.3 ms |
| Grass, sky, rocks, the dragon | paired ablation | 0.4 ms or less each |

Each profiled pass also carries about 0.2 ms of the profiler's own wait.

The crowns are fragment-bound: at a quarter of the pixels they cost 1.0
ms against 2.6. Their cards are alpha-tested, and a `discard` defeats the
tile GPU's hidden-surface removal, so every card behind the front one and
the ground under the forest were lit in full. A depth pre-pass now draws
the crowns' depth first (`foliage_prepass.hlsl`). The crowns are then lit
with an EQUAL test (`foliage_shaded.hlsl`), once a pixel. The picture is
unchanged: at most 19/255 at a few hundred pixels, mean 0.0002. It saves
0.7 ms on the run frame, 0.1-0.2 ms on views with less forest; the
crowns' vertices are drawn twice for it. `DRAGON_ABLATE=tree_prepass` and
Vegetation, "crown depth pre-pass" turn it off. Two things went wrong on
the way:

- With the alpha cut in both passes, about one crown pixel in 7,000 was a
  hole of the clear colour. The depths matched to the bit; the two
  shaders, compiled apart, cut a card's edge a pixel differently. The
  shaded pass now cuts nothing: the pre-pass alone decides coverage.
- The impostor bake read its uniforms from an unbound buffer: its
  fragment stage read nothing at slot 0, so the reflected count was 1 and
  slot 1 was never bound. Metal's API validation stopped on it, which is
  how the playtester's restart crashed. A read of `scene` that changes
  nothing keeps the slot. Every main scene and tier now runs clean under
  `MTL_DEBUG_LAYER=1`.

Not yet, and each changes the picture a little: the mist in the
aerial-perspective froxels instead of per fragment, the far cascades of
the shadow map refreshed every other frame, and dynamic resolution aiming
at the display's refresh rather than at 60.

## 2. Century: Age of Ashes

Playwing, UE4, free to play, PvP dragon combat, released December 2021.
It is the closest commercial game to our arena, and the most useful
contrast to our run.

What it is:

- Three classes at launch (Windguard, Phantom, Marauder), three more in
  the seasons (Stormraiser, Thornweaver, Rimeblood). Each has one power,
  two abilities, a passive and a Rage that charges over a match.
- Modes: Carnage (6v6 deathmatch with bounties), Gates of Fire (a flag
  carried through eight gates), Spoils of War (gold), Survival (4v4v4),
  Skirmish (3v3), and one PvE mode, Outbreak (four players defend towers
  through ten waves).
- Maps are arenas with "extreme verticality": open bowls joined by narrow,
  dense passages, connected so that flight never stops. Weather and time
  of day are fixed per map variant, not dynamic.
- Reception: OpenCritic 73; Steam 73% positive of 8,315, recent reviews
  66%. Steam players went from a 12,222 peak at launch to a 43 average in
  September 2026. The last season was July 2023; the game is in
  maintenance.

### How it looks

The developers said little about lighting, clouds or water, and the
interview page could not be fetched, so the rendering below is what
reviews and the VFX vendor say.

- The visual strength is the effects, not the world. Persistant Studios
  built about 80 effects in PopcornFX, with nine artists over two and a
  half years. The art director said the game "would not have had the same
  success" without them. Two of their techniques fit us directly:
  - Each effect is parameterised by the dragon's size and shape, so one
    breath fits every body.
  - One master material, driven with the particles, shows every status
    effect on the hide.

  We have seven species, six elements, three growth stages and a status
  per element. That is the same problem, and today our statuses are a tint
  and some particles.
- Reviewers praise the draw distance, the water and wind effects in dives
  and boosts, and the "wall of flames". They criticise a palette of
  "browns, blues and yellows that all blur together", so targets merge
  with the ground.
- Motion blur was "extremely aggressive", became optional only after the
  first beta, and players set it to 0. Speed has to read without smearing
  the target.
- The animation was keyframed (the credits list animators and riggers; no
  mocap or procedural animation is mentioned). Ours is procedural, which
  is the harder road and the more distinctive one.

### How it plays

- The dragon flies forward on its own; W and S change speed; a double tap
  boosts on stamina. A boost lasts longer in a dive, and fireballs inherit
  the shooter's speed, so diving attacks pay. Ours does the second already
  (`game/combat.cpp`, the fireball's intercept).
- Brake then boost in a turn is a "drift", the main evasive turn.
- Wind tunnels, updrafts and geysers are part of the level art and refill
  stamina or launch the dragon. Reading the map is a skill.
- Aim assist is generous: "most of the attacks don't require pinpoint
  aim". Our aim assist is the same choice.
- There was no dodge at launch, and the most common early complaint was
  that a lock could not be escaped. A dodge came in 2023: one stamina, and
  it voids every incoming fireball whose indicator has turned yellow.
- No melee, grabs or takedowns were found. Our bite, claw and tail, and
  carrying prey, are ahead of it.
- Growth is between matches: eggs hatch and grow by tasks. Reviewers found
  it slow. Ours is inside a run, which is the better answer.
- The HUD carries the threat: the breath left as arcs on the reticle,
  fireball charges as bars above it, a red mark when an enemy is in breath
  range, and a semicircle arrow per incoming fireball whose size gives the
  distance and whose colour says it will miss. Players still found it
  cluttered and too small on a TV.

### What it teaches us

| Century | Us | Gap |
|---|---|---|
| Updrafts, wind tunnels and geysers as level features | The valley has none; thermals are in `DIRECTION.md` row 9, unbuilt | Thermals over sunlit faces and ridge lift along the walls. The terrain-sun map (F4) already knows which faces are in sun |
| One incoming-shot indicator per fireball, coloured by whether it will hit | Edge arrows point to targets; nothing warns of a shot | An incoming-fire ring, which the roll and the flip need in order to be skill rather than luck |
| VFX parameterised by size, one status material | Particles scaled per species; status as a tint | A status layer in `skinned.hlsl` (frost rime, blight veins, storm sparks, stone crust) driven by the status amount |
| Bowls joined by narrow passages, scaled to dragon speed | One meandering corridor, the same width all along | The valley generator can pinch the corridor into gorges and open it into basins; risk and cover in one |
| Fixed weather per map variant | Four valley kinds from the seed | Weather per valley (section 5) |
| Thin content and no reason to return | A roguelite's run-to-run variety, if the valleys differ enough | The second run playtest said the seeds did not change much; G1 and the pinch points are also variety |

## 3. Other games

| Game | What it does well | Its main criticism | What to take |
|---|---|---|---|
| Day of Dragons (UE, early access since 2019) | Physics flight; growth from hatchling to Alpha | Flight edges punish: stalls, dive pull-outs and hovers crash the dragon. "Nothing to do" in an empty world. Growth takes hours | Growth needs a loop around it. A stall and a landing must be recoverable states |
| Dragon's Dogma 2 (RE Engine, 2024) | A world whose slopes matter: downhill is faster, a throw from above hits harder. Monsters react; the player climbs them and tears their wings to ground them | Performance, enemy variety | Damage to wings as a way to ground a rival |
| Elden Ring | Agheel's dive lands him on the player and leaves him open: a punish window | The camera does not frame a huge enemy; lock-on fixes on a leg | A committed attack that ends grounded and vulnerable; frame large targets |
| Skyrim | A dragon roars before it engages, makes fly-bys, perches out of reach, lands to fight, crash-lands at half health and skids | Endless circling; dragons that leave without a fight | The crash-landing at low health: a rival that loses the air |
| Monster Hunter (World, Rise, Wilds) | Attack tells: Rathalos' mouth glows before a fireball, its wings pull up before a talon dive. Monsters limp to their nest and sleep; they fight each other in turf wars. Wilds' weather phases bring the apex monster with the storm | Wilds' PC performance; little challenge at launch | Tells, retreat and recovery, infighting, weather with a threat attached |
| Ace Combat 7 (UE4, trueSKY) | Clouds change combat: they hide an aircraft, break or slow a lock, ice it if it stays; wind and lightning push and blind | The all-weather map is often skipped | Clouds as cover and as a hazard, with a cost to staying |
| Horizon Forbidden West | The Sunwing flies through voxel clouds made for flying (Nubis cubed) | Flight comes late | Clouds worth flying into |
| Tears of the Kingdom | Sky and ground as one map; a dive is also a survey | -- | The dive as a way to read the valley ahead |
| Lair (PS3, 2007) | Praised visuals and score | Tilt steering and a "lottery" lock-on sank it (Metacritic 53) | Controls and targeting decide the game, not the picture |
| School of Dragons (shut 2023) | Growth unlocks movement: a teen glides, an adult flies | -- | Growth that changes how you move, not only numbers |
| Crimson Desert (2026) | A dragon mount with spit, breath and a barrel roll | 15 minutes of flight, then an hour's cooldown | -- |
| Sagas of Lumin (early access planned 2026) | Dragon dogfights with breath, claw, bite and tackle; base and airship attacks | Not released | The closest design neighbour; watch it |
| Dragontwin (Patreon builds) | Physics-driven dragons, no clips: any proportions move | Placeholder art | The closest technical neighbour to our procedural rig |

## 4. The retro tiers: what each modern feature can become

Deferred (the user's call, 2026-10-21): the modern tier comes first. This
section is the record for when the retro work resumes.

The finding that shapes this section: **the sun does not move within a
run.** Everything the modern tier recomputes each frame from the sun and
the terrain alone can be computed once per valley on the CPU and stored.
The retro tiers then sample a texture (SM3, SM2) or read a vertex colour
(fixed function). Crysis did the same on DX9: it solved its sky on the CPU
into a 128x64 float texture and spread the update over 15-20 s (Wenzel
2006).

| Modern feature | SM3 | SM2 | Fixed function | Notes |
|---|---|---|---|---|
| Hillaire sky (F1) | Bake the sky-view map on the CPU at valley load; sample it as now | The same texture | The same function into the existing 512x256 dome bake (`porting/FIXED_FUNCTION.md`) | The ff dome already bakes "whatever the sky function returns"; the retro tiers pass it the analytic sky today |
| Aerial perspective (F1) | Per-pixel exponential haze, coloured by a CPU-baked table of (distance, view elevation) | Hoffman-Preetham per vertex: extinction and in-scatter as two vertex colours, 33 instructions (GDC 2002) | The same two colours: texture times diffuse, plus the specular colour. The slides name the fixed pipeline as a target | On the GMA 3150 and fixed function the vertex work is CPU work; keep the far mesh dense enough to hide per-vertex error |
| Volumetric clouds (F6) | Sprite clouds (FS2004, Wang 2003) or Harris impostors; or bake the painted slab into the sky texture | A cloud layer baked into the sky-view texture | Clouds in the dome bake | Clouds that do not move are acceptable at a fixed sun. Overdraw is the cost of sprites |
| Cloud shadows | One extra lookup of a scrolling coverage texture | The same | A projected texture on a spare stage (camera-space texture coordinates and a texture transform) | The Rage XL has one stage; it takes an extra pass |
| Long terrain shadows and sky occlusion (F4) | Bake the terrain-sun map on the CPU (it is a march over the heightfield) and sample it | The same map, or fold it into the colour map | Into the terrain's vertex colour, which already carries baked shadow | Erosion already runs on the CPU at load; this is the same kind of work |
| Cascaded shadows (F4) | Two cascades; GeForce 6 has free hardware PCF on D24 | One map, creatures and props only (Crysis moved its SM2 shadow lookup into a screen-space mask to fit 64 instructions) | Blob shadows (planned in R5) | -- |
| SSAO (F6) | Half resolution, ambient only, as the modern one | Baked AO in the colour map | Baked AO in vertex colour | -- |
| TAA (F5) | MSAA on an 8-bit target; GeForce 6/7 has no MSAA on FP16 | MSAA; alpha-to-coverage through ATI's hack on the X550 | The card's MSAA if any | -- |
| Impostors (F7) | The same atlas, four frames blended | One frame, no blend | Crossed quads or one billboard view | The bake needs a render target; run it on the card at load or ship the atlas |
| River (F8) | Planar reflection at half resolution, flow map | Flow-mapped normals, a cube-map reflection (Source's "cheap" water) | EMBM on the G550 and the Radeon 7000; scrolling texture plus a sphere map elsewhere | -- |
| Tonemap and bloom | HDR if the card has FP16 blending, else the sm2 path | Tonemap in each shader (Valve 2006), already `LDR_OUTPUT` | Tron 2.0's glow with overlapping quads (GPU Gems ch.21) | Already built for sm2 and ff |
| Membrane back-light | Yes | Yes, a few instructions | A brighter baked texture | -- |
| Breath (1.3) | All of it | All but soft particles where there is no depth texture | Flipbook, core, sorted smoke, a D3D9 point light | -- |

### 4.1 A risk on the SM3 floor card

`gfx/render_tier.h` gives the sm3 tier an HDR target unconditionally
(`t.hdr = tier == Tier::Modern || tier == Tier::SM3`), and
`gfx/device.cpp` checks only the depth and 16-bit colour formats. HEXUS's
preview of the GeForce 6200 TurboCache (NV44) says it has no 64-bit
floating-point buffer formats, and bit-tech says it has no FP16 blending.
The particles and the water blend into `scene_hdr`. If the preview is
right, the sm3 tier misrenders on its own floor card.

Before the G41 gets the 6200 TC, the backend must check `A16B16G16R16F`
with `D3DUSAGE_RENDERTARGET` and `D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING`.
If either fails, the card runs SM3 shaders with `LDR_OUTPUT`, the sm2
colour path. This needs a baked sm3-LDR shader set, so it is not a
one-line fallback.

### 4.2 Caps to read on the real cards

| Card | Read |
|---|---|
| GeForce 6200 TC | FP16 render target and blending; D24 shadow maps; RAWZ |
| Radeon X550 | Instruction slots; DF16; the R2VB and alpha-to-coverage FOURCCs |
| GMA 3150 | R16F and R32F targets, the real pixel shader profile (the SM3 claim is disputed), MRT count |
| Matrox G550, GeForce4 MX | Cube maps, EMBM, texture stages |

### 4.3 Period references for a flight game on this hardware

- Drakan (1999): dragon flight over terrain, DX6/7. The fixed-function
  tier's reference.
- Panzer Dragoon Orta (Xbox, 2002): DX8-class, a dragon, 60 fps.
- FS2004 and FSX: sprite clouds, water tiered by shader version.
- Far Cry (2004), Oblivion (2006), Crysis on Low and Medium: terrain,
  water, SpeedTree, and an atmosphere designed with SM2 in mind.

## 5. Play

The genre's most common complaints are punishing flight edges (Day of
Dragons, Lair), a lock-on and camera that fail against large flyers (Elden
Ring, Lair), endless circling (Skyrim), and empty worlds or long grinds
(Day of Dragons). Against them:

| Gap | Where we stand | What to build | Source |
|---|---|---|---|
| Attack tells | Rivals and towers fire without a readable wind-up | A held wind-up per attack: mouth glow and a head pull-back before a fireball, wings raised before a dive. The gesture layer (M18.3) can pose it without clips | Monster Hunter |
| Clouds in combat | Volumetric clouds are scenery | Inside a cloud the aim assist cannot lock you and you cannot lock out; a long stay costs (icing for frost, lightning for storm). On the modern tier the cloud is visible; on the retro tiers the rule still holds | Ace Combat 7 |
| A dive with a punish window | Melee and dives have no recovery cost | A committed stoop that hits hard and leaves the attacker grounded or slow for a moment; for rivals as well as the player | Elden Ring, Skyrim |
| Weather per valley | Four valley kinds from the seed; no weather | A phase per valley (calm, storm, clear) with a threat that comes only in the storm | Monster Hunter Wilds |
| Rival arcs | Rivals fight, flee to heal and return (M19) | A wounded rival limps to its nest and sleeps; the player chooses to chase it there. Rivals and hunters fight each other when they meet | Monster Hunter |
| A rival that loses the air | Bots die in the air | Torn wings or low health force a crash-landing and a ground fight | Skyrim, Dragon's Dogma 2 |
| Forgiving edges | A full-brake descent stalls (`STATUS.md` open questions); `safe_landing_speed` is unread | An auto-flare near the ground; a stall that recovers by a flap. Keep crashes out until they are fair | Day of Dragons |
| Framing big targets | The chase camera ignores the target's size | Pull the camera back and widen it against an adult rival or a tower | Elden Ring, Lair |
| Growth that changes movement | Growth moves the tuning sliders (M25) | One new move per stage: a drake cannot hover, an adult can stoop from altitude or carry | School of Dragons |
| Grab and carry | Prey can be picked up | Lift a hunter or a small rival and drop it: a move only a dragon has | Dragon's Dogma 2, Skyrim |
| Lift as a resource | No thermals or ridge lift | Thermals over sunlit faces, lift along windward ridges, a gorge's wind | Century, Tears of the Kingdom |
| Incoming-shot warning | None | A ring segment per incoming projectile, coloured by whether it will hit | Century |
| Status on the hide | A tint | A status layer per element in the creature shader | Century's status material |

## 6. Order

By what each buys per session, with the tier each reaches. The F phases in
`FIDELITY.md` stay as they are; F3's CDLOD mesh is still the way to the
1 m triangles that 1.1 needs up close.

| # | Work | Tiers | Sessions |
|---|---|---|---|
| G1 | Mountain shape (1.1): a stronger ridge octave, terraces that survive erosion, a colour map with rock by slope, and backdrop ranges past the skirt | all | 2 |
| G2 | The breath (1.3): flipbooks, core, smoke, the flame's light, heat haze; per element | all, tiered | 2 |
| G3 | Deferred: the retro bake (4): sky-view, aerial table, terrain-sun map and sky occlusion on the CPU at valley load; clouds in the sky bake; the 6200 TC check (4.1) | sm3, sm2, ff | 2 |
| G4 | Contact and weight (1.4, 1.5): downwash, dust, spray, landing kick, membrane back-light, speed cues | all | 1-2 |
| G5 | Tells, the punish window and the incoming-shot ring (2, 5) | gameplay, all | 1-2 |
| G6 | The river's course (1.2): width, braids, gravel bars | all | 1-2 |
| G7 | Clouds, weather and lift in play (2, 5); pinch points in the corridor | gameplay, all | 2 |
| G8 | Ground level (1.6) and a time of day from the seed (1.7) | all | 1 |
| G9 | Layered air (1.9): height fog pooled in the valley, cloud banks below the peaks, light shafts, sun glare, snow on upward faces, a stronger grade | modern | 2 |

G1 comes first because the mountains are most of every frame on every
tier. G3 is the cheapest large step for the retro tiers, and waits until
the modern tier is done. G5 and G7 are the
play gaps the research names most often.

## Sources

- Century: Steam page https://store.steampowered.com/app/918570/Century_Age_of_Ashes/, news https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=918570&count=100&maxlength=0&feeds=steam_community_announcements, player counts https://steamcharts.com/app/918570
- Century's art director on flight and maps: https://80.lv/articles/century-age-of-ashes-art-director-talks-flying-mechanics-of-the-game, https://gamingbolt.com/century-age-of-ashes-interview-maps-classes-post-launch-plans-and-more
- Century's effects (PopcornFX): https://www.popcornfx.com/work-for-hire-art/
- Century reviews: https://www.noobfeed.com/reviews/1804/century-age-of-ashes-pc-review, https://www.inverse.com/gaming/century-age-of-ashes-preview-star-wars-squadrons, https://www.theouterhaven.net/?p=230534, https://totalgamingaddicts.com/?p=50240
- Century's HUD and mechanics, from players: https://steamcommunity.com/app/918570/discussions/0/3199242034528134362/
- Hoffman and Preetham, GDC 2002: https://renderwonk.com/publications/gdc-2002/
- Wenzel, Real-time Atmospheric Effects in Games (CryEngine 2), SIGGRAPH 2006: https://advances.realtimerendering.com/s2006/Chapter6-Real-time%20Atmospheric%20Effects%20in%20Games.pdf
- Mittring, Finding Next Gen: CryEngine 2, SIGGRAPH 2007: https://www.advances.realtimerendering.com/s2007/Mittring-Finding_NextGen_CryEngine2(Siggraph07%20Course%20Notes).pdf
- Mitchell, McTaggart and Green, Shading in Valve's Source Engine, SIGGRAPH 2006: https://advances.realtimerendering.com/s2006/Chapter7-Shading_in_Valve's_Source_Engine.pdf
- O'Neil, GPU Gems 2 ch.16: https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-16-accurate-atmospheric-scattering
- Zhang et al., GPU Gems 3 ch.10 (PSSM): https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus
- Sousa, GPU Gems 2 ch.19 (refraction): https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-19-generic-refraction-simulation
- James and O'Rorke, GPU Gems ch.21 (glow): https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-21-real-time-glow
- Harris, Real-Time Cloud Rendering for Games, GDC 2002: https://aurora.cs.uaf.edu/~olawlor/academic/thesis/ref/RTCloudsForGames_HarrisGDC2002.pdf
- Pranckevičius, D3D9 GPU Hacks: https://www.aras-p.info/texts/D3D9GPUHacks.html
- HEXUS, GeForce 6200 TurboCache preview: https://hexus.net/tech/reviews/graphics/952-nvidias-geforce-6200-turbocache-preview/?page=2
- bit-tech, 7300 GS review (FP16 blending): https://bit-tech.net/reviews/tech/nvidia_7300_gs/2/
- Valve Developer Wiki, water shader: https://developer.valvesoftware.com/wiki/Water_Shader
- Day of Dragons, Steam page and flight threads: https://store.steampowered.com/app/1088090/Day_of_Dragons/, https://steamcommunity.com/app/1088090/discussions/1/3193616250034838171
- Dragon's Dogma 2 physics: https://wccftech.com/dragons-dogma-2-will-be-a-fantasy-world-simulation-game-with-more-emphasis-on-physics/
- Elden Ring dragon fights: https://exputer.com/features/dragon-fights-elden-ring-always-sucked/
- Skyrim dragons: https://en.uesp.net/wiki/Skyrim:Dragon
- Rathalos tells: https://www.icy-veins.com/monster-hunter-wilds/rathalos-monster-guide
- Monster Hunter Wilds weather: https://www.gamereactor.eu/monster-hunter-wilds-all-weather-conditions-and-how-to-change-it-1505913/
- Ace Combat 7 clouds and weather: https://www.skywardfm.com/post/the-form-and-function-of-clouds-and-weather-in-ace-combat-7
- Nubis cubed (Horizon Forbidden West): https://www.guerrilla-games.com/read/nubis-cubed
- Tears of the Kingdom interview: https://www.nintendo.com/sg/interview/totk/03.html
- Lair: https://en.wikipedia.org/wiki/Lair_(video_game)
- School of Dragons flight: https://schoolofdragons.wiki.gg/wiki/Dragons/Dragon_Flight
- Crimson Desert's dragon: https://mein-mmo.de/en/crimson-desert-unlocking-dragons-how-to-unlock-the-flying-mount-blackstar,1558660/
- Sagas of Lumin: https://www.gematsu.com/games/sagas-of-lumin
- Dragontwin: https://mein-mmo.de/en/drachenspiel-wie-game-of-thrones-dragontwin,1551621/
