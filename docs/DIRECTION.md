# Direction: polishing the modern build before the port

What the game should become, and what to build in which order, now that the
flight core, combat, bots and the match loop exist. `RETRO.md` ranks features
in isolation; this document decides the *shape* -- the genre the features
serve, the look they should share, the interface that presents them and the
assets they need -- and then sequences the work. Written 2026-09-06 from a
read of the docs plus headless captures of the current build; revise it when a
playtest disagrees.

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
| **Weather** | Wind in the flight model, fog uniforms | A wind field that varies along the valley, fog banks in side valleys, rain later | medium |

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
| Prey (goat, sheep, deer) | **Sketchfab CC BY**, rigged, or unrigged at first | A herd needs a walk cycle and a scatter, and the glTF skinned loader and joint mapper already exist. Start unrigged with a bob-and-scatter; a rig is a later upgrade |
| Dragons | **Keep both.** No new hero models | The recolour system already makes a flight of distinct dragons. The hero's CC BY-NC licence is the one long-term liability: replace it with a CC BY or original rig only if the project is ever published. A script-built original was tried on 2026-09-08 and fell short of both imported models; `EMBERCREST.md` records why, and its rig-and-export step is reusable |
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
| 1 | Shared lighting, sun angle, palette header, tree/ground grade | 1 | visuals |
| 2 | HUD kit: fonts, tokens, primitives; port `draw_hud`/`draw_combat_hud`; theme ImGui; hide debug lines | 2 | UI |
| 3 | Run probe: `--run`, seed, rivals and ground defences along the corridor, caches, pass gate, death, results text | 2-3 | gameplay |
| 4 | Grading LUT and bloom | 1 | visuals |
| 5 | Vegetation v2: leaf cards, bark, impostors | 2 | visuals + assets |
| 6 | Growth stages as tuning tables; prey herd; hoard persistence | 2-3 | gameplay |
| 7 | Terrain detail textures, instanced rocks, ridge octave; static glTF loader | 2 | visuals + assets |
| 8 | Screens: title, pause, results, shop; gamepad focus model | 2 | UI |
| 9 | Sky: sun disc, cloud layers, aerial perspective; thermals | 1-2 | visuals + gameplay |
| 10 | Props: towers, ballistae, huts, hoard piles via Blender MCP | 1-2 | assets |
| 11 | Weather along the valley; wingman as a run reward | 2 | gameplay |
| -- | Then the RHI extraction (`RETRO.md` R1) | | port |

The playtest gates: after 3, is the corridor run fun; after 6, does growth
change how you fly; after 8, can a new player start, die and try again
without touching a panel.

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
