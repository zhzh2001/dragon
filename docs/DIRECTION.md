# Direction: polishing the modern build before the port

What the game should become, and what to build in which order, now that the
flight core, combat, bots and the match loop exist. `RETRO.md` ranks features
in isolation; this document decides the *shape* -- the genre the features
serve, the look they should share, the interface that presents them and the
assets they need -- and then sequences the work. Written 2026-09-06 from a
read of the docs plus headless captures of the current build; revise it when a
playtest disagrees. Revised 2026-09-13 after a week spent on the creature
roster instead of on this plan -- see "What happened instead" in section 1
and the melee item in sections 2 and 6.

The one-paragraph version: **make it a hoard-run roguelite** where growth
within a run is expressed as movement of the tuning sliders that already
exist; **pull the world up toward the dragon** rather than the dragon down to
the world, with a shared lighting model, one palette and a mid-detail
vegetation pass; **build a small HUD kit** with its own fonts and tokens and
theme the ImGui panels to match, so the tuning windows become a tool the
player never has to see; and **generate or procedurally build props, import
only creatures and scanned rocks**, so every new asset inherits the same
material language instead of bringing its own.

## 1. What the build looks like today

Captured with `--headless --frames 60 --screenshot` (chase, combat with
`--attack --bots 2`, `--inspect 30 6 10`, `--studio 9`, a 3-bot `--match`).
`concept/engine-baseline-2026-09-06.png` keeps the combat capture beside the
targets, so progress can be judged against where it started rather than
against memory.

- **Three visual languages share one frame.** The dragon is a scanned-quality
  PBR asset with centimetre-scale scale detail, normal and roughness maps,
  dark and desaturated. The vegetation is flat-shaded unlit-looking polygon
  blobs in pastel saturated green, brighter than the ground it stands on even
  though its albedo constants are as dark as the terrain's (the foliage
  shader's wider wrap and its ambient term lift it). The terrain is smooth
  noise shading with no texture, pale and low-contrast at flight distance,
  with mountains that read as clay. Fire is additive glow that whites out in
  the tonemap. Nothing is wrong with any of the three alone; the seam between
  them is what reads as unfinished.
- **The lighting is noon-flat.** Sun elevation 26 degrees at azimuth 135 with
  a strong hemispheric ambient. Flat light is the cruellest condition for a
  style seam: it removes the long shadows and warm/cool split that would tie
  materials of different detail levels together.
- **The HUD is debug output with gold paint.** ImGui's default bitmap font at
  several scales, panels docked across the top that overlap the rally and
  match readouts, the Combat panel open by default in the centre-bottom over
  the dragon, a vertical velocity line and the blue course spline drawn into
  gameplay views. Every element is useful; none of them belong to a system.
- **The game has no frame around it.** It opens in the valley with panels
  showing. There is no title, no run start, no death screen, no way to choose
  anything with a gamepad.

None of this is a criticism of how it got here: every one of those was the
right thing to do while the question was "does the flight feel good". The
question has changed.

### What happened instead (2026-09-11 to 13)

The week after this was written went to the creatures, not to rows 1-3 of
the sequence: an elemental roster of six generated dragons (four quadrupeds
on two skeletons, two wyverns), each a species with its own rig, flight and
breath profile, and then the animation work to make them stand, land, fold
their wings, fly with their legs where a flying animal keeps them, and plant
each foot on the terrain under it. The record is in `ANIMATION.md` (from
"Wings on the ground, and in the dive, per species" onward),
`MODEL_GENERATION.md`, and the tracked renders in `artifacts/`.

Two honest notes for the next session. First, **making new dragons was not
the priority this document set, and it was done because it was enjoyable**;
that is a fine reason on a hobby project, but the run probe is still where
the genre question gets answered, and it has not moved. Second, the detour
paid for infrastructure the plan needs anyway: a species is now a folder of
three partial config files, the rig plants limbs on terrain (the landing
state the hoard cache and the prey herd depend on), every pose dial sits in
the Studio panel, and CLAUDE.md carries a rendered-inspection rule that
would have saved several playtest rounds. **The roster is done for now** (seven,
after gpt-6 added Frostvein on 2026-09-14 as a second ice dragon built on its
own measured skeleton). No new species until an encounter kind needs a
silhouette these do not have;
the one open asset item is the default `dragon.glb`, whose wings still drape
on the ground because its rig has no wrist.

