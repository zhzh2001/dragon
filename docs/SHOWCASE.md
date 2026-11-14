# Showcase: the second gap study, and the vertical slice

Written 2026-11-14, from `main` at `e48ea61`. `GAPS.md` is the first study
(2026-10-21) and stays as the record of G1-G9. This file replaces its
section 6 as the order of work for the modern tier's picture. `FIDELITY.md`
stays the record of the rendering techniques. The retro tiers are out of
scope here.

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

## Order

| # | Work | Who | Rounds |
|---|---|---|---|
| S0 | The slice's seed, hour and shot list as one script. A capture mode without the HUD. Baselines of all eight shots | main | 0.5 |
| S1 | The worker trial: one rock and cliff kit brief to both workers | both | 0.5 |
| S2 | The hero dragon's design: a reference sheet with scale plates, an eye, horn and claw materials, and wing fingers. Then the asset to the specification below | delegate, main judges | 2 |
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

## The hero dragon, as a specification

These numbers are norms the study inferred, not sourced figures. S2 tests
them on one asset before anything depends on them.

- Geometry: 60,000 to 120,000 triangles at the top level, with edge loops
  at the jaw, the eyelids, each wing finger and each limb joint. The eyes,
  the teeth and the tongue are separate meshes.
- Materials: hide, membrane, horn and claw, eye, and mouth. Each has base
  colour, normal and roughness at 2K or 4K. The membrane adds a thickness
  map.
- Rig: the bone names of `src/anim/dragon_rig.cpp` stay a contract. New
  bones are added for the eyelids, the eyes, the jaw and the tongue.
- Clips: authored in Blender and shipped in the glTF.
- Generation alone does not reach this. The 2026-09 survey found that no
  generator rigs a dragon and that generated topology has no loops. The
  plan is to generate the sculpt, retopologise it in Blender, and bake the
  maps from the sculpt to the new mesh.

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
