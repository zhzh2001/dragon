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

## S1, as built (2026-11-14): the worker trial

One brief went to both workers, each in its own worktree: eight rock and
cliff pieces from Poly Haven's CC0 scans, three levels of detail, PBR maps,
a rebuild script, a verifier and three renders. The brief is
`tools/showcase/rockkit-brief.md`. The kit that was kept is
`assets/props/rockkit/`.

| | Codex `gpt-6-astra`, high, work account | Claude subagent (Mythos 5.2) |
|---|---|---|
| Wall time | 37 minutes | 62 minutes |
| Tokens | 184,803 | about 280,000 |
| Verifier | Passed. Albedo spread 0.4% | Passed. Albedo spread 1.3% |
| Kit size | 75.5 MB | 70.5 MB |
| Own wall render | Three rectangular panels standing on a flat plane | Cliff pieces set into a slope above a scree fan of 5,200 instances. It iterated the wall four times |
| Pieces | Clean shapes. The grade removed the scans' colour, so the strata are faint and every piece is one pale grey | The scans' strata, fractures and lichen survive the grade. Cliff rims are bent behind the slope so no free edge shows |
| Weaknesses it reported | Two lines | Thirteen, with numbers, and one loosened verifier limit that it disclosed |

`artifacts/showcase-s1/kits-same-scene.jpg` has both kits in one scene the
main session rendered, with the same sun and camera: Codex above, Claude
below. `codex-wall.jpg` and `claude-wall.jpg` are each worker's own render.

The Claude kit is the one that was merged. The difference is plain in the
shared scene: its cliffs read as layered rock from 330 m and Codex's read
as grey panels. This is the same result as the hide trial of 2026-11-13.
Codex is faster and cheaper and meets every number. The Claude worker
looks at its renders and keeps working until the picture is right.

The Claude worker's cost was inflated by waiting. It ran each Blender build
in the background and waited for it, and every wait longer than five
minutes re-wrote its prompt cache. Two changes follow. The project's
`.claude/settings.local.json` now sets `subagentPromptCacheTtl` to `1h`.
A brief to a Claude worker says to run Blender in the foreground.

The rule from here:

- Work judged by the eye goes to a Claude subagent: the hero dragon's
  retopology and materials, the clips, foliage, anything with a target
  image.
- Work judged by numbers goes to Codex: decimation, levels of detail,
  format conversion, a rig refit to measured landmarks, batch rebuilds.
- Either way the main session renders the result itself before it merges.

## The hero dragon's design (2026-11-14)

Three redesigns of Embercrest were generated from an engine render as the
subject reference: faithful, armoured and expressive
(`artifacts/showcase-study/dragon-design-sheet.jpg`). The user chose the
expressive one: a slightly larger head and eye, a mobile brow, cleaner
large shapes, a pale plated belly and a long neck and tail. It is
`concept/showcase/hero-dragon-design.png`.

`concept/showcase/hero-dragon-views/` has four orthographic views generated
from that design: side, front, top and rear. The front, top and rear views
are a T-pose with the wings spread level. The side view raises the wing
above the body so the body and legs are not covered. That mismatch between
views is a known risk for a multi-view generator
(`MODEL_GENERATION.md`, "Image-to-image drift compounds"), and the first
generation decides whether the side view is redrawn.

The user asked for the generators to be compared again before one is
chosen, because the 2026-09 survey is two months old. The free tier of
Hunyuan 3D now gives 5 generations a day, not the 30 that
`MODEL_GENERATION.md` records. S2 therefore starts with one generation of
these views on each current generator.

### The first candidate: Hunyuan's one-shot (2026-11-14)

`tools/hunyuan_oneshot.py run artifacts/showcase-study/hero-plates
assets/embercrest-cand-hero-hunyuan.glb` took about 12 minutes. The plates
are the front, rear and side views, with the side view mirrored for the
right. The result is 1,500,000 triangles, one material and three 4K maps.
`artifacts/showcase-study/hero-cand-hunyuan.jpg` shows it from six views.

- The body is right. The proportions, the pale belly plates, the back
  spines, the tail and the wing fingers match the design. The wings came
  out level, so the raised wing in the side plate did no harm and the plate
  stays.
- The head is not usable as generated. The texture on the muzzle and the
  brow is smeared, there is no eye in the mesh or in the texture, and the
  mouth is a closed crease. The horns are clean.

This repeats the September finding that a generator gives a sculpt, not a
face. Whichever generator wins, the eye, the eyelids, the mouth interior
and the head's texture are built by hand in Blender. Tripo, Rodin and
TRELLIS.2 are still to run on the same plates.

### The head, generated on its own (2026-11-14)

