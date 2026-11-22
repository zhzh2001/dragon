# Showcase: the hero dragon

The record of the hero dragon's model: the worker trial, the design, each
mesh version and its rig, and its surface in the shader. Part of the
showcase record; `docs/SHOWCASE.md` is the index and holds the order of
work. Sections are in the order they were written.

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

## A membrane that billows (2026-11-15, 21:14)

The blind critic's fourth round put the wings first: "the membrane is a
flat, opaque sheet, the finger bones are thick straight rods, and nothing
bends between the joints" (`CRITIC.md`, "The blind critic, round 4"). The
skin weights are the reason. A membrane vertex follows its finger rigidly,
so the sheet between two fingers is a plane whatever the wing does.

The modern tier now bows each panel under load, in the vertex shader.

A panel is the skin between two spars: two fingers, a finger and the arm,
or the last finger and the flank. `App::mark_membranes` measures every
membrane vertex in the bind pose, seen from above: `d1` to its nearest
spar and `d2` to the next. A sheet pinned on both spars bows as a parabola
across the panel, so the vertex's reach is `4 d1 d2 / (d1 + d2)`: the
panel's width at its middle, nothing on a spar. The slope of that reach
across the wing is stored too, because the lighting needs the bowed
normal.

There was no free vertex attribute, and a new one would change the vertex
layout of every backend. The values ride in the tangent's `w`, which
already carried the membrane share past 1: `|w| - 1` is now a 22-bit
integer over 2^22, the share in 3 bits, the reach in 5 and the slope's two
parts in 7 each. A float holds that exactly. The vertex stage takes it
apart (`skin_common.hlsl`, `membrane_bulge`) and passes the share on as
before. The first packing gave the slope 5 bits each and the panels were
lit in contour bands.

The shader moves the vertex along the bind pose's up, carried by the
vertex's own bones, so the bow follows a folded wing. The same offset is
in the shadow pass and in the velocity pass, which takes last frame's
amount for last frame's position.

The amount is one number a creature, from the rig
(`DragonRig::membrane_load`). The rig reads it off the pose it has just
built: each wingtip's height over its shoulder, in arm lengths, and how
fast that changes. A tip going down loads the sheet and a tip going up
unloads it. It is not read from the flap's phase, so the fold, the phase
delay and a strike's wing all count. Three dials in the rig profile:

| Dial | Default | Meaning |
|---|---|---|
| `membrane_camber` | 0.11 | the bow in a glide, as a share of the panel's width |
| `membrane_billow` | 0.12 | added at the downstroke's fastest, taken off at the upstroke's |
| `membrane_slack` | 0.04 | the sag under its own weight on the ground |

The hero's widest panel is 4.44 m. Its middle sits 0.49 m above the plane
of its fingers in a glide and about 1 m at the peak of a downstroke. At
0.07 the change did not read at game size; 0.16 looked quilted.

The amount rides after the joints in the skin block
(`WorldRenderer::skin_membrane`), not in the model block, which the D3D9
tiers have no register to grow. A packed palette does not carry it, so
the retro tiers keep the flat sheet.

The fragment stage also reads where a pixel sits in its panel. The light
through the membrane is 0.5 of what it was on a finger and 1.3 in the
middle of a wide panel, and the albedo is 20% darker along a finger. The
sheet reads as thin in the middle and thick at its bones.

`artifacts/wing-membrane/` has the glide from below and from above, before
and after, at full size, and `roster.jpg`, the seven species in the glide
with the bow on. Every species was looked at; none tears. The dials are
"membrane bow, glide" and "membrane bow, beat" in the studio's wingbeat
section and in the Dragon panel, with "membrane sag, landed". `--rig
membrane_camber=0,membrane_billow=0,membrane_slack=0` is the flat sheet.

The Metal goldens were captured again: every difference was on a wing or
in its shadow. The Vulkan, D3D12 and D3D11 sets are stale, and the change
has not run on those backends.

Seen and not fixed:

- The fingers are still straight rods. Bending them under load is a rig
  change.
- A wing lit at a low angle shows faint stripes of shadow acne. They are
  there without the bow too.
- The folded wing on the ground is two flat panels with a hard patch of
  transmitted light. The sag there is too small to change that.

