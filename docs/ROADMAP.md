# Dragon Game — Custom Engine Roadmap

> **Historical document.** This is the original plan, kept because its
> reasoning still explains why the engine is shaped the way it is. What was
> actually built is in `STATUS.md`; where the project goes next is `RETRO.md`;
> the running decision log this plan asked for grew into `CLAUDE.md` plus the
> topic docs beside this file.

## Context

You want a single-player 3D dragon game, built with coding agents on a custom engine for freedom and
learning, as a long-term hobby project. You had four candidate concepts (Spyro-like platformer,
Century-like arena combat, novel-scenario demos, open-world survival) and no gamedev/modeling
background yet.

The key finding: **all four concepts share one hard core — a dragon flight controller plus a
third-person camera that feels good.** Every concept is unplayable if flight feels bad, and every one
is already fun to mess with if flight feels great. So the genre commitment gets deferred: we build the
shared flight core first, cheaply turn it into a real game to validate the loop, then add combat and
bots. Nothing is wasted whichever concept eventually wins.

Decisions made:
- **Stack:** C++20 + SDL3 GPU (Metal backend now, Vulkan/D3D12 later without a rewrite)
- **Sequence:** Flight sandbox → Dragon Rally (time trials) → Arena combat vs bots
- **Assets:** greybox placeholder first, rigged Sketchfab dragon once flight feels right
- **Cadence:** steady weekly sessions → milestones sized to 1–2 sessions, each ending in something
  visibly working

Intended outcome: a playable aerial-combat game against bots within a few months, on an engine you
understand, with a codebase that can grow toward boss duels, a hoard roguelite, or the scoped
survival valley later.

## Environment (verified)

| Thing | State |
|---|---|
| Hardware | MacBook Air M5, 10 cores, 32 GB, Metal 4 |
| SDL3 | 3.4.14 installed via brew (`/opt/homebrew`) |
| Toolchain | Apple clang 21, cmake 4.4.2, Xcode 26.6 (`xcrun metal` available) |
| Blender | 5.2 LTS + Blender MCP (Sketchfab, PolyHaven, Hyper3D/Hunyuan3D generation) |
| Repo | `~/src/game-claude` — empty, not yet a git repo |

**Shaders:** author MSL directly and feed it to SDL3 GPU as `SDL_GPU_SHADERFORMAT_MSL` source. No
SDL_shadercross needed, and hot-reload is a file-watch plus a pipeline rebuild. Revisit only if we go
cross-platform.

## Architecture

Custom engine, off-the-shelf libraries. Write what you want to learn; vendor the rest.

```
game-claude/
  CMakeLists.txt
  CLAUDE.md                  # build/run commands, conventions, current milestone
  src/
    core/      math (vec/mat/quat), transforms, time, logging, job system, arena allocators
    gfx/       SDL3 GPU device, swapchain, render passes, pipeline cache, shader hot-reload,
               mesh/texture upload, camera, debug draw (lines/spheres/arrows)
    scene/     entity storage (flat arrays + generational handles), transform hierarchy, culling
    anim/      skeleton, glTF skinning, procedural wing oscillator, spring-damper IK chains, foot IK
    phys/      Jolt integration: bodies, character controller, raycasts, terrain collider
    game/      flight model, dragon state machine, player input, chase camera, gameplay systems
    editor/    ImGui panels: tuning sliders, flight telemetry plots, entity inspector, spawners
  shaders/     *.msl, hot-reloaded
  assets/      models, textures, levels
  third_party/ SDL3 (brew), Jolt, Dear ImGui, cgltf, stb_image, miniaudio
```

**Write ourselves:** renderer, scene/entity system, animation blending and procedural animation,
flight model, camera, AI, all gameplay.
**Vendor, never hand-roll:** Jolt (physics), cgltf (glTF), stb_image, miniaudio (audio), SDL3
(window/input/GPU), Dear ImGui.

Dear ImGui is not optional — as a solo dev it *is* your editor, and tuning-driven design is how
"feel" actually gets found. Every gameplay constant lives behind a slider, and gets serialized to a
JSON tuning file so a good run is never lost.

## Phase 1 — Flight Sandbox

Ends when flying around an empty valley is fun with zero objectives. This is the whole project's
foundation; do not rush it.

**M1. Window + device + triangle.** CMake project, SDL3 window, `SDL_GPUDevice`, swapchain, depth
buffer, one MSL pipeline, spinning triangle. Add shader hot-reload and ImGui immediately — both are
cheap now and compound every session after.

