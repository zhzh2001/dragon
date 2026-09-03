# Dragon Engine

Custom C++20 engine for a single-player dragon game, rendering with SDL3's GPU
API (Metal backend on macOS). Long-term hobby project; see
`docs/ROADMAP.md` for where it is going and why.

## Build & run

```sh
cmake -S . -B build -G Ninja      # first time, or after adding files
cmake --build build
./build/dragon
```

Useful flags:

| Flag | Effect |
|---|---|
| `--headless` | No visible window; still renders offscreen. For automated checks. |
| `--frames N` | Run exactly N frames then exit. |
| `--screenshot PATH` | Save the last frame as a BMP (requires `--frames`). |
| `--cam x,y,z,tx,ty,tz` | Place the camera at a position looking at a target. |
| `--hide-ui` | Hide the ImGui panels, for world-only captures. |
| `--inspect [angle] [dist] [elev]` | Orbit camera locked to the dragon (elev 88 looks straight down -- the only view that shows a lateral tail wave); add `--inspect-head` to orbit the animated head instead (jaw, aim). |
| `--skeleton` | Draw the posed joints as lines, to tell a rig problem from a skinning one. |

The Terrain panel's **Vegetation** node has the tree and grass controls.
| `--model PATH` | Load a different rigged glTF in place of `assets/dragon.glb` (e.g. `assets/alt/prowler.glb`, see ATTRIBUTION.md). |
| `--hue r,g,b,strength` | Recolour the player's hide (the same recolour the bots use). |

`--combat` arms the dragon and spawns a wave; `--attack` also holds breath and
fires, which is how the flame and the projectiles get onto a screenshot.
`--bots N` spawns N bot dragons instead of sentinels.
`--studio N` opens the animation studio playing scenario N (0 glide, 1 flap,
2/3 turns, 4 s-turns, 5 dive, 6 pull-out, 7 brake, 8 attack, 9 grounded).

Verify a visual change without a human at the keyboard:

```sh
./build/dragon --headless --frames 40 --screenshot /tmp/shot.bmp
sips -s format png /tmp/shot.bmp --out /tmp/shot.png   # macOS: BMP -> viewable PNG
```

Tests: `ctest --test-dir build` (math, camera, flight).

Verification without a human at the keyboard: `--headless --frames N
--screenshot`, plus `--cam` to place the view, `--cam-mode`, `--hide-ui`,
`--input pitch,roll,yaw,flap,tuck,brake` to hold a control input, and
`--autopilot` to fly the selected course unattended. That combination is how the
wingbeat, the inverted recovery, and every generated course were checked.

## Controls

| Input | Action |
|---|---|
| W/S | Pitch (W = nose up) |
| A/D | Roll |
| Q/E | Rudder -- gamepad d-pad left/right |
| Gamepad left stick | Pitch and roll (absolute, best feel of the three) |
| Gamepad A / triggers | Flap / tuck-dive (RT) and brake (LT) |
| T / right-stick click | Relock onto the next target |
| Mouse | Optional, off by default -- it has to accumulate to work, and that accumulation is what makes it hard to control |
| Space | Flap -- the only way energy enters the system |
| Shift | Tuck wings and dive |
| Ctrl | Flare and brake |
| R | Restart the run |
| Right-drag / right stick | Free look -- orbit the view without steering. Stick Y is inverted by default |
| V | First person, from behind the dragon's head |
| 1 / 2 / 3 | Camera preset: chase, action, cinematic |
| Tab | Toggle free-fly survey camera (detaches where the chase camera is) |
| F1 | Hide every ImGui panel (the HUD stays) |
| F / left mouse | Fire breath (hold) -- gamepad LB |
| G | Fireball -- gamepad RB |
| X | Boost -- gamepad X |
| Esc | Release the mouse if captured; again to quit |

ImGui keyboard and gamepad navigation are deliberately disabled: with them on,
ImGui claims those devices whenever a panel has focus and silently eats the
flight controls.

## Conventions

- **Coordinates**: right-handed, Y-up. Forward is **-Z**, right is **+X**
  (`Vec3::forward()`, `Vec3::up()`, `Vec3::right()`). Right = forward x up.
- **Matrices**: column-major storage, so they memcpy straight into Metal
  `float4x4` uniforms. `A * B` applies B first.
- **Depth**: reversed-Z. Near maps to 1, far to 0; depth clears to **0** and the
  compare op is **GREATER**. Use `core::perspective_reverse_z`. This buys the
  depth precision a flight game needs at multi-kilometre view distances.
- **Rendering**: the frame renders into an offscreen `scene_color` target, then
  blits to the swapchain. Pipelines must declare
  `device.scene_color_format()` as their color target, not the swapchain format.
- **UI**: Dear ImGui draws in its own pass (`begin_ui_pass`) because its
  pipelines declare no depth attachment, and a pipeline can only be bound in a
  pass whose attachments match it.
- **Shaders**: MSL source in `shaders/`, read from the source tree at runtime and
  hot-reloaded on save (polled every 0.25s). A failed compile logs an error and
  keeps the last working pipeline, so a bad save never blanks the screen.
  Metal's runtime compiler does not resolve local `#include`, so
  `gfx::PipelineCache` inlines them itself and watches every included file.
  Include `scene_common.msl` first -- it pulls in `<metal_stdlib>` and opens the
  `metal` namespace.
- **Shadows**: one directional map following the camera, using a conventional
  [0,1] depth range with a LESS compare -- deliberately unlike the reversed-Z
  main pass, since an independent pass is easier to debug with standard depth.
