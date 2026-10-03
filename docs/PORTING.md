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

## Phase P0 — a build that runs on someone else's machine (done, 2026-10-01)

Done for macOS. `tools/release/package_macos.sh` makes
`dist/Dragon-<version>-macos.zip` (144 MB at 0.1.0). Unpacked anywhere, it
runs, renders and saves.

1. **Data beside the executable** (`src/core/paths.h`). Assets and shaders
   are looked up in `SDL_GetBasePath()` first, which in a bundle is
   `Contents/Resources`, then in the source tree. That fallback is
   compiled in only when `DRAGON_DEV_ROOTS` is on, which it is by default.
   A package is built with it off, so the binary holds no build-machine
   path; the script fails if `strings` finds `$HOME` in it. Hot reload
   works on whichever shader root was found.
2. **Writes go to `SDL_GetPrefPath("Paleshell", "Dragon")`**: the records,
   the saved flight tuning, the saved course. A shipped file is read until
   a user copy exists (`user_or_asset`). A development build's old
   `assets/best_times.txt` and `runs.txt` are therefore still read until
   the first save.
3. **Two OFL faces ship** in `assets/fonts/`: Barlow Condensed SemiBold for
   numerals, Fira Sans Medium for labels. The macOS system faces are the
   fallback. Rendered side by side with the old faces, the numerals sit
   slightly lower in their plate and the hint line is about 10% wider.
4. **Package.** `DRAGON_FETCH_SDL` builds SDL3 3.4.16 from source and links
   it statically. Homebrew's SDL is built for the machine's own macOS, 26
   here, and the binary defaulted to 27. The package targets **macOS 11,
   universal (arm64 + x86_64)**. The bundle is ad-hoc signed, not notarised,
   so Gatekeeper rejects it on first launch. Its `README.txt` gives the
   "Open Anyway" and `xattr` routes. The roster's textures are shrunk to
   2048² by `tools/release/shrink_glb.py`, which copies every non-image
   byte through and keeps each PNG's text chunks. That takes each species
   from about 52 MB to 21 MB, and a packaged frame differs from the
   development build's only in texture filtering on the dragon (no pixel
   off by more than 16 of 255). A package launched with no arguments
   starts a hoard run: a double-click passes none.
5. **Headless no longer claims the swapchain**; the frame is read from the
   offscreen targets. It still creates a hidden window, though. SDL's
   `offscreen` video driver fails creating a window without OpenGL, and
   `dummy` leaves SDL GPU with no backend. A Linux render over SSH still
   needs a display, and which driver to use there moves to P2.

Exit test, as run: the zip unpacked into a temporary directory, with no
source-tree fallback compiled in, rendered a headless frame. A three-valley
autopilot run from it banked and wrote `runs.txt` to the user directory.
Still to do by hand: double-click it, which needs a human at the keyboard;
the Windows and Linux packages (P2).

## Phase P1 — one shader source for every backend (done, 2026-10-01)

**The shaders are HLSL**, compiled by
[SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross): DXC to
SPIR-V, then SPIRV-Cross to whatever the device takes. HLSL won over GLSL or
Slang for one reason: **it is also the D3D9 shader language**, so the SM3
and SM2 twins in R3 and R4 are edits of the same files under `#if` tiers,
not a fourth dialect.

- **The translation is 1:1**: 18 MSL files became 19 HLSL ones, the extra
  one being `common.hlsl`. The uniform blocks were already float4 and
  float4x4 only, so `SceneUniforms`, `ModelUniforms` and the rest keep their
  C++ layout byte for byte under HLSL packing. Matrix `*` became `mul()`.
  Column-major storage with `mul(M, v)` means the same as Metal's `M * v`.
- **One file, two compiles.** Each shader is compiled with `VERTEX_STAGE`
  and then `FRAGMENT_STAGE` defined. `common.hlsl` maps `UNIFORM_SLOT(n)`,
  `TEXTURE2D(name, n)` and `DEPTH2D` onto the register spaces SDL assigns
  each stage (space0/1 for vertex, space2/3 for fragment). Stage-only
  resources sit under their guard rather than relying on shadercross's
  binding culling, which its own help warns can shift slots.