**M2. Camera + debug draw + grid.** Free-fly debug camera, `core/math` with tests, persistent and
one-frame debug line/sphere/arrow API. Debug draw is the tool you will diagnose everything else with.

**M3. Terrain.** Heightmap-generated valley mesh (a few km², single chunk to start), simple
directional light, one directional shadow map. Jolt heightfield collider so the ground is solid.
Grey material only — no textures yet.

**M4. Energy flight model.** The centerpiece. `game/flight.cpp`:
- State: position, orientation quaternion, velocity, angular velocity
- Forces: thrust (flap impulses + sustained), lift as f(airspeed, angle of attack), induced +
  parasitic drag, gravity
- Consequences that must fall out naturally: diving buys speed, climbing spends it, banked turns bleed
  energy, stalling at low airspeed drops the nose
- Assists on top: turn coordination, auto-level toward horizon on no input, soft stall recovery, speed
  floor so you can never just fall out of the sky helplessly
- Inputs: pitch, yaw, roll, flap, tuck-dive, air-brake
- Every coefficient an ImGui slider; a telemetry panel plotting airspeed / AoA / lift / altitude /
  G-load over time

Fly a capsule-and-cone placeholder. Ship a `flight_tuning.json` and keep named presets ("glider",
"agile", "heavy") so you can A/B feel instead of arguing about it.

**M5. Chase camera.** Second-biggest risk after animation. Spring arm with Jolt collision sweep,
velocity-driven FOV and trailing distance, partial roll inheritance (never full — full roll nauseates),
look-ahead into the turn, separate tuning presets. Budget a full session for nothing but sliders.

**M6. Real dragon.** Pull a rigged dragon from Sketchfab via Blender MCP, export glTF, load with cgltf,
GPU skinning. Then procedural animation, which is this project's signature technical feature:
- Wings: phase oscillator whose amplitude/frequency read off flight state (hard flap under thrust,
  locked glide at speed, flare on landing)
- Neck and tail: spring-damper IK chains that lag into turns and whip on direction changes
- Legs: tuck in flight, IK to ground when landed
- Only 6–8 hand-keyed poses needed (glide, flap up/down, hard bank, flare, idle, roar), blended
  procedurally — no full animation set, no mocap, which is exactly why this approach is right for a
  quadruped-with-wings that has no mocap library in existence

**M7. Polish to "fun".** Sky/atmosphere, wind and thermals (updrafts that reward reading terrain),
speed-based screen effects, wing-tip vortex particles, wind audio scaled to airspeed via miniaudio,
ground/vegetation instancing (PolyHaven assets). Landing and takeoff transitions.

## Phase 2 — Dragon Rally

The cheapest possible real game, and more importantly an honest test harness: a stopwatch tells you
whether a flight-model change was actually an improvement, which slider-dragging alone cannot.

**M8.** Checkpoint rings with generous trigger volumes, lap/run timer, next-ring HUD indicator,
restart-instantly flow.
**M9.** Course authoring — place rings in-engine via ImGui, serialize to JSON. Three courses: open
valley, tight canyon, a cave requiring a controlled dive.
**M10.** Ghost replay of your personal best (record transform per frame, play back translucent),
best-time persistence, medal thresholds.

If this is not fun, the flight model is not done — go back to M4/M5. Do not proceed on a bad flight
model; everything downstream inherits it.

## Phase 3 — Arena Combat vs Bots

Your Century-like. Depth from mechanics, not content.

**M11. Combat core.** Fire breath (a cone/sustained-stream hitbox, not a hitscan), fireball
projectiles with deliberately generous hitboxes, cooldown abilities (dash/boost, dive-slam, a
defensive roll or shield), health/armor, damage application, death and respawn.

**M12. Readability — as important as the combat itself.** 3D dogfights are naturally illegible, so
this is a first-class feature, not UI polish: soft lock-on within a cone, off-screen threat arrows,
hit markers and damage numbers, incoming-fire directional indicator, enemy silhouette through terrain
at close range, generous aim assist on projectiles.

**M13. Arena.** One bounded map with rings, arches, and pillars — geometry whose entire job is to
break line of sight and reward energy management. Soft boundary that turns you back rather than
walling you.