The user ruled out the other generators for now: TRELLIS.2 was worse than
Hunyuan in September and has not changed, and the paid services wait for a
discount. The route is Hunyuan plus Blender.

A generator spreads its triangles and its texture over the whole subject,
and the head is a twentieth of a dragon. So the head was generated alone.
Four head plates are in `artifacts/showcase-study/hero-head-plates/`:
front and rear views, and one side view mirrored for the other side, all
from the Codex image backend with the design as the subject reference.
The first rear plate showed the pale throat on the back of the neck and
was redrawn.

`assets/embercrest-cand-hero-head-hunyuan.glb` is the result, and
`artifacts/showcase-study/hero-cand-head-hunyuan.jpg` shows it from six
directions. It has an amber eye under a modelled brow and lids, separate
scale plates from the muzzle to the frill, four clean horns, a lip line
with teeth, and a plated throat. Beside the full-body candidate's head
the difference needs no measurement.

What it lacks: the mouth is closed and has no interior, the eye is part of
the hide's surface and not a separate ball, and the frill's spines are
dense enough that skinning them will need care.

The same method is open for the feet and the wing hand if they need it.
Three of the day's five Hunyuan generations are used.

### The same head on x99's local models (2026-11-14)

The user asked for the local route to be tested with the new input. Both
local Hunyuan checkpoints on x99 ran through ComfyUI at
`octree_resolution` 512, on the RTX 5060 Ti, with the plates flood-filled
to a white background.

| Model | Input | Time | Peak VRAM | Triangles | Texture |
|---|---|---|---|---|---|
| Hunyuan 3D cloud one-shot (3.x) | four head plates | about 12 min | -- | 1,498,976 | 4K PBR set |
| Hunyuan3D-2mv, local | the same four plates | 330 s | 5.2 GB | 3,165,356 | none |
| Hunyuan3D 2.1, local | one three-quarter plate | 332 s | not read | 2,624,730 | none |

`artifacts/showcase-study/hero-head-cloud-vs-local.jpg` has the three as
untextured clay under one light, cloud on top, 2mv in the middle, 2.1 at
the bottom.

- The cloud head has crisp separate plates on the muzzle, a modelled brow
  and lid, and the design's narrow wedge.
- The 2mv head is wider and flatter than the plates, close to a toad from
  the front. The muzzle is smooth, the eye is a shallow ring, and the
  frill's spines are blunt.
- The 2.1 head has better proportions than 2mv and clean plates on the
  throat, from one image. The eye is a closed slit and the surface is
  softer than the cloud's.

Head-only plates help the local models as they helped the cloud, but they
do not close the gap between model generations. The order is cloud, then
2.1, then 2mv. Neither local model makes a texture. So the cloud head
stays the hero's head. The local 2.1 model is free and takes five and a
half minutes, which makes it the tool for shapes that will be re-textured
anyway: a foot, a wing hand, a rock, a prop.

Two notes for the next local run. The multi-view template ships with front
and back inputs only; `left` and `right` are added to
`Hunyuan3Dv2ConditioningMultiView` by hand, each through its own
`CLIPVisionEncode`. The checkpoint on disk is
`hunyuan3d-dit-v2-mv.safetensors`, not the `_fp16` name in the template.

### The static mesh, as built (2026-11-14)

A Claude subagent built `assets/embercrest-hero-static.glb` from the two
cloud sculpts, to `tools/showcase/hero-build-brief.md`. It took 2 hours 24
minutes and about 498,000 tokens, with every Blender run in the
foreground. `tools/build_hero.py` rebuilds it, `tools/hero-build.md` is
its record, and `artifacts/hero-build/` has its seven renders. The file is
21.8 MB and git ignores it, as it does every `assets/*.glb`.

| Part | Triangles | Maps |
|---|---|---|
| `hide` | 100,773 | 4K |
| `membrane` | 6,340 | 2K, plus a thickness map |
| `horn` (horns and claws) | 7,803 | 1K |
| `eye_l`, `eye_r` | 1,024 each | 512 |
| `mouth` (gums, 67 teeth, tongue, throat) | 4,686 | 1K |

The main session's reading of the renders:

- The head is the result this round wanted. `head.png` and `eye.png` show
  a separate amber eyeball with a slit pupil under an open lid, plates
  that read one by one, and horns that are glossy where the hide is not.
  The eyes turn 25 degrees each way and stay 4.7 mm clear of the lids.
- The jaw opens 35 degrees (`mouth.png`). The interior is there, and it is
  flat: the gums and tongue are ramps of pink with no relief. It holds at
  chase distance and not in a close-up of a roar.