- **Resource counts come from reflection.** `PipelineDesc` lost its
  hand-kept `vs_uniform_buffers`, `fs_samplers` and the rest, along with
  the entry-point names; it names a stem now. Every count the descriptors
  carried matched what reflection reports.
- **The shadow map** is a `Texture2D<float>` read with `SampleLevel` and
  compared in the shader, as before. SPIRV-Cross emits it as
  `texture2d<float>`, not `depth2d`, and Metal samples a D32F texture
  through it with identical results.
- **Hot reload survives.** `PipelineCache` still inlines includes itself,
  so it can watch every file, and now writes `#line` markers, so a broken
  save reports `scene_common.hlsl:212`, not a line of the flattened whole.
  Tested: a junk line in `scene_common.hlsl` failed all ten pipelines that
  include it, kept the last working ones on screen, and the fix reloaded
  them.
- **Development builds compile at runtime**: `DRAGON_SHADERCROSS`, on by
  default, links the shadercross dylib, which loads DXC.
  `tools/build_shadercross.sh` builds it, DXC included, pinned to the
  commit this was verified on (`1ff05be`), into `~/.local/opt/shadercross`.
  Homebrew has no DXC. **A package bakes instead**:
  `tools/release/bake_shaders.sh` runs the shadercross CLI to write
  `<stem>.<stage>.msl` plus its reflection `.json`, the package is built
  with `DRAGON_SHADERCROSS=OFF`, and the binary links only system
  frameworks.
- **The gate**, `tools/golden/golden.py`, is eight headless scenes captured
  from the MSL before translation (`tests/golden/`, 7.3 MB). Each one
  stresses one family: the valley with and without post, the skinned
  dragon side-on and close on the head, debug lines, landed in the
  foliage, breath with bloom, and the run HUD.
  - From the HLSL, every scene's mean error is 0.000 and no pixel is off by
    more than 8/255. Most scenes peak at 1/255; `ground` has 3 pixels that
    differ, and `run-hud` has 3, the largest 17/255.
  - The packaged binary, with its baked MSL, gives the same numbers.
  - Making the gate reproducible found one thing that was not the shaders:
    the training room rolled its second-breath start from the wall clock,
    which changed the Y pip from launch to launch. Headless now uses a
    fixed seed.
  - The `fire` golden was then recaptured from the HLSL, after the first
    comparison had shown its shaders within 1/255 of the MSL.

**Not done here, and why.** The PORTING plan before P1 had release builds
load precompiled blobs for every backend. Only MSL is baked so far,
because only Metal runs here. DXIL and SPIR-V blobs belong to P2, built on
the machines that run them.

**Note for anyone filing upstream:** SDL and SDL_shadercross refuse
contributions written with generative AI (their `CLAUDE.md` and PR template
say so). Use them freely; never open an issue or a PR against them from an
agent session.

## Phase P2 — Windows 10/11 and Linux on modern GPUs (done, 2026-10-02)

Done, apart from a Linux package. On x99 (RTX 5060 Ti), the game runs on
Vulkan under CachyOS, and on D3D12 and Vulkan under Windows 10 22H2. All
15 suites pass on every platform, and so do the goldens, each backend
against its own set and every set against Metal's under `--loose`.

**What it took**
- **Shader formats.** The device asks for the platform's formats: MSL on
  Apple, DXIL|SPIRV on Windows, SPIRV on Linux. `--gpu-driver` picks the
  backend. Baked packages choose `.msl`/`.dxil`/`.spv` by what the device
  accepts, so a Windows package carries DXIL and SPIR-V and runs either.
- **Linux builds unchanged.** GCC 16 on CachyOS compiled it with no source
  changes. Shadercross builds there in seconds against the Vulkan SDK's DXC
  and SPIRV-Cross (non-vendored). The SDK ships no `libdxil`, which
  shadercross's finder demands but only uses in an install step that is off,
  so the variable is pointed at `libdxcompiler.so`.
- **Linux headless over SSH**, with nobody logged in: SDL's `offscreen`
  video driver with a Vulkan window (`VK_EXT_headless_surface`). Without
  the Vulkan flag the driver tries to load OpenGL.
