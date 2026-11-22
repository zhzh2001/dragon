# Showcase: effects

The breath, the projectiles and what else burns, glows or flies off. Part
of the showcase record; `docs/SHOWCASE.md` is the index. The literature
these follow is in `docs/MOTION_REFERENCE.md`. The particle system and
the per-species breath profiles are in `docs/EFFECTS.md`.

## The breath as a volume (2026-11-15, 20:11)

The user on the breath: "still too simplistic", and to look at classic
fire implementations and papers. The breath was a stream of camera-facing
puffs. Puffs sum, and the tonemap scales a bright pixel back to its hue,
so the jet came out as one flat colour with a scalloped edge however the
puffs were shaded. Two attempts to soften them were dropped
(`CRITIC.md`, "After round 3, tried and dropped: a softer flame").

The player's breath is now one body of gas, marched
(`src/gfx/breath_volume.h`, `shaders/breath_volume.hlsl`), on the modern
tier.

- The spine. Up to 14 nodes are let go at the mouth, one every eleventh of
  the breath's life. Each moves as a puff did: the launch speed is solved
  against the drag so the jet's length is the damage range, and the
  buoyancy lifts it. The mouth itself is the youngest node, so the jet
  stays on the jaws and curves behind them in a turn. When the breath
  stops, what was breathed travels on and dies.
- The march. One fullscreen pass over the scene, in the pause the clouds
  already take. A pixel's ray steps by its distance to the spine's
  capsules while outside them, and by 0.22 of the local radius inside. A
  sample's density is a round profile about the spine, moved and holed by
  noise that scrolls along the jet, more toward the tip (Fuller et al.,
  I3D 2007). Eddies are sized to the jet's radius where they are.
- The light. Steps are weighted as emission and absorption weight them,
  and what is summed is the gas's heat. The pixel's colour is the mean
  heat read through a ramp: a pale core at the jaws, the profile's hot
  colour, its cool one at the rim, darker toward the tip.

Four things were wrong on the way, each found on a render:

1. The jet was a lance two degrees across. The radius came from the puff's
   size alone, and the puffs' width was mostly their scatter. The radius
   is now a cone to `size_end + range x spread x 0.6`, 14.8 m for the
   hero's fire at 155 m.
2. The flame was not there from the chase camera. A ray beside the jet
   nears it by a fraction of its distance a step, and those steps were
   counted against the march's own. They are counted apart now.
3. The puffs are launched into still air. From a dragon at 40 m/s a jet
   launched at 259 m/s and slowed by drag is never more than a few metres
   ahead of it for long, and the dragon flies through its own flame. A
   node of the volume keeps the dragon's velocity for life, so the jet
   reaches its range ahead of the dragon, as the damage cone does. The
   puffs were not changed.
4. Summing each step's own colour gave the right hues and no shapes. Every
   boundary in the gas was averaged along the ray, and the jet was an
   airbrushed glow with less in it than the puffs had. The ramp on the
   mean heat puts an edge where the mean crosses a step, and the cover is
   read through a step too, so the tongues and the silhouette are lines.

Cost, paired on the frozen frame at 1920x1080 (`DRAGON_ABLATE_ALTERNATE=
breath_volume`): 3.7 ms from the chase camera and 1.9 ms from the side as
first built, 1.1 ms and 0.8 ms after the cuts below. Those runs were not
recorded. The recorded run of the committed build (`~/src/gpu-hist`,
`research/dragon-m5/runs/20261116-0116-mac-m5-dragon-breath-volume-cost`)
was made on a busy Mac and is noisier: 0.37 and 1.51 ms from the chase
camera, 0.95 and 1.22 ms from the side. What cut it: no noise read where no
gas can be, four octaves read once in place of eight reads, and five
spine segments tried a step in place of fifteen. The window of five is
centred on the capsule nearest in metres; centred on the nearest in
radii it stepped over the thin end of the jet from far off.

The pairs and views are in `artifacts/breath-volume/`: `before-after.jpg`
(the puffs and the volume from the side, close and wide), `views.jpg`
(close, the whole jet, the chase camera, and a turn).

`--breath key=value,...` sets the dials for a run (`on=0` is the puffs),
and the Playtest panel has a "Breath" header.

Open:

- The ramp's steps make the flame a little drawn: three flat bands. More
  variation inside the bands, sparks thrown off the jet and heat shimmer
  (`MOTION_REFERENCE.md`) are the next steps.
- Only the player's breath is a volume. A rival's breath is still puffs,
  and so is every breath on a retro tier. The cones the combat code hands
  over carry no stable identity to hang a spine on.
- Every element uses the same march with its profile's colours. Only fire
  was looked at.