One playtest finding from flying the roster belongs in the plan rather than
in an animation doc: **close-range combat has no answer.** The breath cone
and the fireball both need the nose on the target, and in a turning fight the
rival is very often twenty metres away and off the nose, where the player can
do nothing but circle. A dragon that close should bite. Section 2 adds melee
to the encounter kit and section 6 schedules it early, because it changes
what the rival-dragon encounter feels like before the run probe measures it.

## 2. Genre: a hoard-run roguelite

### The decision

The ROADMAP's phase-4 candidates were boss duels, a hoard roguelite, novel
vignettes and the survival valley. Recommendation: **the hoard roguelite, with
the survival valley's growth arc folded into it as the within-run
progression.** Reasons:

- **The engine already generates its content.** Terrain from a seed, courses
  generated, forests placed by rules, every sound synthesized. A roguelite is
  the genre whose content *is* generation, so the project's native mode
  becomes the game's design. Boss duels and vignettes instead need authored
  creatures, animation and scripting -- exactly the skills the project lacks
  and the tools it does not have.
- **A run structure gives every existing system a job.** Rally rings become
  gates and route markers, bots become rival dragons met along the way, the
  match clock becomes the dragonslayers closing in, sentinels become ground
  defences, the landing state becomes how you collect a hoard or eat, the
  river becomes where you douse a burn, ghosts become a previous run to beat.
  Nothing built so far is thrown away.
- **Growth is free.** Every gameplay constant is already a slider, and
  `heft` already turns one number into a whole handling change. A hatchling
  is a low-heft, low-health, short-breath preset; an adult is the opposite.
  In-run growth is a table of slider moves, and permanent unlocks between
  runs are another. The survival valley's "your mechanics change as you grow"
  hook arrives for the price of a lookup table, without the walking, sleeping
  and eating simulation the full survival game would need.
- **It tolerates a small asset budget.** Variety in a roguelite comes from
  recombining a few encounter kinds across generated layouts, not from
  authored levels. Four or five encounter kinds and one valley generator are
  a game; the same four kinds in a survival sandbox are a tech demo.

What it costs: the roguelite needs encounter *variety* and each kind needs a
readable target, so it pulls on the models question (section 5) harder than
the other candidates would. And it needs a persistence layer and menus, which
the project has avoided so far (section 4).

### The run

One run is one generated valley, flown from the head of the valley to the
pass at its far end. The corridor the terrain already carves is the spine.

1. **Start** as a lean drake at the valley head: low heft, small health pool,
   a short breath meter, one fireball. Pick a species (the dragon or the
   wyvern -- the models already fly differently) and a seed.
2. **Fly the corridor.** Encounters are placed along it by the generator,
   spaced by flight time rather than distance so a fast dragon meets the same
   rhythm as a slow one. Side valleys and altitude offer routes: high and
   slow over the ridges is safe from ground fire but costs energy; low and
   fast in the corridor is where the hoard is.
3. **Grow.** Eating prey and seizing hoard advance a growth meter. Each stage
   (drake, young dragon, adult) moves the tuning: more heft and health, a
   longer breath, a second fireball, a stronger flap. Growth changes the
   flying, which is the point: the dragon you land at the pass is not the one
   you launched.
4. **Pressure.** A run clock the player never sees directly, expressed as
   dragonslayers: past a threshold, rival dragons and ground fire escalate
   along the valley behind you. Lingering to grow is a gamble.
5. **End.** Death ends the run and the hoard is lost. Crossing the pass banks
   it. A results screen shows the route, the growth reached, the hoard, and
   the best previous run's numbers beside them.
6. **Between runs**, the banked hoard buys permanent things: starting heft, a
   hide colour, the second species, an extra fireball, a starting breath
   upgrade. Small and legible; the run is where the game is.

### Encounter kinds, cheapest first

Each is one system; the run generator places them.

