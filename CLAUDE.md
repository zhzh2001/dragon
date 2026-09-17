# Dragon Engine

Custom C++20 engine for a single-player dragon game, rendering with SDL3's GPU
API (Metal backend on macOS). Long-term hobby project: `docs/STATUS.md` is
what exists, `docs/RETRO.md` is where it goes next. Ignore RETRO.md for now,
we will deal with it after the modern build is polished, so no need to limit
usage of modern features in the current code.

## Where things are written down

This file is the map plus the rules that apply to every task. The detail lives
in `docs/`, and each of those is worth reading before touching its area --
they are records of what was already tried and why it is the way it is.

| Doc | What is in it |
|---|---|
| `docs/ANIMATION.md` | The procedural rig, glTF import, the seven-species roster and the default asset, the standing stance and terrain plant, the animation studio, and the frame/scale traps that cost the most time |
| `docs/COMBAT.md` | Fire, targeting and aim assist, bot AI doctrine, the match loop |
| `docs/WORLD.md` | Terrain generation and queries, the river, the skirt past the map edge, vegetation placement |
| `docs/EFFECTS.md` | Particles, the per-species breath profiles, and the fully synthesized audio |
| `docs/STATUS.md` | Milestone history, and the open questions a new session should know about |
| `docs/DIRECTION.md` | Where the modern build goes before the port: the hoard-run roguelite, the art direction and its targets in `docs/concept/`, the HUD kit, the asset policy, and the sequenced plan |
| `docs/EMBERCREST.md` | The script-built original dragon: what the Blender generator produced, why it is not the hero model, and how to rebuild it |
| `docs/MODEL_GENERATION.md` | Generating a creature: which cloud and local tools were measured and what each one is actually good for, the four-step pipeline that survived, and how to add a creature that is not a dragon |
| `tools/hunyuan_oneshot.md` | Driving the Hunyuan one-shot headlessly through chrome-use -- the generation step of that pipeline, and the traps that cost a run each |
| `tools/skeletons/*.json` | The deform skeletons the rigger fits, one per anatomy *and per set of measurements* -- a shared file shares its author's leg positions, `wing_field` gates and `jaw_mask`, and those do not transfer. Bone names are a contract with `src/anim/dragon_rig.cpp` |
| `docs/ROADMAP.md` | The original plan and its phases |
| `docs/RETRO.md` | The D3D9-era port study, and the ranked list of what to build next |
| `ATTRIBUTION.md` | The models, their licences, and how to obtain them |
| `.claude/skills/concept-art/SKILL.md` | Generating concept art, HUD mockups and creature reference sheets from a ChatGPT/Gemini subscription -- which backend suits which job, and which quota each one burns |
| `tools/codex_usage.py` | How much of the Codex 5-hour and weekly windows is left, read out of the logged-in browser. Run it before starting a `codex` task |
| `tools/rig_probes/` | Renderer-free probes that run the rig on a species and print numbers -- foot heights, sink below the floor, leg angles in the glide, leg swing through a manoeuvre, jaw direction -- and a candidate renderer. Ten milliseconds a candidate against two seconds a render: they decide what to render, they do not replace looking |

## Build & run

```sh
cmake -S . -B build -G Ninja      # first time, or after adding files
cmake --build build
./build/dragon
ctest --test-dir build            # eleven suites, plain executables, no framework
```

### Verifying without a human at the keyboard

This is the workhorse. Render the thing and look at it:

```sh
./build/dragon --headless --frames 40 --screenshot /tmp/shot.bmp
sips -s format png /tmp/shot.bmp --out /tmp/shot.png   # macOS: BMP -> viewable PNG
```