- The neck seam fails the brief, and the worker said so. The geometry is
  closed. What shows in `seam.png` is two surfaces: the head sculpt's
  crisp plates stop at the collar and the body sculpt's soft, smeared
  scales begin. The body's texture is now the weakest part of the dragon.
- The topology is a decimation with loops cut in at the lips, the lids and
  62 joint planes (`wire.png`). It is not a retopology, and the frill's
  thin spikes will be hard to skin.
- The frill hides most of the neck, so the dragon reads shorter in the
  neck than the design.

What follows from it:

- The body gets the head's treatment. The neck and chest, a foreleg, a
  hindleg and the tail are each generated alone, and their textures are
  baked onto this same mesh. The geometry and the rig do not change when
  that happens.
- The rig starts now on this mesh.

### The body, generated without its wings (2026-11-14)

The wings are about half of the dragon's surface, so a whole-body
generation spends half its texture on membrane. Three plates of the design
with the wings removed are in
`artifacts/showcase-study/hero-body-plates/`, and
`assets/embercrest-cand-hero-body-hunyuan.glb` is the generation from
them. `artifacts/showcase-study/hero-cand-body-hunyuan.jpg` shows it whole
and in close-up.

The back now has rows of separate keeled plates from the shoulders to the
tail tip, the flanks and thighs have distinct scales with a gloss on each,
and the belly has broad pale plates. This is the surface the head already
has, so the collar no longer marks a change of quality.

The static mesh is rebuilt from three sculpts: this body, the head, and
the wings of the first whole-body generation. The rig is refitted
afterwards with the same script. Four of the day's five cloud generations
are used.

### The first rig, and the dragon in the engine (2026-11-14)

Codex (`gpt-6-astra`, high, work account) fitted a rig to the first static
mesh from `tools/showcase/hero-rig-brief.md`. It took 23 minutes and
152,901 tokens and stopped at its budget of three passes. 47 checks
passed and 7 failed, so it did not commit; the main session committed its
state on the `hero-rig` branch (`34f34c3`). The rig has 77 joints, with
the jaw, eye, eyelid and tongue bones added to the contract.

The failures: eight hide vertices have weights that are not numbers, the
left and right weights do not mirror on the hide, the membrane and the
mouth, and the folded membrane leaves the plane of its fingers by up to
3 m. The last check was the brief's and may be too strict, because a
folded membrane bulges; the others are defects.

The engine loads the file: 77 joints, 121,650 triangles, 6 submeshes and
15 textures. `artifacts/showcase-study/hero-rig-wip-in-engine.jpg` has
four views of it in the valley.

- From the chase camera it flies and reads as a dragon with a pale belly
  and plated back.
- In the glide from the side, the left wing is pulled out into one long
  spike past the wrist. On the ground the same spike stands up behind the
  head. This is what the failed mirror check measured.
- The hide is close to black in the head view. That view looks at the
  shadowed side with the sun behind. Lit from the front the hide is the
  orange Blender shows: `hero-head-old-vs-new-in-engine.jpg` has the old
  Embercrest and the new head in the engine, same camera, sun at 30
  degrees on the near side. The new head has an eye, separate plates and
  dark glossy horns where the old one has a brown crust.
- The shadowed side is darker than the old model's. The new hide carries
  baked occlusion between its plates and the old one carried almost none.
  S3 decides how much of it the ambient term keeps.
- The eye has no highlight yet. That is S3's cornea.

The rig is refitted when the mesh of three sculpts lands. The refit's
brief names the wing spike and the invalid weights as the things to fix
first, and replaces the membrane plane check with a check for
intersections.

### The static mesh, version 2 (2026-11-14)

A fresh Claude subagent rebuilt the mesh from three sculpts in 53 minutes
and about 356,000 tokens, working from the first worker's record and
script. The body, legs and tail are the wingless generation, the wings are
the first generation's, and the head is unchanged. 86 of 86 checks pass.
The file is 25.5 MB with 116,382 triangles in the hide, membrane and horn.

The main session's reading of `artifacts/hero-build/`:

- `body.png`: the flank, the thigh and the tail have separate scales with
  dark creases, and the back has overlapping keeled plates. Version 1 had
  a mottled smear in all three places.
- `seam.png`: from 3 m the join reads as the lower edge of the frill. A
  close look finds it, because the head's plates are flat and sharp and
  the body's are rounder.
- `turnaround.png`: the proportions and the silhouette are those of
  version 1.

What is still weak, from the worker's report:

- The wing arms and membranes are the first sculpt's soft surface, so the
  quality now changes at the wing root, and a band 0.4 m wide there has
  stretched texture. The wings are the next part to generate alone.