| Kind | What it reuses | What it needs | Session cost |
|---|---|---|---|
| **Rival dragon** | Bots, combat, hostile slots, hit and kill flow | A spawn along the corridor and a leash; bot tier scaling with run depth | small |
| **Ground defence** (watchtower, ballista) | Sentinels: fire on a timer with spread, health, HUD brackets | Pin the orbit to a fixed point on terrain; a prop mesh; a slower, heavier bolt with a visible arc | small |
| **Hoard cache** | Landing state, `surface_at`, the rally ring trigger volume | A landing zone marker, a hold-to-collect timer while grounded, a pile prop | small |
| **Prey herd** | Landing state, the terrain query, particles | A flock of simple ground movers that scatter from the dragon, a swoop-grab hit test, a bite gesture on the rig | medium |
| **Thermal** | Flight model, emitters | A vertical wind column in `FlightModel`, circling debris to mark it | small, already in RETRO |
| **Pass gate** | Rally ring, match results phase | Placement at the far end, the escape condition | small |
| **Melee** (bite, claw, tail) | The lock (it already knows the nearest rival), the head aim and neck spit gesture on the rig, the hit flash and damage flow | A close-range attack that does NOT need the nose on the target: a bite when the mark is inside a wide forward cone within a few body lengths, a claw or tail strike when it is alongside or behind, with the neck lunging toward it; a cooldown, a lunge that steals a little airspeed, bots that use it too. Distance and cone tests only -- no Jolt | small-medium |
| **Weather** | Wind in the flight model, fog uniforms | A wind field that varies along the valley, fog banks in side valleys, rain later | medium |

**The readable target each of the first three kinds needed now exists as an
asset** (2026-09-11): `assets/ashcoil.glb` for the rival dragon,
`assets/cragjaw.glb` for the ground defence, `assets/mossback.glb` for the
prey herd. Section 5 has the details. What remains for each is the *system*,
not the art -- and two engine assumptions block the wingless pair from being
drawn at all, both recorded under Open questions in `STATUS.md`: a rig without
wings is rejected outright, and model scale is derived from the X extent as
though it were a wingspan.

Melee is the one item here that comes from a playtest rather than from the
design: "very annoying when you are close to the enemy but with the wrong
heading." It is also what makes the rival-dragon encounter a fight rather
than two flame-throwers jousting, so it goes before the run probe measures
that encounter. Dials at the TOP of the Combat panel: bite range and cone,
strike range, cooldown, damage; the feel is found by playing a 2-bot match.

Deliberately later: team matches (a wingman could be a run reward but is not
needed for the loop), water gameplay beyond dousing, ragdoll deaths.

### The first probe

Two or three sessions: a `--run` mode with a random seed, rival dragons and
ground defences placed along the corridor, a hoard counter fed by landing on
three caches, a pass gate, death ending the run, and a results text. No
growth yet. Fly it ten times. The question it answers is whether flying the
corridor under pressure is more fun than the free-form match, and whether
altitude and route matter. If it is not fun, the roguelite is the wrong
shape and the boss-duel probe from `RETRO.md` is the fallback. If it is,
growth and prey come next.

## 3. Visual direction: one world at the dragon's level

### The target

**Painted realism, late afternoon.** A restrained, slightly desaturated
natural palette; warm low key light and cool aerial haze; the same
mid-frequency detail on the ground, the trees and the rock as the dragon
already carries; nothing flat, nothing scanned-perfect either. The dragon is
the hero and the most expensive asset, so it sets the ceiling; the world is
pulled up toward it, not the dragon flattened to the world. The alternative
-- flat-shading the dragon into a low-poly cel look -- is cheaper and would
port to the retro target trivially, but it discards the PBR work and the one
asset that already looks finished, and it commits the game to a look many
small flight games already own. Reference points: Shadow of the Colossus for
atmosphere and scale, The Witcher 3's distant landscapes for how painted
detail reads at range, Firewatch for palette discipline (not for its
flatness).

`concept/valley-target.png` is the generated target for this look;
`concept/vegetation-sheet.png` is the reference for the plant pass. Generate
replacements with the concept-art skill when the look is retuned, and keep
the engine capture beside the target when judging progress.

What the target shows that the build does not, in the order it matters:
a low warm sun with long shadows and a cool blue haze that deepens with
distance; forest whose colour varies tree by tree (yellow-green broadleaf
against near-black spruce) instead of one green; a river with gravel bars and
a braided bed; rock faces with horizontal strata as *geometry*, not tint;
boulders and scree; peaks that are sharp; a sky with clouds and a sun disc.
Ignore in it: the dragon sits closer to the lens than the chase camera ever
places it, and the peaks are more alpine than a 5 km valley's ridges need to
be. The vegetation sheet's lesson is the leaf clusters -- painted cards with
dark undersides and two or three greens per crown -- and a boulder that is
grey with moss on top, which is one texture and a slope-keyed tint.