| Flag | Effect |
|---|---|
| `--headless` | No visible window; still renders offscreen. Frame time is pinned to 1/60 s. |
| `--frames N` | Run exactly N frames then exit. |
| `--screenshot PATH` | Save the last frame as a BMP (requires `--frames`). |
| `--hide-ui` | Hide the ImGui panels, for world-only captures. |
| `--telemetry [N]` | Log one line of flight state every N frames (default 60). A screenshot shows a pose; this shows the state machine behind it. |
| `--cam x,y,z,tx,ty,tz` | Place the camera at a position looking at a target. |
| `--cam-mode chase\|action\|cinematic\|fp` | Pick a camera preset. |
| `--inspect [angle] [dist] [elev]` | Orbit camera locked to the dragon. Elevation 88 looks straight down -- the only view that shows a lateral tail wave. |
| `--inspect-head` | With `--inspect`, orbit the ANIMATED head instead of the body (jaw, aim). |
| `--skeleton` | Draw the posed joints as lines, to tell a rig problem from a skinning one. |
| `--bind-pose` | Freeze the rig, to check an imported asset against its own bind pose. |
| `--input p,r,y,flap,tuck,brake` | Hold a control input for the whole run. It goes through the assists, exactly as a player's stick does. |
| `--autopilot` | Fly the selected course unattended. Doubles as a soak test. |
| `--course N` | Select a generated course. |
| `--studio N` | Animation studio, scenario N: 0 glide, 1 flap, 2/3 turns, 4 s-turns, 5 dive, 6 pull-out, 7 brake, 8 attack, 9 grounded. |
| `--combat` | Arm the dragon and spawn a wave of sentinels. |
| `--attack` | Also hold breath, fire and boost -- how flame, projectiles and boost get onto a screenshot. |
| `--bots N` | Spawn N bot dragons instead of sentinels. |
| `--match` | Start a deathmatch from the CLI (with `--bots N`). |
| `--model PATH` | Load a different rigged glTF in place of `assets/dragon.glb` (e.g. `assets/alt/prowler.glb`, see ATTRIBUTION.md). |
| `--models A,B,C` | Load a whole roster. The player flies the first; bots are dealt the rest in turn, so one match fields several species. |
| `--bot-range N` | Spawn bots N metres out instead of 650 -- the only way to get the player and every rival into one capture. |
| `--cycle-models N` | Swap the player onto the next roster entry every N frames. Sweeps one scenario across every species in one command, and soaks the swap path (it re-initialises both rigs). |
| `--hue r,g,b,strength` | Recolour the player's hide (the same recolour the bots use). |

Soaks that have caught real bugs:

```sh
./build/dragon --headless --autopilot --course 1 --frames 4000   # fly a course
./build/dragon --headless --bots 3 --match --frames 7200         # 2-minute fight
```

The elemental roster, and the two flags that make a multi-species capture
possible at all -- at the default 650 m spawn the rivals are specks:

```sh
./build/dragon --models assets/embercrest.glb,assets/rimefang.glb,assets/frostvein.glb,\
assets/blightmaw.glb,assets/ironroot.glb,assets/stormsail.glb,assets/tidewrack.glb \
  --bots 6 --bot-range 95 --combat
```

Panels worth knowing: **Dragon** has the rig and the hide colour, **Flight**
has the tuning presets and `heft`, **Terrain > Vegetation** has the trees and
grass, **Combat** has the difficulty dials and the bots, **Studio** drives the
animation scenarios and picks which creature is on the stand. Every panel except Combat starts collapsed.

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
| M | Cycle the player onto the next model in the roster (`--models`). The studio scenario keeps playing, so this is how two species are compared under one manoeuvre |
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

## Working agreements

- **Stop at milestones that need a human at the keyboard**, and say explicitly
  what to playtest and what to look for. Feel free to commit as you go.
- **Every pose change ends with a rendered inspection, before it is called
  done and before it is committed.** Not a number, not a test, not a
  contact-sheet thumbnail: full-size renders of the thing that changed,
  from the axis the change is in, on *every* model the change can reach
  (a rig-code change reaches the whole roster; a profile change reaches
  one). The minimum set is written down here because each item was once
  skipped and cost a round with the playtester:
  1. The changed pose from the side, the front, the rear and above
     (`--studio N --inspect 90|0|180 14 6` and `--inspect 90 14 85`).
  2. **The head, close** (`--inspect 90 3.5 0 --inspect-head`) in the
     changed scenario *and* in the glide -- jaws over-close and snouts twist
     out of sight of a body-scale render, and a jaw that looks fine in the
     air can be wrong on the ground.
  3. **The feet at ground level** (`--inspect 60 11 3`) for anything that
     touches the ground: a stance that keeps the joints on the floor can
     still sink the mesh under them.
  4. **The same thing in the game, not just the bench**: land it
     (`--input -0.25,0,0,0,0,0 --frames 900`) or fly it, and look again.
  5. If the change can be reached by switching models (`M`, `--models`,
     `--cycle-models`), switch onto it from `dragon.glb` and look once more.
  Put the renders that matter beside the report in `artifacts/`, tracked.