- **Terrain is generated analytically and queried from the mesh**: the noise
  builds the grid, and once the grid exists `Terrain::height_at` interpolates
  the exact rendered triangle under the point (`analytic_height_at` is the
  generator). The two differed by up to 3.5 m on ridge crests, which is an
  invisible bump the dragon could touch and the player could not see. The
  triangle lookup is also cheaper than ten octaves of noise, which is what
  lets grass be re-placed around the camera every frame. Physics (Jolt) is
  deferred until something actually needs swept or convex collision --
  dragon-vs-dragon, projectiles, ragdolls.
- **Tuning**: every gameplay constant belongs behind an ImGui slider. Feel is
  found by dragging sliders while playing, not by planning.

## Third-party policy

Write what we want to learn; vendor the rest. In: renderer, scene, animation
blending and procedural animation, flight model, camera, AI, gameplay. Out
(never hand-rolled): SDL3 (window/input/GPU), Dear ImGui, Jolt (physics), cgltf
(glTF), stb_image, miniaudio.

## Layout

```
src/core/    math, logging
src/gfx/     GPU device, pipeline cache + hot reload, buffer upload
src/editor/  ImGui integration
src/scene/   (empty) entity storage, transform hierarchy
src/anim/    skeleton, skinned mesh, procedural dragon rig
src/phys/    (empty) Jolt integration
src/game/    (empty) flight model, camera, AI, gameplay
shaders/     MSL, hot-reloaded
tests/       plain executables, no framework
```

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
- **Next:** see `docs/RETRO.md` -- the ranked gameplay/visual list (loadouts,
  water, thermals, team matches) and the D3D9-era port study. The port's first
  phase (extracting an RHI from `src/gfx`) is a pure refactor that pays for
  itself even if the port never ships.

## Combat

`game::Combat` owns player resources, projectiles and the targets, and reads a
`CombatInput` -- so a bot will drive it through exactly the same struct the
player fills. It renders nothing and reads input from nothing.

### Targeting

Aiming a small fast target in three dimensions is close to impossible unaided:
a degree of nose error is tens of metres at engagement range, and the target is
manoeuvring too. So the dragon **picks a target and the shot bends toward it**.

- The lock is **sticky**: acquired only inside a narrow cone off the nose, held
  until it falls well outside a much wider one, so a turn does not drop it.
- Scoring is **angle plus a distance penalty** (`lock_distance_weight`):
  alignment alone locked a 1500 m speck dead ahead over a close target ten
  degrees off the nose, which is never the one the player meant. T or a
  right-stick click **relocks** onto the next candidate by score, wrapping.
- `aim_assist` is the fraction of the way from the nose to the intercept, and it
  is **0.9 by default**. What the player experiences is the *residual*: at 0.7 a
  shot 14 degrees off the nose at 500 m still misses by 37 m, which reads as the
  assist doing nothing. Turn it down for a harder aiming game, not to be fairer.
- The aim solution leads the target **and compensates for the drop**. At 700 m
  the flight time is 2.7 s and the fireball falls 15 m -- more than the target is
  tall, so without this every long shot passes underneath for a reason the player
  cannot see.
- The **breath cone follows the same assisted axis**, so the flame drawn is the
  flame that damages -- no hidden widening. Its assist is additionally capped by
  angle (`breath_assist_max_deg`), because a fireball bending 30 degrees is
  invisible while a flame doing it looks like a garden hose.
- The rig **turns the head toward the lock** (`DragonRig::set_aim_target`). This
  is readability, not flourish: fire leaves along the aim axis, and a head
  pointing elsewhere makes the shot look like it came from nowhere.

Being hit has to be locatable. `CombatEvents::damage_from` reports where the
round came from, the HUD holds an arc at the screen edge pointing at it for three
seconds, and **incoming fire is drawn about three times its true size** -- a
2.5 m hitbox at 400 m is a couple of pixels, and being hit by something invisible
is the least readable thing in the game. Player fire needs no such help.

Three decisions that are the milestone:

- **Hitboxes are generous and swept.** A fireball covers 3.5 m per frame at
  210 m/s; a point test tunnels straight through a target it visibly struck, so
  every hit is a swept-sphere test against the segment the projectile actually
  travelled. A near miss still lands reduced damage out to the blast radius. A
  3D dogfight is hard enough to read without demanding pixel accuracy, and a
  shot that clearly hit but did not is the worst thing an air combat game can do.
- **Boost is a flight force, not a combat one.** `Combat` owns the cooldown and
  reports `boost_active()`; `FlightInput::boost` applies it. Everything that
  pushes the dragon forward stays in the flight model.
- **The breath meter latches and does not refill while held.** Without both, an
  empty meter under a held button crosses the restart threshold every few frames
  and produces a stutter of single-frame damage.

**Tuck commits the nose** (`Assists::tuck_nose_over`): folding the wings only
sheds lift, and at level attitude that is a slow flat mush -- the dive button
dived slower than pushing the stick. Holding tuck now pitches down unless the
stick overrides; measured, RT alone reaches 78 m/s in 8 s where stick-down
alone reaches 57. A **hit flashes hot orange**, because the other bright thing
a sentinel does -- firing -- puts a blue-white bolt on top of it, and two white
flashes are indistinguishable at range.

**One gamepad button, one meaning.** The shoulders once carried rudder and
combat simultaneously: firing a fireball also yawed the dragon, and holding
breath dragged it into a slip that bled energy -- which the player read as
"auto-flap is broken", not as a binding conflict. Rudder lives on the d-pad
now. And auto-flap also protects against unintended sink (descending fast with
neither tuck nor brake held): a fight at healthy airspeed glides steadily
downhill, and a pilot busy aiming does not notice until the ground does.

Projectiles are drawn as **one opaque bolt** stretched along velocity, its
readability exaggeration tapered off near the camera. Both lessons were paid
for: a nested "glow" shell just occludes its own core in a forward opaque
pipeline, and a shot passing the chase camera at 3x exaggeration is a
screen-filling balloon that reads as a volley of different-sized projectiles.