### The work, in the order it pays

1. **Shared lighting and a palette pass** (one session, no new systems). One
   lighting function for terrain, foliage and the skinned mesh -- same wrap,
   same hemispheric ambient with a warm ground bounce, same fog -- so the
   seam vanishes before anything is modelled. Lower the sun toward 12-16
   degrees and warm it; cut the ambient so shadow sides go dark and cool.
   Then a palette: eight to twelve named colours in one header, pushed as
   uniforms, with the tree greens graded toward the terrain greens and the
   grass desaturated. Half of the mismatch in the captures is that trees are
   lit and coloured by a different path than the ground under them.
2. **Colour grading and bloom** (one session). A grading curve or a small LUT
   at the end of the blit, and a cheap downsampled bloom so fire glows
   instead of clipping to white. A single grade applied to everything is the
   strongest glue there is between assets of different origins, and the
   tonemap's whitening of bright things (see `EFFECTS.md`) is what a bloom
   pass fixes properly. Both port: a 2D LUT strip and a half-res blur are
   SM3-era techniques.
3. **Vegetation v2** (two sessions). Keep it procedural -- the generator is
   the reason every tree matches every other tree, and imported trees from
   several authors would recreate the seam -- but move from single-facet blobs
   to **clustered alpha-tested leaf cards** with a small painted leaf texture,
   darker undersides, per-cluster colour jitter and translucency in the wrap.
   Trunks get a bark texture. Distant trees collapse to a two-card impostor.
   Alpha-tested cards are exactly what 2005 shipped, so this is a retro asset
   too.
4. **Terrain detail** (two sessions). A tiling albedo plus normal detail set
   for grass, scree and rock (PolyHaven, CC0), blended by the same
   slope/height rules the shader already uses. This is also `RETRO.md`'s R3
   bake, done early. Then **instanced rocks**: scanned boulders on slopes
   past the rock threshold, through the foliage instancing path, which is
   what turns clay mountains into mountains. Ridge sharpening in the
   generator (a ridged octave) is a cheap third step.
5. **Sky and air** (one session). A sun disc, two billboard cloud layers
   with shadows on the ground from the upper one, and an aerial-perspective
   term that blue-shifts and lightens with distance separately from the
   height fog. Clouds are also the altitude cue the flight model deserves.
6. **Fire and impact** (half a session, after bloom). Fire gets a hot core
   and a dark smoke tail; impacts throw debris that is lit; the vignette on
   damage reddens further. Mostly retuning once bloom exists.

Not recommended: deferred shading, HDR render targets, screen-space
reflections, volumetric fog. They fight the retro track and the look above
does not need them.

## 4. UI: a HUD kit, with the panels as its tool mode

### Principles

- **Two layers, one theme.** A HUD layer drawn every frame and a tool layer
  (the ImGui panels) hidden by default, themed with the same tokens so that
  opening a panel looks like opening a drawer in the same cabinet. F1 already
  toggles the panels; it becomes the boundary between playing and tuning.
- **One accent, one danger, one font pair.** Gold is already the accent and
  should stay. Red for threat and damage only. A condensed display face for
  numerals (speed, timer, score) and a humanist sans for labels, both loaded
  into ImGui's atlas from TTF files -- ImGui's draw list is fine as the
  renderer; it is the bitmap font and the default style that read as debug.
- **Diegetic first.** Speed is already the wind and the FOV surge; keep the
  numeral small. Energy and altitude belong on a slim arc beside the reticle,
  not in a box. Health is the vignette plus a bar; breath is the amber meter
  next to it. Threat is the arc at the screen edge, which already works.
- **Corner-anchored, safe margins, height-scaled.** Layout in fractions of
  the frame height so the same HUD survives 16:10, 4:3 (the retro build) and
  a retina window.
- **Gamepad-navigable menus without ImGui navigation.** ImGui's nav is
  disabled for a good reason (`CLAUDE.md`); the menus need a tiny focus
  model of their own: a list of items, up/down, confirm, back.

### The kit