- **Windows is cross-built on the Mac.** `cmake/mingw-w64-x86_64.cmake`
  uses MinGW-w64 GCC 16 (UCRT). The result is one static `dragon.exe` that
  imports only Windows system DLLs. DXIL is baked on the Mac too:
  shadercross's vendored DXC includes the `libdxil` that signs it. A package
  is a GUI-subsystem program; it attaches to the parent console only when
  it was given no stdout, so a pipe still gets the log.
- **Windows headless works from the SSH session itself** (session 0):
  D3D12 creates a device and renders offscreen there. A windowed run needs
  the console session, so it goes through a one-off scheduled task
  (`-LogonType Interactive`). That is how the swapchain, the HUD scale and
  the double-click start (a hoard run) were checked.
- **Tests had POSIX assumptions.** They wrote to `/tmp`, and found the source
  tree through `__FILE__`, which a cross-compiled binary carries as the
  Mac's path. `tests/test_paths.h` now gives the OS temp directory and
  `DRAGON_SOURCE_ROOT`. Five suites failed on Windows before that. And
  `test_anim` had been passing there by skipping: with the models found, it
  runs 1092 checks instead of 744.
- **A cross-platform float difference exposed a test scenario a player
  cannot reach.** `test_bot` chased a target through the mountains, and on
  Linux the bot hovered over a rising slope and touched down; on macOS
  rounding luck kept it up. The target now stays above the terrain.
- **Headless reads no gamepad.** A controller left on by the desk changed
  the HUD's labels in captures and would have steered the run.

**How far the backends differ**

| Against | Valley mean | Pixels off by more than 8/255 | Where |
|---|---|---|---|
| Metal (M5) → Vulkan (5060 Ti, Linux) | 1.7 | 4.5% | anisotropic terrain, alpha-tested cards, the dragon's maps; the analytic sky is exact |
| Vulkan Linux → Vulkan Windows (same GPU) | 0.001 | 0.001% | nothing |
| Vulkan → D3D12 (same GPU) | 0.37 | 0.3% | one-LSB noise on textured ground, and shadow edges |

So the gate is **one golden set per backend**: `tests/golden/{metal,vulkan,d3d12}`,
each captured on its own backend after being reviewed beside Metal's. Then
`compare --set metal --loose` (mean at most 6, at most 25% of pixels off by more than 8)
is the cross-check, which every backend passes. `ground` is the loosest
scene, at 5.5: it is mostly large cards near the camera. A 900-frame landing
ends in the same flight state on both platforms, so that difference is not the
simulation.

**Packages.** `tools/release/package_windows.sh` builds a 144 MB zip on the
Mac. Unpacked on Windows, it matches the D3D12 goldens exactly and passes
them all on Vulkan. A 9000-frame `--demo` soak ran into valley 2 in 50 s
with no errors. **A Linux package is not done.** A CachyOS build requires
glibc 2.43, which only rolling distros have, so it must be built in an
older container (Ubuntu 24.04: glibc 2.39). x99's Docker keeps its images
on a root disk with 3.7 GB free, so it waits for Docker to move to
`/mnt/Data`, or for a build on another box.

**CI.** `.github/workflows/ci.yml` builds and runs the suites on macOS,
Ubuntu and Windows (MSYS2 UCRT64) on every push to the public repository,
with SDL from source and no runtime shader compiler.

**Display scale:** x99's Windows desktop is at 125% (DPI 120), and the
windowed check ran there. SDL3 declares per-monitor-v2 DPI awareness by
default, and Windows window sizes are physical pixels, so the frame is a sharp
1280x720 with the HUD at the right proportions -- the window just looks smaller
than on a 100% display. Sizing the first window by the display scale is a
polish item. The D3D12 log also warns about
texture rows not aligned to 256 bytes and about the Agility SDK's
UnrestrictedBufferTextureCopyPitch. Both are performance notes, and the
first is the 1x1 and odd-width textures.

### Using x99 for this

- CachyOS is the GRUB default. `sudo grub-reboot 'Windows Boot Manager (on
  /dev/nvme0n1p1)' && sudo reboot` boots Windows once, and Windows' next
  reboot is back in CachyOS.
