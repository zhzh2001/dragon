# Status and milestone history

What is built, in the order it was built. The plan these came from is
`ROADMAP.md`; where the project goes next is `DIRECTION.md`, and the ports are
`PORTING.md`.

## Latest local asset — 2026-10-02: gold dragon

The approved direct-reference Hunyuan gold dragon is rigged and integrated as
`assets/gold-dragon.glb`: 80 joints, 80k triangles, independent measured
skeleton, compact sail wings and a 14-joint tail. It has its own fire breath,
flight and rig profiles and is the eighth local default entry, after Tidewrack.
Embercrest remains first; release packaging still includes the seven release
species. See [validation and limitations](../artifacts/gold-dragon-integration/README.md).

**2026-10-03 tail refinement:** a slow travelling curl now drives the gold
dragon's spring target, with a supported base, softer tip, and smooth landing
fade. Flow controls are tunable and saved per species; other profiles default
to zero flow. Full-size roster, flight, landing and switching inspection plus
all 15 suites pass (1,298 animation checks). Human steering feel remains to be
judged. See [motion preview and evidence](../artifacts/gold-tail-flow/README.md).

## Release — 2026-10-01: public, packaged, one shader source

- **Public** at https://github.com/zhzh2001/dragon. The repo there is a
  filtered mirror of this one (`docs/RELEASE.md`), MIT, with
  `AI_DISCLOSURE.md`. Release v0.1.0 is a universal macOS 11+ app.
- **The release roster** is the seven finished generated species,
  Embercrest first. The two Sketchfab dragons are early attempts, not
  shipped (`ATTRIBUTION.md`).
- **P0**: data beside the executable, saves in the per-user directory, two
  shipped OFL fonts, `tools/release/package_macos.sh`.
- **P1**: the shaders are HLSL through SDL_shadercross, matching the MSL
  goldens to within 1/255 in all but a handful of pixels (`docs/PORTING.md`).
- **P2** (2026-10-02): Linux on Vulkan, and Windows on D3D12 and Vulkan,
  checked on x99, with per-backend goldens and CI on all three. There is a
  Windows package, but no Linux package yet: it needs a build in an older
  container for glibc.
- **R1** (2026-10-03): the RHI (`src/rhi/rhi.h`), with SDL GPU as its one
  backend. A pure refactor: Metal goldens unchanged.
- **Next**: R2's content tiers on the Mac, and the R0 spike once x99 or the
  G41 is up (`docs/PORTING.md`).

## Latest asset candidates — 2026-09-28

Sunspear (fire, triangular sails) and Rimeplume (frost, feather fans) now have
independently measured rigs, repaired materials and selectable game profiles.
Six original elemental concepts were explored; four remain concepts. Both
80k-triangle candidates passed native multi-axis/landing/switch inspection and
1,225 animation checks each; all 15 CTest suites pass. Human motion/playtest
judgment remains. See [evidence and launch commands](../artifacts/elemental-expansion/README.md).

## Status

- **M1** window, GPU device, reversed-Z depth, offscreen render + blit,
  hot-reloadable MSL pipelines, ImGui, headless screenshots, math + tests.
- **M2** free-fly debug camera, immediate-mode debug line drawing, grid.
- **M3** procedural valley terrain, analytic height queries, sky, height fog,
  directional light, directional shadow map.
- **M4** energy flight model, greybox dragon with shader-driven wing flap, chase
  camera, live telemetry.
- **M4.1** playtest fixes: keyboard/gamepad controls, roll and inversion
  recovery, asymmetric wingbeat, 5 km valley, shared tonemapping.
- **M5** chase camera: terrain-swept spring arm, turn lead, free look, first
  person, three presets.
- **M8** Dragon Rally: checkpoints, timing, splits, ghost replays, three
  generated courses, HUD, autopilot.
- **M8.1** difficulty assists: bank limit, auto-flap, inverted pitch by default.
- **M6** skeleton, GPU skinning, the procedural dragon rig, and a glTF skinned
  loader. The animation system drives an arbitrary imported skeleton, not just
  the generated one.
- **M6.1** the real dragon: glTF skinned load, inertial neck/tail chains, full
  PBR material set, and the file's authored clip layered under the rig.
- **M11** combat core: fire breath, fireballs, boost, health and regeneration,
  practice sentinels that shoot back, and the combat HUD.