A `ui::Hud` module: tokens (colours, sizes, margins, fonts), primitives
(plate, bar, arc, bracket, label, numeral), and layouts for each state. The
existing `draw_hud` and `draw_combat_hud` become callers of it. Then the
ImGui style: dark translucent plates, the accent gold, the same two fonts,
panels docked to the right edge and never over the centre.

### The screens

Title (species, seed, start, tune), run start countdown, in-run HUD, pause,
results, and the between-run hoard shop. A small screen state machine owns
which HUD layout is live and where input goes. `concept/hud-run.png` is the
generated mockup of the in-run layout to build from; the concept-art skill's
NB2 backend obeys layout instructions, so regenerate it when the run's
readouts change rather than sketching by hand. Read that mockup as a spec:
the words "Airspeed", "Threat arc" and "Energy arc" are annotations, not HUD
text, and the airspeed value is nonsense. What to take from it is the
palette (warm gold, off-white, dull red, translucent charcoal plates), the
weight of the numeral against the labels, the corner anchoring with the
centre left empty, and the top strip that carries hoard, timer and valley
progress in one plate.

### What to remove from gameplay views

The velocity line and the course spline are debug draw and move behind a
debug toggle. The rally readout and match strip merge into one top-centre
strip. The Combat panel no longer opens by default; its two dials that decide
fun (aim assist, bot spread) get a place in the pause screen's assists page
so that the rule "a dial that cannot be found does not exist" still holds
without a panel.

## 5. Models and assets: build the world, import the creatures

### The rule

**Everything a run places many of is generated or built in-house against a
shared material set; only things that need a scan or a rig are imported.**
Style consistency is a property of the pipeline, not of shopping carefully:
one generator or one material library produces matching assets by
construction, while a Sketchfab tower next to a Poly Pizza hut next to a
scanned dragon never matches however well chosen.

### By category

| Need | Source | Why |
|---|---|---|
| Trees, grass, reeds | **Procedural, in-engine** (vegetation v2) | Matches itself, adapts to the palette, already instanced and shadowed. Blender's tree add-on exported to glTF is the fallback if the in-engine generator plateaus |
| Rocks, boulders, scree | **PolyHaven scans** via Blender MCP, decimated, glTF | CC0, and a scanned rock next to a scanned dragon is the one case where importing gives consistency for free |
| Ground textures | **PolyHaven** tiling sets | CC0, PBR, the detail pass in section 3 |
| Watchtower, ballista, huts, hoard pile | **Modelled in Blender via MCP** from primitives, with PolyHaven stone, wood and metal materials | Simple shapes; the shared textures are what make them belong. Hyper3D/Hunyuan generation is worth one experiment for the hoard pile and a ballista, where the mesh is static and the texture will be replaced |
| Prey (goat, sheep, deer) | **Generated and rigged in-house** -- superseded the Sketchfab plan | `assets/mossback.glb` is a horned grazer taken through the one-shot pipeline: 80 K tris, a 37-bone rig on `tools/skeletons/grazing-quadruped.json`, repaired PBR. No licence question, and it shares the generator's material language. A herd still needs a walk cycle and a scatter; start with a bob-and-scatter |
| Dragons (the player and the rivals) | **The roster of seven is done for now.** No new species until an encounter needs a silhouette these do not have | Embercrest, Rimefang, Blightmaw, Ironroot, Frostvein (quadrupeds) and Stormsail, Tidewrack (wyverns), all generated and rigged in-house through the `MODEL_GENERATION.md` pipeline, each a species with `.rig/.flight/.breath.cfg`. They fly, land, fold their wings and plant their feet on terrain (`ANIMATION.md`). The imported hero `dragon.glb` (CC BY-NC) stays as the default asset but its rig has no wrist and its wings drape on the ground; the generated roster is the long-term answer to that licence question. Adding a species costs one generation plus a rebuild and a few rounds of rendered tuning; the traps are written down, so it is cheap -- which is exactly why it must not become the default thing to do |
| Encounter creatures | **Generated and rigged in-house**, one per encounter kind | This is a revision: the original entry said the recolour system was enough. It is not -- a hue push does not change a silhouette, and a rival read at flight range is a silhouette. `MODEL_GENERATION.md` concludes generation pays exactly for things "few on screen, large, and want a unique silhouette", which is what an encounter creature is. Three exist: `ashcoil.glb` (rival dragon, a legless sky-wyrm), `cragjaw.glb` (ground defence), `mossback.glb` (prey). Each is a body plan the engine had not carried, so they separate by shape rather than by hue |
| Clouds, leaf and bark textures | **Generated** (concept-art skill, NB2) or PolyHaven | Small painted textures are what the tool is good at, and they can be regenerated to the palette |