## Vegetation

`game::Vegetation` places, `gfx::Foliage` draws. Four tree kinds (spruce,
mountain pine, broadleaf, dead snag) and three grass kinds (tuft, waterside
reed, bush), each a generated mesh of rings, cones, lumpy blobs and blade
triangles, each instanced from its own buffer with one `FoliageInstance` per
plant -- position, scale, yaw, shade, sway phase. Trees are planted once per
terrain on a jittered grid, thinned by TWO scales of cover noise (stands at
400 m, clumps at 55 m -- one scale is an even sprinkle) and kept off water,
steep ground and everything above a treeline (`treeline_above_floor`: a
forested peak is a hill). **The kind mix shifts with altitude but is a mix
everywhere** -- half broadleaf on the floor, spruce through the middle, pines
at the top, dead snags in the last stretch and on rough ground -- because a
band of one kind reads as a plantation, which is exactly what the first
all-broadleaf floor looked like. Sizes spread 0.7-1.7x for the same reason.
**Plants sink into slopes** (`slope_sink`, metres per unit of 1 - normal.y)
so a skirt's downhill side meets the ground instead of hanging in the air.
Trees cast shadows through their own depth pipeline and sway in the vertex
shader with the square of their height fraction so roots stay put. **Grass is
re-placed every frame** around the active camera from a stateless hash of the
ground cell, so a tuft is always in the same place when you come back to it,
patchy by a fine noise, and streamed like the particles; no texture, no alpha,
shrinking into the ground over the last third of the radius rather than
blinking out. Both pipelines read the mesh vertex layout plus a second,
instance-rate buffer -- and `instance_step_rate` must be 0: SDL reserves it,
and a 1 fails pipeline creation with an empty message. Plants are visual only;
nothing collides with a tree.

**The world does not end at the playable extent.** The analytic height used
to carry on past the last visible triangle: an invisible mountain range you
could fly into and land on, out toward the sky. The terrain now builds a
coarse **skirt** (36 m cells to three times the half extent) from the same
function, drawn with the terrain shader and queried by the same triangle
lookup, so there is ground under the sky all the way to the fog. It gets no
plants.

The surface query also exposed a frame-time weakness: under the rally test's
4x frame jitter the bank-limited autopilot orbited Canyon Weave's rings that
it flies cleanly at 60 Hz. The real fix was in the flight model: **frames
longer than `max_step` (20 ms) are split into equal substeps** inside
`FlightModel::update`, since the app lets a hitch reach 100 ms and the
explicit integration of a banked, roll-damped turn drifts at that step.
Every dragon benefits, bots included. The autopilot also gained a go-around
(fly back out to an entry point once past a ring's plane), which is a
smaller matter -- an eager version of it, triggering on oblique approaches,
made things worse before the substeps were found.

## Particles and audio (M7, first half)

`gfx::ParticleSystem`: CPU-simulated, GPU-billboarded quads drawn **additively
with depth test but no depth write** -- particles are light, they sum and never
occlude, which is also why one unsorted draw call is correct. The pool is
fixed (4096, swap-remove); when full, a sampled oldest particle is replaced,
because the newest particles are the bright just-happened ones whose absence
would be noticed. Staging rides alongside the debug-line upload: **a copy pass
cannot open inside a render pass**. Emitters live in the app: flame (buoyant
puffs launched down the cone, spawn rate integrated so frame rate cannot thin
the fire), projectile ember trails, and impact bursts with upward splash on
terrain. The damage cones and hitboxes are untouched -- particles are what the
fight looks like, never what it is.

**Boost is air, not fire**: pale slipstream threads off the wingtips, faint
streaks rushing past the body, and a field-of-view surge that eases back
through the camera's own fov lag -- the world moving, not the dragon burning
(flame-coloured boost read as being on fire). The surge rides on top of
whatever camera preset is active. **Bots cry too** -- the same screech and
dying cry as the player, pitched deeper with per-bot variation so a flight
never chorusing, faded by distance to the camera. A bot's hit flash **reddens
rather than brightens** (high emissive whitens through the tonemap, and a
white flash was unreadable as damage), and a target held in the breath sheds
embers continuously, because breath has no projectile impact to detonate.

**Flame reach is solved, not tuned**: launch speed is computed against drag so
a puff's travel distance equals the damage range (v = d*k/(1-e^-kT)), because
the flame's visible length is how the player judges reach. Being inside a
hostile flame swarms embers over the player's own dragon -- the vignette says
"damage", the fire crawling on you says "burning". The damage screech is
rate-limited to one per 0.45 s: a flame deals damage every frame, and forty
overlapping cries per second was the playtest's "strange loud flame".

`audio::Audio` synthesizes every sound at init -- **no audio assets**, in the
same spirit as the procedural terrain. Continuous streams (wind through a
lowpass whose cutoff opens with airspeed, so a dive gets brighter rather than
merely louder; flame noise with a slow crackle) are set by level each frame
and smoothed at audio rate; one-shots (brown-noise explosion with a sub
thump, filter-swept shot whoosh, a wounded-animal screech for damage and a
longer dying one for the knock-out, a rising boost rush, wingbeat) fire from a
lock-free voice pool. Output is stereo with independent noise per channel --
identical channels collapse to mono in the head -- plus a gust LFO on the wind
and sparse crackle pops on the flame, which is what separates fire from
filtered static. Explosion loudness follows distance to the CAMERA -- the ear sits
where the player does. A tanh soft-clip keeps a busy fight loud but never
harsh. Master volume in the Engine panel; headless runs skip the device.

Every ImGui window except Combat starts **collapsed** -- one click away, not
hidden, but the screen belongs to the game.

**Handling per model.** The two dragons fly identical numbers, and the eye
insists the small quick-looking wyvern is lighter. The Flight panel has a
**heft** knob -- one ratio that scales mass up and roll/pitch/yaw rates and
control lag down by its square root, on top of the individual sliders -- and
"save for this model" writes the tuning to `<model>.flight.cfg` beside the
glTF, loaded automatically on top of `assets/flight_tuning.cfg` when that
model is used.

Bots cycle through four hides -- rust, bone, moss, violet -- so a flight is not
four copies of one dragon, and their fire leaves the animated head like the
player's ("bot recolour" slider in the Combat panel, right under the bot
skill buttons). **A multiplicative tint cannot recolour a dark texture**: the first
four bot tints were four indistinguishable greys. `ModelUniforms::recolour`
pushes the albedo toward a hue at its own luminance instead, so scales and
shading survive and "the green one" is a thing a player can say. The player
gets the same control (Dragon panel, `--hue`).

