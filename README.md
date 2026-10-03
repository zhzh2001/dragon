# Dragon

A single-player 3D dragon game on a custom C++20 engine, rendering through
SDL3's GPU API (Metal on macOS). You fly a dragon whose flight is a real
energy model: diving buys speed, climbing spends it, and banking curves the
flight path because the lift vector tilts. The main mode is a **hoard run**
down a generated valley. You fight rival dragons and guard towers, land on
hoard caches, grow from drake to adult, and fly out through the pass. There
is also an arena deathmatch against bot dragons that fly the same flight
model you do.

> **Made with AI.** Most of the code and the docs were written by AI coding
> agents (Claude through Claude Code, and OpenAI Codex). Every dragon model
> was generated with Tencent Hunyuan 3D, and the concept art with GPT
> Image 2 and Nano Banana 2. One person directed it, playtested it, and
> chose what to keep. The details, tool by tool, are in
> [`AI_DISCLOSURE.md`](AI_DISCLOSURE.md). The sound is synthesized in
> code; no AI audio model was used.

Everything above the vendored foundations (SDL3, Dear ImGui, cgltf, stb_image,
miniaudio) is written for this project: the renderer, animation, flight
model, camera, AI, gameplay and audio synthesis.

## Highlights

- **Energy flight.** Thrust, lift, drag and gravity are integrated
  honestly. Landing, walking and taking off are real states. The assists
  (auto-flap, bank limit, stall recovery) make it approachable without
  faking the physics. Aerobatics include a dodge roll and a reversing
  flip.
- **The hoard run.** A roguelite valley: rivals at posts, towers on the
  slopes, hunters loosed when you linger, three caches to land on, and
  growth that moves the same tuning dials the panels expose. A run is three
  valleys, each harder than the last, and the seed is printed so a good
  valley can be flown again.
- **Combat.**
  - Breath and fireballs with sticky lock-on and honest aim assist.
  - Melee bites, claws and tail strikes.
  - Six elements (fire, frost, blight, storm, tide, stone), each with its
    own status effect.
  - Bot dragons with an attack / extend / evade doctrine. Their
    difficulty is imperfect perception, never a different flight model.
- **Seven playable dragon species**, with two more as candidates, and each
  one a species rather than just a mesh: how it
  moves (`.rig.cfg`), handles (`.flight.cfg`) and breathes (`.breath.cfg`).
  All of them are driven by one procedural rig: inertial neck and tail
  chains, wings that bow, sweep and fold, legs that trail in flight and
  plant on the terrain, and a jaw that gapes on the flame. There are no
  animation clips.
- **A valley you can trust.** The gameplay queries read the exact rendered
  terrain triangle. A river is carved through the floor, the ground goes on
  past the map edge, and mixed forests are placed by slope, altitude and
  water.
- **A modern renderer.**
  - HLSL shaders, hot-reloaded and translated to Metal through
    SDL_shadercross; reversed-Z depth.
  - One lighting path for the terrain, plants and creatures.
  - Directional shadows and PBR creatures.
  - An HDR target with bloom and a hue-preserving tonemap.
  - Additive particle breath.
- **Synthesized audio.** Wind, flame, wingbeats, cries and impacts are all
  generated at start-up from noise and sines. The game ships no sound
  files.
- **Verifiable without a human.** `--headless --frames N --screenshot` plus
  scripted cameras, a self-playing demo pilot, and seventeen renderer-free
  test suites.

## Building

On macOS this requires Metal, CMake 3.24 or later, Ninja, SDL3
(`brew install sdl3`), and SDL_shadercross with DXC, which the development
build uses to compile its HLSL shaders at runtime. Build it once with
`tools/build_shadercross.sh`: it takes about half an hour, since it
compiles DXC. Everything else is fetched by CMake. Windows and Linux
are planned: see [`docs/PORTING.md`](docs/PORTING.md).

```sh
tools/build_shadercross.sh     # once: ~/.local/opt/shadercross
cmake -S . -B build -G Ninja
cmake --build build
./build/dragon --run          # a hoard run
./build/dragon --demo         # the game playing itself
ctest --test-dir build        # seventeen suites, plain executables
```

**The models are not in the repository.** They are large, so they are
gitignored. The game loads whichever species are present, Embercrest
first, and falls back to a greybox dragon when there are none. The roster
comes with the release packages. Any rigged glTF can be tried with
`--model PATH`; [`ATTRIBUTION.md`](ATTRIBUTION.md) lists the two Sketchfab
dragons the engine was first built around.

**Packages.** `tools/release/package_macos.sh` builds a universal macOS 11+
app with the roster inside, and `tools/release/package_windows.sh` a
Windows 10+ build (D3D12, or Vulkan with `--gpu-driver vulkan`),
cross-compiled on the Mac. Linux builds and runs on Vulkan from source. The ready-made one is on the
[releases page](https://github.com/zhzh2001/dragon/releases), and its
`README.txt` explains how to open an unsigned app. Records and saved tuning go to
`~/Library/Application Support/Paleshell/Dragon`.

## Playing

| Input | Action |
|---|---|
| Left stick / W, S, A, D | Pitch and roll (on the ground: walk and turn) |
| A (pad) / Space | Flap, the only way energy enters the system. On the ground: leap |
| RT / Shift | Tuck the wings and dive |
| LT / Ctrl | Flare and brake; hold it low to land |
| LB / F or left mouse | Breath (hold) |
| RB / G | Fireball; hold for a charged shot once grown |
| B (pad) / C | Bite; claw and tail strikes when close |
| X (pad) / X | Boost |
| D-pad left/right / Z | Dodge roll |
| D-pad up / B | Flip, to face a chaser |
| Y (pad) / U | Second breath, once adult |
| Right-stick click / T | Relock onto the next target |
| Right stick / right-drag | Free look |
| 1, 2, 3 / V | Camera presets / first person |
| P | Hand the controls to the demo pilot, and take them back |
| R / Enter | Restart the valley / next valley after the results |
| F1 | Hide the tuning panels |

Every gameplay constant sits behind an ImGui slider; the panels are the
game's editor. `CLAUDE.md` has the full flag and control reference.

## Documentation

`CLAUDE.md` is the engineering map: build and verification recipes, the
conventions, the cross-cutting lessons, and how the AI agents that build
the project are directed. The detail lives in `docs/`:

| Doc | What is in it |
|---|---|
| `docs/ANIMATION.md` | The procedural rig, glTF import, the roster, and the frame/scale traps |
| `docs/COMBAT.md` | Fire, targeting, melee, elements, bot doctrine, the match loop, the hoard run |
| `docs/WORLD.md` | Terrain, the river, the skirt, vegetation |
| `docs/EFFECTS.md` | Particles, breath profiles, synthesized audio |
| `docs/MODEL_GENERATION.md` | How a creature is generated, rigged and repaired |
| `docs/DIRECTION.md` | Where the modern build goes: genre, art direction, HUD, assets |
| `docs/PORTING.md` | Windows, Linux, and Direct3D 9 (SM3, SM2, fixed function) |
| `docs/RELEASE.md` | How the public repository and the release packages are made |
| `docs/STATUS.md` | Milestone history and open questions |

## Licence

The code, shaders, tools, docs and original assets are under the
[MIT licence](LICENSE). Third-party material keeps its own licence:
- libraries: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)
- models and their renders: [`ATTRIBUTION.md`](ATTRIBUTION.md)

The renders in `artifacts/` that show the "Black Dragon" by 3DHaupt
(dennish2010) are CC BY-NC 4.0 material. The AI-generated material is
labelled as such in [`AI_DISCLOSURE.md`](AI_DISCLOSURE.md).