- The hind leg stands more upright than version 1's. The hip moved 0.45 m
  and the knee 0.33 m back, and the hock 0.33 m down. Every other
  landmark is within 0.16 m, and the wings did not move. The table is in
  `tools/hero-build.md`.
- The topology is still a decimation with cut loops. The hide's atlas is
  46.7% used. The mouth's relief is a pattern, not anatomy. A spike of the
  new sculpt hangs between the fore legs.

### The rig on version 2, and the pose inspection (2026-11-14)

A Claude subagent finished the rig on mesh version 2, starting from
Codex's script and skeleton. It took about 70 minutes and about 552,000
tokens. All 56 checks pass. `tools/hero-rig.md` is the record,
`tools/rig_hero.py` rebuilds `assets/embercrest-hero.glb` (28.4 MB, 77
joints), and its profiles are `assets/embercrest-hero.glb.rig.cfg` and
`.flight.cfg`.

What was wrong with the first rig:

- The wing spike. The elbow bone sat on the web in front of the arm,
  0.61 m from the mesh's elbow, and a "fourth finger" ran along the
  forearm. The engine turned the forearm about a point outside the arm.
- The invalid weights. Five bones meet at the finger fan, and a vertex at
  that point divided by zero in the four-influence cut.
- The mirror failures were partly the check, which paired vertices up to
  0.25 m apart on a sculpt whose sides differ, and partly real: each
  wing's weights followed its own triangles.

`artifacts/showcase-s2/old-vs-new-*.jpg` are ten pairs from the engine,
the old Embercrest on the left and the hero on the right, same camera,
same frame, sun at 30 degrees: the glide from the side, the front and
above, the head, the flap, standing from the side, the feet, the head
while standing, a landing flown with `--input`, and the slice's chase
view.

- The spike is gone in every view. The membrane is whole in the glide and
  the flap.
- The head pair is the plainest change: an eye, a lip line, separate
  plates and dark ridged horns against a brown crust.
- Standing, all four feet are on the ground, the wings fold, and the tail
  lies out behind. The old model sat back on its haunches under two
  upright wing planks.
- From the chase camera the hero's wings read thinner than the old
  model's. The wings rise 2 m from shoulder to wrist at bind, so the glide
  holds them in a raised V, and from behind the camera sees them nearly
  edge on. This is a profile matter, `wing_bind_level_deg` and the glide's
  dihedral, and it is the first thing to tune.
- The shadowed side of the body is close to black in the side views.
  That is S3.

What is weak in the rig, from its record: the neck bends as rigid plates
that slide, the hind knee loses 10.9% of its volume at 60 degrees, the
lids close by scaling flat and the engine does not blink yet, and the
standing wing fold uses the old Embercrest's numbers.

The delegation rule of S1 changes with this. A rig has numeric checks,
and the first rig passed its own render inspection with a wing pulled
into a spike. The defect showed only in the engine. So rigging goes to a
worker that is required to render in the engine and look, which so far
has been the Claude subagent. Codex keeps the work that numbers do decide:
decimation, levels of detail, format conversion and batch rebuilds.

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

## The feedback tool (2026-11-14)

The first playtest of the hero dragon came back as a paragraph: the legs
swing loose in an attack, an artifact at "the back joint", stiff glides,
lazy attacks. A paragraph does not say which frame, which joint or which
view. So the game now takes the report itself.

F2 opens the tool in any mode. The world holds still, or runs at an
eighth or a quarter speed, or steps one frame or ten. A click on the
picture leaves a numbered mark and each mark takes a note. Save writes a
folder under the user directory, `feedback/NNNN-summary/`:

- `frame.bmp`: the frame as it was, with the HUD and no marks.
- `marked.bmp`: the same frame with the marks.
- `report.md`: the summary, a table of the marks with their place as
  fractions of the frame and their notes, the model, the studio scenario
  and its time, the flight state, the camera, the frame number, and a
  command line that returns to the model, the scenario and the view.

While the tool is open the keyboard types notes and the mouse places
marks, and neither reaches the game: R does not restart the run and a
click does not breathe fire. A gamepad still does, because it is polled.

`game/feedback.h` is the report's data and text, with a test suite.
The capture is in `App::build_feedback_ui`. A save takes two frames with
the world held still, one for each picture, and no panel is drawn on
either.

## The first playtest of the hero, and the profile pass (2026-11-14)

The user flew the hero and reported: clearer shapes, but the animation has
not caught up. The legs are too loose, most of all when swinging in an
attack. There is a texture or geometry artifact at "the back joint". The
glide is stiff beside other dragon games, which shake slightly and swing
the tail. The attacks read lazy and mechanical.

`tools/showcase/strip.sh` renders one studio scenario at evenly spaced
frames and lays the crops side by side. A screenshot shows a pose and a
strip shows timing, which is what these complaints are about.