- The jet does not meet the ground. The depth buffer cuts it; nothing
  splashes or burns where it lands.
- Not played. The stills show the shape and not the motion.

## Fireballs as comets (2026-11-15, 20:21)

A fireball was an opaque sphere stretched along its path with two smaller
spheres behind it: three flat beads from the side. The user had named the
projectiles with the breath as "still too simplistic".

A round is now drawn through the breath's march
(`BreathVolume::draw_comet`): a spine of six nodes laid back along its
path over eight radii, the head its youngest end. So the head is pale and
whole and the tail cools and tears, by the same rules that shape the jet.
It has no state. The eight rounds nearest the camera are comets, each one
fullscreen draw that leaves all but its own pixels at the bound test. The
rest are the spheres, as is every round on a retro tier. A rival's round
keeps its colours and its growth with distance.

The pair: `artifacts/breath-volume/fireball-before-after.jpg`.

`--spit` holds the fireball alone, one every cooldown, as `--breathe`
holds the breath: `--attack` covers a round with the flame.

Open: the cost of eight comets on screen was not measured. A round has no
flash as it leaves the mouth and its impact is the old burst of
particles.

## Sparks, a softer rim, and a final dither (20:30)

- Sparks. About 70 a second leave the jet's skin as bright dashes, fast and
  outward, then fall (`--breath sparks=N`). The first version launched them
  inside the body at the gas's own pace. They never left it, and a pale
  dash on a pale flame is not seen.
- The rim. The ramp's coolest band was the cool colour at 0.55, under a
  hard step in the cover. Close up that was an ink line round the flame.
  The band is the cool colour whole, and the cover fades over a wider
  range.
- The dither. `post_composite.hlsl` adds one 8-bit step of triangular
  noise, fixed to the pixel, after the grade. It is from the showcase
  study's list (`CRITIC.md`, technique 9): a slow gradient in the sky or
  the haze breaks into grain and not bands. Every golden image moved by a
  mean of 0.33 of a step and was recaptured.


## The fire lights what is near it, and its grain moves (2026-11-15, 21:20)

Two items from the blind critic's fourth round (`CRITIC.md`): "the fire
lights nothing", and "a fine diagonal hatch or dither pattern" in the pale
core.

The light. The breath's light was one point, 46 m down a 155 m jet, with
an inverse-square falloff. The breather's own head was 46 m from it and
took about a third of the sun's light; the ground under a low pass took
less. The light is now a line: the jet's bright half, from the jaws on.
The sum of `1 / r^2` along a segment has a closed form, the angle the
segment subtends over the distance to its line, so `local_light`
(`scene_common.hlsl`) costs two arctangents. A line's light falls with the
distance and not its square. It reaches the chest and the wings of the
dragon that breathes, and the banks under a pass at six metres.

