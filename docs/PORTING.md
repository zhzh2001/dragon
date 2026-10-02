# Porting roadmap: Windows, Linux, and Direct3D 9

Where the game goes after macOS, in order, and where each step is tested.
This supersedes the port study in `RETRO.md`. That study was written before
the post stack, the hoard run and the roster existed, and two of its premises
no longer hold: "no HDR target, no post chain", and "GL 2.1 first, because it
debugs on the Mac". A D3D9 build now debugs on the Mac too, under CrossOver
(see [Where it is tested](#where-it-is-tested)).

The short answer to "Windows and Linux shouldn't be hard, right?": **yes, on
modern hardware.** SDL3's GPU API already has Direct3D 12 and Vulkan backends,
the game uses nothing exotic from it, and every system above the renderer is
plain C++. The real work is translating the shaders, plus one packaging fix
that every platform needs, the Mac included. **Direct3D 9 is a different
size of project**, roughly as big as everything before it put together,
because SDL3 GPU has no D3D9 backend and fixed function has no shaders at all.
It is laid out here as one D3D9 backend with three capability tiers.

## What the code depends on today

Measured on 2026-10-01, at `32ac0b0`:

| Dependency | Where | Port cost |
|---|---|---|
| **MSL is the only shader format requested** | `src/gfx/device.cpp:22`, `src/gfx/pipeline.cpp:96`. 18 files in `shaders/`, 1,230 lines (terrain 169, skinned 159, foliage 154, `scene_common` 205) | The main item for Win/Linux |
| **Asset and shader roots are compiled in** as absolute source-tree paths (`ASSET_ROOT`, `SHADER_ROOT`, `src/app.cpp:207` and on) | A copied binary cannot find its data, on any OS | Blocks distributing copies at all, Mac included |
| **Runtime writes go into `assets/`**: `best_times.txt`, `runs.txt`, `course.txt`, `flight_tuning.cfg` | `src/app.cpp:335`, `:1735`, `:2509`, `:4419`, `:6435` | Must move to `SDL_GetPrefPath` |
| **HUD faces are macOS system fonts** (DIN Condensed, Avenir Next, Gill Sans, Futura, Helvetica) | `src/editor/imgui_layer.cpp:36-41`; ImGui's default is the fallback | Apple's fonts cannot be bundled; ship two OFL faces |
| SDL3 from Homebrew | `CMakeLists.txt:12` | Fetch it like the other dependencies |
| `--headless` still creates and claims a hidden window | `src/gfx/device.cpp:15`, `:25` | Linux over SSH has no display; skip the window when headless |
| SDL GPU calls in 13 files, about 570 call sites, 14 of them in `app.cpp` and 26 in `anim/skinned_mesh.cpp` | `src/gfx/` is 4,362 lines | Irrelevant for Win/Linux; this is the D3D9 extraction surface |
| GPU features used | No compute, no storage buffers; one instanced vertex stream (foliage); RGBA16F scene target, RGBA8 and sRGB textures, D32F/D24/D16 depth | Everything is in D3D12/Vulkan. Only the RGBA16F target and instancing matter for D3D9 |
| Simulation | Flight, rig, AI, terrain queries, particles, audio synthesis: plain C++20, renderer-free, and the 15 test suites already prove it | None |
| Dev tools (`sips`, `chrome-use`, Blender scripts, `tools/*.py`) | Not shipped | None; the docs should say they are macOS-side |

## Phase P0 — a build that runs on someone else's machine

This comes first because the release needs it, before any port.

1. **Find data relative to the executable.** Use `SDL_GetBasePath()`, with
   `ASSET_ROOT`/`SHADER_ROOT` kept only as a development override, so that a
   build in `build/` still hot-reloads from the source tree. The CMake
   `install` target copies `assets/` and `shaders/`, or the compiled shader
   blobs after P1, next to the binary.
2. **Write records and tuning to `SDL_GetPrefPath("dragon", "dragon")`.**
   Shipped tuning stays read-only in `assets/`, and a saved file in the pref
   directory overrides it.
3. **Bundle fonts.** Ship two OFL faces, a condensed display face for the
   numerals and a humanist sans for the labels, in `assets/fonts/`, so the
   HUD looks the same on every OS. The macOS system faces stay as a dev
   fallback only. The HUD kit's tokens do not change.
4. **Package.**
   - **macOS:** a `.app` bundle with `SDL3.framework` inside. A downloaded,
     unsigned app gets a Gatekeeper warning. Ad-hoc signing plus "right-click,
     Open" is fine for friends; notarisation needs a paid Apple Developer ID,
     which is the user's decision.
   - **Windows:** a zip with `SDL3.dll`.
   - **Linux:** a tarball. An AppImage is optional.

   Each package carries `LICENSE`, `THIRD_PARTY_NOTICES.md` and
   `AI_DISCLOSURE.md`.
5. **Headless without a window.** Do not create or claim a window when
   `--headless` is set; the offscreen targets already carry the frame. This
   makes headless renders work over SSH on Linux and keeps the
   verification workflow intact everywhere.

Exit test: a release zip unpacked into `/tmp` on a second Mac account runs,
saves a best time, and `--headless --frames 40 --screenshot` works from it.

## Phase P1 — one shader source for every backend

**Author in HLSL** and compile it with
[SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross) to SPIR-V
(Vulkan), DXIL (D3D12) and MSL (Metal). HLSL wins over GLSL or Slang here for
one reason: **it is also the D3D9 shader language**. The SM3 and SM2 twins in
R3 and R4 are then edits of the same files under `#if` tiers, not a fourth
dialect.

- **Translate by hand, file by file**, starting with `scene_common`. The
  shaders are small, and the lighting-is-one-path rule (`direct_sun`,
  `ambient_light`, `apply_fog`) means one shared file carries most of the
  meaning.
- **Hot reload survives.** Development builds link shadercross and compile at
  runtime, as `PipelineCache` does with MSL today, include inlining and all.
  Release builds load precompiled blobs per backend.
- **The gate is golden screenshots.** Before the first translation, capture
  a fixed set on Metal: the valley, a skinned dragon close up, foliage,
  water, bloom on fire, the HUD. Write the frames through the existing
  `--screenshot` with `--cam`, then diff each one against Metal-from-HLSL.
  This set becomes the regression suite for every later backend, so it is
  worth building carefully once (`tests/golden/`, a small diff tool that
  prints the worst tile).

Exit test: Metal from HLSL matches the MSL goldens within a tonemapped
tolerance, and the MSL sources are deleted.

## Phase P2 — Windows 10/11 and Linux on modern GPUs

After P1 this is mostly build plumbing:

- Request `SPIRV | DXIL | MSL` and let SDL pick: Direct3D 12 on Windows,
  Vulkan on Linux, Metal on the Mac. `--gpu-driver vulkan` forces a backend,
  so D3D12 and Vulkan can both be checked on one Windows machine.
- Build Windows with MSVC or clang-cl, or with the MinGW-w64 cross-compiler
  already installed on the Mac (see R6). Build Linux with GCC or Clang. Fetch
  SDL3 with CMake on all three.
- **CI on GitHub Actions:** build and `ctest` on macOS, Windows and Ubuntu on
  every push. The test suites are renderer-free, so they run on CI runners
  with no GPU. Rendering checks stay on the real machines below.
- Gamepad, mouse capture and HiDPI go through SDL already. Check the
  framebuffer-scale logic in `ImGuiLayer::begin_frame` on a Windows 150%
  display, the same bug class that once drew every panel at double size.

Exit test: the goldens match on D3D12 and Vulkan; a hoard run and a
`--demo` soak complete on both Windows and Linux.

## The retro track: one D3D9 backend, three tiers

The target is Direct3D 9 on Windows XP, with **three capability tiers in one
backend**: SM3, SM2 and fixed function. D3D9 drives all three, since its
fixed-function pipeline runs on DX7-class hardware through the same runtime.
So one platform layer, one device wrapper, one texture and buffer path, and
the tiers differ only in how a material is drawn. The tier is chosen from
`D3DCAPS9` at start-up and can be forced with `--tier sm3|sm2|ff`, which is
what lets one modern Windows machine exercise all three.

### What each tier does with each feature

| Feature | Modern (P2) | SM3 (vs/ps_3_0) | SM2 (vs/ps_2_0, 2_a, 2_b) | Fixed function |
|---|---|---|---|---|
| Colour pipeline | RGBA16F, bloom, hue-preserving tonemap | RGBA16F where supported, else RGBA8 with the tonemap in each shader; bloom optional | RGBA8, tonemap folded into the material, no bloom | RGBA8; the "tonemap" is a lighting-range clamp and the palette is pre-graded |
| Terrain | analytic noise per fragment | **noise baked to tiling textures** (patch/fbm map, detail normal) | baked textures, fewer layers, detail normal dropped at 2_0 | two-stage multitexture: base x detail, vertex-lit |
| Lighting (`direct_sun`, ambient, fog) | per pixel | per pixel | per vertex sun and ambient, per pixel albedo | D3D9 lights, vertex or table fog |
| Shadows | directional map, LESS compare | the same map, 1024², hardware PCF where the card has it | one cascade, 512², or blob shadows | blob shadows under creatures; terrain shadows baked into vertex colour |
| Skinning | 256-joint uniform palette | **palette split**, at most 60 bones per batch, done at load | the same split, at most about 50 bones in `vs_2_0`'s 256 constants | **CPU skinning** (DX7-era hardware blends 2 to 4 matrices at most) |
| Foliage | instanced cards, alpha test | stream-frequency instancing | pre-batched static chunks, alpha test | pre-batched chunks, alpha test, fewer cards (the distance LOD's coarse set) |
| Water | fresnel plus analytic sky reflection | the same | fresnel only | an alpha-blended tinted plane with an environment texture |
| Particles, HUD, ImGui | as now | the same; ImGui has a D3D9 backend | the same | the same |
| Depth | reversed-Z, D32F | **standard Z, D24**, near plane 2 m in flight, camera-relative positions | the same | the same |
| Creature meshes | 80K tris, 4096² PBR | an LOD at about 20K tris, DXT, 1024² | about 10K, 512², ORM dropped | about 6K, 512² base colour only |

The rows that are work on every tier are the depth convention, the content
LODs and the texture budget. That is why R2 exists separately from the
backends.

### Phases

| Phase | Work | Exit test |
|---|---|---|
| **R0** spike | A cross-compiled D3D9 window, one textured and fogged triangle, plus miniaudio and a gamepad, built on the Mac with `i686-w64-mingw32-g++` (GCC 16.2, msvcrt CRT, posix threads, already installed) | It runs in CrossOver, on x99-windows and on the G41 under XP. This settles the toolchain and the XP platform-layer questions before any refactor depends on them |
| **R1** RHI | Extract the roughly 20-entry-point interface (device and swapchain, offscreen target, pipeline state, static and dynamic buffers, texture plus sampler, uniform push, pass begin/end, draw, readback). The RHI owns the depth convention. `app.cpp` and `skinned_mesh` stop calling SDL GPU directly | Metal, D3D12 and Vulkan all match the goldens. Pure refactor |
| **R2** content tiers | Bake the terrain noise; DXT compression and per-tier texture caps at import; creature LODs (a `tools/` decimation step, rigged weights preserved); palette splitting; a standard-Z path behind the RHI; an LDR fallback in the post stack | The modern build looks unchanged at normal distances, and `--tier` changes only what it should |
| **R3** D3D9 SM3 | The backend plus HLSL `vs_3_0`/`ps_3_0` builds of the P1 shaders, compiled offline with `fxc` (or `d3dcompiler_47`), which still target SM3 | Goldens within tolerance on x99-windows; a hoard run on the G41 with the GeForce 6200 TC |
| **R4** SM2 | The `#if` tier cuts above, for `ps_2_0`'s 64 arithmetic instructions; one shader per material permutation instead of uniform branches | The Radeon X550 on the G41 runs a valley at 1024×768 |
| **R5** fixed function | Materials as texture-stage state, CPU skinning, blob shadows, vertex fog | The M6 (in the G41 today) and the GeForce4 MX PCI fly a valley |
| **R6** XP platform layer | SDL3 does not support XP. Either a thin Win32 layer (window, raw input, XInput 9.1.0, timers) behind the existing input abstraction, or an XP-capable SDL2. R0 decides which; whatever is chosen, verify that the CRT and winpthreads in the build import nothing newer than XP | It installs and runs on a clean XP SP3 |
| **R7** retro presets | 4:3, 640×480 to 1024×768, particle caps, fewer bots and less grass, a "2005 mode" look on the modern build too | 30 fps on the SM3 floor card at 1024×768 |
| R8 stretch: Windows 98 SE | The D3D9.0c runtime supports 98 SE, but no maintained C++20 toolchain targets 9x. It needs a dedicated CRT/import audit, or a C++ subset for the retro executable. Decide only after R6 | It boots into a valley on 98 SE |

R0 is cheap and is the right first move: in one session it settles
whether the Mac-hosted MinGW toolchain produces XP binaries, and that
question decides how R6 is built. R1 is the load-bearing refactor and is
worth doing even if the retro track stalls, because it is also what makes
the golden-screenshot gate backend-agnostic.

### Budgets to design to

From the cards on hand (`~/src/gpu-hist/data/cards.csv`) that fit the G41's
PCIe x16 and PCI slots. The G41 has no AGP.

| Tier | Floor card on hand | Also on hand | Notes |
|---|---|---|---|
| SM3 | GeForce 6200 TC (NV44, PCIe, works) | Radeon X1300 (RV515, untested), Quadro FX 3500 (G71, works) as the fast SM3 reference | TurboCache: little real VRAM, so the texture budget is the binding one |
| SM2 | Radeon X550 (RV370, PCIe, works) | FireMV 2200 PCI (RV380 unconfirmed, untested); GeForce FX 5200s, if any is the PCI variant | The FX's FP32 is slow; prefer `half` where precision allows |
| Fixed function | Mobility Radeon M6 on PCI, in the G41 now | GeForce4 MX PCI (NV18, in x99 for qemu-gpu); Radeon 7000; Riva TNT2 Vanta (DX6, no T&L) | The TNT2 is below the floor: no hardware T&L. Treat it as a "does it start" check only |

The verified specs are in gpu-hist, not here; the shader-model column is the
working assumption for planning. The G41's Celeron E3300 (two cores,
3.3 GHz) is the CPU budget, and the CPU, not the GPU, is the likelier wall
for the bots, the rig and per-frame grass placement. Profile it on the G41
early (R3), not at R7.

## Where it is tested

The Mac stays the daily driver. Each other machine has one job, chosen so the
slow-to-reach machines are only used for what nothing else can show.

| Platform | Role | Why it, and not another |
|---|---|---|
| **Mac, Metal** | Daily development; the reference goldens | Hot reload, the whole headless workflow, Blender and the tools |
| **Mac, MinGW-w64 + CrossOver** | Build every Windows binary (modern and XP) on the Mac; smoke-run D3D9 builds in a CrossOver bottle | No machine to power on. **Not a correctness oracle:** Wine's wined3d translates D3D9 to OpenGL and implements fixed function itself, so it can only answer "does it build, start and roughly draw". Its caps are generic, so `--tier` must be forced |
| **x99, CachyOS** | Vulkan; the Linux release; Linux headless soaks over SSH once P0 removes the window | The only Linux box with a real modern GPU. Power it off after each session |
| **x99-windows, Windows 10** | D3D12 and Vulkan on Windows; **the D3D9 debugging platform for all three tiers** (`--tier` forced on a modern driver); apitrace for D3D9 call traces and replays | A real D3D9 driver and runtime, a debugger, and fast iteration. Modern drivers still run D3D9 fixed function. Not period hardware, so it proves the code, not the budget |
| **G41, Windows XP, driven by `agent.ahk`** | **Acceptance for each retro tier**, on period cards: SM3, SM2 and FF from the table above | The only place budgets, driver quirks and the XP platform layer are real. The qemu-gpu harness already copies files over SMB, runs jobs and takes OBS captures |
| G41, Windows 98 SE | R8 only | |
| qemu-gpu's emulated M6 and NV18 | Later, and in both directions: the FF tier as a workload for the emulator, and the emulator as a way to replay a failing FF frame | Not a dependency of this roadmap. Worth doing once R5 runs on the real M6 |

### The G41 loop

`--screenshot` already exists, so the G41 is driven exactly like a headless
run, with a game job alongside `hl2-timedemo.ahk`:

1. The Mac cross-builds and copies the build plus the job into the G41's
   `bench` share (`mount_smbfs` over the direct cable, as in qemu-gpu's
   `docs/G41_TESTBENCH.md`).
2. `tools/g41/g41job.sh` submits a job that runs
   `dragon.exe --tier sm2 --frames 600 --screenshot results\x550.bmp --telemetry 60`
   and copies its log out.
3. The Mac pulls the BMP, the log and the fps line back, then diffs the
   frame against the tier's golden. The D3D9 screenshot path is a
   `GetRenderTargetData` readback, so it is the game's own frame and not the
   VGA capture, which qemu-gpu notes is not a pixel oracle.

The game job and a G41-side README belong in this repo, under `tools/g41/`.
The agent itself stays in qemu-gpu, where it is maintained.

## Open questions

- **Does the Mac's MinGW toolchain produce XP-clean binaries?** GCC 16 with
  posix threads pulls in winpthreads, and recent CRT and winpthreads
  builds may import Vista-only functions. R0 answers this by running on XP,
  not by reading.
- **Is the CPU the wall on the E3300?** The rig on 232-joint skeletons, seven
  bots and per-frame grass placement were all sized on an M-series Mac.
- **The 32-bit address space** on XP: a full roster at 4096² does not fit,
  so the retro build loads only the tier's textures. That is another reason
  R2 is separate from the backends.
- **Fixed-function look.** Whether it reads as "the game, earlier", or only
  as "the game, broken", is a judgement to make on a render at R5 against the
  concept targets, not something to settle in advance.