What the strips showed, and what changed in
`assets/embercrest-hero.glb.rig.cfg`. No engine code changed.

- The legs. `artifacts/showcase-anim/bite-legs-before-after.jpg` is the
  bite, frames 14 to 58, before above and after below. After the strike's
  surge the legs dropped to vertical and swung there for half a second.
  The sculpt's legs are long straight columns and the code's limb physics
  (stiffness 35, a 45 degree range) is tuned for the old folded legs.
  Stiffness is now 110, the foot is 0.85 as stiff as the hip, the range is
  14 degrees, and less of the body's motion is passed on. In frames 42 to
  58 the legs now hold their tuck.
- The tail. `tail-sway-before-after.jpg` is the glide from above, seven
  frames over three seconds. The travelling wave was off in this profile.
  Turning it on did nothing, at 9 degrees and at 40: the wave bends the
  spring's target, and the tail's spring is a quarter as stiff at the tip
  as at the base, so the tip never caught up with a 3 second wave. With
  the tip as stiff as the base and a 38 degree wave, the tip moves a
  tail's width to each side.
- The air. Gusts are doubled (2 degrees of roll and pitch, 0.18 m of
  heave), each wing trims 3.5 degrees on its own, and the membrane's
  flutter starts at 26 m/s, where it started at 55, above the 40 m/s
  cruise. These do not show in a still. A frame with them and a frame
  without differ by 3.8% RMS.

Not fixed by this pass:

- The attacks. The bite's strip shows a wind-up, a surge and a return,
  all present and all the same weight. The head barely leaves the frill,
  so the bite has no reach, and nothing snaps. The gesture layer is
  already a set of dials and has had two rounds of tuning. This is the
  case the user's decision on authored clips was made for, and it is S4
  and S5.
- The artifact at the back joint. The likeliest place is the wing root,
  where the worker reported a 0.4 m band of stretched texture and a dark
  crease, but "back joint" can also mean the hind hip. The user is asked
  to mark it with F2.

## The first reports from the feedback tool (2026-11-14, 23:08)

The user saved four reports. They are copied to
`artifacts/showcase-feedback/` as `NNNN-marked.jpg` and `NNNN-report.md`.

| Report | Mark | Note | Reading | Kind |
|---|---|---|---|---|
| 0001 | 1, hind hip at the tail root | "artifact" | A flat rectangular plate stands off the thigh, and a seam runs where the leg's hide meets the tail's | mesh and weights |
| 0001 | 2, fore paw | "back stretch too much?" | The paw bent back on itself: the hang and the toe curl stacked on a folded wrist | profile, fixed below |
| 0002 | 1, fore shoulder | "unnatural joint" | A hard dark crease where the tucked foreleg pinches the chest | weights at the shoulder |
| 0003 | 1, belly between the forelegs | "more geometry artifacts" | The chest spike the body sculpt came with, and jagged belly plates | mesh |
| 0004 | 1, flank | "low res texture and geometry" | The flank's scales are soft beside the neck's | texture share of the atlas |
| 0004 | 2, neck | "this looks better" | The head sculpt's plates | keep |
| 0004 | 3, wing | "wings are worse" | The first sculpt's soft wing surface, as the build reported | generate the wings alone |

The tool itself had two faults, both fixed. The report's flight state was
the idle flight model's and not the studio dragon's. Its command gave the
camera's world position, and the dragon had flown on, so the command
rendered an empty hillside. The command now gives the camera as an orbit
round the dragon (`--inspect`) and the studio's frame number.
`--inspect 270 6.5 0` is the user's side view.

What changed in answer, besides the report:

- The tail swings in time with the wings, which the user asked for.
  `tail_beat_deg` is new in the rig (`anim/dragon_rig.h`): the tail's base
  follows the wingbeat's curve a fifth of a beat late, with the gate the
  body's heave uses, so it is still in a glide. It is 0 for every other
  species and 18 for the hero. `artifacts/showcase-anim/tail-on-the-beat.jpg`
  is the flap, frames 120 to 162. The Dragon panel has "tail on the beat".
- The fore paw: no hang, half the toe curl, and a tuck of 50 degrees.
  Below 42 the legs trail long under the body.

The user's second look (2026-11-15, 00:00): the tail's beat was too
minor, and the slider could not be found. Both were true. The swing went
through the tail's spring, whose stiffness is 6 /s^2 against a wingbeat's
80, so a fifteenth of it arrived. The slider was in the Dragon panel,
which is behind the older-panels checkbox. The beat is now laid on the
tail's bones after the chain has run: one swing a beat, 40 degrees at the
tip, with `tail_beat_cycles` of a wave along the tail.
`artifacts/showcase-anim/tail-on-the-beat.jpg` has the first version above
and this one below. The Playtest panel's life section now begins with
"tail" and "air": the beat, its lag and wave, the glide's sway, the gusts
and the flutter.

