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
| `docs/COMBAT.md` | Fire, targeting and aim assist, bot AI doctrine, the match loop, the hoard run |
| `docs/WORLD.md` | Terrain generation and queries, the river, the skirt past the map edge, vegetation placement and the card-built plants |
| `docs/EFFECTS.md` | Particles, the per-species breath profiles, and the fully synthesized audio |
| `docs/STATUS.md` | Milestone history, and the open questions a new session should know about |
| `docs/DIRECTION.md` | Where the modern build goes before the port: the hoard-run roguelite, the art direction and its targets in `docs/concept/`, the HUD kit, the asset policy, and the sequenced plan |
| `docs/EMBERCREST.md` | The script-built original dragon: what the Blender generator produced, why it is not the hero model, and how to rebuild it |
| `docs/MODEL_GENERATION.md` | Generating a creature: which cloud and local tools were measured and what each one is actually good for, the four-step pipeline that survived, and how to add a creature that is not a dragon |
| `tools/hunyuan_oneshot.md` | Driving the Hunyuan one-shot headlessly through chrome-use -- the generation step of that pipeline, and the traps that cost a run each |
| `tools/skeletons/*.json` | The deform skeletons the rigger fits, one per anatomy *and per set of measurements* -- a shared file shares its author's leg positions, `wing_field` gates and `jaw_mask`, and those do not transfer. Bone names are a contract with `src/anim/dragon_rig.cpp` |
| `docs/ROADMAP.md` | The original plan and its phases |
| `docs/PORTING.md` | Windows, Linux and Direct3D 9: what the code depends on today, the phases (a copyable build, one HLSL shader source, D3D12/Vulkan, then one D3D9 backend with SM3, SM2 and fixed-function tiers), and which machine tests what (Mac + CrossOver, x99, x99-windows, the G41 under XP) |
| `docs/RELEASE.md` | How the public GitHub mirror is exported (`tools/release/export_public.sh`, never a push of this repo) and what a release package carries |
| `docs/RETRO.md` | The ranked list of what to build next; its D3D9 port study is superseded by `PORTING.md` |
| `ATTRIBUTION.md` | The models, their licences, how to obtain them, and **what may ship where** (public repo, public release, personal copy) |
| `AI_DISCLOSURE.md` | Every AI tool used and what it made. The Hunyuan agreement requires generated output to be labelled when published, so a new AI-made asset gets a line here |
| `LICENSE`, `THIRD_PARTY_NOTICES.md` | MIT for our material; the libraries' licences |
| `.claude/skills/concept-art/SKILL.md` | Generating concept art, HUD mockups and creature reference sheets from a ChatGPT/Gemini subscription -- which backend suits which job, and which quota each one burns |
| `tools/codex_usage.py` | How much of the Codex 5-hour and weekly windows is left, read out of the logged-in browser. Run it before starting a `codex` task |
| `tools/rig_probes/` | Renderer-free probes that run the rig on a species and print numbers -- foot heights, sink below the floor, leg angles in the glide, leg swing through a manoeuvre, jaw direction -- and a candidate renderer. Ten milliseconds a candidate against two seconds a render: they decide what to render, they do not replace looking |

## Build & run

```sh
tools/build_shadercross.sh        # once per machine: the HLSL compiler (P1)
cmake -S . -B build -G Ninja      # first time, or after adding files
cmake --build build
./build/dragon
ctest --test-dir build            # fifteen suites, plain executables, no framework
```

Windows is cross-built here and only run on x99-windows (`docs/PORTING.md`,
P2, "Using x99 for this"). A cross-compiled suite finds the source tree
through `DRAGON_SOURCE_ROOT`:

