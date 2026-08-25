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
| Right-drag / right stick | Free look -- orbit the view without steering |
| V | First person, from behind the dragon's head |
| 1 / 2 / 3 | Camera preset: chase, action, cinematic |
| Tab | Toggle free-fly survey camera (detaches where the chase camera is) |
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
- **Next:** M11 (first combat), or M7 polish (thermals, particles, audio).

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
and stretch bones the skinning assumes are rigid. Before trusting a clip, check
that it carries no rotation on the root or pelvis; this one does not, so it is
pure body-local detail.

Two dead ends, recorded so they are not repeated: cutting a rig down to fit a
joint budget (weight transfer produces glitchy wings and snouts -- MAX_JOINTS is
256 for this reason), and picking a pose-bake frame by proxy metrics rather than
by rendering candidates and looking at them.