**Boost threads leave both animated wingtips**, one per side per step. The
first version drew the side from a random bit it never advanced, so whole
frames of threads landed on one wing. The outermost wing joint per side is the
one furthest from the centreline in bind -- not the last joint of the last
finger, which on this asset is a helper bound at the origin. Ignition blows a
ring of air outward so the start of a boost is an event.

## The match loop (M15)

**Weapons-cold gating must precede everything that consumes the decision**: it
once sat between the bot's fireball and its flame, gating one and not the
other, and bots shot through the countdown. And a bot grounded for three
seconds is written off as a crash -- wedged on a slope the flight model cannot
take off from, the recovery reflex has had its fair window.

`game::Match` is pure scorekeeping and phase logic -- it consumes CombatEvents
and emits nothing but state, so the whole loop is testable without the app.
Deathmatch: the player scores kills, hostiles score by killing the player,
first to the target wins; on time expiry the leader wins and a tie is honestly
a draw. **Weapons are cold** in the countdown and on the results screen --
enforced in three places (player input, bot decisions, and the scorer itself
refusing kills outside the fight), because a kill during a countdown is a bug
wherever it comes from. The rally HUD stands down while a match runs. Enter
rematches; `--match` (with `--bots N`) starts one from the CLI.

Bots regenerate like the player does (`hostile_regen`, after a lull), scaled
by skill tier along with their health: a rookie never heals and loses wars of
attrition; an ace refuses to stay wounded. Disengage-and-recover cuts both
ways, which is the balance the player asked for.

## Bots (M14)

`game::BotPilot` is a pilot, not a puppeteer: it reads the world and emits the
same `FlightInput`/fire decisions a player produces, flown by its own
`FlightModel`. Difficulty is **honest imperfection** -- the player is *sampled*
every `reaction_interval` and extrapolated in between, so a break inside the
reaction window genuinely defeats its aim; spread is error in the firing
solution, not damage dice; and the nose must actually point at the solution,
because bots aim by flying.

**Terrain contact scales with violence.** A plummet past 25 m/s of sink is
death; a scrape costs health and triggers the jink; a gentle touch is a touch
-- instantly deleting a dragon that grazed a slope read as a bug, because it
was one. The recovery reflex fires on the **physics of the pull-out**
(sink^2/2a plus margin), not a fixed height or time: 50 m of clearance is
plenty in level flight and nothing in a 70 m/s dive. Recovery flares (brake
adds drag AND lift, tightening the pull) and cancels tuck. Result: zero crash
deaths across repeated 4-bot 4-minute soaks, down from ~6.

**Bots breathe fire** on a latched burst budget, in ANY state when close and
aligned -- gating breath on the attack state left a 20 m window between
min_attack_range and breath_range that nobody ever saw a flame in. The flame
check uses the live player position (a flame visibly connects or does not;
pretending not to see reads as blindness, not fairness -- fairness lives in
the aim solution), and their heads track the player inside 350 m, which is the
tell that a flame is coming. Hostile flames damage through
`Combat::hostile_breath`, buffered and resolved in update() so attribution
goes through the one path that owns it.

**Combat reset clears the bots.** Combat::reset rebuilds the sentinel slots;
bots that survived it were left pointing at freshly spawned drones, puppeting
spheres around the sky while their dragons rendered on top. The panel toggle
now clears the bots, and update_bots refuses to drive a slot that is not
flagged external -- the belt to the button's braces.

A **manual aim** checkbox zeroes the assist (and restores the exact slider
value after), the aim cross is big enough to see, and a bot holding its flame
turns its HUD bracket red with a FLAME tag -- the head tracking is the diegetic
tell, but a tell nobody notices is not a tell.

**Fire leaves the mouth.** The app feeds the rig's animated head position to
`Combat::set_muzzle` each frame, so the player's flame and fireballs start
where the head actually is, and a small HUD cross marks where the mouth's shot
will go -- the head is unreadable from behind, and fire from an invisible
origin toward an unmarked point felt random.

Prediction is **quadratic**: position, velocity, and the acceleration measured
between the last two samples (nothing on the first -- measuring against zero
history invents a lunge). A STEADY turn or brake is the most predictable
manoeuvre there is, and a pilot who cannot lead one is not a pilot; what still
defeats the bot is CHANGING the manoeuvre inside its reaction window. Firing
solutions -- the bots' and the player's aim assist alike -- are solved for the
**inherited-velocity drift**: a round leaves at shooter velocity plus muzzle
velocity, and ignoring the drift lands every crossing shot one drift-length
behind the target. That bug hid in both solvers, found by flying recorded shots
to closest approach in a test.