```sh
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
  -DDRAGON_FETCH_SDL=ON -DDRAGON_SHADERCROSS=OFF -DDRAGON_DEV_ROOTS=OFF
tools/release/package_macos.sh 0.2.0      # dist/Dragon-0.2.0-macos.zip
tools/release/package_windows.sh 0.2.0    # dist/Dragon-0.2.0-windows.zip
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
| `--hide-ui` | Hide the ImGui panels AND the HUD, for world-only captures. |
| `--hide-panels` | Hide the panels but keep the HUD: a capture of what the player sees. |
| `--hide-vegetation` | Hide trees, grass, and tree shadows for unobstructed asset inspection. Terrain and gameplay remain active. |
| `--no-post` | Skip the grade and the bloom: the same frame as it was before the post stack, for a before/after. |
| `--telemetry [N]` | Log one line of flight state every N frames (default 60), plus a combat line (health, kills, bites swung/landed/taken) when combat is on. A screenshot shows a pose; this shows the state machine behind it. |
| `--cam x,y,z,tx,ty,tz` | Place the camera at a position looking at a target. |
| `--cam-mode chase\|action\|cinematic\|fp` | Pick a camera preset. |
| `--inspect [angle] [dist] [elev]` | Orbit camera locked to the dragon. Elevation 88 looks straight down -- the only view that shows a lateral tail wave. |
| `--inspect-head` | With `--inspect`, orbit the ANIMATED head instead of the body (jaw, aim). |
| `--skeleton` | Draw the posed joints as lines, to tell a rig problem from a skinning one. |
| `--bind-pose` | Freeze the rig, to check an imported asset against its own bind pose. |
| `--input p,r,y,flap,tuck,brake` | Hold a control input for the whole run. It goes through the assists, exactly as a player's stick does. |
| `--autopilot` | Hands-off. Without combat it flies the selected course; in a run or with combat on it is the demo pilot (`game::DemoPilot`), which plays through the player's own controls -- sieges guard towers, fights, lands on caches, walks in, collects, flees to heal, flies to the pass -- and rolls into the next valley or rematch after the results. Doubles as a soak test. |
| `--stage N` | Start a run already grown (0 drake, 1 young, 2 adult) at full size: how each stage's size gets onto a capture. |
| `--element NAME` | The player breathes that element (fire, frost, blight, storm, tide, stone) instead of the species' -- its look, its scales and its status (`COMBAT.md` "Elements"). Also the Combat panel's "you breathe". |
| `--status NAME` | Hold that element's status on every enemy (frost holds the freeze), for a capture of how each looks. |
| `--valley N` | Start a run N valleys down the descent (a run is three valleys; each deeper is harder). |
| `--demo` | The game playing itself: a hands-off run on a random valley, one after another. The same as P in play. |
| `--course N` | Select a generated course. |
| `--studio N` | Animation studio, scenario N: 0 glide, 1 flap, 2/3 turns, 4 s-turns, 5 dive, 6 pull-out, 7 brake, 8 attack (spit, breath, spit, bite at 7.2 s), 9 grounded, 10 melee (bite, claw, tail in turn at a close mark), 11 claw (still body, sides alternate; look from the front-below `--inspect 0 14 -25`), 12 tail (still body; look from above `--inspect 90 14 85`), 13 walk (grounded at a walking pace on the spot; the stride). |
| `--studio-speed N` | Studio playback speed, 0.05 to 2; scales both the scenario and the rig. |
| `--combat` | Arm the dragon and spawn a wave of sentinels. |
| `--training` | Arm the dragon and lay out the training room instead: six passive dummies ahead of the spawn, no return fire, back in 2.5 s. With `--attack`, the melee reach on a screenshot; `R` flies the line again. |
| `--attack` | Also hold breath, fire, boost and bite -- how flame, projectiles, boost and the lunge get onto a screenshot. |
| `--bots N` | Spawn N bot dragons instead of sentinels. |
| `--match` | Start a deathmatch from the CLI (with `--bots N`). |
| `--run [seed]` | The hoard run (`DIRECTION.md` row 3): one generated valley flown head to pass, rivals at posts and towers on the slopes along it, three hoard caches to land on, the pass gate, death ending it, results text. No seed picks one from the clock; the seed is printed and on the results screen so a good valley can be flown again. `--seed N` also sets it. |
| `--hunters-after S` | In a run, loose the first hunter after S seconds instead of two minutes, for a capture of one. |
| `--walk F[,T]` | Once grounded, walk at F (-1..1) turning at T: with `--input -0.25,0,0,0,0,0` it lands and then walks, the in-game check of the stride. |
| `--run-empty` | The run with no rivals and no towers: how the autopilot soaks the banking path (`--run-empty --autopilot --frames 8400` banks at the pass). |
| `--model PATH` | Load one rigged glTF instead of the default roster. Bench work on one creature should use it: the default roster loads seven meshes, about 3 s more per headless render (4.7 s against 1.9 s for 40 frames). |
| `--models A,B,C` | Load a whole roster. The player flies the first; bots are dealt the rest in turn, so one match fields several species. With neither flag the roster is the seven release species plus the local gold-dragon study when present, Embercrest first (`kDefaultRoster` in `app.cpp`); Sunspear and Rimeplume are reached only through `--models` while they are in progress. |
| `--bot-range N` | Spawn bots N metres out instead of 650 -- the only way to get the player and every rival into one capture. |
| `--cycle-models N` | Swap the player onto the next roster entry every N frames. Sweeps one scenario across every species in one command, and soaks the swap path (it re-initialises both rigs). |
| `--maneuver roll\|flip` | Begin that manoeuvre at frame 30, so a roll or a flip can be captured without a key press. |
| `--frame-jitter J` | Headless only: alternate the fixed step between (1+J) and (1-J) times 1/60 s. A live window's frames are uneven, and anything reading one frame's state from another twitches only then -- this is how the first-person camera trailing the head by a frame was reproduced. |
| `--hue r,g,b,strength` | Recolour the player's hide (the same recolour the bots use). |

Soaks that have caught real bugs:

```sh
./build/dragon --headless --autopilot --course 1 --frames 4000   # fly a course
./build/dragon --headless --bots 3 --match --frames 7200         # 2-minute fight
```

The elemental roster is the default, so a multi-species capture needs only
the two flags that make it possible at all -- at the default 650 m spawn the
rivals are specks:

```sh
./build/dragon --bots 6 --bot-range 95 --combat
```

Panels worth knowing: **Dragon** has the rig and the hide colour, **Flight**
has the tuning presets, `heft` and the aerobatics dials, **Terrain > Vegetation** has the trees and
grass, **Combat** has the difficulty dials and the bots, **Studio** drives the
animation scenarios and picks which creature is on the stand. Every panel except Combat starts collapsed.

## Controls

| Input | Action |
|---|---|
| W/S | Pitch (W = nose up) |
| A/D | Roll |
| Q/E | Rudder (keyboard only: the d-pad pair went to the roll dodge; turn coordination yaws into a bank on its own) |
| Gamepad left stick | Pitch and roll (absolute, best feel of the three) |
| Gamepad A / triggers | Flap / tuck-dive (RT) and brake (LT) |
| T / right-stick click | Relock onto the next target |
| Mouse | Optional, off by default -- it has to accumulate to work, and that accumulation is what makes it hard to control |
| Space | Flap -- the only way energy enters the system |
| Shift | Tuck wings and dive |
| Ctrl | Flare and brake |
| W/S, A/D on the ground | Walk forward and back, turn in place (the gamepad stick pushed away is forward). Space leaps back into the air |
| R | Restart the run (in a hoard run: the same valley again, from the head) |
| Right-drag / right stick | Free look -- orbit the view without steering. Stick Y is inverted by default |
| M | Cycle the player onto the next model in the roster (`--models`). The studio scenario keeps playing, so this is how two species are compared under one manoeuvre |
| V | First person, from just above and behind the animated head. The eye is composed from the head's measured mesh so every species frames like `dragon.glb` (horn tips at the bottom of the frame); the two composition dials are in the Camera panel, and a species can nudge the result from its rig profile |
| 1 / 2 / 3 | Camera preset: chase, action, cinematic |
| Tab | Toggle free-fly survey camera (detaches where the chase camera is) |
| F1 | Hide every ImGui panel (the HUD stays) |
| F / left mouse | Fire breath (hold) -- gamepad LB |
| G | Fireball -- gamepad RB |
| C | Bite -- gamepad B. Melee: a bite in a wide cone ahead of the mouth within 30 m, a claw or tail strike on anything within 16 m of the body. No aim needed; a hit stuns and knocks the target, hits chain to x1.7 |
| X | Boost -- gamepad X |
| Z | Aileron roll, the way the stick is held (right by default) -- gamepad d-pad left/right, which is also the direction. A dodge: a sideways kick and a push through the first half of the roll, about ten metres |
| B | Flip: a half loop and a roll-out that reverses the heading, to face a chaser -- gamepad d-pad up. Refused below 26 m/s |
| Hold G | Charged shot (a run's young stage on; always in the arena): let go or wait for full |
| U | Swap to the second breath and back (adult on; in the arena it walks all six elements) -- gamepad Y |
| H | Release the fury when its pip is full (ancient on) -- gamepad left-stick click |
| Enter | Rematch from the arena results; in a hoard run that has ended, a new valley |
| P | Hands-off: the demo pilot takes (or gives back) the controls; the HUD says what it is doing |
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
- **Rendering**: the world renders LINEAR into a 16-bit `scene_hdr` target
  (the pipeline cache's default colour format); `gfx::PostProcess` then blooms
  it at half resolution, tonemaps and grades it into the 8-bit `scene_color`
  target, the UI draws onto that, and it blits to the swapchain. World shaders
  end in `scene_out()` and never tonemap themselves -- the tonemap and the
  gamma live in `post_composite.hlsl`, after the bloom. Only the composite and
  Dear ImGui declare `scene_color_format()`. The dials are under Grade &
  bloom in the Engine panel.
- **UI**: Dear ImGui draws in its own pass (`begin_ui_pass`) because its
  pipelines declare no depth attachment, and a pipeline can only be bound in a
  pass whose attachments match it.
- **The HUD is one kit.** Every playing readout goes through `ui::Hud`
  (`src/ui/hud.h`): its tokens (one gold accent, red for threat and damage
  only, off-white on charcoal plates), its primitives (plate, bar, arc,
  bracket, pip, edge arrow) and its two faces (a condensed display face for
  numerals, a humanist sans for labels: Barlow Condensed and Fira Sans, OFL,
  shipped in `assets/fonts/`, with the macOS system faces and then ImGui's
  default as the fallbacks). Layout is in frame units, 1/720 of the
  height, so it survives any aspect -- laid out in ImGui's `DisplaySize`
  (points), with `ImGuiLayer::begin_frame` setting the framebuffer scale from
  the real render target: taken from the window's pixel density, it drew
  every readout and panel at double size, cropped. The panels wear the same theme and dock
  along the right edge, never over the centre. Do not draw a readout with raw
  `AddText` and pixel constants again -- that is what read as debug output
  with gold paint.
- **GPU calls go through `rhi::Device`** (`src/rhi/rhi.h`), never SDL's GPU
  API directly. `gfx::Device::rhi()` reaches it. Only the backend
  (`src/rhi/sdlgpu/`) and Dear ImGui's renderer glue
  (`editor/imgui_layer.cpp`) include `SDL_gpu.h`. That is what lets a
  D3D9 backend be added without touching the renderers
  (`docs/PORTING.md`, R1). Per-frame buffer data goes through
  `map_upload`/`commit_upload` before the pass that draws it.
- **Paths go through `core/paths.h`**, never a literal root. Shipped data is
  `paths::asset("props/x.glb")`, found beside the executable first (a
  package) and in the source tree second (a dev build, `DRAGON_DEV_ROOTS`).
  Anything the game writes goes to `paths::user(...)`, the per-user
  directory (`~/Library/Application Support/Paleshell/Dragon`), and is read
  back with `paths::user_or_asset(...)`. So best times, run records and
  saved tuning are no longer in `assets/`. A path written into the source
  tree works on this machine only; it is what P0 removed (`PORTING.md`).
- **Shaders are HLSL** in `shaders/`, one file per pipeline with `vs_main` and
  `fs_main`, compiled at runtime through SDL_shadercross (DXC to SPIR-V, then
  to MSL on Metal) and hot-reloaded on save (polled every 0.25s). A failed
  compile logs `file:line` and keeps the last working pipeline, so a bad save
  never blanks the screen. `gfx::PipelineCache` inlines `#include`s itself,
  with `#line` markers, and watches every included file. Each file is
  compiled twice, with `VERTEX_STAGE` and then `FRAGMENT_STAGE` defined;
  declare a stage-only resource under its guard. Bindings go through the
  macros in `common.hlsl` (`UNIFORM_SLOT(n)`, `TEXTURE2D(name, n)`), which
  put each resource in the register space SDL expects for that stage, and
  the resource counts come from reflection, so `PipelineDesc` carries only
  the stem. Uniform blocks are float4 and float4x4 only, which is what keeps
  the C++ structs and HLSL's constant-buffer packing byte-identical. A
  development build needs shadercross installed (`tools/build_shadercross.sh`);
  a package bakes the MSL ahead of time (`tools/release/bake_shaders.sh`).
  `tools/golden/golden.py compare` is the gate for any shader or backend
  change: eight scenes, deterministic in headless, against the backend's
  own set in `tests/golden/{metal,vulkan,d3d12}`, plus `--set metal --loose`
  across backends (GPUs filter differently; a wrong frame still fails it).