- **Coming back from Windows by Restart works for the wired network now**
  (2026-10-02). Without help, a warm reboot from Windows left CachyOS with no
  network: the I217-LM (e1000e) linked at 1 Gbit/s but passed no traffic.
  Reloading e1000e in Linux did not clear it. So Windows now disables both
  NICs at every shutdown and restart, from a local Group Policy shutdown script
  (`C:\ProgramData\nic-cycle\nic-down.cmd`). A SYSTEM startup task
  (`nic-cycle-up`) re-enables them, and both log to
  `C:\ProgramData\nic-cycle\log.txt`, which Linux reads at
  `/mnt/Windows/ProgramData/nic-cycle/log.txt`. Tested: Restart from Windows,
  and CachyOS was on the LAN and Tailscale in 85 s. **The RTL8188EUS USB
  Wi-Fi still fails after a warm reboot** ("Firmware failed to start",
  rtl8xxxu -11): disabling it in Windows does not stop its MCU, Linux driver
  reloads and USB re-enumeration do not reset it, and the root hub cannot
  switch port power. Only a cold boot clears it, and remote work does not
  need it. The cost: Wake-on-LAN from a Windows shutdown no longer works,
  because the Intel NIC is disabled then. Linux arms Wake-on-LAN itself at its
  own shutdown, so waking x99 from a Linux power-off is unchanged.
- Windows answers as `x99-windows` (LAN) or `x99-windows-ts` (Tailscale).
  Its SSH shell is PowerShell 7, so join commands with `;`, not `&`.
