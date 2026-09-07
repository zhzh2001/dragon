# Dragon

A single-player 3D dragon game on a custom C++20 engine, rendering through
SDL3's GPU API (Metal on macOS). You fly a dragon whose flight is a real energy
model -- diving buys speed, climbing spends it, banking curves the flight path
because the lift vector tilts -- through a 5 km procedural valley, racing
checkpoint courses against your own ghost and dogfighting bot dragons that fly
the exact same flight model you do.

Everything above the vendored foundations (SDL3, Dear ImGui, cgltf, stb_image,
miniaudio) is written from scratch as a learning project: renderer, animation
system, flight model, camera, AI, gameplay, audio synthesis.

## Highlights

- **Energy flight model** -- thrust, lift, drag and gravity integrated honestly;
  specific energy is the quantity you manage. Landing and taking off are real
  states: set down and the body settles level on the ground, and the first flap
  is a leap. Assists (auto-flap that knows a dive from a sink, bank limit,
  stall recovery) make it approachable without faking the physics, and one
  `heft` knob gives each model its own weight.
- **A valley you can trust** -- procedural terrain whose gameplay queries read
  the exact rendered triangle, a river carved through the floor with a
  reflective surface you can land on, ground that continues past the playable
  extent instead of ending in an invisible wall, and mixed forests of four tree
  and three grass kinds placed by slope, altitude and water.
- **Physically based animation** -- an imported rigged dragon driven entirely by
  procedural animation: inertial neck and tail chains with muscle tone that
  tenses under load, pendulum legs that trail in flight, wings that bow under
  g, sweep back and flutter with speed, a jaw that gapes on the flame and a
  neck that rears on the spit. Two very different rigs (a quadruped dragon and
  a wyvern) are driven by the same code. An in-game **animation studio** plays
  scripted, dynamically consistent manoeuvres on a pinned dragon for inspection.
- **Combat** -- fire breath, fireballs with sticky lock-on and honest aim assist
  (intercept lead, gravity drop, inherited-velocity drift), boost, and bot
  dragons with an attack / extend / evade doctrine. Bot difficulty is honest
  imperfection: stale perception and scattered firing solutions, never a
  different flight model.
- **Dragon Rally** -- three generated checkpoint courses with timing, splits,
  ghost replays of your best run, and an autopilot that both verifies the
  courses headlessly and seeded the bot AI.
- **Hot-reloaded Metal shaders**, reversed-Z depth, directional shadows, PBR
  texturing with normal and ORM maps, additive particle fire, instanced
  swaying foliage.
- **Synthesized audio** -- wind that brightens with speed, flame roar, wingbeat
  whooshes, creature cries, shots and explosions, all generated at startup from
  noise and sines: the game ships no sound files.

## Building

Requires macOS with Metal, CMake ≥ 3.24, Ninja, and SDL3 (`brew install sdl3`).
Everything else is fetched by CMake.

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/dragon
ctest --test-dir build     # ten suites, plain executables, no framework
```

The dragon model is downloaded separately -- see `ATTRIBUTION.md` for the
source and licence (CC BY-NC: this project is strictly non-commercial). Without
it the game falls back to a generated greybox dragon. Any rigged glTF can be
tried with `--model PATH`; a second, CC-BY wyvern is documented there too.

## Playing

| Input | Action |
|---|---|
| Left stick / WASD | Pitch and roll |
| A (pad) / Space | Flap -- the only way energy enters the system |
| RT / Shift | Tuck-dive (noses over, folds the wings) |
| LT / Ctrl | Brake and flare -- hold it low to land |
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
ImGui slider -- the tuning panels are the game's editor. The Dragon panel has
the rig and hide colour, Flight has the presets and `heft`, and
Terrain > Vegetation has the trees and grass.

Headless verification, no human at the keyboard:

```sh
./build/dragon --headless --frames 300 --screenshot /tmp/shot.bmp
./build/dragon --headless --autopilot --course 1 --frames 4000   # fly a course
./build/dragon --headless --bots 2 --match --frames 7200         # match soak
./build/dragon --studio 5                                        # studio: dive
./build/dragon --studio 8 --inspect 90 4.5 --inspect-head        # attack, on the head
```

## Layout

```
src/core/    math, input, noise, logging
src/gfx/     GPU device, pipeline cache + shader hot reload, renderer,
             shadows, additive particles, instanced foliage
src/anim/    skeleton, GPU skinning, glTF loader, procedural dragon rig
src/game/    flight model, cameras, terrain, vegetation, rally, combat,
             bots, match loop, animation studio
src/audio/   synthesized audio -- every sound generated at startup
src/editor/  ImGui integration
shaders/     MSL, hot-reloaded from source
tests/       ten suites
```

## Documentation

`CLAUDE.md` is the engineering map: build and verification recipes, the
conventions, and the cross-cutting lessons. The detail lives in `docs/`:

| Doc | What is in it |
|---|---|
| `docs/ANIMATION.md` | The rig, glTF import, and the frame/scale traps |
| `docs/COMBAT.md` | Fire, targeting, bot doctrine, the match loop |
| `docs/WORLD.md` | Terrain, the river, the skirt, vegetation |
| `docs/EFFECTS.md` | Particles and synthesized audio |
| `docs/STATUS.md` | Milestone history and open questions |
| `docs/DIRECTION.md` | Where it goes next: genre, art direction, UI, assets, and the plan |
| `docs/ROADMAP.md` | The original plan |
| `docs/RETRO.md` | The D3D9-era port study and what to build next |

## Licence

Code: no licence chosen yet -- ask before reusing. The dragon model is
CC BY-NC (see `ATTRIBUTION.md`), which makes the assembled game non-commercial.