The scene block had no spare vector for the line's direction. It is in
`thermal_count.zw`, octahedral, and the line's length is five sixths of
the light's reach. The strength is `light` in the breath's settings
(`--breath light=8`, the Playtest panel's "light on the dragon and the
ground", the Engine panel's "breath light"): a metre of flame's light at
a metre, 8 by default. The old dial's 900 was a point's intensity, and
its unit is gone. `artifacts/breath-volume/light-before-after.jpg` has
the head and chest from the side and a low pass over the river.

The water takes none of it: `water.hlsl` does not call `local_light`.
The banks glow and the river between them stays dark.

The grain. The march's start was offset by one fixed value a pixel, and
held still it was a hatch. The offset now changes every frame (the same
interleaved gradient, shifted by the frame's count modulo 64). In a
still it is grain; in motion it is the flame's flicker. The `fire`
golden was captured again; no other scene moved.

Not done from the same list: the jet's length against the dragon's, the
fireball's compact head, the jet's attachment in a hard turn.

## A fireball with a head (2026-11-15, 21:24)

The blind critic on the comet: "a long streak, not a ball: a beige-orange
smear with a brown rim", "no bright core", darker than the sunlit wall
behind it. Three causes, all in `BreathVolume::draw_comet`:

- The spine was eight radii long and its tail a third of the head across,
  so the round was one long cone. It is now five radii, spaced close at
  the head, and the radius falls as the square of the way back: a ball,
  then a point.
- It marched at the jet's opacity, which is set for a body of gas metres
  across. A round two metres across was half see-through and took the
  colour of the hill behind it. A comet now has three times the jet's
  extinction.
- The jet's heat is in its lines: a quarter of the axis is hot and the
  rest is the body's orange. A head needs solid heat. The shader takes a
  floor on the heat about the axis where the round is young
  (`jet.axis.w`, 1 for a comet and 0 for a jet), so the middle of the
  head reads through the ramp's pale end and its rim through the orange.

`artifacts/breath-volume/fireball-head-before-after.jpg` is the same shot
from 75 m, before and after. The `fire` golden was captured again.

The round's size is still the collision radius times 0.8. The reference
study has a fireball the size of the head; ours is about twice that.

## A released flame burns out (2026-11-15, 21:27)

The critic's turn frame showed a flame "not attached to the mouth", "a
separate orange cloud", "like an explosion sprite". In that frame the
run's fire meter had just emptied. The breath had stopped, and what was
breathed travelled on at full brightness for its whole life, 1.3 s.

A node that is not fed now ages 2.4 times as fast
(`BreathVolume::update`). Age is what cools the colour, opens the gaps
and darkens the tip, so a released jet goes to deep orange and smoke and
is gone in about half a second. While the breath is held, nothing
changes: the youngest node is the mouth, and the jet stays on the jaws
through the turn (checked at frames 100 and 130 of the same run).

Still open from the fourth round: the jet's length. It is the breath's
range, 155 m for the hero against a body 12 m long, and it widens to
14.8 m at the tip. Shortening what is drawn would leave the flame short
of what it burns, so this is the range's question and the playtester's:
"width at the tip" is in the Playtest panel, the range is in the species'
breath profile.

## The river takes the fire's light, and the lens leaves the flame alone (2026-11-15, 21:33)

The river. `water.hlsl` did not call `local_light`, so under a low pass
the banks glowed and the water between them stayed dark. It now takes the
light twice. The bed and the water's own colour are lit through the
surface (0.4 and 0.12 of the light on the bank: more of either turned the
teal water green). And the flame has a streak in the mirror:
`local_light_mirror` (`scene_common.hlsl`) finds the point of the jet's
line that the mirrored view ray passes nearest and puts a lobe about it,
an eighth of the flame's light at a steep view and all of it at a
grazing one. The ripples in the water's normal break the streak up.

The lens. The ghosts of the lens flare are the bloom's bright spots
mirrored through the centre of the frame. A breath of fire is a bright
spot a tenth of the frame across, and its ghosts were dull magenta discs
hanging over the flame; the blind critic's fifth round called them "a
bad-blend artefact". A ghost's source now counts only within about a
quarter of the frame's height of the sun (`lens_at_sun`,
`post_composite.hlsl`). The sun's ghosts are as they were: no golden
scene but `fire` moved, and it was captured again.

## The pale core at the jaws, and a skin of smoke (2026-11-15, 21:37)

The blind critic's fifth round: "the pale core sits mid-plume, not at the
mouth", and the flame has "no dark, broken, smoky edge".

The core. The heat fell as one minus the age to the power 1.25. A node's
age is not its distance: the gas slows, so half the plume's length is
flown in the first third of its life. Where the jet was wide, 60% of the
way down, the gas was still young enough for its tongues to read through
the ramp's pale end, and from 60 m that was a second, larger core. The
power is now 3.5 (`BREATH_COOLING`, `breath_volume.hlsl`), and the
tongues' gain over the body falls with the age. The pale colour is at
the jaws and thins to streaks over the first third; the rest is the
body's orange with its lines.

The skin. Soot was only at the tip, past half the gas's life. Past the
first of its life the jet now also wears it on the outer part of its
radius (from 0.55 of the radius, full at 0.95), wherever the gas there is
cool. A ray that grazes the jet meets only the skin and comes out dark
red-brown; a ray through the middle meets the skin thinly and then the
fire. The strength follows the same dial as the tip ("soot at the tip",
`smoke`).

`artifacts/breath-volume/core-and-skin-before-after.jpg` has both, at
16 m and at 60 m. The skin is a darker edge, not yet smoke that breaks
away: a trail of smoke behind the plume is still not drawn for the
marched breath.

## Smoke off the marched breath (2026-11-15, 21:53)

The puffs left smoke (`smoke_for`: fire's soot, frost's vapour, stone's
dust). The marched breath did not, so the plume ended in its own orange
and left nothing behind. It now emits smoke puffs from the skin of the
far third of the jet: slow, rising, 1.6 to 2.6 s of life, in the
element's smoke colour, at 50 times the element's rate a second (7.5 a
second for fire).

It is light on purpose, and far from the jaws. From the chase camera the
jet is seen end on, so smoke anywhere along it is in front of the view.
The first version, from the far half at 12 a second, put a grey veil over
the way ahead, which is what the playtest of 2026-10-21 called too much
fog. `artifacts/breath-volume/smoke-before-after.jpg` is three seconds of
breath from 120 m, before and after.

The new random draws shift the particle sequence, so the `fire` golden's
streaks and embers moved; it was captured again.

## Fire through a sensor (2026-11-20, 17:53)

The flame was three flat tones: a cream blotch, one orange, a brown rim.
It read as a painted shape, and it was no brighter than the sunlit rock
behind it. The cause was in two places.

- The composite's tonemap keeps hue. Any fire colour above about 1 shows
  as its hue at full strength, so brightness inside the flame carries no
  form. Only hue and darkness do.
- The shader read the mean heat of the ray through a stepped ramp. A mean
  is nearly the same across the jet, and the steps were the only shapes.

Two attempts kept the mean and failed on a render. Summing each step's
colour through a continuous ramp gave one airbrushed orange, as the first
build of the volume had (`f1` to `f3` in the session; not kept).

What works is what a camera does. Hot gas is thin and its light adds along
the ray. The sum then goes through a curve that saturates one channel at
a time, `1 - exp(-L)` on each of red, green and blue. An orange light
that is weak shows deep orange; stronger, its red is full and the green
catches up, and it shows yellow; stronger still, pale. So the depth of
gas and its heat show as hue, and the form of the flame comes out of the
sum without a ramp. The result is divided by one minus its luminance, the
inverse of the composite's curve, so what is shown is what was meant and
a bright flame is bright enough to bloom.

In `breath_volume.hlsl` (`summed`, when `hull_b.w` is 1):

- The temperature: the old cooling along the jet, a round profile, and
  the broad noise cut into billows with creases between. A metre of gas
  emits as the fourth power of it, more on the ridges of the fine noise
  (the tongues), and up to five times more at the jaws.
- The hue of a sample goes from the profile's cool colour to its hot one
  with the temperature. Every element uses the same curve: frost goes
  from blue to white, blight from green to pale yellow.
- Gas under a temperature of 0.3 is smoke. It adds no light of its own,
  takes a little of the day's, and hides what is behind it. `smoke` sets
  how much. The far third of the jet goes dark and lifts off the top.
- A flame bright enough to saturate hides the scene behind it. A thin one
  adds to it.

Against the open points above: the pale core is at the jaws from every
view, and from the chase camera the jet is a pale centre in an orange
ball behind the horns. Smoke breaks from the far end. A fireball's head
is pale and whole, with a halo from the bloom; its tail is smoke.

`artifacts/breath-volume/sensor-before-after.jpg`: the old shading on the
left, from 16 m, from 60 m and from the chase camera, cropped and not
scaled.

Cost, paired on the frozen frame at 1920x1080 with two Blender jobs
sharing the Mac: 1.22 ms from the chase camera and 0.41 ms from the side,
against 1.12 and 0.53 ms for the old shading (`~/src/gpu-hist`,
`research/dragon-m5/runs/20261120-2254-mac-m5-dragon-breath-sensor-cost`,
one run each). An earlier unrecorded pass gave 1.34 and 1.32 ms against
1.59 and 1.30 ms. The two shadings are not told apart at this noise. The
new path reads no more noise than the old one.

`--breath style=0` is the old shading, and so is the Playtest panel's
"light as a camera sees it" when off. The `fire` golden was captured
again; no other scene moved.

Open:

- The body is soft from 16 m. The noise is value noise four octaves deep,
  and its finest eddy is a third of the jet's radius.
- Stone's breath is dust and glows like a flame. Storm's starts well
  ahead of the jaws, as it did before.
- A fireball's tail goes straight to smoke. A short flame between the
  head and the smoke would be better.
- Not played. The stills do not show the motion.

## The ground burns (2026-11-20, 18:55)

The jet did not meet the ground: the depth buffer cut it, and nothing
burned where it landed. The reference study's two dragons both leave "a
burning line on the ground for 1.7 s and more" (`MOTION_REFERENCE.md`,
"Breath and projectiles in the references").

