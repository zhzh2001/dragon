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

Verify a visual change without a human at the keyboard:

```sh
./build/dragon --headless --frames 40 --screenshot /tmp/shot.bmp
sips -s format png /tmp/shot.bmp --out /tmp/shot.png   # macOS: BMP -> viewable PNG
```

Tests: `./build/test_math` or `ctest --test-dir build`.

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

M1 done: window, GPU device, reversed-Z depth, offscreen render + blit,
hot-reloadable MSL pipelines, ImGui, headless screenshots, math library + tests.