The state machine is the fight's rhythm: **attack** (fly at the intercept,
fire in the cone), **extend** (out past the merge, turn, come back with
energy -- passes, not orbiting), **evade** (a jink on taking a hit). Three
doctrine rules earned by failing tests: the attack clock only runs inside gun
range, because timing out of a stern chase oscillates forever (nine seconds
closing, seven extending, no progress); terrain must be sampled **ahead along
the velocity**, not just below, or bots fly into rising slopes; and the aim
point is floor-clamped over the terrain, because following a player into the
weeds is how bots die of enthusiasm.

Each bot occupies an **external hostile slot in Combat** (`spawn_external` /
`drive_external` / `fire_hostile`), so health, lock-on, projectile sweeps, hit
flash, HUD brackets, kills and respawn timing all come free; the app owns the
body -- flight, rig, rendering (drawn as the real dragon, warmed slightly red)
-- and repositions it when the slot comes back alive.

Sentinels are **not AI**: they fly a fixed orbit and fire on a timer with
deliberate aim spread. They exist so health, aim and the cooldown rhythm can be
tuned against something that shoots back. M14's bots replace them, driving the
same flight model the player uses.

A bug worth remembering: `spawn_wave` originally set a sentinel's orbit but never
its `position`, so a freshly spawned or respawned one sat at the **world origin**
-- a live, shootable target in the middle of the map -- until its first update
moved it. The respawn path returns early, so nothing else would have placed it.
Initialise derived state at spawn, not on the first tick.

**Put the difficulty dials where they can be found.** `aim_assist` and
`sentinel_spread` decide whether combat is fun, and they spent a session inside a
collapsed ImGui header, which is the same as not existing. They are now at the
top of the panel with forgiving/standard/sharp presets beside them.

Fire is drawn **unlit** (`ModelUniforms::material.w`). The shared tonemap ends in
a gamma encode, so anything bright desaturates toward white; adding two units of
sunlight on top of a flame turns it into a white balloon. A light source should
not also be lit.

## Importing a rigged model

`anim::load_skinned_gltf` reads any rigged glTF; `anim::map_dragon_joints`
identifies the neck, tail, wing and leg chains in an arbitrary skeleton so the
same procedural animation drives it. Drop a file at `assets/dragon.glb` and it is
used automatically, falling back to the generated rig if anything is wrong.

Three things learned the hard way, all now handled:

- **Use the file's inverse bind matrices, not recomputed ones.** An exporter may
  write mesh vertices in a space that does not coincide with the joint hierarchy,
  and the file's matrices are what reconcile the two.
- **A joint whose parent lies outside the skin must absorb the whole scene
  transform above it.** Exporters routinely leave scale and orientation on nodes
  above the skeleton.
- **Sides cannot be read from bone names or from a bone's own position.** This
  asset labels its +X wing "_L", and both wing roots sit on the centreline -- the
  side only shows in the subtree, so that is what gets measured.

The rig applies rotations about **body-space axes, composed with each joint's bind
rotation**. Replacing the bind rotation destroys the rest pose, and a real rig's
bones each point along their own axis, so "rotate about local Z" means something
different for every bone.

### A second dragon: what the joint mapper had to learn

`assets/alt/prowler.glb` (a CC-BY wyvern, see ATTRIBUTION.md) is the second
rig the procedural animation drives, and it taught `map_dragon_joints` to
**skip rig plumbing by name** (ik, pole, cont, target, chain, roll, fly,
muscle, kneecap): a wing candidate nearest the root was its IK target, a thigh
candidate its corrective chain. Chains now **descend the main line** --
a branch counts only where the children are peers of the largest subtree, so
an elbow corrective hanging off the upper arm does not end the arm while six
fingers off a hand do -- and the **longest chain wins** among several leg
candidates. Nothing inside a wing is a foot whatever it is called (this rig's
wing hands are "Hand"), a leg chain **stops before the foot**, and a foot that
already hangs off its leg is **not re-anchored** -- only the body-parented
IK-target kind is. The first asset's mapping is unchanged by all of this,
which is the test: `mapped rig:` in the log must read the same for it.

The frame fix above is what made a second asset possible at all: this one also
faces +Z, and the rig detects that itself.

Three more lessons from flying it. **The ground idle is chosen by name**
(idle/stand/rest/breath/hover), and an asset with only a landing gets that
clip's LAST frame held -- a standing pose beats a landing replayed on loop
(`DragonRig::set_base_clip(clip, hold_at)`). **On the ground the authored
stance wins the whole body** (`ground_contact_`, contact not proximity): a
wyvern folds its wings into forelegs and a tail sim cannot know that; the
artist did. A quadruped keeps its wings with the rig even there -- it stands
on its legs, and this asset's authored fold drapes the membranes. And **a
straight-rested, seven-segment tail read as a robot arm** for two reasons the
first asset's short curved tail had hidden: one stiffness per point turns a
uniform load into a rigid rod pivoting at the root (`tail_tip_stiffness`
tapers the spring toward the tip so the chain curves), and a per-vertebra
bend cap generous enough for four segments let seven take the whole bend in
two -- a hinge at the base and a straight boom beyond -- so the cap is
normalised to a four-segment chain. The tell was invisible from the side and
from behind; only the top view (`--inspect 0 24 88`) showed it, after two
rounds of probing a chain that was, numerically, bending exactly as designed.
**Look from the axis the motion is in.**

### Getting a usable asset: download glTF, never route it through Blender

Drop a rigged `.glb` at `assets/dragon.glb` and it is used automatically, falling
back to the generated rig if anything is wrong. See ATTRIBUTION.md for the model
currently in use and how to obtain it.

**Download the glTF variant from the model's Sketchfab page. Do not use the
original FBX.** Blender's FBX importer mangles this rig, which was established by
rendering the model in Blender's own viewport across eight frames of its
animation -- broken at every frame, before any processing, while Sketchfab's
viewer shows it correctly. The signature is around sixty auto-generated `*_end_*`
leaf bones plus vertices flung far from the body. The glTF download needs no
Blender step at all: no pose bake, no bone reduction, no export options to get
wrong.