Where the jet meets the ground it now leaves patches that burn
(`BreathVolume::ignite`, `draw_ground_fires`):

- The contact. Each frame the app tries fourteen points along the jet
  against the terrain under them. A point within 0.6 of the jet's radius
  of the ground lights a patch beside it, up to 0.6 radii off the jet's
  line, so a pass leaves a ragged strip. Not on water.
- A patch. 1 to 5 m in radius, burning 2.4 to 4 s (`ground_fire`, 3.2 s
  in the middle). A point that lands within 1.15 radii of a burning
  patch feeds that one, which burns on from near its start. At most 48
  burn; the oldest goes first.
- The flame. Three nodes of the same march: a base, a middle and a tip,
  1.3 radii high (`ground_height`), leaning and flickering by its own
  seed. Age is what cools and tears the gas, so the base is young while
  the patch is fresh and the tip is always old: a pale foot, an orange
  body, smoke off the top. It catches in a fifth of a second, and it
  sinks and narrows as it burns out.
- The draw. The fifteen patches nearest the camera, five to a fullscreen
  draw, before the jet.

`artifacts/breath-volume/ground-fire-before-after.jpg`: where a dive's
jet landed, 1.3 s after, without and with the fire, cropped and not
scaled. The command is `--breathe --input -0.35,0,0,0,0,0 --frames 200
--cam 440,85,-2225,525,46,-2290`; `--breath ground_fire=0` is without.