- **M12** combat readability: sticky lock-on with aim assist and drop
  compensation, target cycling, threat direction, difficulty presets.
- **M14** bot dragons: a BotPilot state machine (attack / extend / evade)
  steering the same flight model the player flies, honest gunnery, terrain
  doctrine, spawn/respawn through Combat's external hostile slots.
- **M15** the match loop: countdown / fight / results / rematch, first-to-N
  deathmatch with an optional clock, weapons-cold phases, per-tier bot
  survivability (health and regeneration), match HUD.
- **M7 (part)** additive billboard particles (fire, ember trails, impact
  bursts) and fully synthesized audio -- wind that brightens with speed, flame
  roar, wingbeat whooshes, shots, hits and explosions, no sound assets at all.
- **M16 (part)** the world and the ground: exact-surface terrain queries, a
  carved river with a reflective surface, a coarse skirt past the playable
  extent, four tree and three grass kinds placed by slope, altitude and water,
  landing and take-off states, and `heft` as per-model handling.
- **Two generated hero candidates are rigged and load in the engine.**
  `assets/embercrest.glb` (six-limbed dragon, 80K tris, 62 joints)
  and `assets/stormsail.glb` (wyvern, 80K tris, 55 joints), both with UVs and
  4096² PBR sets, both accepted by the rig mapper with no joint warnings.
  Generated from original concept art through Hunyuan's one-shot, then
  decimated and rigged locally by `tools/rig_embercrest_candidate.py`. Neither
  is wired in as the default model, and their licence is unresolved -- see
  `ATTRIBUTION.md`. The whole procedure, and what was measured to arrive at
  it, is `docs/MODEL_GENERATION.md`.
- **M17 (part)** visuals, row 1 of `DIRECTION.md`: one lighting path for
  terrain, plants, props and the skinned creature (`direct_sun`,
  `ambient_light`, `translucent_sun`, `apply_fog` in `scene_common.msl`), a
  late-afternoon sun at 14 degrees from the south-west, a hemispheric ambient
  with a warm ground bounce, and the world's colours as a named palette pushed
  as uniforms (`gfx/palette.h`, the Palette panel). Plant meshes carry palette
  indices, not colours, and each tree gets a per-instance warm push. Also found
  and fixed while comparing captures: the terrain's map-edge sky fade was still
  at the playable extent after the skirt was added, so the default course's
  start sat inside it and every run opened on a floor blended halfway to sky.
  Renders in `artifacts/visual-row1/`.
- **M18** melee (row 2b): a bite in a wide cone ahead of the mouth and a
  claw/tail strike around the body, no aim needed, on a cooldown that costs
  airspeed; bots swing with the same geometry and an aligned attack now
  presses to bite range before extending; the neck lunges and the jaw snaps
  on the rig; dials at the top of the Combat panel; `bites swung/landed/
  taken` on the telemetry line. `COMBAT.md`, "Melee".