`artifacts/dragon_broken_fbx_import.glb` is kept as the counter-example.

### How the loader reads a skin

Joint bind transforms are derived from the file's **inverse bind matrices**, not
from the node hierarchy's TRS. An inverse bind matrix is by definition the
inverse of that joint's bind world transform, so inverting it recovers that
transform exactly, and building the skeleton from those makes "skinning at the
bind pose is the identity" true by construction for any exporter.

This matters because reading node TRS means reconstructing the same information
through a chain of conventions -- which node absorbs the scene transform, whether
the mesh node's own transform is divided out -- and two different assets
disagreed about those conventions. One skinned perfectly while the other was
deformed by 12% of its size, and a "fix" derived from the glTF spec's skinning
formula broke the one that had been working. Nothing can disagree about
`inverse(inverseBind)`.

The loader rejects an asset whose bind pose does not reconcile, reporting how far
skinning moves the average vertex at rest. That check cannot false-positive on a
legitimately-shaped creature, unlike the vertex-spread and bone-distance
heuristics it replaced -- a wing membrane is legitimately far from the bone that
drives it. What no automated check can catch is a rig that was mangled before it
reached the file, so a new asset still needs a look.

### Textures and materials

`anim::load_skinned_gltf` decodes each material's images with stb_image and
returns them free of any GPU dependency; the app uploads them via
`gfx::create_texture_from_image`, which builds a full mip chain -- a 4K texture
seen across a valley aliases into shimmering noise without one, and that reads as
a broken model rather than a sampling artefact.

Three maps are read per material: base colour, normal, and glTF's packed
occlusion-roughness-metallic (R/G/B). **Colour space is not optional and is not
recoverable from the pixels** -- base colour uploads as sRGB, the other two as
linear. A normal map read through the sRGB curve gives wrong directions and a
roughness map read that way is far too glossy; both look like shading bugs. The
loader records the space per image in `GltfLoadResult::texture_srgb`, since only
it knows which glTF slot each image came from.

Normal mapping needs a tangent frame, taken straight from glTF's `TANGENT`
attribute (xyzw, w = handedness) rather than derived. A submesh without tangents
gets no normal map -- on this asset that is the eyes, which ship neither.

The mesh is split into **submeshes**, one per glTF primitive, so each can bind its
own textures. A submesh missing a given map still has to fill the sampler slot, so
the renderer binds a 1x1 white texture and `ModelUniforms::material` (x colour,
y normal, z ORM) says which samples to actually use. The shadow map cannot serve
as that placeholder: it is a depth texture and the shader declares a colour one.

Shading is one GGX specular lobe over the wrapped diffuse term -- no IBL, no
environment probe. Enough to separate wet horn from matte membrane, which is all
the roughness map is being asked for.

Images must be decoded **before** `cgltf_free`. Their bytes live in a buffer that
free releases, and reading afterwards is a use-after-free that looked plausible --
every material appeared to share one image, because the freed pointers happened
to compare equal.

### Driving an arbitrary rig

`anim::map_dragon_joints` identifies the neck, tail, wing and leg chains by name
then structure. Wings are a shared root chain plus any number of finger chains,
and sides come from the mean X of a bone's *subtree* -- this asset labels its +X
wing "_L" and puts both wing roots on the centreline.

Rotations are applied about **body-space axes, composed with each joint's bind
rotation**, and **normalized across the chain**. All three matter: replacing the
bind rotation destroys the rest pose, a real rig's bones each point along their
own axis, and rotations down a chain add up -- so applying the flap angle at
every bone makes total bend depend on how many bones the rig happens to have. The
generated rig has two bones per wing and the imported one has five, which put the
imported dragon's wings in a steep V at rest.

### The animation studio

Judging animation from live flight means chasing a manoeuvre with the camera
while also flying it, and no two takes match. The studio (`--studio N`, or the
Studio panel) pins the dragon at one spot and plays a scripted manoeuvre on
loop. Each scenario is **dynamically consistent**: orientation is a pure
function of time and the body-frame angular velocity is finite-differenced from
that same curve, so a scenario cannot lie about its own rotation and every
physically based response reacts exactly as it would in flight. Nothing
downstream of the rig reads position kinematics, so pinning the position is
safe. Combat, rally and the HUD idle while the studio is up.

### One frame: the rig works in model space, and the model may face +Z

**Every world quantity the rig reads arrives in the engine's body frame
(forward -Z) and every bone lives in model space, and this asset faces +Z in
its own space** (the app yaws it 180 to fly). For a long time the rig applied
engine-frame gravity, airflow, pseudo-forces and the aim target straight to
model-space bones. On the generated rig the frames coincide, so every test
passed; on the real dragon it silently inverted the fore-aft axis: pitch
steering worked backwards (nose-up input LOWERED the head), the brake surge
pushed the neck the wrong way (the "neck buckles under the chest" saga was
this), the tail was upstream of its own airflow so the v^2 drag never acted on
it, and the head aim reared the head 50 degrees UP and turned it the wrong way
when locked on. `DragonRig` now measures which way the model faces from the
bind pose (head forward of tail, the same rule the app uses) and carries one
rotation, `body_to_model_`, across everything; `model_frame()` re-expresses
the FlightState so `conj(orientation)` lands in model space. Sign-bearing
constants (sweep aft, washout, roll lean, leg trail, foot hang) are written in
engine terms and multiplied by the measured facing. **When a generated test
rig and an imported asset disagree, suspect the frame before the physics** --
and probe the imported asset numerically (load the glb in a test binary), not
just the generated one.