Cost, paired on the frozen frame of that view at 1920x1080
(`DRAGON_ABLATE_ALTERNATE=breath_volume`, one run, not recorded): 2.76 ms
for the jet and fifteen patches together, about 1.4 ms over the jet
alone. Five patches share a draw, and its bound is one capsule about all
five, so a ray between two patches steps through air.

The Metal goldens pass without a new capture: no golden scene's jet
reaches the ground.

Open:

- The patches throw no light on the grass. The breath's light is one
  line (`local_light`), and it is the jet's.
- Nothing is left when a patch burns out: no scorch, no smoke that
  lingers.
- A rival's breath lights nothing, and neither does a fireball's impact.
- A patch on a steep slope stands straight up from one point of it.
- The breath's fuel lasts about three seconds in the arena, so a strafe
  lights a short strip.
- The cost wants a bound a patch.

## A jet that surges (2026-11-20, 19:56)

The blind critic's seventh round on the flame from the side: "an even,
cream-orange tube of constant width", "a beam". Two changes to the summed
shading (`breath_volume.hlsl`, `summed_style`):

- The width pulses. A sample's distance from the axis is scaled by 0.72
  to 1.42 from a noise read along the jet alone, which travels with the
  gas: swellings a few radii long, with necks between them.
- The gas is torn from the jaws on. `wear` starts at 0.62 (it was 0.4),
  and the noise may carry the edge 0.7 radii out (it was 0.45), so
  tongues leave the body. The jet's bounds grew by the same.

One thing was wrong on the way. The pulse first read the scrolled
distance along the jet divided by the local radius. The scroll is
hundreds of metres and the radius changes along the jet, so the quotient
raced, and the jet near the jaws was a comb of bars. It now reads the
noise's own octave coordinates, this scale's and the next one's blended.

`artifacts/breath-volume/surge-before-after.jpg`: the critic's two side
views, before on the left, cropped and not scaled. The jet has a neck
and a swelling and a ragged lower edge. It is still a cone: the change
is plain beside the old frame and modest alone. The `fire` golden was
captured again.

Cost, paired on the frozen frame at 1920x1080, two runs each on a shared
Mac (not recorded): 1.93 and 1.99 ms from the chase camera against 1.59
and 1.57 ms for the old shading in the same minute, and 0.35 and 0.41 ms
from the side. The wider bound and two more noise reads cost about
0.4 ms where the jet is seen end on.

Open: tongues that break off and burn out alone, and an end that rolls
up, want gas that leaves the spine. That is particles lit as the volume
is, or a second, slower layer of nodes.


## A breath that is not fire (2026-11-20, 23:38)

The playtester: "the fire effects don't fit on other elements, and blight
tanks fps significantly". Every element went through the flame's shading:
light summed along the ray and put through a sensor curve. Frost was a
blue flame, blight a yellow one, and each left fire on the ground.

Fire and storm give light. The other four are matter. `BreathJet::medium`
marks a jet as matter, and `breath_volume.hlsl` then shades each step as
a body the sun and the sky light:

- The colour is an albedo, pale on the axis (`whiten` takes it toward
  white) and the breath's cool colour at the rim.
- The light is the world's own, through `direct_sun` and
  `ambient_light`. The normal is the direction out from the spine's
  axis, bent by the noise, and the creases between billows are in shade.
- Each step hides what is behind it. There is no sensor curve, no smoke
  and no bloom.
- `glow` adds a share of a flame's light in the core.

| Element | Reads as | whiten | glow |
|---|---|---|---|
| Frost | mist | 0.45 | 0.45 |
| Blight | gas | 0.1 | 0.12 |
| Tide | spray | 0.5 | 0.12 |
| Stone | grit | 0.2 | 0.1 |

The values are in `App`'s feed of the jet. Only fire sets the ground
alight now. Before and after: `artifacts/breath-volume/elements-before-after.jpg`
(the old storm frame is empty: the capture came after its breath ran out).