- **Every gameplay constant belongs behind an ImGui slider.** Feel is found by
  dragging sliders while playing, not by planning. A dial that decides whether
  the game is fun (aim assist, bot spread) belongs at the TOP of its panel,
  not inside a collapsed header -- one that cannot be found does not exist.
- Tests are plain executables with a `CHECK` macro, no framework. Add a suite
  when a system gets its own file; `ctest --test-dir build` runs all of them.
- **A creature is a species, not just a mesh.** Each carries `<model>.rig.cfg`
  (how it moves), `<model>.flight.cfg` (how it handles) and
  `<model>.breath.cfg` (what it breathes) beside its glTF. All three are
  partial files: absent or unknown keys leave the defaults alone.
  Beware the asymmetry: a partial file inherits *code* defaults, and for the
  standing wing (`ground_wing_aim` and its directions, or the older
  `ground_stow_*` angles) and the stance (`ground_stance`) the default is
  off, meaning "do not fold, do not re-pose". Every shipped profile carries
  its own block; a new species that lists only its character fields cannot
  close its wings or stand. Copy a sibling's blocks and re-tune by render.
  Asset names follow one rule: `assets/<name>.glb` ships, `<name>-raw.glb` is
  the rig before the material repair, `<name>-cand-*.glb` the generator's
  output.
- Commit messages describe what changed and *why it was wrong before*.
- **Delegate complex 3D model rigging -- and asset repair when it is more than
  a one-liner.** Two workers have been measured on the same task; pick one and
  say why. `Agent` with `model: "fable"` is the default. **`codex exec -m
  gpt-6-astra` is the alternative, and it must not be started without checking
  usage first -- see below.**

  Work to delegate: fitting or refitting a deform skeleton to a generated mesh,
  diagnosing a bind pose or bone-axis problem inside a `.glb`, weight painting,
  anything driving Blender through `tools/`, and repairing a model that imports
  wrong. Give the subagent the asset paths, the relevant `tools/` script and
  `tools/skeletons/*.json`, and tell it what the engine expects
  (`docs/ANIMATION.md`, `docs/MODEL_GENERATION.md`). Judge the complexity
  yourself: a one-line config tweak is not worth a subagent, a re-rig is.
  Keep engine-side work (`src/anim`, `src/game`, the rig profiles) in the main
  session so the C++ and the asset do not get edited from two places at once,
  and scope each subagent to explicit paths -- the repo is shared.
- **Check the usage windows before starting a `codex` task: run
  `tools/codex_usage.py`.** It reads the account analytics page out of the
  logged-in Chrome through `chrome-use` and prints how much of the 5-hour and
  weekly windows is left, plus how many rigging tasks that buys:

  ```sh
  tools/codex_usage.py            # 5-hour: 8.0% remaining (resets 20:00) ...
  tools/codex_usage.py --json     # for a script
  ```

  Nothing local knows this figure -- `codex exec` prints token counts but no
  rate-limit data, nothing under `~/.codex` carries a live one, and even a
  do-nothing `codex exec` burns **7.3 k tokens** of prompt overhead, so probing
  by running something is not free. The page is the only source, and it needs
  Chrome up and signed in to ChatGPT.

  Measured on 2026-09-12, both substantive rigging tasks:

  | | tokens |
  |---|---|
  | fit a measured leg skeleton to one creature and rebuild | 93,011 |
  | measure one creature's membrane field and rebuild | 75,737 |
  | a `codex exec` that replies "OK" | 7,292 |

  Two tasks plus one aborted launch left the 5-hour window at **8% remaining**
  and the weekly at **85%**, read off the page afterwards. So a task is about
  **45% of the 5-hour window -- two per window** -- and about **7% of the
  weekly**, i.e. the 5-hour window is the binding constraint by a wide margin
  and the week is not worth worrying about. Plan around the 5-hour reset.

  The same page carries a **usage reset** (one was available, expiring in
  October) that restores a window early. Do not spend one without being asked
  to: it is a scarce manual lever, not a way around pacing.