- **Lighting is one path.** Every world shader lights through `direct_sun`,
  `ambient_light` and `apply_fog` in `scene_common.hlsl` (plants add
  `translucent_sun`). Do not add a per-shader wrap or ambient: the seam
  between the trees and the ground was exactly that. World colours live in
  `gfx/palette.h` and reach shaders as uniforms; plant meshes store a palette
  index, a brightness and a MATERIAL tag (`FoliageMaterial`: plain, leaf
  card, needle card, bark, plus 10 for a detail card the distance LOD drops)
  in their vertex colour, not an RGB. Crowns are alpha-tested cards cut from
  `assets/textures/leaf_cluster.png` and `needle_spray.png`, grey detail maps
  rendered in Blender and coloured by the palette; the coarse cards of a
  crown are its impostor, so a far tree is the same mesh with its detail
  cards discarded.
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
     `--cycle-models`), switch onto it from the default player (Embercrest; from
     Rimefang when the change is Embercrest's) and look once more.
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
  a one-liner -- to `codex exec -m gpt-6-astra`, and never start it without
  checking usage first -- see below.** Do not delegate to Fable 5.1 any more
  (the user's call, 2026-09-26); when Codex has no headroom, do the work in
  the main session or wait for the reset.

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
  | build two verified props in Blender (full access, medium), 2026-09-25 | 62,201 |
  | four second-generation props (two towers, two caches), 2026-09-25 | 85,773 |
  | decimate and animate a prey creature (three baked clips), 2026-09-25 | 80,127 |
  | improve the demo pilot, measured on eight 12-minute headless runs per pass, 2026-09-26 | 325,999 -- the whole 5-hour window from 100%; it hit the limit before committing |
  | measure one creature's membrane field and rebuild | 75,737 |
  | a `codex exec` that replies "OK" | 7,292 |

  Two tasks plus one aborted launch left the 5-hour window at **8% remaining**
  and the weekly at **85%**, read off the page afterwards. So a task is about
  **45% of the 5-hour window -- two per window** (for an asset task; a
  gameplay task that measures itself with long headless runs and iterates
  cost three to four times that -- give it a pass budget in the brief and
  have it commit after each pass) -- and about **7% of the
  weekly**, i.e. the 5-hour window is the binding constraint by a wide margin
  and the week is not worth worrying about. Plan around the 5-hour reset.

  The same page carries a **usage reset** (one was available, expiring in
  October) that restores a window early. Do not spend one without being asked
  to: it is a scarce manual lever, not a way around pacing.

- **Codex is for 3D, asset and animation work only** (the user's call,
  2026-09-26): its cache time and usage limits are tight, and a gameplay
  task that iterates on long headless measurements ate a whole window.
  Gameplay, AI and tuning stay in the main session.
- **Keep headless soaks light**: at most two at a time; a `--demo` soak
  rolls through many runs on its own, so it usually replaces a seed sweep.
- **Which worker (history).** gpt-6-astra is now the only delegate; this is
  the comparison that was measured when Fable 5.1 was still used. Both
  produced a correct, verified fix to the same brief with the bone-naming
  contract intact:
  - **gpt-6-astra**: finished in ~12 minutes and one unattended pass at 93 k
    tokens; placement was tighter and more consistent (-0.007..-0.013 against a
    +-0.016 target); stayed inside the brief, and where it went beyond it, it
    had measured the reason. Sparser in-file comments.
  - **Fable 5.1**: ~22 minutes and 207 k tokens, and it stalled twice waiting on
    its own background Blender build, needing the main session to notice.
    Deeper measurement write-up and richer in-file documentation, closer to
    house style. It also changed a constant it was not asked about
    (`wing_bind_level_deg` 35 -> 40), which moved the wingspan 8%.
  So: **gpt-6-astra, for well-specified, numerically checkable 3D, asset and
  animation work, only with usage headroom confirmed** -- and brief it to
  write the in-file record it tends to skip.
  Run `codex exec -m gpt-6-astra -c model_reasoning_effort=medium -s
  danger-full-access` -- the user's call on 2026-09-25: the default
  auto/workspace-write mode drained usage about twice as fast. Full access
  once reached outside its `-C` directory into the main checkout, so contain
  it: a **git worktree** (`git worktree add`) of its own, a brief that names
  the only paths it may write, stdin from nowhere (it waits on stdin
  otherwise), and a `git status` of the main checkout after it finishes.
  Symlink the large gitignored input assets in. The props task
  (`tools/props.md`) ran this way: 62 k tokens, stayed inside its worktree,
  verified its own output, committed on its branch.

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
- **The tonemap preserves hue** (`hue_preserve` in the composite): a bright
  colour keeps its ratios and is scaled back by its peak channel instead of
  clipping channel by channel, so fire is orange and frost is blue however
  many puffs stack. Before it, per-channel Reinhard sent everything bright to
  white and every emissive colour had to be tuned around that. A light source
  should still not also be lit, and a damage flash still reddens rather than
  brightens -- it is on the HUD, past the tonemap.
- **Initialise derived state at spawn, not on the first tick.** A sentinel
  whose position was only set by its update sat at the world origin as a live,
  shootable target.

## Third-party policy

Write what we want to learn; vendor the rest. In: renderer, scene, animation
blending and procedural animation, flight model, camera, AI, gameplay. Out
(never hand-rolled): SDL3 (window/input/GPU), SDL_shadercross (the shader
compiler; dev builds only), Dear ImGui, Jolt (physics), cgltf (glTF),
stb_image, miniaudio. SDL and SDL_shadercross refuse AI-written
contributions: never file an issue or a PR against them from a session.

## Layout

```
src/core/    math, input, noise, logging, paths (where data and user files live)
src/rhi/     the render hardware interface, and its SDL GPU backend (sdlgpu/)
src/gfx/     GPU device, pipeline cache + shader hot reload, world renderer,
             shadow map, additive particles, instanced foliage
src/anim/    skeleton, GPU skinning, glTF loader, procedural dragon rig
src/game/    flight model, chase + debug cameras, terrain, vegetation,
             course/rally, autopilot, combat, breath profiles, elements and
             status effects, bots, match loop, the hoard run, prey herds, the
             demo pilot, studio
src/audio/   synthesized audio -- every sound generated at startup
src/editor/  ImGui integration: context, the two HUD faces, the panel theme
src/ui/      the HUD kit -- tokens, primitives, text -- every readout draws through it
src/scene/   (empty) entity storage, transform hierarchy
src/phys/    (empty) Jolt integration, deferred until something needs it
shaders/     HLSL, hot-reloaded from the source tree
tests/       fifteen suites: math, camera, camera_rig, flight, rally, anim, hoard_run, demo_pilot, element, prey,
             combat, bot, match, vegetation, breath
docs/        the detail -- see the table above
```

## Status

See `docs/STATUS.md` for what is built, milestone by milestone, and for
the open questions. `docs/RETRO.md` has the ranked list of what to do next.