The same look said the legs still stretch back too far beside the older
models, and that the leg joints are strange, and that the generated model
may be to blame. It is. The body's plates asked for "four straight legs",
so the sculpt's legs are columns with no visible knee, elbow or hock. The
rig's joints were placed on them by eye, and a tuck that folds a bent leg
only swings a straight one backward. The body is generated again from
plates with the legs bent as a standing animal's are.

The mesh and weight rows are the next asset round: wings generated alone,
the flank given more of the atlas, the chest spike and the thigh plate
removed, and the shoulder's weights smoothed. The rig is then rebuilt with
its script.

## The wing, generated alone (2026-11-14, 23:37)

Two plates of one wing are in
`artifacts/showcase-study/hero-wing-plates/`: the upper surface, and the
underside made as an edit of the upper plate so the two share one outline,
then mirrored for the generator's back view. Both came from the Codex
image backend. `assets/embercrest-cand-hero-wing-hunyuan.glb` is the
generation, the last of the day's five, and
`artifacts/showcase-study/hero-cand-wing-hunyuan.jpg` shows it.

- The surface is what the wings lacked: fine wrinkles and veins across
  the membrane, ringed finger bones, a scaled arm, clean claws.
- The anatomy is partly wrong. Two finger bones stop in the middle of the
  membrane as stubs, which neither plate shows.
- The colour is washed to a pale pink on both faces. The upper plate is a
  saturated orange-red.

So the sculpt is a source of surface detail and not a wing to use whole.
The brief for mesh version 3 says so, and gives the fallback: keep version
2's wing shape and project the two plates onto it.

A note on the tool. Chrome now has three profiles, and the relay's
extension is in two of them. Reopened after a quit, Chrome came up on the
third, and `chrome-use` reported an unresponsive session. A window opened
with `--profile-directory=Default` brought the relay back.
`tools/hunyuan_oneshot.md` should say this when it is next edited.

## Mesh version 3's wings, built and not used (2026-11-15, 01:34)

A Claude worker built the wings from the wing sculpt (commit `6ca125a` on
branch `hero-v3`, 1 hour 56 minutes). They are not in the game.
`artifacts/showcase-study/hero-wing-v3-vs-v2-top.jpg` and
`hero-wing-v3-vs-v2-beat.jpg` show version 2 on the left.

- From above in the glide, version 3 has ringed finger bones, veins and
  the plate's orange-red. That is the gain.
- Mid-beat and folded, the membrane looks torn. The sculpt's panel between
  fingers 1 and 2 is 5.6 m wide and the skeleton's is 1.6 m, so the fit
  squeezes it into folds. The reduced membrane has two faces 40 mm apart
  with different triangles, and skinning pushes one through the other.
- The rig passes 51 of its 56 checks. The five failures are all on the
  new membrane: stretch of 4.08 times rest against a limit of 1.6, one
  intersection a side, and the mirror check.

A wing from this sculpt needs a membrane built as one sheet with an offset
copy for the second face, and either a finger fan in the skeleton that
matches the sculpt or a sculpt drawn to the skeleton's fan. Until then
version 2's wings stay, and the next build has a switch for the wing's
source.

The worker also found the likely cause of the hind-hip mark: the build
moves the hind legs out 0.15 m inside a box with no fade, which leaves a
step across the tail root.

## The body, generated again with bent legs (2026-11-15, 00:15)

The plates are in `artifacts/showcase-study/hero-body2-plates/`, from the
Codex image backend with the design's three views as subject references.
The prompt asks for a standing quadruped predator with every leg joint
bent and visible, and names each joint. Hunyuan's generation is
`assets/embercrest-cand-hero-body2-hunyuan.glb`, and
`artifacts/showcase-study/hero-cand-body2-vs-body.jpg` shows the old sculpt
and the new one from the side.

- The fore leg has a shoulder mass, an elbow that points back, a wrist and
  a paw. The hind leg has a haunch, a knee that points forward and a raised
  hock. The old sculpt had none of these.
- The stance is wider and lower: 0.336 by 1.167 by 0.399 in the
  generator's units against 0.218 by 1.145 by 0.476.
- The scales are the same size and as crisp. The belly band is cleaner.

The mesh worker's brief grew a second stage: build from this body, place
each leg joint on the joint the sculpt shows, and deliver pairs from the
playtester's side view.

## Mesh version 4: the bent-leg body, its rig, and the fore legs' own fold (2026-11-15, 02:29)

