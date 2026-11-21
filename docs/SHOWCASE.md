# Showcase: the second gap study, and the vertical slice

Written 2026-11-14, from `main` at `e48ea61`. `GAPS.md` is the first study
(2026-10-21) and stays as the record of G1-G9. This file replaces its
section 6 as the order of work for the modern tier's picture. `FIDELITY.md`
stays the record of the rendering techniques. The retro tiers are out of
scope here.

## Where it stands (2026-11-20, 19:30)

Read this first when you pick the work up. The 26 suites pass and the
Metal goldens match on `main`. The session of 2026-11-20 put the Tripo
models of 2026-11-16 into the game and changed the fire.

The standing direction (the user's, 2026-11-15): the visual showcase
first, gameplay later; the dragon, its animation and its effects before
the world; ignore the rings and the placeholder sphere.

What 2026-11-20 built, each with its own section:

| Work | Where |
|---|---|
| The flame's light summed along the ray and saturated a channel at a time, as a sensor does: a pale core, an orange body, smoke at the tip. The fireball too | `showcase/EFFECTS.md`, "Fire through a sensor" |
| Fire left on the ground where the jet lands | `showcase/EFFECTS.md`, "The ground burns" |
| Five Tripo wall crags, 50 m high, in rows on the steep faces; a detail layer on large rocks; level-of-detail limits in metres | `showcase/WORLD.md`, "Wall crags in rows" |
| Five Tripo landmarks (a ruined tower, an arch, a stone circle, a monolith, a cairn) and five more rocks | `showcase/WORLD.md`, "Landmarks, and five more rocks" |
| Talus mounds and rubble at the feet of the crags and landmarks | `showcase/WORLD.md`, "Scree and rubble at the feet" |
| Blind critic round 7 | `showcase/CRITIC.md`, `artifacts/showcase-critic/round7` |
| The Tripo hide on the hero, tried: no visible gain | `MODEL_GENERATION.md`, "The hide trial" |
| Three Codex conversions on `gpt-6.2-astra`: crags and rocks, landmarks, prey | `tools/rockkit.md`, `tools/landmarks.md`, `tools/prey.md`; the briefs are in `tools/showcase/` |

Waiting on the user:

- A yes or no on new wing geometry for the hero (slack modelled into the
  membrane, thinner fingers, a fold that creases on the ground). Seven
  critic rounds name the wings, and shader and rig tuning has not changed
  that. It is a Codex asset job. Do not start it without the answer.
- The playtest of the new picture: breathe in a low pass and in a dive
  at the ground (F; the flame, and the fire it leaves), spit (G), fly a
  run past the crag rows (`--run 3`), and find a landmark. And still open
  from 2026-11-15: the beat (`--studio 1`, and Space held in play).
- Calls that are the user's: the flame's length (155 m, about nine
  dragons); how much the plume smokes from the chase camera; the wind
  lines, which the critic read as "scratches on the lens" in five
  frames; the landmarks' size (a tower 35 to 48 m high) and how many
  stand in a valley (about 20).
- The Tripo hide on the hero: the comparison shows no gain, so the swap
  was not made. Say so if it is wanted anyway.

If no answer has come, the next items, in the critic's order:

1. The join between rock and ground: a band of the terrain's own rock
   colour under each crag row (it needs a mask the terrain shader
   reads), and scree that runs up a face.
2. The jet from the side: "an even tube", "a beam". A width that pulses,
   tongues that leave it, an end that rolls up.
3. The rock kit's memory: 87 maps at 1024, not compressed, about 490 MB.
   Block compression before more pieces go in.
4. The river's stair-stepped edge from above, and the fire's reflection
   in it ("a coarse checker of orange pixels").
5. `bank_a` along the river; the keep, the bridge and the dragon
   skeleton, which failed conversion and would make places of the
   landmarks.

Things a new session would otherwise learn the slow way:

- The blind critic's scores do not compare between rounds to better than
  half a point: a different agent scores each round. Use its wording, and
  its ranking within one round.
- A view chosen to show one thing from far off scores as a poor picture.
  Give the critic framed shots.
- `--inspect 180` is the front and `--inspect 0` the rear.
- The hero is `--model assets/embercrest-hero.glb`. The default roster
  flies the generated Embercrest, which is what the goldens show.
- Under a tonemap that keeps hue, brightness inside a bright thing
  carries no form: only hue and darkness do. The flame's form came from
  saturating its summed light a channel at a time, not from a ramp.
- A texture sample inside a branch has no screen gradients and reads the
  smallest mip. Take `ddx` and `ddy` outside and use `SampleGrad`
  (`rock.hlsl`, `rock_detail`).
- The kit chose a level of detail by distance in the piece's own radii.
  A piece 100 m across then drew its full mesh out to 1.5 km. The levels
  have limits in metres now.
- Tripo's crags are rounded lumps. The critic reads them as "loaves". A
  cliff band wants faces with joints, which Tripo did not make in three
  tries; a row of crags is rock on a wall, not the wall.
- The breath's fuel lasts about three seconds in the arena. A capture of
  the breath after frame 200 shows none.
- A Codex job's Blender renders can hold the Mac at a load average over
  80. A `gpu: median` taken then is not a measurement; even paired runs
  wandered by 13 ms. The crags' scree and rubble want a clean cost run.
- Codex on `gpt-6.2-astra`: 414 k and 477 k tokens for the two kit jobs,
  45 to 49 minutes each, 4% of the work profile's week together. Both
  stayed inside their worktrees and rejected their own weak pieces with
  renders.
- The Vulkan, D3D12 and D3D11 golden sets are stale since 2026-11-15.
  They need `golden.py capture` on x99.
- The hero's build cache is `build-hero-cache/` in the main checkout
  (3.1 GB, ignored). `tools/hero-build.md` says how a worktree links to
  it.

The record is in parts since 2026-11-15. This file holds the study, the
decisions, the order of work and the open questions. What was built is in
`docs/showcase/`:

| File | What is in it |
|---|---|
| `showcase/HERO.md` | The hero dragon's model: the design, mesh versions 1 to 5b, the rig, the surface |
| `showcase/PLAYTEST.md` | The feedback tool, every round of playtest reports, and the legs, the wingbeat and the strikes they changed |
| `showcase/WORLD.md` | The river, the valley walls, the bushes, the rock kit, the frame's cost |
| `showcase/CRITIC.md` | What other showcases do, and the six blind critic rounds with what each changed (cliffs, the braided river, the ward stone, the breath's colours, the air, the grass; then the wings and the fire) |
| `showcase/EFFECTS.md` | The breath as a ray-marched volume, fireballs as comets through the same march, the breath's light, smoke and sparks |

A new section goes in the part it belongs to, at the end, with its time
from `date`. "Where each section is", at the end of this file, maps every
section name a code comment or another document cites to its part.

## Why a second study

The rounds after `GAPS.md` moved dials inside the existing picture: the
hour, the mist, the camera distance, the hide's grade. The playtester's
verdict on 2026-11-14 was that the changes were minor and the frame still
looks amateur. Six fresh captures (`artifacts/showcase-study/now-*.jpg`)
show why. The frame holds three art styles at once, and no dial removes
that:

- The dragon is a dense generated mesh with a baked, blotchy hide.
- The trees, bushes and grass are flat-shaded low-polygon shapes.
- The mountains are smooth noise with a stretched texture and boulders
  that sit on the slope like pasted discs.

The sky, the sun and the aerial haze are the one part that holds up beside
a published game.

## Decisions (the user's, 2026-11-14)

| Question | Decision |
|---|---|
| Art direction | Realistic, as far as the assets allow. Stylized realism is the floor and realism is the ceiling. Painterly was shown and not chosen |
| Dragon scope | One hero dragon first: Embercrest rebuilt. The other six species follow the proven pipeline later |
| Animation | Authored clips for cycles and actions, with the procedural rig on top for aim, the terrain plant, the tail and secondary motion. Spyro Reignited is the reference for the principles, not for the cartoon proportions |
| Showcase | One vertical slice: one valley, one hour, one dragon, finished |
| Asset sources | AI generation with Blender cleanup, CC0 libraries (Poly Haven), and geometry built in Blender by script. No paid packs |
| Cadence | Long autonomous rounds. Each ends with before and after crops and numbers |
| Delegation | One brief goes to Codex (`gpt-6-astra`, work account) and to a Claude subagent. The renders decide who gets the rest |

This revises `DIRECTION.md` in two places. Section 3's "painted realism"
becomes the floor, not the target. Section 5's "the roster of seven is done
for now" no longer holds for Embercrest.

## The targets

Three frames of the same shot were generated from a HUD-free engine capture
as the composition reference (GPT Image, through `image-use --backend web`).
`artifacts/showcase-study/style-sheet.jpg` has all three beside the build.

| File | What it sets |
|---|---|
| `concept/showcase/slice-target-floor.png` | The floor. Large readable scale plates, a visible eye, membranes lit from behind, cliff bands and scree fans in broad masses, forest in clumps with two greens, a river with gravel bars |
| `concept/showcase/slice-target-ceiling.png` | The ceiling. Dense scales with varied gloss, veined membranes, strata and talus on the rock, forest with understory, white water, mist in the side valleys |

Both targets show the dragon from behind with the hind legs trailing and
the claws spread. Both add detail the engine's valley does not have, so
they set material and density, not layout.

## The gaps, worst first

Each row names the evidence in `artifacts/showcase-study/`.

### 1. Three styles in one frame

`now-ground.jpg` is the clearest case. A flat-shaded tree with a brown
prism trunk, block bushes and blade grass half the dragon's height stand
beside a dragon with pore-level texture. No published game in section
"Against published games" mixes shading models this way. Every later gap is
smaller than this one, and the fix is a rule: every asset in the slice
carries a base colour, a normal map and a roughness map, and lights through
one material model.

### 2. The dragon is one mesh with one material

`assets/embercrest.glb` is 80,000 triangles, one primitive, one material,
62 joints and no clips. It has base colour, normal and roughness maps, and
`skinned.hlsl` has a specular lobe, but:

- The eye is painted into the hide. `now-head.jpg` shows no eye at 3.5 m.
  A face without an eye cannot act, which rules out the animation the user
  likes.
- The teeth, horns and claws share the hide's material, so nothing is
  glossy and nothing is bone.
- The membrane is the same opaque material. The back-light of G4 is a
  shader term, not a thin veined surface.
- The generated texture is noise at the scale of a pore. It has no scale
  plates, so the hide reads as bark (`now-head.jpg`, `now-glide.jpg`).
- The mesh is a decimated generation. It has no edge loops at the jaw, the
  eyelids, the wing fingers or the joints, so no rig can deform it cleanly.

### 3. The motion is a mechanism

The rig is procedural. It is numerically right and it carries no
anticipation, no overshoot and no acting. `now-ground.jpg` shows the
standing pose: the wings stand up as two planks, the neck is a straight
post, and the legs are columns. The 2026-11-13 clip trial showed that an
authored overlay of 2 to 6 degrees is invisible. Authored clips must own
the pose, not nudge it.

### 4. The mountains are a heightfield

`now-chase.jpg` and `now-run.jpg`. The walls are smooth slopes with one
texture stretched down them. The erosion of G1 shaped the ridges, but there
is no cliff band, no overhang, no talus, and the boulders are rounded
blobs with a dark ring. The targets get their rock from three things: mesh
cliffs on the steep faces, materials placed by slope and by the erosion's
flow and deposit maps, and scree fans below each cliff.

### 5. The forest is a carpet of toys

From the air (`now-chase.jpg`) the crowns are evenly spaced and all one
height, so the forest reads as a lawn with dots. Up close the crowns are
flat-shaded cards. The targets have stands with gaps, a dark interior, two
or three crown colours, and height that varies by a factor of two.

### 6. Ground level has no ground

`now-ground.jpg`. The grass is a few hundred blades a metre tall on a flat
green plane. There is no ground texture at this range, no small plants, no
stones and no transition where a tree meets the soil.

### 7. The river is a painted strip

`now-head.jpg` and `now-chase.jpg` show a repeating ripple pattern with
visible moire and a hard bank. There is no foam, no depth colour, no
gravel bar and no reflection of the far wall.

### 8. The clouds are cut-outs

In `now-chase.jpg` the cumulus are three soft blobs with a flat grey base.
They are small, few and near. The targets have fewer, larger, lit cloud
masses and mist that sits in the side valleys.

### 9. The HUD covers the subject

`now-run.jpg`. The health plate sits on the dragon, the labels of four
markers overlap on its back, and the top plate is a third of the width. The
slice needs a capture mode and a lighter in-play layout. This is the
smallest job on the list.

## Against published games

A web study ran on 2026-11-14. Its sourcing was thin, and the table says
so. "(inferred)" means a reading of the game's frames, not a statement by
its developers. No other game's image is in this repository.

| Game | What its frames do | Ours |
|---|---|---|
| Spyro Reignited Trilogy (Toys for Bob, UE4, 2018) | The stated goal was a "storybook brought to life", with "clean shapes and flowing lines over hyper-detailed textures" and a skin shader that "shows detail and depth when sunlight hits him at the right angle". Concept artists painted over the original levels and lighting artists matched the paintings. Colour was scripted per area. The idle has him stretch and lick his paws. Large eyes with lids and blinks keep the face acting (inferred) | No eye. No idle acting. No colour script: the grade is one global setting. Texture noise where it has clean shapes |
| Kena: Bridge of Spirits (Ember Lab, UE4) | Hand-keyframed animation. Lighting paced to the level. Foliage is cards, with hero foliage built per location | Foliage has no hero pieces. Motion has no keyframes |
| Tears of the Kingdom | Large shapes and atmosphere carry the picture, not texture detail (inferred). Its art director said early sky islands read as "specks of trash" from the ground, and the fix was shape | Our boulders and tree dots are the same problem |
| Century: Age of Ashes (UE4) | Dragon skins were painted by an outsourced texture team for variety over shared meshes. About 80 PopcornFX effects carry the look | Hide from a generator. The breath of G2 is the nearest we have to its effects |
| Avatar: Frontiers of Pandora (Snowdrop) | A scatter tool places plants by parent and child rules: plants around trunks, flowers around riverbank rocks. Artists finish hero areas by hand | Our placement is noise by slope and height. Nothing clusters around anything |
| Horizon Forbidden West | Dense alpha-tested foliage made affordable by deferred texturing | F7's impostors cover the far forest. The near forest has too little to render |

Not found: any rendering talk or material breakdown for Spyro Reignited.
The art book is the likely source and was not read. Monster Hunter, Hogwarts
Legacy and the House of the Dragon breakdowns were not researched. A new
Spyro game with full flight was announced in June 2026, and its frames are
worth a look when the hero dragon is designed.

Techniques the study named from memory, to check before use: Guerrilla's
run-time procedural placement (GDC 2017), Heitz and Neyret's
histogram-preserving blending against tiling (2018), Ghost of Tsushima's
grass (GDC 2021), and Sea of Thieves' water (SIGGRAPH 2018).

## The slice

One capture set defines done. Every round re-renders it, and the pairs go
in `artifacts/showcase-*/`.

- The valley: one seed of the hoard run, chosen in S0 for a wall with a
  steep face, a river bend and a side valley.
- The hour: late afternoon, the sun low and to one side.
- The dragon: the rebuilt Embercrest.
- The shots: the chase view, a side glide, a banked turn past a cliff, the
  head at 3.5 m, the landing, the standing idle at ground level, the breath
  on a target, and a skim over the river.
- The budget: 60 frames a second at 1440p on the M5 at the Ultra preset.

### S0, as built (2026-11-14)

`tools/showcase/capture.sh OUT_DIR` renders the eight shots with the HUD
off. The slice is hoard-run seed 3 with the sun 19 degrees up at azimuth
235. Six seeds were rendered in that light; seed 3 has a wide floor, a
sunlit far wall, a steep near wall and a river bend in one view. Seeds 19,
23 and 42 start inside a gorge whose walls show the stretched texture
worst, and are the test cases for S6.

The baselines are in `artifacts/showcase-s0/`. The GPU's median time a
frame at 1080p on the M5 was 9.0 to 14.5 ms for seven shots and 21.4 ms
for the river skim. The 1440p budget of 16.7 ms is therefore already
missed, and S8 starts with the river's cost.

Three shots do not yet show what they are for. The banked turn frames sky
instead of a cliff. The breath shot's flame leaves the frame and a
training dummy fills the left edge. The skim shot has a course ring in it.
Each is fixed when its round begins, because the fix is a camera path and
the camera path depends on what the round builds.

## Order

| # | Work | Who | Rounds |
|---|---|---|---|
| S0 | The slice's seed, hour and shot list as one script. A capture mode without the HUD. Baselines of all eight shots | main | 0.5 |
| S1 | The worker trial: one rock and cliff kit brief to both workers | both | 0.5 |
| S2 | The hero dragon. The design and its four views are done. Next: one generation on each current generator, compared in the engine at the head and the wing; then retopology, baking and the rig, to the specification below | delegate, main judges | 2 |
| S3 | The creature shader: a membrane material with thickness and veins, an eye with a cornea highlight, a clear-coat lobe for horn and claw | main | 1 |
| S4 | The clip system: a clip player, a state machine for the flight and ground states, additive layers, and the procedural rig as the last layer | main | 2 |
| S5 | The clips: glide, flap, bank, dive, brake, land, take off, idle with acting, walk, bite, breath | delegate, main judges | 2 |
| S6 | Rock: mesh cliffs on steep faces, materials from slope and the erosion maps, scree, a boulder kit from CC0 scans | main, kit from S1 | 2 |
| S7 | Forest and ground: card-built crowns with normal maps, stands with gaps and height variation, clustered understory, a ground texture set, dense short grass | main, assets delegated | 2 |
| S8 | The river: depth colour, foam at banks and rocks, gravel bars, a reflection of the walls | main | 1 |
| S9 | Clouds and mist in larger masses, a colour script for the slice's hour, the HUD's in-play layout | main | 1 |

S2 to S5 come before the world because the dragon fills the centre of every
frame and the user asked for it first. S6 comes before S7 because the walls
cover more of the frame than the forest. S0 and S1 are one round and start
now.

## Workflow

- A round is one row of the order. It ends with the capture set, crops at
  full size, and numbers for what moved.
- Asset work goes to a worker in its own git worktree, with a brief that
  names the paths it can write. The main session keeps the engine code.
- The trial of S1 gives both workers the same brief: a kit of rocks and
  cliff pieces from Poly Haven's CC0 scans, with levels of detail, PBR
  maps and a verification render. The main session judges the two kits in
  the engine, on the same wall, and records tokens and minutes.
- The concept-art skill now uses `image-use`. It was rewritten on this
  date.

## Open questions

- The hero dragon's design. Embercrest's current silhouette can stay, or a
  new reference sheet can change it. The plan keeps the silhouette and the
  colours and redesigns the surface.
- Whether one authored clip set can be retargeted to the other six species,
  or each needs its own.
- Whether mesh cliffs need collision. `Terrain::height_at` reads the
  heightfield, and a cliff piece that the dragon can fly through is worse
  than no cliff.

After the blind critic's round 3 (2026-11-15), in its order, what is not
yet done:

- Trees: card crowns in one style, the largest remaining break between the
  dragon and the world. New crown art is an asset job, not a tuning one.
- Cliff faces up close: strata and seams are colour; there are no crags,
  no scree fans under a band, and the rock kit's pieces still read as
  stickers.
- The flame: two flat tones with an outline (see "tried and dropped").
- The dragon dark from the chase camera, back to the sun.
- Water: no visible reflection of the banks, no froth on a riffle; the
  gravel has no stones up close.
- Bushes: faceted.
- The valley's end seen as the end of the map from 150 m up.

For the playtester, since the terrain and the river changed under the
game: fly a run (`--run 7`), skim the river, land on a gravel bar and on a
bench half way up a wall, and fight in the arena beside a cliff. Look for
a bot or the demo pilot grounding on a cliff band, a cache or a tower
placed badly on a bench, and the river's second thread reading as ponds.
And three calls that are the user's: the course rings' size and look (a
ring cuts frames 3 and 5), how bright the wind lines are, and the wing
poses the critic names (the far wing's root in the glide, the fold on the
ground, the near wing over the jaw while breathing).

After the blind critic's rounds 4 to 6 (2026-11-15, evening), what is
not yet done, in the order six rounds agree on:

- The wings. The membrane bows and the fingers bend, and the critic
  still reads planks on rods outside the upstroke. The next step is
  geometry: slack modelled into the membrane, thinner fingers, a fold
  that creases on the ground. The beat itself was retuned twice
  (`showcase/PLAYTEST.md`); its top is limited to +66 degrees because the
  wingtips touch over the back past that.
- The ground and the cliffs: the meadow has drifts and is still a sheet;
  no stones, no relief in the rock.
- The flame: no hot point at the jaws, no smoke that breaks from the
  edge. Its length is the breath's range, nine dragons, and is the
  playtester's call.
- Clouds read as cotton balls up close; the river's edge is
  stair-stepped from above; the water's ripples tile.

## Where each section is

Code comments and other documents cite sections by name ("SHOWCASE.md,
'The legs in flight'"). Each name, and the part that holds it:

| Section | Part |
|---|---|
| S1, as built (2026-11-14): the worker trial | `showcase/HERO.md` |
| The hero dragon's design (2026-11-14) | `showcase/HERO.md` |
| The hero dragon's design: The first candidate: Hunyuan's one-shot (2026-11-14) | `showcase/HERO.md` |
| The hero dragon's design: The head, generated on its own (2026-11-14) | `showcase/HERO.md` |
| The hero dragon's design: The same head on x99's local models (2026-11-14) | `showcase/HERO.md` |
| The hero dragon's design: The static mesh, as built (2026-11-14) | `showcase/HERO.md` |
| The hero dragon's design: The body, generated without its wings (2026-11-14) | `showcase/HERO.md` |
| The hero dragon's design: The first rig, and the dragon in the engine (2026-11-14) | `showcase/HERO.md` |
| The hero dragon's design: The static mesh, version 2 (2026-11-14) | `showcase/HERO.md` |
| The hero dragon's design: The rig on version 2, and the pose inspection (2026-11-14) | `showcase/HERO.md` |
| The hero dragon, as a specification | `showcase/HERO.md` |
| The wing, generated alone (2026-11-14, 23:37) | `showcase/HERO.md` |
| Mesh version 3's wings, built and not used (2026-11-15, 01:34) | `showcase/HERO.md` |
| The body, generated again with bent legs (2026-11-15, 00:15) | `showcase/HERO.md` |
| Mesh version 4: the bent-leg body, its rig, and the fore legs' own fold (2026-11-15, 02:29) | `showcase/HERO.md` |
| Mesh version 5b: wings from the skeleton-guided sculpt (2026-11-15, 13:10) | `showcase/HERO.md` |
| S3, as built: the rim, the clear coat and the cornea (2026-11-15) | `showcase/HERO.md` |
| A membrane that billows (2026-11-15, 21:14) | `showcase/HERO.md` |
| Fingers that bend (2026-11-15, 21:49) | `showcase/HERO.md` |
| The rim's grain (2026-11-15, 22:12) | `showcase/HERO.md` |
| The feedback tool (2026-11-14) | `showcase/PLAYTEST.md` |
| The first playtest of the hero, and the profile pass (2026-11-14) | `showcase/PLAYTEST.md` |
| The first reports from the feedback tool (2026-11-14, 23:08) | `showcase/PLAYTEST.md` |
| The legs in flight, joint by joint (2026-11-15, 10:34) | `showcase/PLAYTEST.md` |
| The playtest's second round of reports (2026-11-15, 11:30 to 11:37) | `showcase/PLAYTEST.md` |
| Feedback 0013 to 0015, and the turn to the dragon (2026-11-15, 17:20) | `showcase/PLAYTEST.md` |
| Feedback 0013 to 0015, and the turn to the dragon: The hind legs hang (17:20) | `showcase/PLAYTEST.md` |
| Feedback 0013 to 0015, and the turn to the dragon: The wrist hook and the root panel (18:15) | `showcase/PLAYTEST.md` |
| Feedback 0013 to 0015, and the turn to the dragon: The legs trail and tuck (19:00) | `showcase/PLAYTEST.md` |
| Feedback 0013 to 0015, and the turn to the dragon: A deeper, slower beat (19:08) | `showcase/PLAYTEST.md` |
| S4 and S5, first pass: keyframed strikes (2026-11-15, 11:20) | `showcase/PLAYTEST.md` |
| Strikes that hold, and legs that act (2026-11-15, 19:55) | `showcase/PLAYTEST.md` |
| The beat, second pass: faster and deeper (2026-11-15, 22:58) | `showcase/PLAYTEST.md` |
| The breath as a volume (2026-11-15, 20:11) | `showcase/EFFECTS.md` |
| Fireballs as comets (2026-11-15, 20:21) | `showcase/EFFECTS.md` |
| Sparks, a softer rim, and a final dither (20:30) | `showcase/EFFECTS.md` |
| The fire lights what is near it, and its grain moves (2026-11-15, 21:20) | `showcase/EFFECTS.md` |
| A fireball with a head (2026-11-15, 21:24) | `showcase/EFFECTS.md` |
| A released flame burns out (2026-11-15, 21:27) | `showcase/EFFECTS.md` |
| The river takes the fire's light, and the lens leaves the flame alone (2026-11-15, 21:33) | `showcase/EFFECTS.md` |
| The pale core at the jaws, and a skin of smoke (2026-11-15, 21:37) | `showcase/EFFECTS.md` |
| Smoke off the marched breath (2026-11-15, 21:53) | `showcase/EFFECTS.md` |
| Fire through a sensor (2026-11-20, 17:53) | `showcase/EFFECTS.md` |
| Wall crags in rows (2026-11-20, 18:18) | `showcase/WORLD.md` |
| Landmarks, and five more rocks (2026-11-20, 18:32) | `showcase/WORLD.md` |
| Scree and rubble at the feet (2026-11-20, 19:20) | `showcase/WORLD.md` |
| The ground burns (2026-11-20, 18:55) | `showcase/EFFECTS.md` |
| The blind critic, round 7 (2026-11-20, 18:40) | `showcase/CRITIC.md` |
| The river's streaks and whirls (2026-11-15, 11:55) | `showcase/WORLD.md` |
| Trees in a mountain's shadow: what does not work (2026-11-15, 12:05) | `showcase/WORLD.md` |
| The valley walls: fewer, larger buttresses (2026-11-15, 12:17) | `showcase/WORLD.md` |
| Bushes of leaf cards, fuller tufts (2026-11-15, 12:27) | `showcase/WORLD.md` |
| Where a frame's time goes (2026-11-15, 12:45) | `showcase/WORLD.md` |
| S6, first part: the scanned rock kit in the world (2026-11-15, 13:30) | `showcase/WORLD.md` |
| A meadow in drifts (2026-11-15, 22:00) | `showcase/WORLD.md` |
| What other showcases do that this build does not (2026-11-15, 12:55) | `showcase/CRITIC.md` |
| What other showcases do that this build does not: Licences | `showcase/CRITIC.md` |
| What other showcases do that this build does not: What the engine already has | `showcase/CRITIC.md` |
| What other showcases do that this build does not: Method: the part all three agree on | `showcase/CRITIC.md` |
| What other showcases do that this build does not: Techniques, ranked by what the playtester would see against the work | `showcase/CRITIC.md` |
| What other showcases do that this build does not: What does not transfer | `showcase/CRITIC.md` |
| What other showcases do that this build does not: Order | `showcase/CRITIC.md` |
| The blind critic, round 1 (2026-11-15, 13:15) | `showcase/CRITIC.md` |
| The blind critic, round 1: Fixed in this round | `showcase/CRITIC.md` |
| The blind critic, round 1: The valley walls: grass with grey rock, not one tan (13:22) | `showcase/CRITIC.md` |
| The blind critic, round 1: The eye opens in shade (13:31) | `showcase/CRITIC.md` |
| The blind critic, round 1: The claws on the wing (13:31) | `showcase/CRITIC.md` |
| The blind critic, round 1: World labels keep clear of each other (13:36) | `showcase/CRITIC.md` |
| The blind critic, round 1: The sward: short grass edge to edge near the camera (13:43) | `showcase/CRITIC.md` |
| The blind critic, round 1: Crowns lit as masses, and no more lollipops (13:49) | `showcase/CRITIC.md` |
| The blind critic, round 1: Tried and dropped: more haze | `showcase/CRITIC.md` |
| The blind critic, round 1: Shade under the crowns, from the sky (14:59) | `showcase/CRITIC.md` |
| The blind critic, round 1: Not changed, because they are design calls | `showcase/CRITIC.md` |
| The blind critic, round 2 (2026-11-15, 15:05) | `showcase/CRITIC.md` |
| The blind critic, round 2: Fixed after round 2 | `showcase/CRITIC.md` |
| The blind critic, round 2: Cliff bands on the walls (15:25) | `showcase/CRITIC.md` |
| The blind critic, round 2: A braided river (15:45) | `showcase/CRITIC.md` |
| The blind critic, round 2: The ward stone (16:00) | `showcase/CRITIC.md` |
| The blind critic, round 2: The breath has a core and a fringe (16:05) | `showcase/CRITIC.md` |
| The blind critic, round 2: Spray as wisps and dashes (16:10) | `showcase/CRITIC.md` |
| The blind critic, round 3 (2026-11-15, 16:15) | `showcase/CRITIC.md` |
| The blind critic, round 3: After round 3: unlike beds, and a face with strata (16:20) | `showcase/CRITIC.md` |
| The blind critic, round 3: After round 3: quiet marks, a darker ward stone (16:30) | `showcase/CRITIC.md` |
| The blind critic, round 3: After round 3: more air, less aerosol (16:25) | `showcase/CRITIC.md` |
| The blind critic, round 3: After round 3: fine tufts, reeds at the water (16:35) | `showcase/CRITIC.md` |
| The blind critic, round 3: After round 3, tried and dropped: a softer flame (16:38) | `showcase/CRITIC.md` |
| The blind critic, round 4 (2026-11-15, 20:25) | `showcase/CRITIC.md` |
| The blind critic, round 5 (2026-11-15, 21:30) | `showcase/CRITIC.md` |
| The blind critic, round 6 (2026-11-15, 22:07) | `showcase/CRITIC.md` |

## Sources

- Spyro Reignited's visual goals: https://news.xbox.com/en-us/2018/11/13/visual-storytelling-spyro-reignited-trilogy-xbox-one/
- Spyro Reignited's paintovers and colour: https://blog.playstation.com/?p=205567
- Spyro Reignited's outsourced levels: https://gfactory.artstation.com/projects/Pm6xr4, https://airbornstudios.artstation.com/projects/nQ4Rk4
- Kena's visual identity: https://80.lv/articles/visual-identity-development-of-kena-bridge-of-spirits
- Tears of the Kingdom's sky islands: https://gamedeveloper.com/production/zelda-devs-originally-made-sky-islands-look-like-specks-of-trash
- Century's skin textures and effects: https://limbheim.artstation.com/projects/1NRnGq, https://popcornfx.artstation.com/projects/EaLX4n
- Horizon Forbidden West's deferred texturing: https://www.guerrilla-games.com/read/adventures-with-deferred-texturing-in-horizon-forbidden-west
- Avatar: Frontiers of Pandora's scatter tool: https://www.massive.se/?p=18290
- The new Spyro game's reveal: https://news.xbox.com/en-us/2026/06/07/spyro-a-realm-beyond-reveal-xbox-games-showcase-2026/