- **M18.1** melee after the first playtest ("underpowered, harder to land
  than the breath, cannot tell the animation, bots never use it"): cooldown
  0.55 s, a hit stuns and knocks its target and hits chain to x1.7, a bigger
  lunge, a swing sound and a separate hit sound with an ember burst and a
  camera jolt, bots charge a lined-up target instead of timing out, studio
  scenario 10 for the bite, and the **training room** (`--training`): six
  passive dummies ahead of the spawn.
- **M18.2** after the second playtest ("bots rarely bite, the bite is slow
  like calmly eating, no claw or tail animation"): the charge judges
  alignment on the live position inside the bot's bite cone, flies at the
  body, boosts beyond 150 m and matches speed inside 80 m (closest approach
  47 m -> a bite pass at 16 m); the bite is out in a third of 0.32 s; **three
  gestures** chosen by the mark's bearing -- bite, claw (foreleg, or hind leg
  on a wyvern, forward and out), tail whip -- with studio scenarios 11 and
  12 on a still body to diff them frame to frame. `COMBAT.md`, "Melee".
- **M18.3** the gesture layer: every melee swing is wind-up, strike and
  settle, and the whole body answers it -- the bite rears and flares its
  wings then surges and sweeps them back; the claw rolls and yaws into the
  strike with the tail balancing; the tail whip counter-turns the body and
  swings the head. Directions pinned in `test_anim`. `ANIMATION.md`, "Melee
  is the whole animal".
- **M19** aerobatics and bot personality: a one-button roll (a dodge) and
  flip (a half loop and roll-out to face a chaser), written as control
  inputs to the real flight model with the assists stood down; bots draw an
  aggression each, their nerve follows their health, a hit is answered with
  a flip, a roll or a jink by nerve, wounded pilots run boosted, bold ones
  boost to close; terrain outranks all of it. `COMBAT.md`, "Personality,
  nerve and aerobatics".
- **M20** the HUD kit (row 2 of `DIRECTION.md`): `ui::Hud` with tokens,
  primitives and two TTF faces; the rally and combat HUDs ported onto it
  (health and breath plate at the bottom centre above the airspeed -- it
  spent one build top-left and was read only once empty -- one top strip for score/clock/target or
  timer/checkpoints/progress, airspeed plate with the ability pips at the
  bottom, gold reticles and brackets, red only for threat and damage); the
  panels themed the same and docked along the right edge; the debug lines
  (flight path, ground probe, course spline) off by default;
  `--hide-panels` for a player's-eye capture. Before/after in
  `artifacts/hud/`. The screens (title, pause, results, shop) are row 8.
- **M21** grading and bloom (row 4): the world renders linear into a 16-bit
  target; `gfx::PostProcess` runs a half-resolution bright pass and two
  blurs, then a composite that adds the bloom, tonemaps (Reinhard, as every
  lighting value was tuned against) and grades -- exposure, contrast,
  saturation, white balance, split-toning, lift, gamma, vignette -- into the
  8-bit target the HUD draws onto. Fire glows instead of clipping to white.
  Before/after in `artifacts/post/`. The first grade read a stop dark: it
  applied contrast in linear light about 0.18, which crushed every shadow
  below the pivot, and its split-tone colours darkened as they tinted. The
  grade now works on the gamma-encoded value about display middle grey,
  the tone colours are normalised to unit luminance, and the graded frame
  measures within two percent of the ungraded one (`--no-post` renders that
  for comparison; `artifacts/post/grade_*.png`).
- **M22** vegetation v2 (row 5): crowns are alpha-tested leaf and needle
  cards cut from two grey detail maps rendered in Blender and coloured by
  the palette; bark streaked in the shader; dark undersides; card-shaped
  shadows; and a distance LOD that drops a crown's detail cards so its
  coarse cards are the impostor, in one draw. `WORLD.md`, "Vegetation v2".
  Renders in `artifacts/vegetation-v2/`.

- **M23** after the row 4/5 playtest: the composite tonemaps with a
  hue-preserving curve (luminance through Reinhard, ratios kept, an
  out-of-gamut colour scaled by its peak channel; `hue_preserve` 0.8 blends
  a little of the old white-hot core back in), so a stack of fire puffs is
  orange and frost is blue instead of both going cream; the default fire is
  a deeper orange to match. The brake is a flare: root nose-up, wings
  raised and protracted, tail dropped, legs forward, eased and made
  asymmetric by the roll input so a braking turn leans instead of sitting
  up (pigeon landing kinematics; `brake_*` dials in the Dragon panel).
  Trees continue onto the skirt at a thinner cover (`skirt_trees`,
  `skirt_cover`), the ground behind the spawn no longer bare; the tree
  instances are uploaded sorted into 320 m ground cells and each pass draws
  only the cells its frustum and `tree_draw_distance` admit, so half again
  as many trees cost the same frame. The health and breath plate went back
  to the bottom centre, bots got a health bar under the bracket, and the
  grade was retuned in perceptual space after reading a stop dark.
  Renders in `artifacts/post/`, `artifacts/brake/`, `artifacts/vegetation-v2/`.
- **M28.2** hunting with the lock and the assist (prey get combat slots),
  the pounce and the fury toned down, hunters and arena aces pounce too, and
  (the player roar was tried and reverted: it sounded bad).
- **M28.1** playtest fixes: the charged shot seeks its lock, the ram
  pounces onto a mark, the fury fills faster and reaches 340 m with a bar
  and a call-out, hits and damage over time are told apart (a crosshair X,
  a burn segment on the health bar), bots break a turning circle, growth
  thresholds raised (ancient at 2200), and a restarted run no longer keeps
  the last run's second breath.
- **M28** row 7: a static glTF loader, instanced rocks on the slopes
  (rocks.glb and the detail tile built in Blender by a delegated agent), a
  tiling detail texture per terrain material, a ridge octave and strata as
  geometry. `WORLD.md` "Row 7".
- **M27** abilities, one per growth stage (charged shot, second breath,
  ram, fury) instead of only bigger numbers; every ability's cooldown and
  power grows with the dragon; per-element sounds and breath voices.
  `COMBAT.md` "Abilities".
- **M26.1** balance after the playtest: blight lighter and damage-over-time
  no longer blocks healing; tide now weakens the drenched body's own attacks
  (it only stopped regeneration, which any hit already does); enemies scale
  per valley deeper (+40% damage, +50% health, keener); growth goes on to
  elder and ancient; prey drawn 1.4x and marked.
- **M26** elements, the descent, prey. Six elements, one status each (burn,
  chill and freeze, corrode, shock and arc, drench, stagger), a body shrugs
  off its own, no type chart; every run enemy rolls one, drawn on its body
  and named on its tag. A run is three valleys, banked at each pass and
  harder per valley. Taller towers (40 m keep, 44 m spire) and two caches,
  built by gpt-6-astra; herds of grazers (Mossback, decimated and animated by
  gpt-6-astra) eaten by swoop, bite or fire for growth; the demo pilot hunts.
  `COMBAT.md` "Elements" and "The descent, the props, the prey".
- **M25.3** growth continuous rather than three steps, the hit body scaling
  with it, the adult fireball rate toned down (0.6x -> 0.8x cooldown), and
  first person scaled to the grown head.
- **M25.2** growth made visible: the body scales 0.8 / 1.0 / 1.25 with the
  stage and the wings 0.85 / 1.0 / 1.15 on top, eased in on "YOU GREW";
  `--stage N`. `ANIMATION.md` "Growth".
- **M25.1** the demo pilot's climb-out, after "stuck in cycles after the
  first hoard": take-off holds until 35 m at the attitude a flight-model
  probe measured (20 degrees; lower never leaves ground effect), a walk of
  up to 250 m for a near miss, a 25 s budget; on the rush the fall-back
  flies toward the pass. `COMBAT.md` "The climb-out".
- **M25** hands-off: the demo pilot (`COMBAT.md` "The demo pilot") plays
  the run and the arena through the player's controls -- P, `--autopilot`,
  `--demo` -- sieging guard towers, fighting, landing, walking in,
  collecting, falling back to heal, rushing the pass once the hunters are
  due, and rolling into the next valley. The run's watchtower and hoard
  pile are real props now (`assets/props/`, built by
  `tools/build_props.py` -- the first task delegated to gpt-6-astra in
  full-access mode, 62 k tokens, `tools/props.md`), drawn through the
  skinned path with one identity bone and casting shadows. Tower bolts 14 ->
  10 damage and their tight-aim range 200 -> 150 m, after the playtest's
  "towers overpowered" and the demo dying inside every guard's reach.
  Thirteenth suite.
- **M24.2** the run's second pass, after "towers and hunters overpowered,
  ignoring everything is best, seeds change nothing": guard towers at every
  cache, bounties and health on kills, towers back to one answer, hunters
  one at a time and later, four valley kinds from the seed that reshape the
  terrain and the mix, and growth (drake, young, adult) that moves the
  tuning. Also fixed: every run carried five stray drones, the seed never
  reached the terrain, and -- after this machine's OS update -- ImGui took
  its framebuffer scale from the window's pixel density instead of the
  render target, drawing the whole HUD and every panel at double size.
  `COMBAT.md` "Second pass".
- **M24.1** after the first run playtest ("cannot move when grounded, so
  very hard to land in the caches; towers are easy by landing and breathing;
  hard to tell rivals from hunters"): the dragon walks on the ground with a
  stride on the terrain IK (studio 13, `--walk`); towers resist breath, shrug
  off stun and shove, fire faster and tighter at a close or grounded dragon
  and splash where their bolts land; the HUD tags HUNTER in red, RIVAL and
  TOWER, and rivals no longer wear red. `COMBAT.md` "After the first
  playtest", `ANIMATION.md` "The walk". Renders in `artifacts/walk/` and
  `artifacts/run/`.
- **M24** the run probe (row 3): `--run [seed]` and a "run" section at the
  top of the Combat panel. `game::HoardRun` lays a seeded valley out along
  the corridor's spine -- three caches on flat ground to land on, four
  towers on the slopes firing a heavy arcing bolt, three rivals dormant at
  posts until the player comes within range, the pass gate at the far end,
  hunters loosed behind the player on a hidden clock -- and keeps score:
  death loses the hoard, the gate banks it, results beside the best
  previous run (`assets/runs.txt`). The arena is untouched: a run is a mode
  beside the match. Twelfth suite. `COMBAT.md` "The hoard run". Renders in
  `artifacts/run/`. The playtest gate is the genre question itself: is the
  corridor under pressure more fun than the free-form match?
- **Wing motion and landing, after a playtest report.** The wingbeat was
  flying 43-49% of the stroke the flight model commanded and the fold was
  overshooting its commanded angle by up to 49%, both from normalizers that
  balanced the wrong quantity; the fold hinged about body up on two assets
  whose wings are bound 35-45 degrees off level; the outermost finger bone was
  never driven on three of four assets; and the auto-flap assist fought the
  brake all the way down, so the dragon could not be landed deliberately.
  `docs/ANIMATION.md` has the detail. `assets/stormsail.glb` has its first rig
  profile and both generated assets now have a standing wing stow.
- **The generated models looked plastic, and it was their data maps.** Both
  shipped a constant 1.0 occlusion channel, a near-constant roughness (one
  gloss for horn, hide and membrane) and a nearly flat normal map.
  `tools/repair_model_materials.py` bakes AO from the mesh, rebuilds roughness
  from geometric zones and derives a detail normal; the repaired files take
  the plain names and the untouched originals are kept as `<name>-raw.glb`.
- **Three encounter creatures, and the generation pipeline generalised past
  dragons.** `ashcoil.glb` (a legless serpentine sky-wyrm, for the rival
  dragon), `cragjaw.glb` (a wingless armoured ground drake, for the ground
  defence) and `mossback.glb` (a wingless horned grazer, for the prey herd) --
  one per encounter kind in `DIRECTION.md` that had no readable target, and
  each a body plan the engine had never carried, so they separate by
  silhouette rather than by the bots' hue push. Concept board, reference
  sheets and turnarounds in `artifacts/dragon-options/`; three new skeletons
  in `tools/skeletons/`; all three through the Hunyuan one-shot at 80 K tris
  with repaired PBR. **Ashcoil loads and flies** -- `neck 3, tail 14, wing
  root 3/3, fingers 4/4, legs 0/0, front legs 0/0, feet 0, jaw jaw`, with the
  lateral body wave visible top-down, and no engine change was needed for a
  legless body. The two wingless ones rig correctly and are then rejected by
  the loader; see the first two Open questions.

  What this cost in the tooling, all of it lifted into data with defaults that
  preserve the existing builds: the wing bind-pose levelling is now
  `wing_bind_level_deg` and skips when there are no wing bones (it was an
  unconditional `rig.pose.bones['wing_root_l']`, a hard KeyError on anything
  wingless); the ground-offset wingspan is now `reference_span`; the
  `jaw_mask` lookup no longer assumes head and jaw groups exist; and the
  `binding` stat reports the passes that actually ran instead of always
  claiming the wing field and the Embercrest jaw mask. Re-running the
  Embercrest build after those changes reproduced `max_weight_sum_error`
  5.122274160385132e-08 and `ground_offset` 3.9933978544450914 exactly, which
  is the regression test for this file.

  Also: cutting a turnaround into upload plates is now
  `tools/split_turnaround.py`. Equal quarters do not work -- a serpent's
  profile is three times the width of its front view, and on Ashcoil one
  view's tail tip reaches *past* the next view's wingtip, so there is no blank
  column to cut on. It labels connected components, and repaints a
  neighbour that intrudes into a crop; the hand-cut Stormsail plates turn out
  to carry such fragments.

- **Multi-model: a match can field several species.** This was the binding
  constraint on visual variety, and it is gone. `--models A,B,C` loads a
  roster; the player flies the first and bots are dealt the rest in turn. The
  per-asset state that used to be a dozen loose members of `App` is now one
  `LoadedModel` -- skeleton, joint map, mesh, textures, clips, alignment
  transform, and the resolved `.rig.cfg` and `.flight.cfg` -- held by pointer
  so a bot can refer to one by index. Each bot therefore gets its species'
  skeleton, scale, pose profile and handling, not the player's.

  Flame colour moved onto the model too, which needed `BreathCone` to carry
  the tag of whoever breathed it -- combat never reads it, the renderer does.
  That makes the breath the place a species reads as elemental. The colours
  are pickers in the Dragon panel; nothing persists them yet, which is the
  next thing to do when there is an elemental variant worth saving.

  Two constants came out of `place_bot` while the verification needed them:
  `bot_spawn_range_` and its jitter, now a Combat slider and `--bot-range`.
  At 650 m a whole roster cannot be got into one frame, which is why this went
  unverified by screenshot for so long.

  Verified: a 7200-frame mixed-species match, clean; single-model runs log
  `model roster: 1 entry(s)` and are otherwise byte-for-byte the old path.

- **An elemental roster of six, on two skeletons.** Ember (Embercrest), stone
  (Ironroot), frost (Rimefang), blight (Blightmaw) on `winged-quadruped.json`;
  storm (Stormsail) and tide (Tidewrack) on `winged-biped.json`. The four new
  ones cost one generation each and **no skeleton authoring at all** -- the
  rigger's fit remaps an existing skeleton onto a differently-proportioned mesh
  of the same structure, so a variant is nearly free where a new anatomy is
  not. That only works because the concept prompt asked for the same neck
  length, tail length and finger-rib count; ask for a variant, not a redesign.
  All four rigged at 80 K tris with zero unweighted vertices and
  `max_weight_sum_error` 5.2e-08, 62 bones for the dragons and 55 for the
  wyvern.

  **A creature is now a species, not just a mesh**: `<model>.rig.cfg` says how
  it moves, `<model>.flight.cfg` how it handles, `<model>.breath.cfg` what it
  breathes. All three are partial files, so a model that ships without one
  behaves exactly as everything did before species existed.

  Two things the generator finally does right, both fixed in the *plates*
  rather than in code. **Mouths open**: every earlier sculpt came back fused
  shut with no cavity, which left heat weighting nothing to split on; asking
  for a parted mouth in every panel produced a real mouth interior on all four.
  And the view count is settled -- see `MODEL_GENERATION.md` -- four cardinal
  plates, no top.

- **The wingbeat is a beat, not a wave.** Every species flapped as a plank
  hinged at the shoulder, identical going up and coming down, because every
  shaping term was keyed to the wing's position. Four terms keyed to the
  beat's phase and velocity now give it direction -- a tilted stroke plane,
  a phase-keyed recovery flex with the hand drooping, a velocity-keyed
  feathering twist, and a body heave -- plus the spanwise lag on by default.
  Checked as an eight-phase contact sheet per species from the front and the
  side, and pinned by `test_wingbeat_is_not_a_wave`. `ANIMATION.md`,
  "A wingbeat is not a wave".
- **Roster polish after the first playtest of the beat.** The recovery flex
  moderated (it read as cloth). Tidewrack, Blightmaw and Stormsail close
  their sculpted gapes at rest and open on the breath. Rimefang and Ironroot
  had the jaw bone skinned to the snout, an asset fault; both were rebuilt
  with per-head jaw masks (`tools/skeletons/{rimefang,ironroot}.json`,
  `artifacts/*/jaw-mask/README.md`) and now close and gape like the others
  (see `ANIMATION.md`, "The jaw bone may own the wrong half of the mouth"). Rimefang, Blightmaw
  and Ironroot carry leg profiles instead of the code defaults that hung
  their legs like landing gear. Blightmaw's neck is streamlined so the head
  flies level and the horns sweep back.
- **Dive and ground wings per species; wyverns stand on their wrists.** The
  dive tuck and the standing stow were Embercrest's numbers on every
  species, which folded hands out sideways in the stoop and stood the
  membranes up as sails on the ground; each species now carries its own,
  found by rendering candidate grids. Stormsail and Tidewrack plant their
  wrists as forelegs with the hand folded up the forearm, which took two
  new stow fields (`ground_stow_tuck_share`, `ground_stow_elbow_scale`).
  The stow owns the standing wing outright now, after the bench and a real
  landing were found to disagree by 13 degrees of elevation; the legs trail
  along the tail base so the beat clears them; the stance dials live in the
  Studio panel under their scenario.
  `ANIMATION.md`, "Wings on the ground, and in the dive, per species".
- **The standing wing is aimed, and a rearing sculpt gets a stance.** The
  two grounded screenshots that started it: Embercrest's stow curled into a
  hoop, Rimefang standing on its hands. The angle stow's hinges turn about
  axes the sculpt chose, so the fold is now described by where each segment
  points in the body frame (`ground_wing_aim` and six directions, target
  `docs/concept/standing-dragon-reference.png`) and aimed bone by bone;
  Embercrest's directions fold Rimefang unchanged. Rimefang's bind pose is
  a leap, so `ground_stance` re-poses body pitch and legs by angle, and
  `ground_feet_level` lifts the root so the lowest foot stays on the bind
  floor and `ground_offset` stays true. 1,600 angle sets were screened
  numerically for level feet before three were rendered. Studio's Grounded
  section carries the dials and a per-foot height readout. Blightmaw, which
  looked beyond saving with its membrane hanging to the ground, was only
  rearing: the same recipe folds it over the back. Ironroot needed its own
  hip (its skeleton binds the thigh aft), a fold that shows the membrane
  without standing its wrist claws beside the head, and a jaw rest that
  does not swing the mandible through the skull. The wyverns rest on their
  wings: `ground_wing_plant` counts the wrists as feet, and since these
  upright sculpts cannot reach the ground from a level body, they crouch
  with the wrists planted wide and ahead of the feet like the landed
  wyvern of modern games (`docs/concept/landed-wyvern-reference.png`).
  Cycling onto another model no longer carries the previous model's idle
  clip into the rig, which had been reverting the wyverns' stance and
  turning every jaw about a stranger's hinge on the ground.
- **Second look: jaws, soles, legs in flight.** Rimefang's jaw rest was
  over-closing; Tidewrack's snout warped on close because its jaw bone owned
  the snout tip; rebuilt with a measured per-head mask
  (`tools/skeletons/tidewrack.json`), snout displacement to zero. The soles
  that sank on the flat bench were measured by CPU-skinning the mesh in the
  stance, 0.14-0.35 m on the quadrupeds and two metres of TAIL on the
  wyverns; each profile now carries the measured `ground_lift_m` and the
  wyverns drop the tail less. The hind feet flew two metres above the hips
  because tuck and trail both rotate the thigh; a glide probe screened a
  grid and the quadrupeds now stream their legs back under the tail.
  CLAUDE.md gained a mandatory rendered-inspection list for pose changes.
- **Limbs plant on the terrain.** The app hands the rig its model transform
  and a surface query; on the ground the rig lifts and tilts the body onto
  the mean contact and closes each standing limb (hip-knee-foot,
  shoulder-elbow-hand, planted wing wrists) with a two-bone solve. Tested on
  a 0.3 slope: 0.50 m worst foot error to 0.00. `ANIMATION.md`, "Each limb
  plants on the terrain under it".
- **Flight legs, second pass (external, gpt-6).** Smaller per-species leg
  folds and a 10/8 foot hang and toe curl on all six generated profiles so
  the paws hang below the hips and off the chest; a 34-degree wingbeat cap
  with a 14-degree upstroke fold on Blightmaw, Rimefang, Ironroot and
  Tidewrack; a wider 40/10 dive tuck on the wyverns so the long fingers no
  longer cross over the spine. `artifacts/flight-pose-fix/README.md`;
  `tools/capture_flight_pose.py` regenerates the 19-view inspection set.
- **Frostvein, a seventh species (external, gpt-6, 2026-09-14).** A new ice
  dragon generated from an original turnaround and rigged on its own
  measured 74-bone skeleton with four finger chains, tip joints and a
  measured jaw mask -- the first species built the way `MODEL_GENERATION.md`
  now recommends. Reviewed here against the CLAUDE.md inspection list:
  glide, flap, dive, head in glide and on the ground, soles at ground level,
  an in-game landing and a switch from `dragon.glb`; probes read zero sink,
  feet 1.9 m below the hips in the glide, jaw opening downward.
  `artifacts/frostvein/README.md`; `tools/capture_frostvein.py`.
- **Asset names say which file ships (2026-09-16).** Every species now
  follows one rule: `assets/<name>.glb` is the shipped, material-repaired
  file; `<name>-raw.glb` the rigged file before the repair;
  `<name>-cand-*.glb` the generator's output. Embercrest's hero, formerly
  `embercrest-textured.glb`, is `embercrest.glb`, and the 2026-09-08
  script-built experiment that held that name is `embercrest-scripted.glb`
  (`docs/EMBERCREST.md`, `artifacts/embercrest-scripted/`). Frostvein's
  shipped file was the unrepaired rig with the repaired build parked beside
  it as `-final`; the repaired build is now `frostvein.glb` and the session's
  intermediates are under `assets/frostvein/intermediates/`.
- **Legs that hang can swing (2026-09-16).** Frostvein's compact leg fold was
  tidy and rigid; it now hangs its legs (thigh 15 degrees aft, a 46-degree
  knee) and every profile's leg pendulum swings wider, springs slower and
  floats further on the brake, so the reversals and the brake visibly move
  the legs. `tools/rig_probes/leg_swing_probe` measures the travel; eight-
  frame sequences on all seven species are `artifacts/legs_swing_*.png`.
  `ANIMATION.md`, "Legs that hang can swing; legs that are tucked cannot". `ANIMATION.md`, "A folded wing is
  where its bones point".
- **Next:** see `docs/DIRECTION.md` -- the shape of the game (a hoard-run
  roguelite with growth as tuning), the art direction and its generated
  targets in `docs/concept/`, the HUD kit, the asset policy, and a sequenced
  plan. Revised 2026-09-13: the roster (seven species) is done for now, and
  **melee** (bite and claw when the rival is close but off the nose, the
  playtest's one loud complaint) is scheduled before the run probe. `docs/RETRO.md` keeps the feature-by-feature ranking and the D3D9-era
  port study; the port's first phase (extracting an RHI from `src/gfx`) is a
  pure refactor that pays for itself even if the port never ships, and it
  follows the polish work in DIRECTION.md.

## Open questions

- **Ground beyond the fog.** The terrain builds a coarse skirt out to three
  times the playable extent (see `WORLD.md`), and headless captures from
  inside the valley show ground continuing to the horizon. A playtest report
  of "the rolling ground behind the fog doesn't appear" was not reproduced;
  the suspicion is the height fog itself saturating before the skirt is
  reached, rather than missing geometry. Needs the altitude and heading it
  was seen from.
- **A rig without wings is rejected outright.** `DragonJoints::valid()`
  (`src/anim/dragon_rig.h:58`) returns true only when `wing_root` is non-empty
  on *both* sides, so `app.cpp:146` logs "imported skeleton has no
  recognisable wings; falling back" and substitutes the 218-vertex generated
  dragon. Both wingless creatures hit this: `cragjaw.glb` and `mossback.glb`
  map their chains correctly first -- `wing root 0/0, fingers 0/0, legs 3/3,
  front legs 3/3, feet 4, jaw jaw` -- and are then discarded. The gate is
  doing a real job (a genuinely unmapped skeleton should fall back), so the
  fix is to widen the test rather than delete it: wings on both sides *or*
  legs on both sides. Whatever else assumes wings exist has to be checked at
  the same time, starting with `find_wingtips`.
- **Model scale assumes the X extent is a wingspan.** `app.cpp:166` is
  `scale = 19.0f / extent.x`, which held for every asset so far because they
  were all winged and X *was* the wingspan. On a wingless body X is merely
  body width, so Cragjaw would enter the world 50 m long and Mossback 40 m
  long and 30 m tall -- 4x and 9x oversize. The skeletons already record the
  intent (`"reference_span": {"axis": "y", "metres": 12}` and `4.5`), and the
  rigger writes it into the `.flight.cfg` comment, but nothing reads it:
  `asset_.scale` is computed at load, before the cfg files are read. Wiring it
  up is a small change with an ordering wrinkle, and it is a prerequisite for
  either wingless creature appearing at a believable size.
- **Nothing collides with a tree.** Vegetation is visual only.
- **The default asset's grounded wings are a wide flat drape.** Its wing root
  chain is two bones that bind at the model origin, so the shoulder sweep
  pivots about the origin, and its authored clip is a flying idle with the
  wings spread -- there is no folded stance to hand the ground to. Both the
  procedural stow and the clip were tried and neither reads as a folded wing.
  The wyvern and Embercrest, which have real wrist joints, stow correctly.
- **`FlightTuning::safe_landing_speed` is not read by anything.** "Landing
  softer than this keeps you intact; harder is a crash" describes a crash that
  does not exist; a dragon can arrive at 26 m/s and simply stop. Either wire it
  up or drop the field.
- **A full-brake descent stalls rather than flares.** Holding the brake all the
  way down settles at about 9 m/s airspeed and -5.7 m/s sink with the stall
  flag set. Half brake gives a clean, controlled -4 m/s approach, so this is
  flare drag tuning rather than a broken state machine.