## Fingers that bend (2026-11-15, 21:49)

The second half of the critic's line on the wings: "the finger bones are
thick straight rods". The fifth round saw it from the front on a
downstroke: "bone rods with at most a 20 px strip of membrane", "a
skeleton with no skin" (`CRITIC.md`, round 5, frame 4).

The rig now bends each wing finger up along its length with the load the
membrane carries. The angle is spread over the finger's joints, about the
same fore-aft axis the flap turns on. A quarter of it is the glide's
steady load. The rest follows the stroke, as the membrane's bow does: all
of it at a downstroke's fastest, at most 0.3 of it the other way on the
upstroke, where the recovery terms already droop the hand. On the ground
the stow owns the wing and the bend is off.

`finger_load_curl_deg` is the dial: degrees at the tip at a downstroke's
fastest. It is 0 in the code and 24 in the hero's profile. On the
generated Embercrest the far wingtip already arcs in the glide, and the
first version, with the whole glide load in the bend, hooked it up over
the back like a horn. So a species takes the bend in its own profile
after it has been looked at, and the roster's golden frames did not move.

`artifacts/wing-fingers/before-after.jpg` is the hero at frame 300 of the
flap, from the front and at three quarters. From the front the hand rises
from a hanging rod to an arc and the sheet under it shows. The beat was
looked at from the side, the front, the rear and above at frames 290, 310
and 330, and in the chase view with the flap held. The dial is "finger
bend under load" in the studio's wingbeat section and the Dragon panel.

The load is read from the wingtip's motion and the bend moves the
wingtip. The loop is negative: a tip that bends up reads as a rising tip,
which lowers the load. It was stable in every run.

## The rim's grain (2026-11-15, 22:12)

Rounds 4 and 5 named single bright pixels along the wing's edges and
over the mane. They were the hide's rim (`skinned.hlsl`, S3): the rim is
the square of the grazing angle, and where the mapped normal turns
faster than a pixel, on the sub-pixel scales of the neck and on every
silhouette, one pixel in ten caught it. Counted as pixels more than 45
levels over the median of their 5 by 5 neighbours, on the dragon in the
upstroke frame: 606 with the rim, 71 without it.

Three changes, all on the modern tier:

- Where the grazing angle jumps from one pixel to the next (its
  `fwidth` times 3), the rim takes a fixed mean, 0.5, in place of its
  own value. The dots together were the mane's glow against the sun;
  zeroing them took 8 levels off the mane. With the mean it keeps its
  brightness (63.5 to 61.3) and loses the grain.
- The specular lobe widens by the normal's variance across the pixel
  (Kaplanyan and others, 2016). On its own this took 5% of the dots.
- The light through a membrane is 0.12 of its full value on a spar,
  where it was 0.5: a finger is bone.

The count went from 635 to 224 in the upstroke frame and from 795 to 277
in the glide. `artifacts/rim-grain/before-after.jpg` has the head and
wing root at 2.5 times. The Metal goldens were captured again.

## Mesh version 6: broad wings (2026-11-21, 00:58)

The playtester on the wings: "they do look too thin". From above each
wing was a blade 3 to 4 m from front to back on a 19 m span, and the
flank behind the shoulder was bare. Seven critic rounds had named the
wings first.

A Codex job (gpt-6.2-astra, high, 23 minutes, 371,261 tokens; the brief is
`tools/showcase/hero-wide-wings-brief.md`, its record `tools/hero-build.md`,
"Version 6") rebuilt the wing sheet by construction:

| In the bind pose | Version 5c | Version 6 |
|---|---:|---:|
| Chord at the body | 1.70 m | 4.83 m |
| Chord at the wrist | 2.96 m | 5.61 m |
| Area of one wing | 18.86 m2 | 41.23 m2 |
| Wingspan | 18.99 m | 18.98 m |

Fingers 2 and 3 and the elbow rib are longer and swept back. The membrane
runs along the flank past the hip onto the tail's base. The trailing edge
hangs in a scallop between each pair of tips. The mesh passes 112 checks
and the rig 76; the longest folded edge is 1.38 of rest against 1.6. The
bone contract holds (`fingers 3/3`). The profile's `ground_wing_hand_elev_deg`
went from -20 to -6 and `ground_wing_fan_deg` from 8 to 6.