Two more asset-facing traps found the same day. **The per-vertebra bend limit
must be relative to the rest shape**: this neck's vertebrae sit at 27-29
degrees to each other at rest, so an absolute 20-degree limit straightened
the neck permanently and clamped away most of any steer -- the "stiff neck no
slider could fix". And **measure the jaw's tip as the most forward descendant,
not the deepest**: the deepest bone under the jaw is the tongue, which points
back into the mouth, and the measured open direction came out inverted.

### Why the tail was abrupt (and how to find such things)

Three causes, found in this order, each hiding the next. The **hard joint
clamps** stopped a tail tip arriving at 25 m/s in one frame; a soft limit
(`chain_limit_stiffness`/`_damping`) now brakes progressively from half the
allowed bend, and the range clamp removes only the velocity into it. That
was not the main cause. **Pseudo-forces on a spring sized for 1 g**: a 4 g
S-turn reversal flung the tail to its constraints and it snapped back;
muscle tenses against load, so chain stiffness now saturates with the local
inertial acceleration (`chain_load_tone_accel`) -- deflection bounded, whip
gone; this took the old dragon's worst tip acceleration from 276 to 51 m/s^2.
And the Prowler's remaining one-frame kick was a **26 cm stub bone** at the
head of its tail chain, free to swing 180 degrees in a frame and drag the
chain after it; the mapper now trims stub bases (`trim_stub_base`). Method:
trace the tip's per-frame acceleration through a scripted manoeuvre
(`probe`-style, off the studio's S-turn state), bisect by zeroing one force
term at a time, and do not trust a view that cannot show the axis the motion
is in.

### Attack and speed posture

The rig takes a `RigAction` (breath 0..1, fire edge, boost) each frame from
combat, the bots, or the studio. **Fire from a closed, still mouth reads as a
particle effect stapled to a model**; the jaw, the neck and the claws are what
say the creature is doing it. The jaw (`Bone_024` here -- found as the parent
of the lower lip when nothing is named "jaw") opens with the breath and gapes
and shuts on a spit; the neck **thrusts forward and down** into the flame,
stiffens (`breath_neck_tone`) and trembles faintly on the head only, after the
aim so the aim cannot correct it away; the **spit rears the neck back and
whips it forward** through the same spring as everything else, helped by a
velocity impulse and a 7x stiffness for the gesture, because a steer alone
asks an overdamped neck to move through its spring and a spit is a snap. The
**neck carries a share of the aim** (`neck_aim_share`), curling toward the
lock through the spring while the head snaps the residual -- an animal looking
40 degrees off its body turns its whole neck. Talons open while breathing.

Speed shapes the wing on its own: past cruise the wings **sweep back and
part-fold** whether or not the tuck is held (`speed_sweep_deg`, blending into
the full tuck angles), the outer membrane **flutters** with the square of a
speed factor so cruise is calm and a dive is alive, a flare **buffets**, and
under g the tips **wash out** and the wings come a little forward. The studio's
Attack scenario now spits and breathes on an 8 s schedule and Dive releases the
tuck halfway through its cycle, so both can be watched on loop.

### Flight response: the active posture layer

The chains are passive -- they lag, swing wide and settle. On top of them sits
an active layer, because a flying animal is not a fuselage with dynamics bolted
on: the **tail steers** (rudder with yaw/roll input, elevator with pitch), the
**neck leads a manoeuvre** and lowers into the wind at speed, and the **wings
bow upward under g** and lean asymmetrically with roll input.

Steering deflects the chain's *target shape* rather than its joints, so the
spring pulls the simulation toward the deflected pose and the active motion
eases, overshoots and settles through the same integrator as everything
passive -- one system, one look. The wing roll lean is deliberately not
mirrored between sides: the same rotation about the body axis on both wings is
exactly the antisymmetric shape that produces a roll.

The **legs are pendulums**: they hang from the hips in the effective gravity of
the dragon's frame -- true gravity plus the frame's pseudo-forces at the hip --
held toward the tuck pose by a muscle spring. Braking floats them forward, a
skid slings them outward, and near the ground they extend and stop swinging. A
subtlety worth keeping: in a **coordinated** turn the legs deliberately do NOT
swing laterally, because gravity plus centrifugal force point through the body's
floor -- that is what coordinated means. The tell the pendulum fixes is the
skid, the roll transient and the brake.

**Chain aero drag is quadratic and slender-body**: the v^2 term acts across a
segment, not along it, and only where it is restoring. A segment pointing
downstream (the tail) is straightened by the flow; one pointing upstream (the
neck) is the arrow flying backwards -- aerodynamically unstable, and left to raw
physics it flutters metres wide at dive speed, so the destabilizing case is
suppressed the way real muscle would. One force law gives a tail that hangs at a
hover, streams level at cruise and pulls dead straight in a dive. Chain steering
**curls** progressively down the chain rather than rotating rigidly at the root:
a tail curves, it does not hinge like a door.

Tucking adds **droop** (`tuck_droop_deg`): sweep and fold both act in the
horizontal plane, so without it a folded wing stays at glide dihedral and the
membrane drapes below the body -- half-folded, not a stoop.

**The authored clip is a ground idle and is gated to the ground**: full
strength standing (via the smoothed ground-proximity signal), ~15% in a calm
glide, and that trace fades to zero with flight intensity (max of speed, g
excess, turn rate, tuck, brake -- max, not sum). Toes gripping ground at
100 m/s read as someone else's animation on the wrong creature.

**Limbs in flight trail, they do not dangle.** The whole leg rotates aft at
the hip (`leg_trail_deg`) before the fold bends the knee -- a flying quadruped
presses its legs back along the body, and a pure fold leaves them hanging like
landing gear. The forelegs are their own chains (`upper_arm` -> `ik_underarm`
on this asset -- the `ik_` prefix is on a *deforming* bone here, so it is not
excluded from that search) and trail the same way. The correct trail sign was
settled by rendering both and looking, not by deriving it.

**The upstroke folds the wrist** (`upstroke_fold_deg`): as the wing rises past
~20 degrees it progressively part-folds, which is real bird kinematics and what
keeps two raised wings from crossing over the spine at the top of the beat --
that, plus a flap-up ceiling of 44 degrees (`flap_up_angle_deg`), because 54
put the membranes through each other in any front view.

**The neck braces** (`neck_inertia_scale`): it feels only a third of the
frame's pseudo-forces. Under braking a full-inertia neck buckled under the
chest; a real animal holds its head as a stable platform for the eyes.

**This asset parents all four feet directly to the body root** (IK targets),
so no leg motion ever moves them -- pose the legs however you like, the feet
stay nailed to their bind position in space, which was the whole
standing-in-air look. In flight each foot is therefore **re-anchored to the end
of its leg chain** (`foot_follow`): matched by bind distance, its bind offset
carried in the anchor's frame, converted back to a body-local transform each
frame -- and that conversion must divide by the parent's scale, because the
joint below the root absorbs the scene's scale and a local position lives in
scaled space; dropping the divide sent every foot to within a metre of the
origin. On the ground the authored planted stance wins. The hang and claw curl
(`foot_hang_deg`, `toe_curl_deg`) compose on top.

**The neck is heavy**: overdamped (`neck_damping_scale`), low inertial
response (`neck_inertia_scale` 0.18), and a small lead angle -- it moves slowly
and settles without ringing, the feel of muscle rather than a spring. And the
**leg trail backs off as the tuck deepens**: fold plus full trail rotated the
thigh ~100 degrees in a dive, pointing the shin up and parking the re-anchored
feet above the wings; a stoop stows the legs under the body, not rotated past
it.

**A positional brace initializer silently disconnected all of this once.**
`ChainFeel{stiffness, gravity}` kept compiling as the struct grew, so the
neck's brace, aero gate and articulation range were defaults (range 178
degrees) for three commits of "fixes" that were dead code -- every keyframe
check was watching an unfixed sim. The call sites now assign named fields.
When a fix does not change behaviour, first verify its values actually reach
the code that runs: the neck-position recorder in the transcripts caught this
by showing the sim violating a clamp that provably worked in isolation.