Cost. A cloud has no tongues to resolve, so a medium takes steps of 0.3
radii (the flame's are 0.22), at most 30 of them, and skips the surge's
two noise reads. Blight's cloud across the middle of a 1080p frame, 2.5 s
into a breath, cost 3.2 ms with the flame's steps and 2.6 ms with these
(`DRAGON_ABLATE_ALTERNATE=breath_volume`, frozen at frame 150, on an M5
that other work was loading). At 0.36 radii it was 2.5 ms and the cloud
showed a hatch. At the start of a breath blight costs 1.65 ms, fire 2.14.

Measured again at 23:39 with the machine quiet (load average 4): blight
1.91 ms at frame 150, frost 1.80 and fire 2.60 at frame 80. The record is
`~/src/gpu-hist`, `20261121-0439-mac-m5-dragon-element-breath-cost`.

Weak: the four are soft clouds of one build. Tide does not fall as water
and frost leaves no rime. Storm is still a violet flame and not an arc.

## Six breaths, six materials (2026-11-21, 00:58)

The playtester, after "A breath that is not fire": "the breaths still look
strange other than fire/storm. please reference other games like glyde for
more realistic breath types". A search found little written on how other
games draw their breaths (`docs/MOTION_REFERENCE.md`, "Breaths that are
not fire"). What it did give: separate the elements by what they do to
density and light, not by colour. Fire rises and emits. Frost sinks and
scatters. Gas sinks slowly and stays. Lightning is a line with no volume.
Water is a coherent jet that falls and breaks up. Grit is matte and
ballistic.

`App::look_for` holds each element's look past its profile's colours:

| Element | The jet | Thrown off it | Left on the ground | Light on what is near |
|---|---|---|---|---|
| Fire | flame, summed light | sparks | marched flame patches | 1 |
| Frost | white fog, falls | slow pale glints that fall | a white cloud, 7 s | 0.12 |
| Blight | gas, 5 m/s2 heavier than its profile | a few green drips | a green cloud, 10 s | 0.1 |
| Storm | arcs; the volume at 45% is their glow | fast short sparks | nothing | 1.5 |
| Tide | a jet 0.6 as wide, white to the rim, falls | many drops | a pale mist, 2.5 s | 0 |
| Stone | matte grit, glowing only at the jaws | chips that fall | dust, 5 s | 0.15 |

Storm is drawn by `App::emit_storm_arc`: a line from the jaws to 82% to
100% of the range, broken by midpoint displacement into 32 pieces, each
piece a thin white streak inside a wider violet one, alive for three
frames. Twenty-eight arcs a second flicker. One arc in two throws a fork
of seven pieces. A streak is brightest at its middle, so pieces laid end
to end were a row of dashes; each is drawn 2.2 times its length.

What a medium leaves on the ground is smoke puffs, twelve a second for
the whole contact, each spreading to 2.4 times its size. They were first
marched patches, as fire's are. Fifteen of them about a landed dragon
covered the screen three times over: 11.6 ms a frame at 1080p for blight,
and 8.8 ms at 16 steps with two noise octaves, where the steps also
showed as arcs. As smoke the same frame costs 0.7 ms with every particle
in the scene, and 0.17 ms measured again on the broad-wing hero
(`~/src/gpu-hist`, `20261121-0556-mac-m5-dragon-blight-ground-cloud-cost`).
This is the frame-rate drop the playtester met with blight.

A medium's jet reads two noise octaves, not four, and takes at most 24
steps. Sheets: `artifacts/breath-volume/elements-materials.jpg` (top row
the first pass, bottom row this one).

Weak, and not done:

- Not seen by a playtester yet. The ground clouds were checked in one
  headless view, a frost jet in a dive; the breath's meter runs out
  before most staged views reach the ground.
- Nothing stays on the ground itself: no rime, no wet ground, no scorch
  from an arc, no pool of acid. Each needs a mark the terrain shader reads.
- Tide does not refract and has no bright line along it. It is a teal
  spray, not water.
- The arcs go where the breath points. They do not fork to the target or
  to the ground, and nothing flashes when one lands.
- The rivals' breaths are still puffs in their element's colours.

## A second style for each breath of matter (2026-11-22, 00:20)

The playtester on the six materials: "elemental breaths still look
strange in default view (not side view) ... they still look like clouds,
e.g. frost doesn't seem like ice, tide unlike water. storm is pretty
good."

The default view is the chase camera, which looks down the jet's axis. A
lit cloud seen end on is a ball of fog in front of the head, whatever its
colour. Storm reads because it is made of separate bright pieces. So each
of the other four now has a second style built from pieces, on by
default. `--breath frost_style=0` (and `tide_style`, `blight_style`,
`stone_style`) gives the cloud back, and the Playtest panel's "Breath"
has the four dials under "What each breath is made of".

| Element | Style 1 | How |
|---|---|---|
| Frost | a thin blue vapour full of snow grains and ice slivers | the volume at 0.22 of its opacity and 0.75 of its width; 520 opaque white grains a second that ride the jet and fall slowly; 520 slivers a second, short lines of light that lie any way, one in eight bright |
| Tide | white water: a rope with an edge that breaks into foam | `BreathJet::liquid`; 520 small white drops a second |
| Blight | a stream of acid | the same liquid, keeping its green; 90 drops a second; the gas cloud only where it lands |
| Stone | gravel in thin dust | the volume at 0.4 of its opacity; 380 dark stones a second, 0.25 to 0.6 m across |

A liquid in the marched volume (`breath_volume.hlsl`): the noise moves
its edge a third as far, the density has an edge 0.16 radii wide, and the
body breaks into drops from the first sixth of its length on. It is
opaque in a hand's width, so the first sample inside it is its surface.
That sample is shaded as water: dark and of its own colour looked into,
the sky at a grazing angle, a sun glint, and foam where one more read of
the noise, drawn out along the jet, is high. Foam grows with the
distance flown. The more a liquid whitens, the less of the breath's hue
its body keeps, so water is blue-grey and acid stays green.

A sliver's length lies any way, not along its flight. Along the flight
every sliver pointed away from the chase camera and drew as a dot.

Cost at 1080p, paired, the breath and the particles together
(`DRAGON_ABLATE_ALTERNATE=breath_volume,particles`, frozen at frame 110,
load average 8 to 9): frost 2.34 ms (the cloud 2.30), tide 2.12 (2.26),
blight 2.03 (2.25), stone 2.29 (2.11). No style is shown to cost more
than its cloud. A first, unlogged run under a heavier load printed 0.6
to 4.9 ms for the same eight. The record is in `~/src/gpu-hist`,
`20261122-0512-mac-m5-dragon-breath-styles-cost`.

Sheets: `artifacts/breath-volume/styles-chase-before-after.jpg` (the
chase camera, the cloud on the left) and `styles-side.jpg`.

What is weak. These are first prototypes, made for the playtester to
choose from:

- Tide from the side is white with teal cells, not strands. From behind
  it is a white clump over the head.
- Frost's slivers are few on screen at any moment. The snow carries it.
- Blight widens to a broad green sheet by its middle.
- Stone's gravel is sparse and the dust still leads.
- None of them marks what it hits. Ice is known by what it leaves.

Options not built, in the order I would try them:

1. Marks on what is hit: rime and ice spikes that grow where frost
   lands, a wet dark patch and a splash for tide, a pitted smoking patch
   for acid, rubble for stone. This needs a decal or a small instanced
   mesh path, which the engine does not have. It is the largest gain
   for frost.
2. A bolt in place of a jet for frost and stone: one shard of ice or one
   boulder a third of a second, a mesh that flies and shatters. The
   fireball's path already exists.
3. The tide as a mesh: a tube along the jet's spine with refraction of
   the frame behind it, in place of the marched body. This is how water
   reads as water; the marched liquid has no refraction.
4. A cone of frozen air for frost: nothing in the air but a shimmer, and
   everything inside the cone whitens with rime (a screen-space tint on
   the terrain and the trees). Cheap, and unlike any other breath.
5. Sound: none of the four has a voice of its own yet.

## Breaths of matter as pieces (2026-11-22)

The playtester on the second styles, the same day they went in: "breaths
just look bad in motion in general", and on reports 0021 to 0024, each a
side view of one breath: "too white, still cloud like instead of
acid/poison", "similarly cloud like, unlike water", "more like sand
instead of stone", "cloud like". Then: "please implement the breath
options, we can revert if bad".

A marched body with a soft edge is a cloud whatever its colour and
whatever rides in it. So the four breaths of matter have a third style,
2, the default now, in which the breath is solid pieces. Each piece has a
hard outline and a highlight, flies its own path and leaves something
where it lands. Fire and storm do not change.

| Element | Style 2 | What it leaves |
|---|---|---|
| Frost | 22 ice lances a second, 2 to 4.5 m long, tumbling end over end at 80 m/s over the dragon's own speed | Chips where a lance bursts. On the ground a patch of rime, and half the lances stand in it as a spike for 14 s |
| Stone | 10 boulders a second, 0.4 to 0.9 m in radius, thrown 9 degrees above the aim, tumbling | Five pieces of rubble each, which bounce and lie for 10 s, and a patch of grit |
| Tide | One stream: a tube 0.45 m in radius at the jaws that arcs down under gravity, clear at the jaws and white with air further out, with 320 drops a second off it | A wet, darker patch |
| Blight | The same as acid: thinner, green, less air in it | A patch eaten black, and a wisp of fume from it |
| Frost, style 3 | Frozen air: nothing in the jet but diamond dust | The ground under the cone turns white, and six spikes a second grow out of it |

Fire leaves soot now where the ground burned, through the same marks.

### How it is built

- `game/breath_matter.h` is the motion and the bookkeeping, with no
  renderer in it: the pieces, their flight, the ground, the water and the
  targets they meet, the marks, and an impact list the app answers with
  sparks and dust. `tests/test_breath_matter.cpp` covers it.
- `shaders/breath_matter.hlsl` draws a piece: one mesh a piece through
  `WorldRenderer::draw_matter`. `mesh.hlsl` lights a surface with the
  sun's diffuse term and the sky's, and ice lit that way is chalk. This
  shader adds a tight highlight, the sky mirrored at a grazing angle,
  light through the body, and flat facets from the position's
  derivatives. The crystal and the three boulders are built in
  `gfx/primitives.cpp`.
- `shaders/breath_stream.hlsl` draws a liquid as one tube. The mesh is a
  unit cylinder; the vertex stage bends it along a Catmull-Rom curve
  through up to 24 nodes, which are the stream's slugs in flight,
  youngest first, with the mouth as the first node while the breath is
  held. The fragment stage runs ridges down it at the water's own speed,
  whitens it with distance, and tears its last half into strands.
- The marks are sixteen discs in the scene block (`marks`, `mark_looks`,
  `mark_count`). `terrain.hlsl` tints the ground's colour inside each,
  with a torn edge. Rime is bright and cold, so the terrain's own snow
  glint lights it. Marks near each other of one element are one mark.
- `App::feed_player_breath` holds everything a held breath does for a
  frame. The fight calls it, and the animation studio calls it too.

### What was tried and dropped on the way

- The lances flew point first. From the chase camera, which looks along
  the breath, each was a dot. They tumble now, so a lance shows its
  length for most of each turn.
- The liquid was first a string of ellipsoids, one a slug. Blended, each
  had its own rim and the stream was a row of lily pads. Solid, each had
  its own highlight and the stream was a bag of pills. A stream is one
  body: the tube replaced them.
- With the pieces in, a ball of fog still sat over them in the chase
  view. It was not the marched volume (its cover was already 0) but the
  smoke puffs every breath sheds from its far third (`smoke_for`). A
  breath of pieces sheds none. The playtester's "strange half transparent
  lens like" shapes in the breath are these soft discs and the second
  styles' chips, as far as could be told from stills: styles 2 and 3
  draw neither.
- Thrown level, a stream is below the aim all the way, and from the
  chase camera it was behind the dragon's own head. What falls is thrown
  9 degrees above the aim (`loft_deg`), as a hose is held: it rises into
  view and comes down near the breath's reach.

### The fire's square holes

Report 0020, on a jet seen from the side after the breath was let go:
"artifacts, unlike fire". The dark holes in the flame were squares. The
four noise octaves were read on one lattice, so their cells lined up, and
where the noise eats a thin jet the holes kept the lattice's shape. Every
other octave is now read on a lattice turned off the first's
(`breath_volume.hlsl`). The turn goes by the octave's own number, so a
scale's octaves stay the same ones as the jet widens.
`artifacts/breath-volume/fire-noise-before-after.jpg`, before on top.

### What is weak

- From behind, a liquid is still small: a stub over the head with drops
  round it. Its width at the jaws, its loft and its drops are dials.
- The breath's damage is still the combat cone's. A piece that reaches a
  target bursts there for show, and a boulder that misses on screen can
  still have hit.
- Marks tint the ground only. Grass and trees inside a mark keep their
  colour, and a spike can stand in a bush.
- Sixteen marks is a strafing pass of about 60 m; the oldest goes first.
- No sound of its own for any of them yet.
- The rivals' breaths and the retro tiers keep the puffs.

Sheets: `artifacts/breath-volume/pieces-chase-before-after.jpg` and
`pieces-side-before-after.jpg` (style 1 on the left), and
`pieces-grounded-studio.jpg` (all six from the standing studio dragon,
with what each leaves).

The Playtest panel's "Breath" has a dial an element (0 the cloud, 1 the
cloud with bits, 2 pieces, and 3 for frost's frozen air), "marks on the
ground", and under "the pieces" every rate, speed and size above.
`--breath frost_style=1` and the like do the same on a capture.