It is `assets/embercrest-hero.glb` on this machine now. Version 5c is
kept beside it as `assets/embercrest-hero-v5c.glb` with its profiles, for
a comparison in play (`--models assets/embercrest-hero.glb,assets/embercrest-hero-v5c.glb`
and `M`). Pairs, old on the left: `artifacts/hero-build/v6-final-*.jpg`.
In the game with the deeper beat: `artifacts/hero-build/v6-in-game.jpg`
(the chase camera, the beat from the side and the front, the glide,
landed, the top of the stroke).

Worse than version 5c, known:

- Folded on the ground the wing is a heavy cloak over the flank and the
  hind leg. The fold was tuned for a narrow wing.
- The membrane's large panels are lit in flat facets from some angles.
- In the close view the near wing hides most of the body.

## Version 6 withdrawn, version 7 asked for: wings with thickness (2026-11-22, 00:30)

Version 6 answered the wrong fault. The playtester, the next evening:
"sorry 'thin' I meant they are like paper, not broadness, please revert."
The merge of `hero-wide` is reverted (5c7737b): the build and rig
scripts, the skeleton, the thickness map and the ground fold are version
5c's. Version 6's scripts are on commit 1ba633f. Its one picture left in
the tree is `artifacts/hero-build/v6-final-above.jpg`.

What read as paper on version 5c (`artifacts/hero-build/v7-fault.jpg`):
the membrane was a shell 19.8 mm thick on a wing 9 m long, its trailing
edge a knife cut; each bone was a rod laid on a flat sheet; a panel was
one smooth orange with no relief.

A second Codex job (gpt-6.2-astra, high, 48 minutes, 550,683 tokens; the
brief is `tools/showcase/hero-thick-wings-brief.md`, its record
`tools/hero-build.md`, "Version 7", and `tools/hero-rig.md`, "Mesh
version 7") kept version 5c's outline and rebuilt the shell and its maps:

| In the bind pose | Version 5c | Version 7 |
|---|---:|---:|
| Shell thickness within 150 mm of a bone or the flank | 19.8 mm | 62 to 82 mm |
| Shell thickness at the middle of a panel | 19.8 mm | 34 mm |
| Trailing edge | a 19.8 mm wall | a rolled hem 78 mm across |
| Mean tilt of the normal map over the panels | 6.2 degrees | 17.6 degrees |
| Linear luminance, upper face, beside a bone and mid panel | one colour | 0.021 and 0.181 |
| Membrane triangles | 7,912 | 22,640 |
| Hide, membrane and horn triangles | 118,859 | 133,587 |
| Wingspan | 18.99 m | 18.99 m |

Both faces rise onto each bone over 175 mm. A finger tapers to about a
third of its wrist thickness. A panel sags 3.3% to 3.8% of its width at
rest. Tension folds 23 to 46 mm apart fan from the elbow, the wrist and
the finger joints, with veins that fork. The mesh passes 118 checks and
the rig 74. The bone contract holds (`fingers 3/3`), and no profile key
changed.

It is `assets/embercrest-hero.glb` on this machine. Version 5c stays as
`assets/embercrest-hero-v5c.glb` for a comparison in play (`--models
assets/embercrest-hero.glb,assets/embercrest-hero-v5c.glb` and `M`).
Pairs, version 5c on the left: `artifacts/hero-build/v7-matched-chase.jpg`,
`v7-final-close.jpg`, `v7-final-edge.jpg`, `v7-final-hem.jpg`,
`v7-final-landed.jpg`. The main session rendered it landed, in the dive,
at the bottom and the top of the beat, in the chase view and from above,
and saw no tear and nothing through the body.

Weak, known:

- Three checks the brief added fail: the shell's two faces pass through
  each other during the beat, the dive fold and the ground fold (up to
  885 vertices, 79 mm). Codex showed the cause is the fold itself: a
  shell 1 mm thick crossed as much. It did not show in the renders above.
- Edge on from the front the hem is thin along some stretches.
- The forearm keeps a seam under direct light, and the folded root has
  angular corners.
- In the chase view the gain is in tone and relief. The silhouette is
  the same wing.