- **Which worker.** Both produced a correct, verified fix to the same brief with
  the bone-naming contract intact, so this is about cost and behaviour rather
  than capability:
  - **gpt-6-astra**: finished in ~12 minutes and one unattended pass at 93 k
    tokens; placement was tighter and more consistent (-0.007..-0.013 against a
    +-0.016 target); stayed inside the brief, and where it went beyond it, it
    had measured the reason. Sparser in-file comments.
  - **Fable 5.1**: ~22 minutes and 207 k tokens, and it stalled twice waiting on
    its own background Blender build, needing the main session to notice.
    Deeper measurement write-up and richer in-file documentation, closer to
    house style. It also changed a constant it was not asked about
    (`wing_bind_level_deg` 35 -> 40), which moved the wingspan 8%.
  So: **Fable for work where the write-up and the in-file record matter and the
  budget is Claude's own; gpt-6-astra when the task is well-specified and
  numerically checkable, and only with usage headroom confirmed.**
  Run `codex exec` with `-s workspace-write`, never
  `--dangerously-bypass-approvals-and-sandbox`: given full access it
  immediately reached outside its `-C` directory into the main checkout.
  Give either one a **git worktree** (`git worktree add`) so two workers cannot
  write the same file, and symlink the large gitignored input assets in.

## Cross-cutting lessons

These are not about one system, and every one of them cost real time.

- **Verify without a human at the keyboard.** `--headless --frames N
  --screenshot` plus `--cam`/`--inspect` is how the wingbeat, the tail whip,
  the forests and every generated course were actually checked. Render the
  thing and look at it; do not reason about it from the code.
- **Look from the axis the motion is in.** A lateral tail wave is invisible
  from the side and from behind. Two rounds of probing were wasted on a chain
  that was numerically correct before a top-down view showed the real shape.
- **When a generated test rig and an imported asset disagree, suspect the
  frame or the scale before the physics.** The generated rig faces -Z and is
  authored in metres; the real assets face +Z at eight units per metre. Probe
  the imported asset numerically (load the .glb in a test binary), not just
  the generated one.
- **When a fix does not change behaviour, check that its values reach the code
  that runs.** A positional brace initializer silently dropped three new
  struct fields, and three commits of "fixes" were dead code.
- **Assign named fields.** `Thing{a, b}` keeps compiling as `Thing` grows, and
  quietly means something different.
- **Verify long runs, not just cold starts.** A stable-attractor bug in the
  neck only appeared minutes in; every fresh-start check missed it.
- **Batch text edits must assert what they matched.** Silent no-op replaces
  caused several regressions; prefer the Edit tool, and when scripting, assert
  the anchor count.
- **A copy pass cannot open inside a render pass.** Per-frame uploads
  (particles, grass) stage before the pass begins.
- **The tonemap ends in a gamma encode**, so anything bright desaturates
  toward white. A white flash is unreadable as damage; redden instead. A light
  source should not also be lit.
- **Initialise derived state at spawn, not on the first tick.** A sentinel
  whose position was only set by its update sat at the world origin as a live,
  shootable target.

## Third-party policy

Write what we want to learn; vendor the rest. In: renderer, scene, animation
blending and procedural animation, flight model, camera, AI, gameplay. Out
(never hand-rolled): SDL3 (window/input/GPU), Dear ImGui, Jolt (physics), cgltf
(glTF), stb_image, miniaudio.

## Layout

```
src/core/    math, input, noise, logging
src/gfx/     GPU device, pipeline cache + shader hot reload, world renderer,
             shadow map, additive particles, instanced foliage
src/anim/    skeleton, GPU skinning, glTF loader, procedural dragon rig
src/game/    flight model, chase + debug cameras, terrain, vegetation,
             course/rally, autopilot, combat, breath profiles, bots, match
             loop, studio
src/audio/   synthesized audio -- every sound generated at startup
src/editor/  ImGui integration
src/scene/   (empty) entity storage, transform hierarchy
src/phys/    (empty) Jolt integration, deferred until something needs it
shaders/     MSL, hot-reloaded from the source tree
tests/       eleven suites: math, camera, camera_rig, flight, rally, anim,
             combat, bot, match, vegetation, breath
docs/        the detail -- see the table above
```

## Status

See `docs/STATUS.md` for what is built, milestone by milestone, and for
the open questions. `docs/RETRO.md` has the ranked list of what to do next.