**M14. Bot AI.** The most enjoyable engineering in the project. Steering behaviors producing a desired
velocity fed through the *same* flight model the player uses (so bots are physically honest), plus a
state machine: approach → joust → boom-and-zoom → evade → reposition → retreat-and-heal. Terrain
avoidance via raycast whiskers. Lead prediction for shots, with deliberate error scaled by difficulty.
Bots may cheat on reaction time and awareness as long as it reads as skill rather than as omniscience.

**M15. Match loop.** 1v1, then 2v2 and 3v3 team deathmatch, scoreboard, match flow (countdown →
fight → results → rematch), difficulty tiers, three dragon loadouts with distinct ability sets.

## Phase 4 — Long-term shape (decide later, with information)

Deliberately not decided now. Once Phase 3 is playable you will know what your dragon is good at:

- **Boss duels** — Monster-Hunter-shaped fights against large creatures. Each new boss is a discrete
  self-contained deliverable with near-zero engine cost after the first. Best fit for sustained weekly
  sessions.
- **Hoard roguelite** — procedural valley raids, escape before the dragonslayers converge, permanent
  upgrades between runs. Procedural generation covers the content gap; grows by adding items and enemy
  types rather than by authoring levels.
- **Novel scenarios** (Wings of Fire, Age of Fire) — once flight, combat, and NPC dragons exist, each
  vignette is a weekend mission assembled from existing parts. Needs a sequencing/dialogue system.
  Keep strictly non-commercial as a fan project.
- **Scoped survival valley** — the hatchling → drake → adult growth arc where your *mechanics change
  as you grow* (can't fly → glide → fly → carry prey → breathe fire). The most original hook in your
  list; also the most systems work. Year-two candidate.

The Spyro-like platformer is intentionally last: free flight and platforming actively fight each other
(platforming denies traversal, free flight grants it), and it needs the most handcrafted level content
of anything here.

## Files to create first (M1)

- `CMakeLists.txt` — C++20, `find_package(SDL3)` from brew, FetchContent for Jolt/ImGui/cgltf/stb
- `CLAUDE.md` — build and run commands, code conventions, the current milestone, and a running
  decision log so a cold session resumes without re-deriving context
- `src/main.cpp`, `src/gfx/device.{h,cpp}`, `src/gfx/pipeline.{h,cpp}` (with hot-reload)
- `shaders/triangle.msl`
- `.gitignore`, and `git init` — this is not yet a repo

## Verification

Per-milestone, run it and look at it — this is a game, so tests are the floor and not the goal:

- **M1–M3:** `cmake --build build && ./build/dragon` shows the scene; hot-reload verified by editing an
  MSL file mid-run and seeing the change without restart
- **M4:** unit tests on `core/math` (quaternion composition, transform round-trips) and on flight
  integration invariants — energy conservation in an unpowered glide within tolerance, no NaN under
  extreme inputs, stall recovers. Then fly it and read the telemetry plots.
- **M5:** camera never clips terrain over a scripted aggressive flight path; no roll-induced
  disorientation on a 10-minute session
- **M6:** skinned dragon renders with correct bone transforms; wings visibly change behavior between
  hard flap, glide, and flare; tail lags correctly through a hard direction reversal
- **M9–M10:** complete all three courses; ghost playback overlays the recorded run exactly
- **M14:** bots complete 100 headless simulated matches with zero terrain collisions and zero
  stuck/idle states; win rate lands near 50% against a scripted baseline pilot at medium difficulty
- **Every phase gate:** play it for 15 minutes. If it isn't fun, fix that before adding features.

The real gate is Phase 2: if Dragon Rally isn't fun, the flight model isn't finished, and no amount of
combat will rescue it.


---

## Status check-in (2026-08-29)

Everything through Phase 3 shipped, in a different order than planned and
richer in places: the flight sandbox, Dragon Rally with ghosts, the real
rigged dragon with physically based procedural animation (chains, pendulum
legs, articulation limits, an animation studio for inspecting it), combat
with sticky lock-on and honest aim assist, bot dragons flying the same flight
model with an attack/extend/evade doctrine, the M15 match loop
(countdown/fight/results/rematch), additive particle fire, and fully
synthesized audio. Nine test suites guard it.

Phase 4 remains open. Two tracks are on the table, written up in `RETRO.md`:
the ranked gameplay list (loadouts, water, thermals, team matches, a boss-duel
probe of the campaign question), and a D3D9-era retro port -- a new game for
old GPUs -- whose first phase is an RHI extraction that is worth doing
regardless.
