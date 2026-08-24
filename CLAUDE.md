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
src/anim/    (empty) skinning, procedural wings, IK
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
  generated courses, HUD, autopilot. **Awaiting playtest.**
- **Next: M6** a real rigged dragon with procedural wing, neck and tail
  animation -- or M11, first combat.

## The rally

`game::Course` holds checkpoints; `game::Rally` runs the clock. A flying start
rather than a countdown: the clock begins when you cross the first ring, so
choosing your entry speed and line is part of the skill.

Ring tests are **segment based**, not point based. At 100 m/s a 60 Hz frame
covers 1.7 m, so a point-in-volume test would simply miss a thin checkpoint --
the fastest runs would be the ones that failed to register.

Ghosts record the best run, never the last, at a fixed 30 Hz, including wing
angle so the replay is visibly flying rather than sliding along a path.

### Generated courses must be *flyable*, not just well-formed

Two passes run over every generated course, and both exist because a course
failed without them:

- `limit_climb` caps the rise per leg. The dragon gains about 5 m/s of energy
  flapping at 45 m/s forward, so it sustains a gradient near 0.11. The first
  Summit Climb demanded 0.51 on one leg -- not hard, impossible.
- `clear_line_of_flight` raises rings until the *chord* between consecutive rings
  clears the terrain. Rings being individually clear says nothing about the line
  between them, and a path curving round a mountain produces chords straight
  through it.

`game::steer_toward` / `steer_through` is a PD controller that flies the course
unattended. It is both the test harness -- "the autopilot completes every course"
is an assertion -- and the seed of the bot AI, since it commands the same
`FlightInput` a player does and cannot cheat the flight model.

## The camera

`game::ChaseCamera`. The arm is *swept* against the terrain, not merely clamped:
a clamp alone happily places the camera on the far side of a ridge, showing the
inside of a mountain. The arm shortens fast (clipping is instantly ugly) and
extends slowly (snapping out is jarring), then the final position is hard-clamped
above ground as a last resort -- being inside a mountain for even a few frames is
worse than a small jolt.

Roll inheritance is partial by design and full inheritance is not offered: it is
nauseating, and it hides the horizon, which is the player's main reference for
reading their own attitude.

`tests/test_camera_rig.cpp` flies scripted manoeuvres and asserts what a player
would actually notice: never underground across six terrain-hugging cases and all
three presets, the dragon never off screen through rolls and loops, no
single-frame jump over 6 m, first person rigidly attached, free look not steering
the dragon, and no drift when parked on a slope.

## The flight model

`game::FlightModel` integrates real forces: thrust, lift, drag, gravity. Diving
buys speed, climbing spends it, hard turns bleed energy, and flapping is the only
way energy enters the system. `FlightState::specific_energy` is the number a
pilot actually manages.

With the default tuning the envelope is roughly:

| | |
|---|---|
| Hands-off glide | 26 m/s, sink 2.8 m/s, 9.3:1 |
| Best glide | 27 m/s, sink 2.3 m/s, 11.6:1 |
| Full flap, level | 51 m/s |
| Tucked dive | 98 m/s (354 km/h) |
| Flared brake | 8.5 m/s -- slow enough to land |
| Stall | past 16 degrees angle of attack, recovers hands-off |
| Inverted | recovers to level in ~1.7 s, losing ~35 m |

Energy budget, in metres of specific energy per second -- flapping is the only
positive entry, which is the whole design:

| gliding | flapping | tucked | hard turn | braking |
|---|---|---|---|---|
| -3.1 | **+5.0** | -2.5 | -4.9 | -8.9 |

Every coefficient is an ImGui slider, and `assets/flight_tuning.cfg` (flat
`key value` text, not JSON -- no dependency, trivially diffable) persists a good
session. Presets: glider, agile, heavy.
