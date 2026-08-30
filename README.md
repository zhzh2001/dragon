# Dragon

A single-player 3D dragon game on a custom C++20 engine, rendering through
SDL3's GPU API (Metal on macOS). You fly a dragon whose flight is a real energy
model -- diving buys speed, climbing spends it, banking curves the flight path
because the lift vector tilts -- through a 5 km procedural valley, racing
checkpoint courses against your own ghost and dogfighting bot dragons that fly
the exact same flight model you do.

Everything above the vendored foundations (SDL3, Dear ImGui, cgltf, stb_image,
miniaudio) is written from scratch as a learning project: renderer, animation
system, flight model, camera, AI, gameplay, audio synthesis. See `CLAUDE.md`
for the engineering log and conventions, `docs/ROADMAP.md` for the original
plan, and `docs/RETRO.md` for the next frontier: porting the game to
D3D9-class GPUs -- a new game for old hardware.

## Highlights

- **Energy flight model** -- thrust, lift, drag and gravity integrated honestly;
  specific energy is the quantity you manage. Assists (auto-flap, bank limit,
  stall recovery) make it approachable without faking the physics.
- **Dragon Rally** -- three generated checkpoint courses with timing, splits,
  ghost replays of your best run, and an autopilot that both verifies the
  courses headlessly and seeded the bot AI.
- **Combat** -- fire breath, fireballs with sticky lock-on and honest aim
  assist (intercept lead, gravity drop, inherited-velocity drift), boost,
  practice sentinels, and bot dragons with an attack / extend / evade doctrine.
  Bot difficulty is honest imperfection: stale perception and scattered
  solutions, never a different flight model.
- **Physically based animation** -- an imported rigged dragon driven by
  procedural animation: inertial neck and tail chains with muscle tone and
  articulation limits, pendulum legs that trail in flight, wings that bow under
  g-load and flex through the upstroke, and a ground-idle clip that fades as
  flight gets violent. An in-game **animation studio** plays scripted,
  dynamically consistent manoeuvres on a pinned dragon for inspection.
- **Hot-reloaded Metal shaders**, reversed-Z depth, cascade-free directional
  shadows, PBR texturing with normal and ORM maps, additive particle fire.
- **Synthesized audio** -- wind that brightens with speed, flame roar, wingbeat
  whooshes, shots and explosions, all generated at startup from noise and
  sines: the game ships no sound files.

## Building

Requires macOS with Metal, CMake ≥ 3.24, Ninja, and SDL3 (`brew install sdl3`).
Everything else is fetched by CMake.

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/dragon
```

Tests (plain executables, no framework):

```sh
ctest --test-dir build
```

The dragon model is downloaded separately -- see `ATTRIBUTION.md` for the
source and licence (CC BY-NC: this project is strictly non-commercial). Without
it the game falls back to a generated greybox dragon.

## Playing

| Input | Action |
|---|---|
| Left stick / WASD | Pitch and roll |
| A (pad) / Space | Flap -- the only way energy enters the system |
| RT / Shift | Tuck-dive (noses over, folds the wings) |
| LT / Ctrl | Brake and flare |
| D-pad ⇄ / Q, E | Rudder |
| LB / F or LMB | Fire breath (hold) |
| RB / G | Fireball |
| X (pad) / X | Boost |
| R-stick click / T | Relock onto the next target |
| Right stick / right-drag | Free look |
| 1 / 2 / 3, V | Camera presets, first person |
| Tab | Free-fly camera |
| F1 | Hide the tuning panels |
| R | Restart |

Start in the Rally panel to pick a course, or tick **combat enabled** in the
Combat panel and press **spawn bots**. Every gameplay constant sits behind an
ImGui slider -- the tuning panels are the game's editor.

Headless verification, no human at the keyboard:

```sh
./build/dragon --headless --frames 300 --screenshot /tmp/shot.bmp
./build/dragon --headless --autopilot --course 1 --frames 4000   # fly a course
./build/dragon --headless --bots 3 --frames 7200                 # bot soak
./build/dragon --headless --bots 2 --match --frames 7200         # match soak
./build/dragon --studio 5                                        # animation studio: dive
```

## Layout

```
src/core/    math, input, noise, logging
src/gfx/     GPU device, pipeline cache + shader hot reload, renderer,
             shadows, additive particles
src/anim/    skeleton, GPU skinning, glTF loader, procedural dragon rig
src/game/    flight model, chase camera, terrain, rally, combat, bots,
             match loop, animation studio
src/audio/   synthesized audio -- every sound generated at startup
src/editor/  ImGui integration
shaders/     MSL, hot-reloaded from source
tests/       nine suites, ~1100 checks
docs/        ROADMAP.md (the plan), RETRO.md (the D3D9 port study)
```

## Licence

Code: no licence chosen yet -- ask before reusing. The dragon model is
CC BY-NC (see `ATTRIBUTION.md`), which makes the assembled game non-commercial.