- `D:` under Windows is `/mnt/Data` under Linux: the source tree is at
  `D:\dragon` and `/mnt/Data/dragon`. Builds stay off the Linux root disk,
  which is nearly full. Windows has Python 3.11 with Pillow, so
  `tools/golden` runs there with `--binary`. On Linux, Pillow is in a venv
  at `/mnt/Data/scratch/venv`.

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
| **R8** Windows 98 SE | The D3D9.0c runtime supports 98 SE. The toolchain is [gcc-for-Windows98](https://github.com/fsb4000/gcc-for-Windows98): GCC 11.1 for i686 with the win32 thread model, so no `std::thread`, `std::mutex` or `std::filesystem`. `src/` uses none of the three (checked 2026-10-01), so the work is keeping it that way plus the platform layer: R6's Win32 layer limited to 98's APIs, and miniaudio on DirectSound or WinMM. GCC 11 lacks some C++20 library pieces the code may lean on; the first 98 build will list them. MSVC 6 is the alternative, but it means C++98, a rewrite of the language level. It is worth it only if the GCC route fails | It boots into a valley on 98 SE |

R0 is cheap and is the right first move: in one session it settles
whether the Mac-hosted MinGW toolchain produces XP binaries, and whether
gcc-for-Windows98 produces 98 ones. Those answers decide how R6 and R8 are
built. R1 is the load-bearing refactor and is
worth doing even if the retro track stalls, because it is also what makes
the golden-screenshot gate backend-agnostic.

### R1, as built (2026-10-03)

The RHI is `src/rhi/rhi.h`, one `rhi::Device` interface, about 30 calls:
- buffers: static, or dynamic with `map_upload`/`commit_upload` recorded
  outside any pass;
- textures, which are also render targets, and samplers;
- pipelines, built from a description plus a `ShaderSource`;
- passes with clear and keep flags, binds, uniform pushes by stage and slot,
  draws;
- one `end_frame` that presents and optionally reads back.

Handles are opaque. The SDL GPU backend (`src/rhi/sdlgpu/`) casts its own
objects to them, so it adds no wrapper and no indirection beyond one
virtual call.

What moved where:
- **The backend** owns what used to be spread across `gfx/`: device
  creation, swapchain, staging and copy passes, enum translation, and the
  whole shader path (shadercross at runtime, or the baked files).
- **The pipeline cache** keeps include inlining and hot reload. It hands
  the backend preprocessed HLSL, and asks it which baked files to watch
  when it does not compile HLSL.
- **`gfx::Device`** is now policy only: the window, the scene, HDR, bloom
  and depth targets, the named passes, and screenshots.
- **`gfx/buffer.cpp`** is gone, folded into `create_buffer`.
- **SDL's GPU API** is included only by the backend and by
  `editor/imgui_layer.cpp`. Dear ImGui's renderer is API-specific by
  nature and reaches the native handles through `rhi/sdlgpu/sdlgpu.h`; a
  D3D9 build swaps in `imgui_impl_dx9` there.

**The depth convention** is explicit, not assumed. Each pass says what it
clears depth to and whether it keeps it, and each pipeline names its
compare op. So no backend bakes in reversed-Z. Choosing standard Z for
D3D9 is R2.

**Checked on the Mac** (Metal):
- All eight goldens give the same numbers as before the refactor (most
  scenes at most 1/255 off), from both the runtime-compiled and the baked
  shader paths.
- All 15 suites pass.
- A windowed run presents correctly, panels and HUD included.
- Hot reload still fails a broken shared header across its ten pipelines,
  then reloads them.
- The MinGW Windows build compiles.

**Not yet run on D3D12 or Vulkan.** That waits for x99: run
`golden.py compare` against `tests/golden/vulkan` and
`tests/golden/d3d12`.

### R2, in progress

`--tier modern|sm3|sm2|ff` (`gfx/render_tier.h`) shapes the content for a
tier on any backend, so each R2 piece is checked on Metal against the modern
render before a D3D9 device exists.

- **Palette splitting, done (2026-10-03).** `anim::partition_palettes`
  cuts each material submesh into batches of at most 60 joints (sm3) or 50
  (sm2). Vertices shared between batches are duplicated, and joint indices
  are remapped to the batch's palette. Each draw pushes only its palette.
  The shadow pass draws split meshes batch by batch too. In the roster,
  Frostvein (74 joints), one other species and the local gold dragon need
  two batches, at a cost of 0 to 174 duplicated vertices. Embercrest's
  joints already fit.
  - The split is bit-exact: on the CPU in `tests/test_skin_partition.cpp`
    and on Frostvein's real mesh, and on the GPU when one batch is enough.
    With two batches, Metal differs in about 300 pixels (at most 18/255).
    The same unsplit mesh drawn in two halves differs the same way, so that
    is the draw boundary, not the palette.
- **Conventional depth, done (2026-10-03).** Retro tiers project
  0.5 m to 16 km, clear depth to 1 and compare LESS (`gfx::DepthConvention`).
  The main-pass pipelines' GREATER is flipped by the pipeline cache, and
  the shadow pipelines, conventional already, are untouched. Against the
  modern goldens, `--tier sm3` differs by a mean of at most 0.03/255 and in
  at most 0.1% of pixels. Those are single pixels on mid-range foliage edges
  and the line where the river meets its bank: depth precision at work. The
  first-person view keeps the head and horns whole at the 0.5 m near plane.
  On real D3D9 hardware the buffer is 24-bit, so far-distance z-fighting is
  still to look for at R3. Camera-relative positions, the other mitigation
  in "The five real problems", wait until a capture shows they are needed.
- **The LDR fallback, done (2026-10-03).** SM2 and fixed-function cards
  have no float render target (`RenderTier::hdr` is false for them). Their
  world renders straight into the 8-bit scene colour target, with no HDR
  target, no bloom chain and no post pass. The pipeline cache compiles every
  shader with `LDR_OUTPUT`. Under it, `scene_out` and the particle shader
  finish each pixel with `ldr_encode` (`common.hlsl`): exposure, Reinhard
  blended toward its hue-preserving form, gamma, contrast and saturation,
  the composite's own curve fed from the same Grade & bloom dials
  (`SceneUniforms::output_grade`).
  - Correctness check: `--tier sm2 --no-post` against the `valley-nopost`
    golden differs by a mean of 0.04/255 in 0.1% of pixels, so the
    in-shader curve is the composite's "post off" picture.
  - With the grade on, `--tier sm2` differs from the modern goldens by a
    mean of 3.7 to 4.3/255. That is the missing bloom, split-toning, white
    balance and vignette, which an SM2 pixel shader has no room for.
  - Additive particles are brighter (fire: mean 10.5/255): each puff is
    tonemapped on its own, so overlapping puffs sum in display space and
    the curve cannot compress the total. Real SM2 hardware does the same.
    Tune the per-tier particle intensity on the card at R3, not blind.
- **Baked noise, done (2026-10-03).** The ground and the bark grain with
  value noise. The modern shader hashes four lattice corners per lookup, a
  dozen lookups per terrain pixel, which is hundreds of instructions. Every
  retro tier compiles `BAKED_NOISE` (`shaders/noise.hlsl`) and reads the four
  corners from one point sample of a 512^2 RGBA8 lattice
  (`gfx/noise_lattice.h`, baked at start-up, 1 MB). The smoothstep blend
  stays in the shader, because old hardware keeps bilinear weights to a few
  bits.
  - The lattice is the shader's own hash. It is exact over cells -256..255
    and repeats seamlessly past them (`tests/test_noise_lattice.cpp`: within
    half an 8-bit step). That window covers the patch noise everywhere, the
    grain across the whole map, and the micro-relief within 730 m of the
    centre.
  - Against the modern goldens, `--tier sm3` is unchanged except where the
    micro-relief lies outside its window: valley mean 1.2/255, ground 5.0
    at the spawn about 2 km out. With the micro-relief switched off in both,
    the difference falls back to the depth change's 0.03. Past the window it
    is the same noise with other values. Side by side, the ground reads the
    same.
  - A trap found on the way (`common.hlsl`): the count of textures SDL binds
    comes from reflection, which counts only those the shader uses. A debug
    edit that returns before reading slots 0 and 1 makes slot 2 read zero.
- **Texture caps and DXT, done (2026-10-03).** `create_texture_from_image`
  applies the tier's budget (`gfx::TextureBudget`) at upload, so no call
  site knows about it. Every texture is halved to the cap: 1024 for sm3,
  512 for sm2 and ff. Its mips are built on the CPU, averaged in linear
  light for colour. Each kind then compresses its own way (the model
  loader tells normal and ORM maps apart from the materials that use them):
  - Colour: BC1, or BC3 when any texel has alpha. Mean error 2.3 code
    values on Embercrest's base colour.
  - Normal maps: DXT5nm. X goes to alpha and Y stays in green, the shaders
    rebuild Z (`SWIZZLED_NORMALS`). Mean error 1.0 degree, worst 20.6.
  - ORM: BC1, accepting crosstalk between occlusion, roughness and
    metallic. Mean error 1.8 to 3.0 code values per channel, worst about
    100 on hard edges.
  - The terrain detail and the card masks: RGBA8. They are four
    independent channels and small.
  - The roster's textures: about 2 GB modern, 29 MB at sm3, 8.5 MB at sm2.
    The retro tiers also load faster, 5.4 s against 6.3 s, with less to
    upload.
  - Against the modern goldens, sm3's dragon close-up moves from a mean of
    0.16 to 0.47/255: scale detail a little softer. At sm2's 512 the
    softening is plain up close, as budgeted.
  - The CPU encode costs load time. A D3D9 package should bake the
    compressed chain (R3's packaging) rather than repeat it on a 2005 CPU.
- **Creature LODs, done (2026-10-03).** At load, `anim::simplify_skinned`
  (meshoptimizer, MIT) cuts every skinned mesh to the tier's triangle
  budget: 20K for sm3, 10K for sm2, 6K for ff, against the roster's 80K.
  That is before the palette split, since fewer triangles can need fewer
  joints. Edges collapse onto existing vertices, so every surviving vertex
  is an original, joints and weights byte for byte
  (`tests/test_skin_lod.cpp`). Nothing is re-rigged, and no `tools/` step
  or Blender pass is needed. The full mesh still feeds every CPU
  measurement (the head, the wingtips, the stance). Only the upload is cut.
  - The first pass held every UV seam. The generated creatures are cut
    into hundreds of atlas islands, so the simplifier stalled at 11K to 20K
    triangles for the 10K budget. Forced further, it tore the wing
    membranes (Tidewrack from above). It now runs permissive (it may cross
    a seam) with normals (weight 0.5) and UVs (weight 4) as attributes, so
    crossing a seam is charged for the texture it slides. At weight 1 the
    close-up head at 6K showed streaks along collapsed seams; at 4 they are
    mostly gone.
  - Every species reaches its budget. The error, attributes included, is
    at most 0.5% of the model's size at sm3, 0.9% at sm2 and 1.6% at ff.
    Rendered from the side, from above and on the ground, each tier keeps
    the silhouette. The close-up head is faceted at 10K and 6K, as those
    budgets must be. The props' 6K to 13K are cut too where they exceed a
    tier's cap.
- **Baked tier variants and Windows, done (2026-10-03).** A package has no
  runtime compiler, so `bake_shaders.sh` bakes every shader once per define
  set a tier compiles with: modern, sm3 (`BAKED_NOISE`, `SWIZZLED_NORMALS`),
  and sm2/ff (adding `LDR_OUTPUT`). Each variant's files carry their defines,
  e.g. `terrain.baked_noise.swizzled_normals.fragment.dxil`, and the loader
  builds the same name from the pipeline's defines. On x99-windows with the
  cross-built exe:
  - All 19 suites pass.
  - The modern goldens are exact on D3D12 and within 0.003/255 on Vulkan:
    R1's RHI changed nothing there either.
  - Every tier renders on both backends, and D3D12 and Vulkan agree per tier
    to 0.01/255.
- R2's content work is complete. What is left of the retro track needs the
  D3D9 backend (R3) and real hardware.

### Budgets to design to

From the cards on hand (`~/src/gpu-hist/data/cards.csv`). **Period cards run
only in the period benches.** The G41 takes PCIe x16 and PCI and has no AGP.
The AGP cards need one of the AGP benches in `systems.csv`: the 865G
Pentium 4 board (AGP 8x, XP) or the SiS universal-AGP board (98 SE and XP).
x99 under Windows 10 cannot drive any pre-DX9 card.

| Tier | Floor card on hand | Also on hand | Bench | Notes |
|---|---|---|---|---|
| SM3 | GeForce 6200 TC (NV44, PCIe, works) | Radeon X1300 (RV515, untested); Quadro FX 3500 (G71, works) as the fast SM3 reference; GeForce 7600 GT (AGP/PCIe, shows artifacts) | G41 | TurboCache: little real VRAM, so the texture budget is the binding one |
| SM2 | Radeon X550 (RV370, PCIe, works) | FireMV 2200 PCI (RV380 unconfirmed, untested); GeForce FX 5200 (four, AGP or PCI per board) | G41; the AGP benches for AGP FX boards | The FX's FP32 is slow; prefer `half` where precision allows |
| Fixed function | Mobility Radeon M6 on PCI, in the G41 now | GeForce4 MX PCI (NV18; in x99 for qemu-gpu's Linux work, it moves to the G41 for this); Radeon 7000; Radeon 9200 LE (AGP, DX8.1: no SM2, so it gets this tier); Riva TNT2 Vanta (DX6, no T&L) | G41 for PCI; the AGP benches for the rest | The TNT2 is below the floor: no hardware T&L. Treat it as a "does it start" check only |

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
| **x99-windows, Windows 10** | D3D12 and Vulkan on Windows; **the D3D9 debugging platform for all three tiers**, with `--tier` forced on its modern card's driver; apitrace for D3D9 call traces and replays | A real D3D9 driver and runtime, a debugger, and fast iteration. A modern driver still runs D3D9 fixed function. But Windows 10 cannot drive a pre-DX9 card, so this proves the code, not the hardware or the budget |
| **G41, Windows XP, driven by `agent.ahk`** | **Acceptance for each retro tier**, on the PCIe and PCI cards from the table above | The only place budgets, driver quirks and the XP platform layer are real. The qemu-gpu harness already copies files over SMB, runs jobs and takes OBS captures |
| The AGP benches (865G P4; the SiS universal-AGP board) | The AGP cards: the FX 5200 boards, the Radeon 7000 and 9200 LE, the TNT2 | Not yet driven remotely: they need the same agent and share setup as the G41, or the HID-and-capture rig in gpu-hist's `docs/bench-automation.md` |
| G41 or the SiS bench, Windows 98 SE | R8 | |
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
  not by reading. If it fails, gcc-for-Windows98 is the fallback for XP
  too, and then one toolchain covers both: building R0 with it as well
  answers the question for 98 at the same time.
- **Is the CPU the wall on the E3300?** The rig on 232-joint skeletons, seven
  bots and per-frame grass placement were all sized on an M-series Mac.
- **The 32-bit address space** on XP: a full roster at 4096² does not fit,
  so the retro build loads only the tier's textures. That is another reason
  R2 is separate from the backends.
- **Fixed-function look.** Whether it reads as "the game, earlier", or only
  as "the game, broken", is a judgement to make on a render at R5 against the
  concept targets, not something to settle in advance.