### Pipeline notes

- A **static glTF loader** is needed: today only skinned meshes import. It is
  the smaller half of `load_skinned_gltf` and unblocks rocks and props.
- Every imported texture passes through one import step that caps size,
  builds mips and, later, compresses to DXT (`RETRO.md` R3), so no asset
  arrives at 4K again.
- Imported albedos are graded by the same LUT as everything else, which is
  how a scan from one source and a paint from another end up in one palette.
- Record every third-party asset in `ATTRIBUTION.md` as it lands, with its
  licence; CC0 and CC BY only from here on.

## 6. Sequencing

Ordered by how much visible or playable improvement each session buys, and
so that the run probe arrives before the world is fully dressed -- the genre
question is the one that could still change the plan.

| # | Work | Sessions | Area |
|---|---|---|---|
| 1 | ~~Shared lighting, sun angle, palette header, tree/ground grade~~ done 2026-09-17 | 1 | visuals |
| 2 | ~~HUD kit: fonts, tokens, primitives; port `draw_hud`/`draw_combat_hud`; theme ImGui; hide debug lines~~ done 2026-09-20 | 2 | UI |
| 2b | ~~**Melee**: bite in a wide forward cone within a few body lengths, claw or tail strike alongside, neck lunge on the rig, cooldown, bots use it; dials at the top of the Combat panel~~ done 2026-09-17, awaiting the playtest gate | 1 | gameplay |
| 3 | Run probe: `--run`, seed, rivals and ground defences along the corridor, caches, pass gate, death, results text | 2-3 | gameplay |
| 4 | ~~Grading LUT and bloom~~ done 2026-09-20 (a parametric grade rather than a LUT: every term is a slider) | 1 | visuals |
| 5 | ~~Vegetation v2: leaf cards, bark, impostors~~ done 2026-09-20 (the impostor is the crown's own coarse cards, not a baked billboard) | 2 | visuals + assets |
| 6 | Growth stages as tuning tables; prey herd; hoard persistence | 2-3 | gameplay |
| 7 | Terrain detail textures, instanced rocks, ridge octave; static glTF loader | 2 | visuals + assets |
| 8 | Screens: title, pause, results, shop; gamepad focus model | 2 | UI |
| 9 | Sky: sun disc, cloud layers, aerial perspective; thermals | 1-2 | visuals + gameplay |
| 10 | Props: towers, ballistae, huts, hoard piles via Blender MCP | 1-2 | assets |
| 11 | Weather along the valley; wingman as a run reward | 2 | gameplay |
| -- | Then the RHI extraction (`RETRO.md` R1) | | port |

The playtest gates: after 2b, does a close fight resolve instead of
circling; after 3, is the corridor run fun; after 6, does growth change how
you fly; after 8, can a new player start, die and try again without touching
a panel.

Status on 2026-09-13: none of the rows has started; the week went to the
roster (section 1, "What happened instead"). The next session starts at row 1
or row 2b, and the roster is not a reason to reorder anything above.

Status on 2026-09-17: **row 1 is done** (`STATUS.md` M17 part; before/after in
`artifacts/visual-row1/`). Lighting is one path in `scene_common.msl`, the sun
is at 14 degrees from the south-west, the palette is `gfx/palette.h` with a
Palette panel, and plants index it instead of carrying colours. Two things
learned doing it, for rows 4-9: the comparison capture found a bug no target
would have -- the map-edge sky fade sat 500 m inside the playable extent, and
the default course starts there, so the pale valley floor in every capture
since M3 was the fade, not the grass or the fog. And per-tree colour variation
(a warm push per instance) does more for "many trees" than any palette value;
vegetation v2 should keep it. Next: row 2b (melee) or row 2 (HUD kit).

Same day: **row 2b is built** (`STATUS.md` M18, `COMBAT.md` "Melee"). What it
still needs is the playtest gate above -- does a close fight resolve instead
of circling -- with the melee dials at the top of the Combat panel. One thing
the headless checks could not answer: bots break off at 80 m and rarely come
within bite range of a player who is not steering at them, so how often a
RIVAL bites is a live question; the doctrine change (an aligned pass presses
to bite range) is the first dial to turn if they never do. Next: row 2 (HUD
kit) or row 3 (the run probe).

2026-09-19, after the first playtest of it ("underpowered, harder to land
than the breath, animation and sound indistinguishable, bots never use it"):
`STATUS.md` M18.1. Melee is now a flurry (0.55 s) whose hits stun, knock and
chain, with its own swing and hit sounds and a camera jolt; bots charge a
lined-up target and one did bite in a two-minute autopilot match; studio 10
is the bite bench; and a **training room** of passive dummies is a click
away at the top of the Combat panel. Back to the same gate: does a close
fight resolve. Then row 2 or row 3.

Second playtest, same day: melee feels good, bots bit once in three matches,
the bite read as calmly eating, no claw or tail. `STATUS.md` M18.2: the bite
is a strike now; three gestures by bearing (bite, claw, tail) with their own
studio scenarios; and the bot charge was rebuilt from measurements -- live
alignment, body steering, boost then speed-match -- taking the closest
approach in a passive two-minute match from 47 m to a bite pass at 16 m.
Gate unchanged; the dials are `charge_range` and the bot melee cone.

Third pass, same day ("still too weak and mechanical -- the other body parts
should move accordingly"): `STATUS.md` M18.3, the gesture layer. Every swing
is wind-up, strike, settle, and the root, wings, neck and tail answer the
limb; the directions are pinned by a test and the renders are in
`artifacts/melee/gesture_*`. The reference asked for was Legend of Spyro and
Glyde; nothing frame-level was findable online, so this is animation
principle (anticipation, weight, follow-through) rather than a copy.

Fourth pass ("add flip and roll; bots are conservative, flee when chased;
personality by health or at random; they lack boost"): `STATUS.md` M19.
Aerobatics on Z and B through the real flight model, and a personality
layer -- drawn aggression, health-moved nerve, flips and rolls as the answer
to a hit, boosts to close or to run. Gate unchanged. Next: row 2 (HUD kit)
or row 3 (the run probe); the melee/bot work has had four passes and should
now be left to playtesting.

2026-09-20: **row 2 is done** (`STATUS.md` M20, before/after in
`artifacts/hud/`). The kit is `ui::Hud`; the two faces come from the
machine's system fonts (DIN Condensed Bold for numerals, Avenir Next for
labels) with ImGui's default as the fallback -- shipping fonts is an asset
question for later. Two things from section 4 deliberately did not move:
the Combat panel still opens by default, because its two fun dials have
nowhere else to live until the pause screen exists (row 8), and the rally
and match strips share one plate but not one state machine, which the
screens will bring. Next: row 3, the run probe.

2026-09-20, later: **rows 4 and 5 are done** (`STATUS.md` M21, M22), the
arena being fun enough to want it prettier first. The post stack is a
parametric grade and a half-resolution bloom, not a LUT -- every term is a
slider, which is the house rule. Vegetation v2 kept the generator and changed
what it generates: card crowns cut from two Blender-rendered grey maps that
the palette colours, so the tree greens are still dials. What section 3
still lists: row 7 (terrain detail textures, instanced rocks, a ridge
octave; the static glTF loader), row 9 (sky, clouds, aerial perspective), and
the fire retune (item 6) now that bloom exists. Next: row 3, the run probe,
or row 7 if the picture is still the priority.

## 7. Prototyping with concept art

The loop from `.claude/skills/concept-art/SKILL.md`: generate a target, look
at it, implement toward it, capture the engine, compare. Three targets live in
`docs/concept/` now; conventions for the rest:

- **Key art and mood** through the GPT web backend; it invents scenery, so
  strike anything that is not in the game from the prompt and ignore what it
  adds.
- **Layouts and sheets** through NB2: HUD mockups, orthographic reference
  sheets with a scale bar, texture swatches. It obeys, so write the prompt as
  a spec.
- Name files for what they are, commit only the ones that set direction, and
  when a target is superseded replace it rather than adding a numbered
  sibling.
- Always capture the engine from the same viewpoint as the target
  (`--cam`, `--inspect`) and look at both. The comparison, not the target,
  is the deliverable.