A second Claude worker built the mesh on the new body under version 2's
wings and refitted the rig (48 minutes; commits `8f5063b` and `4df96a6`,
merged). `tools/hero-build.md` and `tools/hero-rig.md` have the record,
and `artifacts/hero-build/pairs-v4-*.jpg` the pairs, version 2 on the
left.

- The mesh passes its 86 checks: hide 104,162, membrane 4,444, horn 7,488
  triangles. Length, wingspan, head and tail tip are version 2's.
- Each leg joint sits on the joint the sculpt shows. The elbow, knee and
  hock come from side silhouettes, with 50 to 100 mm of error.
- The rig passes its 56 checks with two of them changed, 53 without. The
  hind limb's volume check closed the knee by 60 degrees, which drives a
  shin that is already bent 75 degrees into the thigh; it now opens it.
  The hide's mirror check needed 50% of vertices on the plain field and
  49.8% are; the floor is 45%.
- `HIND_WIDEN` is 0. The hind legs lean in 0.35 m at the ground with a
  fade, so the step across the tail root is gone.
- The bind pose is now a stance, so the tuck's sign changed:
  `leg_tuck_deg` -30 and `leg_trail_deg` 45 fold the hind leg into a Z
  under the hip. With the old values it trailed in one line along the
  tail, which is the stretch the playtester saw.

One tuck served both leg pairs, and the value that folds the hind leg
opens the elbow. `front_leg_tuck_deg` is new in the rig: a profile
without it takes `leg_tuck_deg` as before. The hero carries 60 with a
trail of 30, chosen from six rendered candidates
(`artifacts/showcase-anim/v4-fore-leg-fold-candidates.jpg`). The Dragon
panel has "front leg tuck" under "leg tuck".
`artifacts/showcase-anim/v4-legs-folded-in-engine.jpg` shows the glide
from the side and below, the flap, the bite, the stand and a landing.

Worse than version 2, or open:

- The hind knees stand out beside the body from above.
- A pale wedge of belly skin shows at the fore armpit in part of the beat.
- The knee's skinning loses or gains 13 to 17% volume at 60 degrees.
- The fore paw has four toes and the rig three toe bones.
- Dive, brake, turns and the head close-up were not looked at on this
  mesh. The Blender pose sheets in `artifacts/hero-rig/` are older.

## The legs in flight, joint by joint (2026-11-15, 10:34)

Feedback report 0005 (`artifacts/showcase-feedback/0005-marked.jpg`) said
of version 4's fold: "don't like the new folding, refer online/other games
for more natural leg postures in flight". Its marks are the fore leg,
"unnatural folding", and the hind leg, "looks like standing in the sky".
Both were right. The hind Z kept the foot under the hip as on the ground,
and the fore tuck pushed the elbow up past the shoulder.

Flying quadrupeds in other games and in film carry the hind legs back
along the root of the tail, loose at the knee, and hold the fore legs to
the chest. One tuck value with alternating signs cannot do that, so the
rig has a pose by joint: `leg_flight_pose 1` and an angle each for hip,
knee and hock, shoulder, elbow and wrist, positive aft, with
`leg_flight_splay_deg` for the roots. With it on, the tuck and the trail
are not applied; the brake's reach and the pendulum still ride on the
root. A profile without the keys is unchanged.

The hero carries hind 45, 5, 35 and fore 65, -140, 105, from two rounds
of candidates (`artifacts/showcase-anim/flight-legs-pose-candidates.jpg`).
`flight-legs-pose-before-after.jpg` is the bite from the side, the fold on
the left; `flight-legs-pose-views.jpg` has the glide from the side and
the front, the dive, the brake, a landing and the stand. The Dragon
panel has "flight pose by joint" and its seven sliders under "front leg
tuck".

From behind, the chase camera's side, the trailing hind feet sat wide of
the tail. `leg_flight_splay_deg` is -18: the legs lie along the tail's
root without touching it. `flight-legs-splay-candidates.jpg` has -10, -18
and -26 from behind, above and the front.

Open: from the front the fore paws show their spread claws under the
chin. The trailing-fore variant (candidates Q5 and Q6) is the other
natural choice if the tuck reads wrong in play.

## The playtest's second round of reports (2026-11-15, 11:30 to 11:37)

Reports 0006 to 0012 are in `artifacts/showcase-feedback/`. The playtester
also said of the flight postures: "I don't like any new folding postures,
maybe use old dangling and keep these as alternative postures for future
adjustments".

