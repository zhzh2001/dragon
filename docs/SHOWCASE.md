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
| 0009 | "vegetation quality can be improved": the bushes and the grass | plants | the bush and the tuft rebuilt, below; ground cover is S7 |
| 0011 | no tree shadow inside the mountain's shadow | lighting | open; one route tried and ruled out, below |
| 0011 | "rocks look unnatural" | rock | open: S6 |
| 0012 | "mountain shapes seem weird" | terrain | first step done: the spurs, below |

The dangle is the hero's default again: hind 30, 20, 30, fore 25, -20, 45,
splay -6. The legs hang a little aft of where they stand with the feet
drooping, and the limb physics swings them.
`artifacts/showcase-anim/flight-legs-dangle-candidates.jpg` has the six
tried and `flight-legs-dangle-views.jpg` the choice in the glide, the
flap, from behind, in the bite, the dive and landed. The other postures
are the Dragon panel's "flight leg posture" presets (dangle, trail and
tuck, trail both, fold), and their numbers are in the profile's comment.

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

## Mesh version 5b: wings from the skeleton-guided sculpt (2026-11-15, 13:10)

Version 3's wing sculpt had a finger fan the skeleton did not, and its
membrane tore. For this one the plates were drawn along the skeleton's
own wing bones laid flat (`artifacts/showcase-study/hero-wing2-guide.png`,
`hero-wing2-plates.jpg`), Hunyuan made the sculpt
(`hero-cand-wing2-hunyuan.jpg`: three fingers, one rib, no stub bones),
and an Opus worker built the wings from it in two passes (branch
`hero-wing2`, merged; `tools/hero-build.md`, "Version 5" and "5b").

- The sculpt's panels are within 2% of the skeleton's, except the outer
  one, 28% wide, whose texture is compressed across.
- The membrane is one sheet with a second face of the same triangles
  19.8 mm away, so skinning cannot push one face through the other.
- The mesh passes its 86 checks and the rig its 56, none changed. The
  longest folded membrane edge is 1.40 of rest against a limit of 1.6;
  version 3's was 4.08.
- The first pass left the panel between the body and the rib creased: its
  target shape turns back under the arm, and a fit onto that is not
  one-to-one. The second pass builds that panel by construction, a ruled
  sheet between the flank and the arm-and-rib line. It has no fold past
  60 degrees at rest.

`artifacts/hero-build/pairs-v5b-*.jpg` are the worker's pairs, version 4
on the left; `artifacts/showcase-s2/v5b-wings-in-game.jpg` is the merged
model in a run, the glide, the flap, standing, landed and the bite.

Worse than version 4, known:

- The root panel has a smoothed colour and no veins. Mid-beat from the
  side it reads as a plain wall between the arm and the flank.
- The arm meets the back as an intersection, not a blended join.
- Folded on the ground, the forearm and the thumb claw stand higher over
  the back.

Version 4 is kept beside it on this machine as
`assets/embercrest-hero-v4.glb` with its profiles, for a comparison in
play (`--models assets/embercrest-hero.glb,assets/embercrest-hero-v4.glb`
and `M`).

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
