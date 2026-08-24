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
- **Next:** a usable dragon asset (see below), or M11 (first combat).

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

### Getting a usable asset: do NOT route it through Blender's FBX importer

Drop a rigged `.glb` or `.gltf` at `assets/dragon.glb` and it is used
automatically, falling back to the generated rig if anything is wrong. The
loader handles the awkward parts (file-supplied inverse binds, scene transform
above the skin, side detection by subtree, facing and scale derived from the
rig). What it cannot fix is an asset that arrived broken.

**Sketchfab models delivered as FBX are mangled by Blender's FBX importer.** This
was established by rendering the dragon in Blender's own viewport, dragon meshes
only, across eight frames of its animation: broken at every frame, before any
processing of mine. Sketchfab's own viewer shows the same model correctly. The
signature is roughly sixty auto-generated `*_end_*` leaf bones plus a handful of
vertices flung far from the body, which is the classic FBX bone-orientation
import failure -- normally cured by importing with `ignore_leaf_bones` and
`automatic_bone_orientation`, neither of which the download tool exposes, and it
deletes the source `.fbx` afterwards.

So: **download the glTF variant from the model's Sketchfab page directly** (the
Download button offers glTF alongside the original format) and drop it in
`assets/`. That bypasses Blender entirely. No pose bake, no bone surgery, no
export options to get wrong.

`artifacts/dragon_broken_fbx_import.glb` is kept as the counter-example. It
parses, validates, and skins to identity at bind pose -- every automated check
passes -- and still renders as a tangle of sheets. No loader-side test can catch
"the rig was mangled on import", so a new asset needs a look, not just a green
test. Two dead ends recorded so they are not repeated: cutting a rig down to fit
a joint budget (weight transfer produces glitchy wings and snouts -- MAX_JOINTS
is 256 for this reason), and picking a pose-bake frame by proxy metrics rather
than by rendering candidates and looking at them.