| Report | Mark | Kind | Answer |
|---|---|---|---|
| 0006, 0007 | fore leg, "unnatural bending", "look at the bending!" | pose | the dangle, below |
| 0006 | belly, "geometry artifacts"; flank, "not completely fixed" | mesh | open: the next mesh round |
| 0007 | wing, "self shadow aliasing" (low priority) | shadows | open |
| 0008 | "water artifacts" close to the river, and 37 fps there | water | fixed, below; the cost is down 4 ms of 16 |
| 0010 | "strange whirls?" on the river from a distance | water | fixed, below |
| 0009 | "vegetation quality can be improved": the bushes and the grass | plants | open: S7 |
| 0011 | no tree shadow inside the mountain's shadow | lighting | open: trees need contact shade without the sun |
| 0011 | "rocks look unnatural" | rock | open: S6 |
| 0012 | "mountain shapes seem weird" | terrain | open: S6 |

The dangle is the hero's default again: hind 30, 20, 30, fore 25, -20, 45,
splay -6. The legs hang a little aft of where they stand with the feet
drooping, and the limb physics swings them.
`artifacts/showcase-anim/flight-legs-dangle-candidates.jpg` has the six
tried and `flight-legs-dangle-views.jpg` the choice in the glide, the
flap, from behind, in the bite, the dive and landed. The other postures
are the Dragon panel's "flight leg posture" presets (dangle, trail and
tuck, trail both, fold), and their numbers are in the profile's comment.

## The river's streaks and whirls (2026-11-15, 12:20)

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

## S4 and S5, first pass: keyframed strikes (2026-11-15, 11:20)

The engine side is `DragonRig::set_attack_clips`: five optional clips from
`assets/clips/<model>.attacks.glb` (`bite`, `claw_l`, `claw_r`, `tail_l`,
`tail_r`), each played once from its action's edge and added to the
finished pose after the head aim. The brake-bank trial put its clip under
the limb physics and it was smoothed away; a strike goes on last. Under a
playing clip the procedural neck, limb, wing and tail gesture drops to
`attack_clip_under` (0.15) and the bite's jaw is the clip's. The body's
pitch, roll, yaw and surge stay procedural, because a clip cannot move the
root.

Codex (gpt-6-astra, high, work account, 123k tokens, 18 minutes) authored
the hero's clips from `tools/clips/hero-attacks-brief.md`:
`tools/clips/make_hero_attacks.py`, a checker with 133 passing checks, and
`tools/clips/hero-attacks.md` with the measured axis table. Each clip has
a wind-up, a fast strike, an overshoot and a slower recovery, with the
joints down a chain peaking after their parents.

In the engine at full weight the clips overshot: the claw twisted the head
past the shoulder, the tail whip curled under the body, and the bite stood
the tail upright. The hero's profile plays them at `attack_clip 0.6`.
`artifacts/showcase-anim/strike-clips-bite-before-after.jpg` has the bite
at three frames, the procedural gesture above and the clip below;
`strike-clips-in-engine.jpg` has bite, claw and tail at four frames each.
The Dragon panel has "strike clips", "strike clip rate" and "procedural
gesture under a clip".

Not judged: how the strikes feel in motion and in a fight. Stills show
the poses and not the timing. The claw's large roll is the body's own
gesture and was there before.

## S3, as built: the rim, the clear coat and the cornea (2026-11-15)

A submesh now carries a surface class (`anim::SkinSurface`), read from the
glTF material's name by `surface_from_name`: hide, membrane, horn (and
claw), eye, mouth. `world_renderer.cpp` passes it in `material.w`, and
`skinned.hlsl` adds three terms.

- The rim. With the sun beyond the creature, the edges of the hide take
  its light: grazing angle squared, times how far the view looks toward
  the sun. The body's own shadow dims it to 40% and does not remove it,
  because the shadow map cannot tell a scale's lit edge from the flank
  behind it. Before this, a dragon seen against the sun was a black
  cut-out. A model with one material gets the rim too.
- The clear coat. Horn and claw wear a second, tight lobe (roughness 0.22)
  on the face normal, and reflect the sky through it.
- The cornea. The eye wears the same coat at roughness 0.11 and reflects
  the sky at 2.5 times the horn's share, so it has a window of sky in it
  from most angles.

The gains are "hide rim", "horn clear coat" and "cornea" in the Engine
panel's lighting section, beside "sky in the hide". They ride in
`scene.clouds2.yzw`, which was unused.
`artifacts/showcase-s3/rim-coat-cornea-before-after.jpg` has the head
against the sun and the body from below, before on the left.

The Metal goldens were captured again: every difference was on the
dragon. The Vulkan, D3D12 and D3D11 sets are stale until x99 next runs
`golden.py capture`. The retro tiers do not have these terms, and a model
pack does not store the surface class.

Not done in S3: the membrane's thickness and veins wait for mesh version
3's wing textures.

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