**Chains have an articulation range** (`neck_range_deg`, `tail_range_deg`): no
segment may deviate further from its steered rest direction than muscle allows,
whatever the forces say. And the **v^2 aero gate is per-chain, not
per-instant** (`ChainFeel::aero`): gating on the momentary direction created a
trap where a surge that folded the neck backward made it read as "downstream",
and the drag then pinned it folded under the chest like a windsock -- a stable
attractor that only appeared MINUTES into a run. Every fresh-start keyframe
check missed it; the user's screenshot at t=94 s found it. **Verify long runs,
not just cold starts.**

**Axial chain forces are mostly suppressed** (`chain_axial_response`):
transverse forces bend a spine, axial compression only buckles it, and muscle
resists exactly that. A braking dragon's neck under full axial pseudo-force
folded under its chest. Note the correct steady state of a constant spin with
no airflow is a radial tail with NO lateral offset -- the test asserts the
onset whip, not a sustained deflection that would be wrong physics.

**The upstroke redistributes outboard**: past 20 degrees of elevation the
shoulder's share of the flap shrinks and the wrist leads, real bird
kinematics, keeping the inner membranes from crossing above the spine while
the tip still reaches the full angle.

**Muscle tone** (`chain_tone`): chain stiffness scales with flight intensity,
damping with its square root to stay near critical. That is what keeps the tail
a rudder rather than a streamer in a dive or a hard pull. The **neck has its
own profile** (`neck_stiffness_scale`, `neck_gravity_scale`): it is muscle
wrapped around a spine carrying the head the animal aims with, several times
stiffer and better supported than the tail. And `chain_max_bend_deg` is tight
(20 degrees): a spine bends a long way in total but never sharply at one
vertebra -- a looser limit accordioned the tail under hard manoeuvres.

### Authored motion under the procedural rig

The rig drives what flight determines -- wings, neck, tail, leg tuck -- and the
file's own clip supplies everything else: toes, jaw, small shifts of the body.
Without that layer the extremities are perfectly still, which reads as uncanny
even when the big motions are correct.

`DragonRig::update` resets to bind, samples the clip, then lets the procedural
pass override. Joints the rig owns are rebuilt **from bind**, so the clip cannot
fight them; the leg tuck instead **composes onto** the clip (`rotate_joint`'s
`onto_current`) so the feet keep their authored motion while still folding.

Only **rotation** tracks are imported. Translation and scale tracks would import
root motion -- fighting the flight model for control of where the dragon is --
and stretch bones the skinning assumes are rigid.

**Track keys are stored as deltas from each joint's rest rotation and composed
onto the bind rotation**, never written over it. The skeleton's bind rotations
come from the inverse bind matrices, which for this asset do not agree with the
node hierarchy's TRS -- the joint below the root absorbs a whole scene transform
its own node rotation knows nothing about. An absolute rotation discards that
transform and rolls the entire dragon onto its back, while every individual bone
still moves plausibly. A delta is identity at the clip's rest key, so the bind
pose is reproduced exactly whatever convention built the skeleton.

That bug hid from a check that measured how much each track *changes*: the
offending joint's track is constant, so it read as zero motion. **Compare a
clip's rest pose against the bind pose, not a track against itself.**

Two dead ends, recorded so they are not repeated: cutting a rig down to fit a
joint budget (weight transfer produces glitchy wings and snouts -- MAX_JOINTS is
256 for this reason), and picking a pose-bake frame by proxy metrics rather than
by rendering candidates and looking at them.
