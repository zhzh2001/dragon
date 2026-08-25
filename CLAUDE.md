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

`--combat` arms the dragon and spawns a wave; `--attack` also holds breath and
fires, which is how the flame and the projectiles get onto a screenshot.
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
| Q/E | Rudder |
| Gamepad left stick | Pitch and roll (absolute, best feel of the three) |
| Gamepad A / triggers | Flap / tuck-dive and brake |
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
- **Terrain height is analytic**: `Terrain::height_at` evaluates the same noise
  the mesh was built from, so gameplay queries never touch triangles. Physics
  (Jolt) is deferred until something actually needs swept or convex collision --
  dragon-vs-dragon, projectiles, ragdolls. A heightfield collider would be pure
  overhead for ground clearance.
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
- **Next:** M12 readability (soft lock-on, damage numbers) and M14 bots, or M7
  polish (thermals, particles, audio).

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

The **ground-idle clip fades with flight intensity** (`clip_flight_fade`) --
max of speed, g excess, turn rate, tuck and brake, smoothed. Toes curling and a
jaw working are right in a calm glide and absurd in a 100 m/s dive, where a
real animal goes tense and still. Grounded, nothing fades.

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
