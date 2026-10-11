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

### Where the port stands (handoff, 2026-10-03)

Published to the mirror at `4d90e0b` (`docs/RELEASE.md`), through R7's
graphics settings.

| Phase | State | What is left |
|---|---|---|
| P0-P2 | Done | A Linux package (an older-glibc container) |
| R0 | Built; runs under Wine and on x99-windows | **The G41 under XP.** The XP toolchain is llvm-mingw's msvcrt build on x99 (`D:\third\llvm-mingw-msvcrt`); the Mac's MinGW is UCRT |
| R1, R2 | Done | -- |
| R2b (model packs) | Done on the Mac: `--bake-packs`, pixel-identical at sm3 and sm2 | The Windows packager bakes them but no package has been built since; a retro-only package that ships packs without the glTFs |
| R3 (SM3) | Matches D3D12 at sm3 on x99's 5060 Ti (`tests/golden/d3d12-sm3`) | The G41 with the GeForce 6200 TC. A lost device is reset (render targets remade, dynamic buffers refilled from their shadows, ImGui's objects through hooks); exclusive fullscreen with `--fullscreen` (R6's optimisation round) |
| R4 (SM2) | Runs on x99's Radeon X550, matching D3D12's sm2 render within `--cross-compiler` | The G41 with the X550 (budgets on a real 2005 CPU). Exit test is "a valley at 1024×768": 28 to 33 fps at Very low, 21 to 24 at Low |
| R5 (FF) | Done through the gate ("Steps 2 to 4, as built"): the X550 through D3D9 fixed function matches D3D12 at `--tier ff` within a mean of 1.1/255 in all eight scenes. Minimal, the sixth preset, with a 48 m grid, a far 96 m mesh and a baked sky: 173 fps on the X550, **54 fps with every vertex processed in software** (`DRAGON_D3D9_SOFTWARE_VP=1`, or `=2` for a pure software device) | The FF cards on the G41 (the M6, the GeForce4 MX, the Radeon 7000): the look, the fps, and whether software VP needs the foliage batched and the terrain coarser. Blob shadows. A `--preset minimal` benchmark line |
| R6 (XP layer) | **Runs on XP** ("R6, as built"): the G41 under XP SP3 with the M6 picks the ff tier itself and draws the valley, 19 fps at Minimal 800x600, and 52.6 at 640x480 fullscreen after the optimisation round; the gate checks the imports against XP SP3's recorded exports and the PE header's 5.1 | The other cards on the G41; keyboard and gamepad play by hand |
| R7 (presets) | Graphics settings, five presets, the Graphics panel, the benchmark (`tools/bench/presets.py`), the world resolution and SM2's `NO_SHADOWS` variant; the window size and an fps readout are saved | Particle caps and fewer bots at the low presets; 30 fps on the SM3 floor card |
| R8 (98 SE) | Not started | -- |
| D3D11 | Done at feature levels 11, 10 and 9 ("The D3D11 backend"): x99's 5060 Ti through it is within 0.3/255 of Metal in all eight scenes, `tests/golden/d3d11`; WARP runs at 1.8 to 19.6 fps; the X550 at feature level 9_2 draws the sm2 tier within `--cross-compiler` of D3D12's set, at 20 fps Low | The HD 5450 and Bay Trail boards it was built for; the shaders in the package; a 9_1 or 9_3 device |

**Loose ends a new session should know:**

- **x99's display.** The X550 (testsigned, XP-era driver) is the primary
  adapter on VGA, and the 5060 Ti is still installed.
  - SDL's D3D12 driver takes DXGI's first high-performance adapter, which is
    now the X550, and refuses the machine. So D3D12 does not run on x99 at
    the moment; Vulkan does.
  - `DRAGON_GPU_LOW_POWER=1` reaches the 5060 Ti anyway (2026-10-04): SDL
    takes the first adapter of DXGI's high-performance order, and with
    nothing to rank a WDDM 1.0 driver on, that order is adapter order, the
    display's X550 first. The low-power preference is the same list
    reversed, so with two cards it is "the one not driving the display".
    D3D12 then creates on the 5060 Ti and renders headless from the SSH
    session as before. `dxdiag` confirms the X550 at WDDM 1.0 with feature
    levels 9_2 and 9_1, and D3D11 at 9_2 runs on it (`--gpu-driver
    direct3d11:1` from the SSH session, `direct3d11:0` on the desktop,
    where it drives the display: DXGI numbers the adapters per session).
  - The `d3d12`, `d3d12-sm3` and `d3d12-sm2` golden sets predate the
    sequenced random draws (R7, "A preset renders the same on every
    backend") and fail strict until recaptured.
- **The X550 can hang the machine.**
  - Its driver model has no timeout recovery: a GPU hang freezes x99
    outright.
  - Do not run High or Ultra on it. Keep soaks short.
  - The user reported a freeze after minutes of play at Low. The leak
    meter rules out a leak (R7, "Played, not benchmarked"); heat or the
    driver are the suspects.
- **Running D3D9 on x99** needs the desktop session:
  - run it through `tools/x99/run_interactive.ps1`;
  - log to a file with `DRAGON_LOG_FILE` (the exe is GUI-subsystem);
  - a per-pipeline profile with written pixels comes from
    `DRAGON_D3D9_PROFILE=1`;
  - `--restart-every N` with `--telemetry` soaks resets against the
    `gpu:` resource line.
- **D3D9 and D3D11 shaders are deployed by hand.** Bake them with
  `tools/d3d9/bake_d3d9.py OUT --tier sm3|sm2|sm2-noshadows|ff`, which
  needs Wine on the Mac, and copy them into `D:\dragon-win\shaders`. The
  Wine is Sikarugir's engine, and it finds its libraries only with the
  Frameworks directory on the fallback path:
  `WINE=~/Applications/Sikarugir/Test.app/Contents/SharedSupport/wine/bin/wine
  DYLD_FALLBACK_LIBRARY_PATH=~/Applications/Sikarugir/Test.app/Contents/Frameworks
  WINEPREFIX=<a scratch dir>` (2026-10-04). CrossOver's wine refuses
  without a bottle. The modern variants a Windows build loads
  (`bake_shaders.sh OUT dxil spv`) and the D3D11 set
  (`tools/d3d11/bake_d3d11.py OUT`) go the same way. The release packagers
  do not bake D3D9 or D3D11 yet.
- **A preset must build the same content on every compiler.** Keep one
  random draw per statement, and build the LOD code without FMA
  contraction (CLAUDE.md, "One random draw per statement"). The LOD log
  line's fingerprints compare the Mac and Windows directly.
- **x99 has the gold and silver studies** in `D:\dragon-win` and in
  `D:\dragon`, the source copy the cross-built tests read through
  `DRAGON_SOURCE_ROOT`. That is a personal copy. Their profiles are public
  in `assets/species/`; the models are not (`ATTRIBUTION.md`).

**Suggested next:** the G41. It is the exit test of R0, R3 and R4 and the
only place the CPU budget is real. Then R6, which R0's XP run decides.
While the G41 is not built, the independent item is R2b, the per-tier
asset packs ("Beyond the three tiers", below).

### R0, as built (2026-10-03)

`tools/r0/r0.cpp`, built by `tools/r0/build.sh`: one 32-bit Win32 + D3D9
program, no SDL. It opens a window and draws a textured, fogged triangle
twice over: through a hand-assembled `vs_2_0` with fixed-function pixel
stages, and through fixed function alone (`--ff`). It reads the back buffer
back with `GetRenderTargetData`, plays a tone through miniaudio, and looks
for XInput. Everything it learns goes to `r0.log`. Results:

| Where | Result |
|---|---|
| x99-windows, RTX 5060 Ti, the real D3D9 runtime | SM3 caps, DXT1/5, FP16 targets, D24X8. Both paths draw, and they are pixel-identical. Readback works, WASAPI plays the tone, XInput 1.4 loads |
| Mac, plain Wine 10 (Sikarugir's engine, run directly, no CrossOver), wined3d on OpenGL | Draws within 0.017/255 of x99, both paths. Wine reports a GeForce 6800 with SM3 caps and sees the desk's gamepad through XInput. **Good enough as a D3D9 smoke test**, though its caps are fake |
| Mac, Wine + D9VK (DXVK's D3D9 on MoltenVK) | The 32-bit DLL does not load in Wine's experimental WoW64 mode (status c0000135). Not pursued |
| The G41 under XP | Not run yet: the G41 is off |

- **The SSH session cannot run D3D9.** Session 0 has no adapter, so
  `CreateDevice` fails with D3DERR_INVALIDCALL. `tools/x99/run_interactive.ps1`
  runs a command on the signed-in desktop through a one-off interactive
  scheduled task and prints its output: the D3D9 loop on x99.
- **The Mac's i686 MinGW (Homebrew GCC 16) is UCRT, not msvcrt** as this
  plan assumed. It imports `api-ms-win-crt-*`, which XP has only with the
  VC++ 2015-2019 redistributable (14.27 or older). `-mcrtdll=msvcrt` does not
  help: its libstdc++, libgcc and winpthreads were built for UCRT. Its
  kernel32 and user32 imports are all XP-era, and `_WIN32_WINNT=0x0501`
  compiles.
- **llvm-mingw's msvcrt build** (20260922, LLVM 23.1.2, SHA-256 checked) is
  the XP toolchain. Its releases ship msvcrt only for Linux and Windows
  hosts, so it lives on x99 at `D:\third\llvm-mingw-msvcrt`. The spike built
  with it imports only `d3d9`, `msvcrt`, `user32` and `kernel32`, all
  functions XP's msvcrt.dll and kernel32 have. It renders identically under
  Wine. Whether it really starts on XP is the G41's to say.
- **Windows Defender quarantined that msvcrt build** as
  `Trojan:Win32/Wacatac.C!ml`, a machine-learning verdict on a small
  unsigned executable. The UCRT build was not flagged. It is a false
  positive, but a real distribution problem: a release for Windows will want
  code signing or a Microsoft false-positive submission.

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

### R2, as built (2026-10-03)

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

### R3, in progress: the D3D9 backend

`--gpu-driver direct3d9` (Windows) selects `rhi/d3d9`, a D3D9 device on
SDL's window. Everything above the RHI is unchanged, Dear ImGui included:
it draws through `imgui_impl_dx9`. With `--tier sm3` on x99's RTX 5060 Ti,
all eight golden scenes match D3D12 at the same tier within a mean of
0.024/255 (`tests/golden/d3d12-sm3`, the gate). A 6000-frame `--demo` soak
and a windowed hoard run ran clean.

**Shaders.** The HLSL stays SM 6 and is written once. `tools/d3d9/bake_d3d9.py`
takes each shader through shadercross (HLSL to SPIR-V, the tier's defines
plus `D3D9`), SPIRV-Cross's `--shader-model 30` HLSL output (`brew install
spirv-cross`), and `d3dcompiler_47`. The last is a Windows DLL, so
`tools/d3d9/hlslc.cpp` wraps D3DCompile and runs under plain Wine on the Mac,
with Microsoft's DLL copied from x99 to `~/.local/opt/d3dcompiler`. The
script then reads the compiler's constant table (CTAB) back and checks every
constant. That check paid for itself on the first run:
- **fxc ignores `packoffset` for SM 3.0** and packs what a shader uses, so 80
  constants landed elsewhere. The script now unwraps each cbuffer into
  globals pinned with `register(cN)`, block after block, and writes the map
  (`<stem>.<variant>.d3d9.json`) the backend pushes uniforms by.
- Samplers are pinned to their slots from SPIRV-Cross's reflection, and its
  `gl_HalfPixel` uniform to the register after the blocks. The backend sets
  it to (1/width, 1/height), D3D9's half-pixel shift.

What SM3 lacks is spelled in the shaders (`common.hlsl`,
`skin_common.hlsl`), so the other backends compile the same source:
- **No SV_VertexID.** The fullscreen passes read TEXCOORD0, which the
  backend feeds from a buffer of its own.
- **No depth-only fragment stage, and no portable depth-texture sampling.**
  The shadow map becomes an R32F colour target plus a D24X8 surface, and
  the depth passes write z/w into it. R32F is filtered only where the card
  can filter it (`D3DUSAGE_QUERY_FILTER`); the GeForce 6 and 7 cannot, so
  they point-sample.
- **No unsigned integers.** Skinning uses int indices, and a UBYTE4
  attribute arrives as floats.
- **256 vertex constants.** The retro tiers push joints as three rows
  (`PACKED_JOINTS`, 64 joints in 192 registers).

**The backend** (`src/rhi/d3d9/d3d9_device.cpp`, about 800 lines):
- RGBA8 is uploaded as A8R8G8B8, swizzled on the way in and on readback.
  sRGB is a sampler state set from the texture's format. BC1 and BC3 are
  DXT1 and DXT5.
- Index buffers wait in memory until the first bind gives their width.
- Dynamic vertex buffers are DEFAULT-pool DISCARD locks.
- Instanced foliage uses SM3 stream frequencies.
- The device is created with `D3DCREATE_FPU_PRESERVE`. Without it, D3D9
  drops the whole process to single-precision x87.
- Two bugs the goldens found:
  - The bake scripts recognised a pipeline by a literal `fs_main(`, which
    the depth passes no longer contain. That dropped the shadow shaders, and
    the mountain lost its shadow. The scripts now take every file without
    `#pragma once`.
  - D3D9 binds a stream with the stride, so `bind_pipeline` re-binds the
    streams with its own. Before that, the debug skeleton's lines fanned
    out across the screen.

**Running it.** D3D9 has no adapter in an SSH session (session 0), so a
D3D9 run on x99 goes through `tools/x99/run_interactive.ps1`.
`DRAGON_LOG_FILE` sends the log to a file, since a GUI-subsystem exe started
that way reaches no console. Under plain Wine on the Mac the backend draws
within 0.29/255 of x99's frame. The differences are wined3d's (tree cards),
so Wine stays a smoke test, not the gate.

Not done yet:
- device reset (resize, alt-tab in fullscreen);
- SM2 profiles (R4);
- the 32-bit XP build (R6);
- the G41.

### R4, in progress: SM2

Every shader now compiles for vs_2_0/ps_2_0, from the same source, under
the SM2 define (the sm2 and ff tiers, `RenderTier::vertex_lighting`).
- **The lighting moves to the vertex** (`sm2_light_vertex` and `sm2_finish`
  in `scene_common.hlsl`). The pixel stage keeps the albedo, a 2x2 shadow
  lookup, the combine and `ldr_encode_fast`, the tonemap without the
  hue-preserving blend.
- **Counts, from the bake's `--counts`** (ps_2_0 allows 64 arithmetic and 32
  texture): terrain 55, foliage 57, skinned 50, mesh 45, water 57, sky 54.
  Terrain's vertex stage takes 249 of vs_2_0's 256, with the patch noise at
  two octaves.
- **What the tier loses:**
  - terrain: the broad detail scale, triplanar rock, strata, grain,
    micro-relief and snow glint;
  - skinned: the normal and ORM maps and the specular;
  - foliage: the bark's streaks;
  - water: the ripples and the glint ("fresnel only").
  Without HDR the post stack is not created, and its shaders are not baked.
  The Metal preview is `artifacts/r4-sm2/sm2-metal-preview.png`.
- **ps_2_0's limits found on the way:**
  - 32 float constants. fxc packs only what a shader uses, so the bake lets
    it and reads the constant table back.
  - No `tex2Dlod`, so the SM2 shadow lookup uses plain samples.
  - A dependent-read chain limit. A kill that depends on a texture, ahead
    of the shadow taps, exceeds it, so foliage kills once, at the end.
- **The gate:** with `--tier sm2 --gpu-driver direct3d9` on x99's 5060 Ti,
  all eight scenes pass against D3D12 at the same tier
  (`tests/golden/d3d12-sm2`) under `golden.py --cross-compiler`: mean 1.0,
  2% over 8. The means are 0.17 to 0.51, with about 1% of pixels over 8 on
  lit slopes. That is fxc's SM2 math against DXC's, in vs_2_0's per-vertex
  lighting; the sky is exact.
- **The shadow map is capped per tier:** 1024 at sm3 and 512 at sm2.
  4096² of R32F plus depth is 128 MB, the whole of an X550. The capped map
  covers less (±512 m and ±256 m) to keep about a metre a texel. At the
  modern ±900 m a 512 map's 3.5 m texels lost every tree's shadow and the
  dragon's. Ridges past that range go unshadowed on those tiers.
- **Measuring a run:** every run ends with a wall-clock line, `frames: N in
  S s, F fps (driver, tier)`. `--gpu-driver direct3d9:N` picks D3D9 adapter
  N. On the 5060 Ti headless, D3D9 runs 130 fps at both tiers against
  D3D12's 380. `DRAGON_D3D9_PROFILE=1` times each kind of backend call and
  logs ms/frame at exit. Over 522 draws a frame the backend itself takes
  0.3 ms (draw calls 0.22, uniforms 0.04). The other 5.9 ms is `end_frame`
  waiting for NVIDIA's D3D9 driver and GPU, so it is not the backend's to
  fix on this card. Redundant render and sampler states are cached anyway,
  which a 2005 driver will feel more. The profile to read is the G41's.
- **SM2 hardware has no stream-frequency instancing.** The backend uses
  ATI's `INST` switch (R300 and later), and otherwise draws one instance at
  a time with a zero-stride instance stream.
- **The X550 on x99 (2026-10-03).** With a secondary card's outputs unused,
  D3D9 does not list it: D3D9 enumerates outputs with a desktop, not cards.
  So the X550 became primary, with VGA connected. It reports vs_2_0/ps_2_0,
  2048² textures, DXT, R32F (not filterable), 1,048,575 primitives a draw,
  and ATI's `INST` instancing switch. The game runs on it at both sizes,
  HUD included.
  - **The first frame took 1.24 s, entirely vertex work.** 4.3 million
    triangles a frame went through 160- to 250-slot vertex programs, on a
    card with two vertex units. Resolution did not matter (320×240 ran no
    faster). `DRAGON_D3D9_PROFILE`'s per-pipeline primitive counts found
    where they went, and each became a tier budget (`gfx/render_tier.h`):
    - the terrain grid: 18 m cells (12 m at sm3), the skirt alike. The
      terrain no longer casts into SM2's ±256 m shadow map;
    - the trees: drawn to 800 m (2.5 km at sm3), 1.8× the spacing. A cell
      wholly past the LOD's dither band draws only the crowns' coarse cards,
      on every tier, which is invisible since the detail cards were
      discarded there anyway;
    - the rocks, the surprise: flat-shaded scans of 500 to 1,500 triangles
      with no shared vertices, 2,928 of them. They are now welded and
      simplified to 150 triangles (400 at sm3, `gfx/mesh_lod.h`) and drawn
      to half the distance;
    - the vertex programs: the terrain's patch noise is computed once on the
      CPU into the unused uv.x (163 slots, from 249), and the per-vertex
      lighting skips three normalizes and a divide.
  - **Then the creatures and the terrain's cull:**
    - A skinned mesh gets a distance LOD (`SkinnedMesh::upload_far`): 1,500
      triangles past 100 m at sm2, 3,000 past 150 m at sm3. The renderer
      picks it by itself.
    - A skinned caster outside the shadow map's square is skipped (all
      tiers).
    - The terrain is uploaded in 320 m chunks (`gfx::chunk_mesh`, the skirt
      in 1280 m ones). It is drawn only where the camera, or in the shadow
      pass the light's box, sees it. That is all tiers too: modern goldens
      move by at most 6/255 on a few pixels where equal-depth triangles now
      resolve in another order.
  - **Result on the X550:**

    | | 320×240 | 640×480 | 1024×768 |
    |---|---|---|---|
    | valley, first frame | | 0.8 fps | |
    | valley, now | 19.5 | 15.4 | 10.4 |
    | hoard run, now | | | 11.0 (from 6.3) |

    About 50 ms of each frame is geometry and the driver, and the rest is
    fill: at 1024×768 the SM2 pixel programs (near 60 instructions) cost
    about 45 ms over the overdraw. R7's presets (640×480 to 1024×768,
    particle caps, fewer bots and less grass) are where that is settled.
  - **SM2 point-samples its shadow map on every backend,** since its cards
    cannot filter R32F, so the tier looks the same everywhere. SM2 also
    treats points beyond the light's far plane as lit, as `sun_visibility`
    does. Missing that drew a dark trapezoid over the far valley.
  - **The X550 against the 5060 Ti's D3D12 SM2 render:** seven scenes fall
    within `--cross-compiler`, at means of 0.36 to 0.71. `ground` is at
    1.2, along the near shadow's 1 m point-sampled staircase, where the
    card's FP24 pixel maths moves the steps by a texel. That is hardware
    variance, so the X550 is a result, not a gate.

### R6, as built (2026-10-04): the XP platform layer

The Windows XP build is `build-xp/dragon.exe`: 32-bit, the D3D9 backend
alone, and no SDL3, which needs Windows 7.

```sh
cmake -S . -B build-xp -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-i686-xp.cmake \
  -DDRAGON_XP=ON -DDRAGON_SHADERCROSS=OFF -DDRAGON_DEV_ROOTS=OFF
cmake --build build-xp
tools/xp/check_imports.py build-xp/dragon.exe     # the gate
tools/release/package_xp.sh 0.2.0                 # dist/Dragon-0.2.0-xp.zip
```

**The toolchain** is R0's choice, now hosted on the Mac: llvm-mingw's
msvcrt target files (mingw-w64's headers and import libraries for
msvcrt.dll, libc++, libunwind, compiler-rt) unpacked from its Linux-hosted
release into `~/.local/opt/llvm-mingw-msvcrt` -- target files are the same
whatever the host -- and compiled by Homebrew's clang 21 with lld, given a
resource directory of its own that holds the release's i386 builtins. The
release's LLVM 23 libc++ headers compile under clang 21 unchanged.
`cmake/llvm-mingw-i686-xp.cmake` has the recipe. SSE2 maths keeps the
content identical to the x86-64 builds: the XP build's frame under Wine is
**pixel-identical** to the 64-bit Windows build's D3D9 frame (sm3, the same
Wine), LOD fingerprints and all. All 21 test suites pass as 32-bit
libc++ executables under Wine.

**The platform layer is an SDL3 subset, not a new interface.** The engine
calls about sixty SDL functions outside the SDL GPU backend -- one window,
its events, scancodes, the mouse with a relative mode, gamepads, logging,
files, paths, ticks and a BMP writer. Rather than put an interface under
twenty-odd files, the XP build compiles the same sources against
`src/platform/xp/include/SDL3/`, which declares exactly those, with SDL3's
names, signatures and values (HID scancodes, button and axis order), and
`src/platform/xp/sdl3_xp.cpp` implements them on XP SP3's Win32: a
`CreateWindowExW` window and a message pump into SDL events, raw-input
mouse motion with a clipped, hidden cursor for relative mode, XInput
loaded from whichever DLL exists (9.1.0 ships with XP's DirectX), UTF-8
paths through the wide-character APIs, `SHGetFolderPathW` for the user
directory, `QueryPerformanceCounter` for ticks. CMake aliases it as
`SDL3::SDL3`, so every target, tests included, links it unchanged. The only
extension is `SDLXP_SetWndProcHook`, through which Dear ImGui's Win32
backend sees the window's messages (`imgui_impl_win32` and `imgui_impl_dx9`
replace the SDL3 and SDL GPU ones in that build). A call the shared code
starts making that the subset does not declare fails the XP build -- which
is the intended guard. This is hand-rolled platform code against the
third-party policy's letter; it is the "thin Win32 layer" R6 planned, kept
to SDL3's shape so nothing else changes, and every other build still links
SDL3.

**What XP lacks is thunked in the executable.** llvm-mingw's libc++ is
built for Windows 7 and its msvcrt for Vista's msvcrt.dll: the first link
imported SRW locks and condition variables, `InitOnceExecuteOnce`, fiber
storage, `GetLocaleInfoEx`, `GetThreadId`, `K32EnumProcessModules`,
nineteen `_l` locale functions and `wcrtomb_s` from msvcrt.dll, and Dear
ImGui's Win32 backend pulled in dwmapi.dll, which XP does not have. Any one
of those and XP's loader refuses the program before `main`.
`src/platform/xp/xp_thunks.cpp` defines each import itself -- the
`__imp__Name@N` pointer and the `_Name@N` stub, compiled into the
executable so the linker takes them before the import libraries -- and
each calls the system's function when it exists and otherwise does the job
with what XP has (a spin lock in the SRW word, a condition variable that
wakes spuriously, a three-state call-once, TLS for fiber storage, LCIDs for
locale names, ntdll for a thread's id, psapi.dll, the plain CRT functions
under the "C" locale). The system set of locks is used only when all of it
is there, so a lock is never taken by one implementation and released by
the other. `DRAGON_XP_THUNKS=fallback` forces the XP side on any Windows:
under Wine the frame is identical either way and the suites pass. The file
is compiled with `-fno-threadsafe-statics`, since libc++abi's static guard
would call back into the locks it defines.

**The gate** is `tools/xp/check_imports.py`: the executable's import table
against what XP SP3's own DLLs export. The lists come from the real files:
`--dlls DIR` reads kernel32, user32, gdi32, msvcrt, shell32 and DirectX
9.0c's d3d9 directly, `--record DIR` writes them to
`tools/xp/xp-sp3-exports.txt` for the packager. **That list is not
recorded yet**: x99 (whose qemu-gpu XP image has the files) and the G41
were both off when R6 was built, so the check has run only against Wine's
DLLs, which proves the parser and nothing about XP. Recording it is the
first thing to do with either machine; the packager refuses to run
without it.

**Picking the tier.** A D3D9 device now asks the card's caps before the
tier is fixed (`rhi::d3d9::probe_shader_caps`): vs/ps 3.0 is sm3, ps 2.0
sm2, less fixed function, and a tier asked for or saved above that comes
down to it. On XP that is what makes a double-click work on any card.

**On the G41** (operated as `~/src/qemu-gpu/docs/G41_TESTBENCH.md`
describes: `agent.ahk` runs job scripts dropped in XP's shared `bench`
folder, `g41job.sh` submits them from the Mac over the direct cable, OBS
watches the VGA output), two jobs are ready in `tools/xp/g41/`:
`xp-dlls.ahk` copies XP's system DLLs to `bench\results\xp-dlls\` for
`check_imports.py --record`, and `dragon-run.ahk` runs the unzipped package
from `bench\dragon\` with the arguments in its `args.txt`, the game's log
through `DRAGON_LOG_FILE`, and its own `--screenshot` (the pixel oracle; the
OBS capture is analog VGA). The G41 is shared with the qemu-gpu sessions,
so jobs go in only when the bench is free.

**It runs on XP (2026-10-04).** The G41 (Celeron E3300 at 3.34 GHz, XP
SP3, the Mobility Radeon M6 on PCI with Omega 2.6.87) started the package
from the share with no flags: the caps probe found vs_0/ps_0 and picked the
ff tier, the driver reports no hardware T&L so every vertex program ran in
D3D9's software pipeline, and the valley, the dragon, the HUD and its
fonts all drew. `tools/xp/xp-sp3-exports.txt` is recorded from the G41's
own DLLs (kernel32 5.1.2600.5512, msvcrt 7.0.2600.5512, d3d9 5.3.2600.5512)
and the build's 227 imports all resolve against it.

| Preset, window | fps (headless timing off: a real window, 2 back buffers) |
|---|---|
| Low, 800x600 | 8.0 to 9.3 |
| Minimal, 800x600 | 19.1 (median 50 ms) |
| Minimal, 640x480 | 24.7 (median 36 ms) |

Two things the Wine runs could not show, each a no-picture failure:

- **lld stamps an executable for Windows 6.0** unless told otherwise, and
  XP's loader refuses it outright (`CreateProcess` fails; the first run
  ended at once with nothing logged). The toolchain file passes OS version
  5.1, and the subsystem is restated as `windows:5.1` after `-mwindows`,
  which otherwise resets it; `check_imports.py` now checks the header too.
- **The M6's driver claims `D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES` and
  fails the call** (`D3DERR_INVALIDCALL`). The world, rendered at 85% and
  stretched into the UI's target, never arrived: uninitialised video memory
  under a HUD that smeared across frames. The D3D9 backend now stretches
  with a pre-transformed textured quad through fixed function when the cap
  is absent or the first `StretchRect` fails (`DRAGON_D3D9_NO_STRETCHRECT=1`
  forces it); the picture matches StretchRect's under Wine within
  filtering.

`dragon-run.ahk` takes `NAME=VALUE` lines from `bench\dragon\env.txt` for
such switches. OBS can be off: the game's own screenshot is the record.

**Making 640x480 Minimal playable on the M6 (2026-10-04).** 25 fps
windowed was not playable. Measured with `DRAGON_FRAME_PROFILE=1`
(`run()`'s phases plus `CORE_PROFILE` sections in `render()`),
`DRAGON_D3D9_PROFILE=1` (draws and primitives per pipeline) and
`DRAGON_D3D9_VSYNC=0` (present at once, so a frame's real cost shows):

| Step | Median frame | fps |
|---|---|---|
| Start: windowed, world 50% stretched into scene_color, then copied to the back buffer | 36 ms | 25 |
| ff terrain chunks 1280 m instead of 320 (264 draws to ~60; a draw is ~50 us in software VP) | -- | 33.8 |
| The UI on the back buffer itself (`rhi::Device::frame_target`): the full-screen copy gone | 25 ms | 40 |
| **Exclusive fullscreen**, 32-bit | 19.7 ms | 48.8 |
| Fullscreen 16-bit, and a 5-6-5 world target (`--color 16`, the ff default) | 18.6 ms | 52.6 |

What the profile said, so the next card's round starts from it:

- **The CPU is not the limit.** Update is 0.15 ms; the whole scene is about
  11,000 triangles in 86 draws, and issuing them, software vertex
  processing included, is ~3.5 ms. The rest is the card: in a window
  the driver blocks inside whichever draw fills its queue (terrain "took"
  11 ms), and fullscreen the same wait moves into Present.
- **The M6 is limited by memory, and the desktop shares it.** A sky-only
  frame (camera pointed up) cost 15.5 ms at a full 640x480 world: ~21 ns a
  world pixel, and the textured-quad copy to the back buffer ~26 ns a
  pixel -- about 45 Mpix/s against the RV100's ~150 peak. Halving the
  world target's bytes (R5G6B5) bought only 5% in a window. Fullscreen
  bought 21%: the card no longer scans out a 1024x768x32 desktop and
  copies each frame into it.
- **Not worth it:** D16 depth (0.3 ms, and 16 bits over the conventional
  0.5 m to 16 km range z-fights at a kilometre); a world at 100% (35 fps
  fullscreen, against 53 at 50%).
- A full-size LDR world now draws straight into the back buffer too, so
  `--graphics resolution=0` pays no copy either.

Fullscreen needed the device-lost path R3 skipped: `reset()` releases and
remakes every `D3DPOOL_DEFAULT` object the backend made (render targets
from their descriptions; dynamic vertex buffers are written through a CPU
shadow so their contents survive), ImGui's renderer releases its own
through `rhi::d3d9::set_reset_hooks`, and the state caches forget.
`tools/xp/g41/dragon-alttab.ahk` minimises the game 8 s in and restores
it: "device reset (3 targets)", and the frame after it draws.

Two traps from the round. **XP's screensaver** (`logon.scr`, after ten
idle minutes) holds the foreground, so a fullscreen device created under
it stays lost and the run "hangs" until the job's timeout; a synthetic
mouse move does not end it. The XP shim now refuses `SC_SCREENSAVE` while
the game is in front (SDL3 does the same by default), and the G41 jobs
close the configured screensaver before launching. And **`rhi::Format`
is persisted** -- model packs store it as a byte -- so a format inserted
mid-enum shifted every later value: a pack's BC1 textures read back as the
new RGB565 and the RGBA swizzle ran off the end of the data (Dr. Watson's
log on the G41, `%ALLUSERSPROFILE%\Application Data\Microsoft\Dr Watson`,
named `upload_texture_level`). New formats go at the end.

**Second round: the broken ground and the dips (2026-10-04).** Two reports
from play at Minimal fullscreen.

*Broken triangles in the terrain.* The ff tier draws the ground from a
48 m mesh near the camera and a 96 m one past it, choosing per chunk.
The chunk lines sat at multiples of the chunk size from 0, but both grids
start at -2500 m, so each mesh's border cells fell to whichever side
their centroids did, and the seams opened into holes up to a cell wide.
The 30 m flanges only cover a difference in height. It was visible at
320 m chunks too (sky through a slope on the Mac at `--tier ff`), and the
1280 m chunks of the first round made it worse: the near/far choice, by
chunk centre against 1000 m, could put the 96 m mesh under the camera. Now
the chunk is a whole number of far cells measured from the grid's corner
(1248 m at Minimal), so both meshes end on the same lines. A chunk is near
when its *nearest point* is within the Terrain panel's "fine ground
within" (1000 m; `DRAGON_FAR_TERRAIN` for a run). At 400 m the far mesh's
staircase river reaches the foreground, for about 4%.

*The dips.* With the card side measured separately (fullscreen, vsync
off), the costs come apart:

| What changed | Frame |
|---|---|
| Nothing on screen but the sky, the stretch and the HUD | 5.0 ms |
| The flying scene at 640x480 | 20.0 ms |
| The same at 320x240 (a 160x120 world) | 15.2 ms |
| Terrain chunks 600 m: +43 draws, same geometry | +2.2 ms (~50 us a draw) |
| Terrain chunks 2500 m: -10 draws, +3,000 vertices | +3.3 ms (~1 us a vertex) |

So most of the frame is geometry, at about a microsecond per software-
processed vertex and 50 us per draw, and the slow moments of a run
(`DRAGON_FRAME_TRACE=1` lists the slowest frames and each second's mean,
and a headless run replays the same frames on the Mac to look at them)
are the views along the valley with props, rivals and projectiles in
them; looking at the ground runs at 15 ms. `DRAGON_D3D9_PROFILE` now
counts the vertices each pipeline sends.

- **Culling.** `draw_skinned` and `draw_mesh` drew everything asked of
  them, so every grazer within 2.2 km, every rival and every prop was
  skinned on the CPU and sent each frame, in view or not. Meshes keep a
  bounding sphere now (padded 1.5x for a pose) and the renderer tests it
  against the view: in a run, 22.7 -> 10.1 skinned draws and 5,357 ->
  2,892 triangles a frame, 8.6 -> 2.4 plain meshes.
- **A 2-component texture coordinate** in the ff vertex programs (it was
  a float4 whose zw were always 0): 40 bytes a transformed vertex, in
  principle 32. It measured no different on the M6 (19.9 against 20.0 ms),
  so either the runtime sends four components for `vs_2_0` regardless, or
  the bytes are not what costs; it stays, as the smaller output.

Then, profiling only the slow stretch of a headless run (frames 3180 to
3600 of `--run 7 --autopilot`, a long view down the valley with a tower,
the hoard, rivals and projectiles; `DRAGON_FRAME_PROFILE=N` and
`DRAGON_D3D9_PROFILE=N` count from frame N):

| Step | Card's time a frame (headless waits for it) | Run, headless |
|---|---|---|
| After culling | 30.7 ms (render 6.5) | 41.0 fps, 1% low 40.2 ms |
| Models under 1.5 world pixels skipped (`RenderTier::min_draw_pixels`, ff only), a 66-vertex sphere for projectiles | 27.5 ms | 45.2 fps, 1% low 35.7 ms |
| The skirt in one draw (terrain 59.8 -> 16.5 draws a frame) | 27.5 ms | 45.2 fps |

The last row settles the per-draw question: 43 fewer draws a frame cost
nothing, so it is the vertices, about 1.5 us each. The earlier "50 us a
draw" from smaller chunks was their flange vertices. The CPU readout
(`frame profile: cpu`) shows 100% of one core throughout, the wait
included, but both waits spin, so it cannot separate a card at work from
a driver at work. What fits is D3D9 buffering the draws and the M6's
DX7-class driver pushing each transformed vertex across the PCI bus when
the buffer is handed over. Fullscreen, a run is now 47.0 fps, median
22.5 ms, 1% low 42.3 ms (from 43.9 fps and 48.4 ms).

**Third round: taking a slow frame apart (2026-10-05).** The slow frames
were steady, not stutter: long views down the valley. One was frozen and
dissected on the G41 itself. `DRAGON_FREEZE_AT=3300` holds frame 3300 of
`--headless --run 7 --autopilot` (player, a rival, a tower, the hoard and
projectiles in a long valley view) while it renders again and again, and
`DRAGON_ABLATE` leaves out one category at a time. Each line is 240
identical frames, CPU plus card (headless waits for the card every
frame):

| Removed (Minimal, 34.8 ms) | Saved |
|---|---|
| The HUD | **9.4 ms** |
| Far terrain | 5.0 |
| Near terrain | 4.0 |
| Prey (a herd at 570 m) | 2.3 |
| Player | 1.8 |
| Run world (hoard, rings) | 1.6 |
| Particles | 1.2 |
| Towers | 1.1 |
| Sky | 1.0 |
| Water | 0.8 |
| Rivals | 0.7 |
| Projectiles, skirt, trees | <= 0.4 each |
| All of the world | 20.8 (14.0 left: the HUD and a 4.6 ms floor of clear, stretch and present) |

Inside the HUD, the same way: its 3,054 vertices and 3,197 triangles are
one draw. Without ImGui's anti-aliasing fringe (1,735 triangles) the
frame was 1.9 ms faster, about 1.3 us a triangle, the world's rate; with
blending off too, 2.1 ms more. The font atlas (512x128, never
re-uploaded) and the buffers' pool (system memory or not, ImGui's and
ours) made no difference. So a frame on this card is triangles at ~1.3 us
each, plus fill, and the HUD was a third of it.

What came of it:

- **Flanges only on seams.** A chunk's flanges are stored per side and
  drawn only where the neighbour comes from the other mesh: 34.8 -> 30.7
  ms. With them gone from the inside of each mesh, small chunks cost
  nothing extra, and 312 m (three far cells) culls best: 30.3 ms.
- **The size test on the unpadded radius**, so a herd at 570 m (2.3 px
  padded, 1.5 unpadded) is culled.
- **Lowest**, a seventh preset below Minimal for slower cards (a Rage
  XL), so Minimal keeps its look: 600-triangle creatures (120 far, from
  30 m), the view cut to 2.5 km in fog (a new View distance setting:
  terrain chunks and models past it are not drawn, and exp² fog reaches
  2% there), and a Plain HUD (a new HUD setting: no fringe, circles in a
  third of the segments, every colour made opaque over the plate's
  charcoal, alpha-tested on D3D9, and solid edge bars for damage and
  status instead of gradient bands a fifth of the screen tall).

| Frozen slow frame | Frame |
|---|---|
| Minimal, start of the round | 34.8 ms |
| Minimal now | 30.2 ms |
| Lowest, smooth HUD | 22.4 ms |
| Lowest | **16.7 ms** |

The first alpha-tested HUD lost every dim label (text at alpha 140 under
a 0x60 reference) and turned the gradient vignettes into solid red bars;
the opaque tokens and the solid edge bars are what fixed it. Grey marker
labels over the terrain ("THE PASS") read dark in Plain.

**Headless overstates the HUD.** Headless waits for the card every frame,
so CPU and card work add; a fullscreen game overlaps them. With the
freeze also working in a window (`DRAGON_FREEZE_AT` pins the step to
1/60 before the freeze, so a fullscreen run reaches the headless frame),
the same frozen frame in the real present path is 26.3 ms at Minimal and
14.2 at Lowest, and there the Plain HUD costs 1.9 ms, not 7.6. The rest of
this round was measured that way:

| Removed (Lowest, fullscreen, 14.0 ms) | Saved |
|---|---|
| Near terrain | 2.2 ms |
| Far terrain | 1.9 |
| Particles | 1.2 |
| Sky | 1.0 |
| Water, run world | 0.8 each |
| Projectiles | 0.6 |
| Rivals, towers | 0.4 each |
| Skirt, player | <= 0.3 |
| All of the world | 9.8 (4.2 left: the HUD, the stretch, present) |

- **Particles, a new setting**: Half at Lowest (every other spawn,
  counted, not random, each a little larger): 14.0 -> 13.2 ms.
- **Linear fog for a short view.** exp² thick enough to hide the 2.5 km
  cut was 47% at a kilometre and greyed the whole valley out; the ff fog
  now runs linearly from 45% of the view distance to its end
  (`SceneUniforms::fog_range`, read by `ff_fog` alone), clear up close as
  the cards of the time did it. At no cost. 3.5 km instead of 2.5 costs
  2.2 ms (`DRAGON_VIEW_DISTANCE` to try others).
- The fine terrain stays within 1000 m on Lowest: at 500 m the 96 m
  mesh's river broke into pools, which read as broken ground.
- No colour clear on the ff tier (the sky dome covers every pixel): 0.2 ms.
- Water only on wet cells (ground below the line, nine samples a cell and
  a cell's margin), instead of a plane under the whole valley: no
  measurable change, kept for the triangles out of view.
- StretchRect from a plain render-target surface was tried for the
  stretch: the M6 refuses it too, even at matching formats, and the
  fallback's extra copy made it slower. Reverted.

| Frozen slow frame, fullscreen | Frame |
|---|---|
| Minimal | 26.3 ms (38 fps) |
| Lowest | **13.2 ms (76 fps)** |

Whole runs of `--run 7 --autopilot`, fullscreen, 3600 frames:

| | Average | Median | 1% low |
|---|---|---|---|
| Minimal, before the second round | 47.0 fps | 22.5 ms | 42.3 ms |
| Minimal, real time | 59.4 fps | 15.5 ms | 22.0 ms |
| Lowest, real time | 82.2 fps | 12.2 ms | 13.7 ms |
| Minimal, fixed step (the slow stretch at ~26 ms) | 57.8 fps | 17.0 ms | 26.7 ms |
| Lowest, fixed step (the slow stretch at ~14 ms) | 87.8 fps | 11.7 ms | 15.8 ms |

**The low LODs, made whole (2026-10-06).** The playtest saw the dragon
"very broken" at Lowest and "a bit broken" from Minimal up. Renders split
the cause: in the bind pose the 2.5K LOD was nearly the 10K one and even
600 triangles kept the shape, but every LOD at 5K and below drew its
texture as a scramble of patches. Reaching those budgets means collapsing
across the UV seams of an atlas cut into hundreds of islands
(`skin_lod.cpp` explains why holding them stalls at 15-20K), so the
surviving vertices map their triangles across islands; a 512 texture only
showed the scramble sharper. Two things tried first changed nothing on a
render and were dropped: a skinning term in the collapse cost (each
vertex's weights' blend of its joints' centres of influence), and the
LOD's normals recomputed from its faces on their own.

- **At 5,000 triangles and below the LOD is vertex-coloured**
  (`anim::VERTEX_COLOUR_TRIANGLES`): every full-mesh vertex gives its base
  colour to the nearest LOD vertex of the same texture, each LOD vertex
  takes the mean, the maps are dropped, the normals are recomputed from
  the LOD's faces, and the seam copies the UVs made are welded. The
  dragon is clean at 5K, 2.5K, 1.2K and 600; and the weld halves the
  vertices it costs on a card that pays per vertex (the 1.2K dragon 1,339
  -> 599, its far LOD 238 -> 127). Model packs are version 2, so an older
  pack's scrambled LODs are refused and rebuilt (`--bake-packs`).
- **The run's static props have their own budget**: 5,000 near and 800
  past 80 m from Low through Minimal, 2,500 and 400 past 60 m at Lowest,
  where they had the creatures' 1,200 near and 248 past 45 m -- a
  hard-surface watchtower lost a wall at 1,200 and was a ruin at 88 m.
  The grazer, an animated prop in herds, keeps the creatures' budget.

The frame rate the playtest read under 60 at Lowest is vsync: a 640x480
mode at 60 Hz caps it there, and the 82 fps above was measured with it
off (`DRAGON_D3D9_VSYNC=0`). Vsync is now a Graphics-panel setting
(`--no-vsync`).

**For the cards below the M6.** A Rage XL (and the DX6/DX7 parts like it)
takes only power-of-two textures, and some drivers of that age cannot
render into a texture from D3D9 at all; either would have meant no
picture, as the 320x240 world target could not be made. Both are met now
without the card to try them on, each forced on the G41 to check it:

- A power-of-two-only card (`D3DPTEXTURECAPS_POW2` without
  `NONPOW2CONDITIONAL`; `DRAGON_D3D9_POW2=1` forces it) gets its render
  targets and depth surfaces at the next powers of two, drawn into at the
  corner; the stretch quad's coordinates and the readback use the real
  size. The picture is the same.
- A device that cannot make a colour-target texture
  (`DRAGON_D3D9_NO_RT_TEXTURES=1` forces it) draws everything straight
  into the back buffer, the world at full size (Minimal at 36 fps on the
  M6, against 59), and takes no screenshots.
- A card without `MODULATE2X` (`DRAGON_D3D9_NO_MODULATE2X=1` forces it)
  draws the terrain's and the trees' stage as the pre-lit colour alone:
  the detail tile is lost, not half the brightness, as plain `MODULATE`
  would have done. Every ff recipe is already one stage, for a
  single-texture-unit card. Terrain is ~60% of the draws and ~45% of
the vertices. The next levers are a third terrain level (192 m) past
2 km, a shorter view under the fog, and the props' near LODs (1,200
triangles but 2,200 to 2,600 vertices each, from texture seams).
`DRAGON_FRAME_PROFILE=N` profiles from frame N, to take that stretch
alone.

**A second machine: the Atom D525 and its GMA 3150 (2026-10-11).** The
PT55D board (Atom D525, 1.8 GHz, 8 GB) runs the G41's own XP disk in IDE
mode, agent and all, beside a Windows 10 install. The GMA reports ps_2_0,
no vertex shaders and no T&L, so the sm2 tier with pure software vertex
processing, under Intel's XP driver (14.37.50.4.5260, installed from its
INF) and under Windows 10's (a modded "PHDGD" build) alike.

- **Windows 10 is no place to measure such a card.** The same frame took
  1.8-2.2 s there (sm2 Minimal) and 28 ms under XP: Windows 10's D3D9
  software vertex pipeline cost ~50 us a vertex on this CPU, inside the
  draw calls, whatever the resolution. The reason is in the runtime, not
  the card or its (modded) driver: **Windows 10's `d3d9.dll` interprets
  vertex shaders.** `tools/d3d9/swvp/` measures it without the game
  (`swvp.exe`: a 128x128 grid, fixed function or a 17-slot vs_2_0 program,
  each pool, one draw or 127) and finds where the time goes
  (`sampler.exe`: suspends the busiest thread 1,000 times a second, by
  module and RVA, named offline against Microsoft's public PDBs). Time
  inside the draw call for one 16,384-vertex vs_2_0 draw:

  | | ms | us/vertex |
  |---|---|---|
  | Atom D525, XP SP3, Intel 14.37.50 | 1.35 | 0.08 |
  | Atom D525, Windows 7 x64 (WoW64), "Chell 1.8b" | 4.66 | 0.28 |
  | x99's Xeon, Windows 10 22H2, X550 | 13.5 | 0.82 |

  On x99, 92% of the samples were in `d3d9.dll`'s `CVertexVM`
  (`EvalSource`, `GetDataAddr`, `EvalPredicate`...), an instruction-by-
  instruction interpreter. XP's `d3d9.dll` and Windows 7's 32-bit one carry
  `CodeGeneratorSSE2`, `CSSEPSGPShader4V`/`8V` and AMD's `DeclJIT`: they
  compile each vertex shader to SSE code that runs four or eight vertices
  at a time, and on XP that code was 0.6% of the samples. Windows 10's has
  only the interpreter (the Atom's copy is the same build as x99's, PDB
  `4D99A75D...`). Fixed function (no program) is native there, 0.31 us a
  vertex on x99 against 0.82, but the game's ff tier is vertex programs
  too, so both D3D9 tiers pay the interpreter on Windows 8/10 with a card
  that has no vertex shaders.
- **Windows 7's `d3d9.dll` beside the exe undoes it (2026-10-11).**
  `d3d9.dll` is not a KnownDLL, so a copy in the program's folder loads in
  place of the system's. Windows 7's 32-bit one (SysWOW64) runs on Windows
  10 once one import is repointed: `user32!SfmDxGetSwapChainStats`, a DWM
  statistics call Windows 10 dropped, called from one site that takes a
  zero return as "no statistics" -- `GetWindowRect` has the same shape and
  returns zero for a pointer that is not a window. On x99 (Windows 10
  22H2, the X550's 8.14 WDDM 1.1 driver) the vs_2_0 draw went 13.5 ->
  0.27 ms, fixed function 5.1 -> 0.26, and the game itself, ff tier, pure
  software vertex processing, Minimal: draw calls 26.5 -> 0.9 ms a frame,
  the median frame 26.4 -> 1.9 ms, the picture unchanged. Microsoft's DLL
  cannot ship with the game; it is a fix a player with a Windows 7 licence
  can make on their own machine. Not yet run on the Atom's own Windows 10
  (its SSD wants the BIOS in AHCI).
- **Fixed-function vertex processing only pays where the card does it.**
  `swvp.exe`'s "ff-terr" stage is fixed function set up as the ff terrain
  program would be (texture coordinates generated from the camera-space
  position through a texture matrix, range EXP2 vertex fog). In software
  it is slower than the vs_2_0 program everywhere: 6.9 ms against 1.4 on
  XP, 21 against 13.5 on Windows 10 (which appears to turn fixed function
  into a program for the same interpreter), 0.6 against 0.3 with Windows
  7's DLL. On a device with T&L it costs no CPU at all (the X550's
  hardware pass: 0.00 ms in the draw call), so it is for the M6 and the
  other DX7 T&L parts, not the GMA 3150. Two constraints from XP's
  runtime: fixed function refuses a float3 colour (D3DERR_INVALIDCALL;
  Windows 10 takes it), and it reads a declaration as the FVF it stands
  for, so the elements must come in FVF order -- position, normal,
  D3DCOLOR diffuse, texture coordinates. A colour declared after the uv
  was read from the uv's bytes, as black. The ff tier's vertices are
  already in that order with a float3 colour, so it would want the colour
  packed to 4 bytes on this path. Windows 7 boots on the Atom in IDE mode as
  it is (it had failed once in AHCI, and its next boot went straight to
  Startup Repair).
- **The GMA's XP driver has no 32-bit indices** (MaxVertexIndex 65534)
  and does not pretend otherwise: given the terrain's and the creatures'
  32-bit index buffers it drew a few huge triangles fanned from one vertex
  (sm2) or a black world (ff), and no dragon. The M6's Omega driver reports
  the same cap and copes, which is why the G41 never showed it. Where the
  cap is under 0x10000 each 32-bit draw range is now drawn from a 16-bit
  copy of its own, rebased to its lowest index (`D9Buffer::narrow`;
  `DRAGON_D3D9_INDEX16=1` forces it, `=0` refuses it). Both tiers then
  match the Mac's `--tier` frames. The M6 takes the same path now and has
  not been run with it yet.

640x480 fullscreen, hoard run seed 7, autopilot, 3,600 frames, vsync off,
on the Atom under XP:

| | Average | Median | 1% low |
|---|---|---|---|
| ff, Minimal | 91.4 fps | 11.4 ms | 14.8 ms |
| ff, Lowest | 118.6 fps | 8.8 ms | 11.2 ms |
| sm2, Minimal | 59.8 fps | 14.5 ms | 29.8 ms |
| sm2, Very low | 31.8 fps | 36.1 ms | 55.7 ms |
| sm2, Low (1,200 frames) | 23.5 fps | 37.6 ms | 75.4 ms |
| sm2, Medium (1,200 frames) | 12.6 fps | 71.3 ms | 127.7 ms |

So a card the sm2 tier fits by its pixel shaders can still have software
vertices, and then sm2's own default (Medium) runs at 12 fps; the tier's
default should follow the vertex processing, not only the shader model. sm2 Minimal
also hides the river behind the coarse ground, on the Mac's `--tier sm2`
as well; the ff tier's water, built on the wet cells only, shows it.

**Not done:** the other cards. The order is the G41 under XP SP3 with
each card (record the exports, then `dragon.exe` with no flags, then the
R4/R5 budgets); XP's own D3D9 drivers will have their quirks, and the
thunks' XP sides have run only when forced on a newer Windows. The tests
link the import libraries directly and are for Wine, not XP. A Pentium III
or Athlon XP has no SSE2, which the build requires.

### R7, in progress: graphics settings and presets

The tier is what the hardware can do: HDR, per-pixel or per-vertex
lighting, packed joints, the depth layout. How much it is asked to draw is
eight settings (`gfx/graphics_settings.h`). Each setting is a level, Ultra
(0) to Very low (4), and each step down roughly halves its vertex load:

| Setting | Ultra | High | Medium | Low | Very low | Applies |
|---|---|---|---|---|---|---|
| Model detail (near, far LOD) | as authored | 20K, 3K past 150 m | 10K, 1.5K past 100 m | 5K, 800 past 80 m | 2.5K, 400 past 60 m | restart |
| Terrain grid | 6 m | 12 m | 18 m | 24 m | 24 m (36 m lost the river) | reload |
| Trees (distance, spacing) | 4.2 km, 1× | 2.5 km, 1× | 800 m, 1.8× | 600 m, 2.2× | 400 m, 2.8× | reload |
| Grass radius | 1× | 0.7× | 0.45× | 0.3× | off | live |
| Rocks (triangles, distance) | scanned | 400, 1× | 150, 0.5× | 80, 0.4× | 40, 0.3× | reload |
| Textures | as authored | 1024 | 512 | 256 | 128 | restart |
| Shadows | 4096 | 2048 | 1024 | 512 | off | reload |
| Bloom | on | on | on | off | off | live |

- **A preset is every setting at one level.** A tier starts on its own:
  modern Ultra, sm3 High, sm2 Medium, ff Low. The settings are content, not
  shaders, so a preset renders the same on every backend that runs its tier.
- **What a tier or device cannot do is not offered:**
  - bloom without HDR;
  - a texture or shadow map past the card's largest (the RHI reports it:
    2048 on the X550);
  - full maps uncompressed, or a shadow map past what an SM2 card's memory
    holds.
  Presets clamp to the nearest cheaper level, and are still named as
  themselves (`GraphicsSettings::preset_level`).
- **Terrain casting.** The terrain casts into the shadow map only at 2048
  and above. At 1024 and below the map covers too little of it to repay
  its triangles.
- **The Graphics panel** (first in the right-hand stack) has:
  - the renderer, tier and preset;
  - each setting, with only its allowed levels;
  - Apply and Revert.
  Terrain, trees, rocks and shadows rebuild in place. Model detail,
  textures, the tier and the renderer need a restart, which the panel does
  itself: `main` relaunches without the overridden arguments. Choices are
  saved per user in `graphics.cfg`. `--preset` and `--graphics
  key=level,...` override them, and a headless run ignores them so
  captures stay reproducible.
- **The benchmark.** `tools/bench/presets.py` runs every preset through
  three headless scenarios: `valley` (the start's glide), `course` (the
  autopilot round a generated course) and `run` (the demo pilot in a hoard
  run). It reads each run's summary line: wall-clock fps, median and
  99th-percentile ("1% low") frame times, past the first second.
- **The X550, D3D9, sm2** (2026-10-03). fps, with the median and 1% low
  frame times in ms. High and Ultra are not run on this card any more: the
  Ultra `run` scenario froze x99 outright (no bluescreen, the clock
  stopped), and they ran at 1 to 5 fps in any case.

  | preset | valley | course | run |
  |---|---|---|---|
  | **1024×768** | | | |
  | medium | 11.9 (84 / 85) | 13.3 (77 / 79) | 11.8 (85 / 87) |
  | low | 17.5 (57 / 58) | 19.2 (53 / 55) | 18.4 (54 / 55) |
  | very low | 20.9 (48 / 48) | 23.1 (44 / 46) | 22.9 (43 / 45) |
  | **640×480** | | | |
  | medium | 16.0 (62 / 63) | 18.1 (57 / 59) | 15.2 (66 / 69) |
  | low | 26.8 (38 / 38) | 30.2 (34 / 35) | 27.5 (37 / 38) |
  | very low | 35.1 (29 / 29) | 41.2 (25 / 26) | 38.7 (26 / 27) |

  - Geometry carries Ultra to Medium: 1.4, 4.3 and 9.9 fps in the valley
    at 1024×768, measured before the sky change below. That is a 7× gain.
  - Drawing the sky last at the far plane, and the terrain nearest first,
    lets early depth rejection work. It was worth 20 to 35% at 1024×768
    (Low valley 13.4 → 17.5, Very low 15.4 → 20.9). The sky had been a
    full screen of a 54-instruction shader drawn first.
  - **Below Medium the card is fill-bound.** Very low at 640×480 takes 60%
    of the time at 1024×768 for 39% of the pixels. That puts about 16 ms in
    vertices and the CPU, and the rest in pixels. So Low and Very low are
    640×480 presets on this class of card: 27 to 41 fps, against 17 to 23
    at 1024×768.
  - Frames are steady: the 1% low sits within 5% of the median.
- **A preset renders the same on every backend.** Checked per preset
  (2026-10-03). Each preset's eight golden scenes were captured on Vulkan
  (the 5060 Ti, `--tier sm2 --preset P`). The same scenes were then
  rendered on:
  - D3D9 on the X550 (Medium, Low, Very low): all 24 pass at
    `--cross-compiler`, with means 0.4 to 1.0.
  - Metal on the M-series Mac (all five presets): all 40 pass at
    `--loose`. 37 also pass at `--cross-compiler`. The other three are
    `ground` at Ultra and Medium (2.3% and 3.2% of pixels past 8, against
    2%), and they are shadow texels: SM2's one point-sampled map has texels
    metres wide, and Apple and NVIDIA round the depth test at a caster's
    edge differently.

  Getting there took two content fixes. Before them the same preset built
  different content on the Mac and on Windows, which no GPU tolerance
  should forgive:
  - **The LODs were shaped by the CPU.** clang on arm64 fuses a multiply
    and an add into one FMA by default; x86-64 without `-mfma` cannot. The
    simplifier's collapse errors then round differently and pick different
    edges, and the Very low dragon was a different shape on the Mac.
    meshoptimizer and the two LOD files now build with
    `-ffp-contract=off` (`cmake/meshoptimizer.cmake`). Every LOD logs a
    fingerprint of its input and output (`model0: LOD 80000 -> 2500
    triangles ... (d3c0f447 -> ba49b8e8)`), and all of them match between
    the Mac and x99.
  - **Random draws were unsequenced.** `card(..., unit() * PI, ...,
    palette_dim(c, unit()))` evaluates its arguments in the compiler's
    order: GCC goes right to left, clang left to right. So every crown card
    swapped its spin and its shade between the two builds. The breath
    spray, the boost streaks, the lightning and the status bursts did the
    same with `a * unit() + b * unit()`. So did two draws in the hoard run's
    layout, so one seed built a slightly different valley on each OS. Each
    draw is now its own statement, in the source's order (clang's, so the
    Metal goldens are unchanged). The `vulkan` set was recaptured; the
    `d3d12`, `d3d12-sm3` and `d3d12-sm2` sets still hold the old crowns and
    flames, and fail strict until they are recaptured. SDL's D3D12 driver
    takes DXGI's first high-performance adapter, which on x99 is now the
    X550, and then gives up. It needs the 5060 Ti primary again for that
    (a per-app GPU preference does not reach it).
- **To check a preset across backends**, capture on one and compare on the
  other, with a set name the repo does not keep:

  ```
  golden.py capture --set tmp-sm2-low --extra "--gpu-driver vulkan --tier sm2 --preset low"
  golden.py compare --set tmp-sm2-low --cross-compiler --extra "--gpu-driver direct3d9 --tier sm2 --preset low"
  ```

  On D3D9 this runs through `run_interactive.ps1`, which needs the desktop
  session.

- **Played, not benchmarked** (2026-10-03, after a playtest). On the X550
  the game played at about 20 fps at Low, under 20 on the ground, and after
  several minutes of R restarts x99 froze.
  - **The freeze is not a leak.** Both backends now count what they hold
    (`rhi::ResourceStats`, on the `--telemetry` line as `gpu:`, with D3D9's
    `GetAvailableTextureMem`), and `--restart-every N` presses R every N
    frames. Twelve run restarts and a five-valley `--demo` soak held flat at
    103 buffers (8.7 MB) and 41 textures (5 MB) at Very low. That is a
    tenth of the card. The X550's driver is an XP-era model with no
    timeout detection and recovery, so any GPU hang stops the whole
    machine instead of resetting the card. Heat under minutes of full load
    is the other suspect. Neither can be fixed from here, so the remedy is
    asking less of the card.
  - **It is not vsync.** Windowed D3D9 on this machine does not wait for
    the refresh: one back buffer or two (`DRAGON_D3D9_BACKBUFFERS`) gave
    the same 41.8 fps. The backend now asks for two, the counterpart of
    the SDL backend's mailbox.
  - **It is the terrain's pixels.** `DRAGON_D3D9_PROFILE` now also counts
    each pipeline's written pixels, with an occlusion query around every
    draw. At 640×480 the terrain writes 260 to 330 K pixels a frame (a
    whole screen), and nothing else is a tenth of that. Its ps_2_0 shader
    used 62 of 64 slots, and about 20 of them were the shadow lookup.
  - The saved settings were Low, not Very low. At 1024×768, or in a
    maximised window on its 1024×768 desktop, Low measured 22 to 24 fps,
    which matches the playtest.
- **Two settings for a fill-bound card:**
  - **Resolution** (100%, 85%, 70%, 50%; Live). The world renders at a
    fraction of the window and the HUD stays whole (`gfx::Device` world
    targets). On the HDR tiers the composite samples the smaller scene by
    UV; on the LDR tiers `finish_world` stretches it into the UI's target
    with the new `rhi::Device::blit` (`StretchRect`,
    `SDL_BlitGPUTexture`). Low is 85% and Very low 70%; 50% is offered,
    not preset.
  - **Shadows off is a shader variant on SM2.** ps_2_0 cannot branch past
    a lookup, so `NO_SHADOWS` compiles it out (`sm2_finish`, a bake tier
    `sm2-noshadows`). Terrain goes from 62 slots to 42, foliage from 65 to
    44, meshes from 51 to 31, and the dragon from 57 to 38. The frame is
    pixel-identical to the lookup's at strength 0. The shadow map is still
    read once at a fixed texel, because a slot the shader does not read
    drops out of reflection and the slots must run 0..n-1. The other tiers
    branch on strength and need no variant.
  - Presets now turn shadows off from Low; 512 is offered, not preset.
- **The X550 in a live window** after both (2026-10-03, fps, median / 1%
  low ms):

  | | start | ground | run |
  |---|---|---|---|
  | low, 640×480 | 32.9 (30 / 33) | 43.6 (22 / 31) | 34.0 (30 / 36) |
  | very low, 640×480 | 42.5 (23 / 26) | 54.9 (17 / 25) | 44.9 (23 / 25) |
  | low, 1024×768 | 21.8 (46 / 49) | 24.0 (42 / 49) | 22.3 (46 / 52) |
  | very low, 1024×768 | 28.1 (35 / 38) | 33.2 (29 / 41) | 30.4 (34 / 38) |

  Very low at 640×480 was 32 to 41 in the same window before. `ground` is
  the benchmark's new scenario: landed and walking.

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

### R2b, as built (2026-10-03): model packs

`dragon --bake-packs` writes `<model>.glb.pack` beside every roster model
and prop (`src/anim/model_pack.h`), and a retro tier loads the pack when
there is a fresh one. What it holds is what `load_model` used to compute on
every start: the skeleton and clips, the authored mesh, the LOD at every
model-detail level's near and far budget (eight per species; a budget the
mesh already fits is recorded as "whole"), and each texture's mip chain at
the largest retro cap, 1024, block-compressed with the normal maps already
swizzled. A lower texture level is the same chain with its top dropped
(`gfx::PreparedTexture::trim`), because `cap_image` and `build_mips` halve
the same way; so one file serves sm3, sm2, ff and every preset. The modern
tier never reads one.

- **Where the memory went.** A process with no model peaks at 151 MB. One
  species through the glTF added 530 MB: its five 4096² maps decoded at once
  (320 MB), the file's bytes, the halving copies. From the pack it adds 30.

  | | glTF route | pack |
  |---|---|---|
  | one species, ff | 594 MB, 2.2 s | 182 MB, 0.42 s |
  | one species, sm3 | 651 MB, 1.4 s | 238 MB, 0.58 s |
  | nine species, ff | 983 MB, 6.7 s | 217 MB, 0.59 s |

  A species' pack is 12 to 14 MB, a prop's 5 to 6, 143 MB for everything
  here; the mesh and its LODs are most of it, the textures about 3 MB.
- **The gate:** the eight golden scenes captured from packs and compared
  against the glTF route (`DRAGON_NO_PACKS=1`), at sm3 and at sm2: every
  pixel identical, both tiers. `tests/test_model_pack.cpp` holds the
  round trip, the trim-equals-smaller-cap property, and a real model's LOD
  fingerprint against `simplify_skinned` run directly.
- **Freshness** is the source's size and an FNV hash of its first and last
  256 KB, not its mtime: a zip keeps times to two seconds, so a packaged
  pack would have been refused against its own model. A pack whose model
  is absent is trusted, which is what a retro-only package needs. A stale
  or unreadable pack logs a warning and the glTF is loaded; a device
  without BC formats does the same, since the chains are DXT.
- **The packager** (`package_windows.sh`) bakes the species' packs from the
  staged, shrunk glTFs with the Mac's own build, so the fingerprints are
  the shipped files', and copies the props' from the tree.
- **On the X550, through D3D9**, the pack frame and the glTF frame differ
  in a 14×2-pixel strip of Embercrest's wing edge by 2/255, reproducibly:
  each route alone renders twice identical, and the LOD fingerprints
  match on both. The first suspect was the texture chain -- `std::pow` in
  the sRGB tables and FMA contraction in the box filter and stb_dxt could
  round a few texels differently on arm64 and x86-64, so a pack baked on
  the Mac would carry the Mac's bytes while the glTF route on x99 computed
  Windows'. The sRGB tables are constants now (generated at double
  precision, with a threshold search that reproduces the old rounding
  exactly), `texture.cpp` builds with `-ffp-contract=off` like the LOD
  code, and `test_model_pack` prints each chain's fingerprint, which the
  Mac and x99 now agree on. That is right on its own terms -- one more
  entry for "a preset must build the same content on every compiler" --
  **but it did not remove the strip**: the same 28 pixels differ by the
  same 2/255 afterwards. Mesh, LOD and textures are identical by
  fingerprint, Metal shows no difference in sixteen scenes, and the gate's
  tolerance is three orders of magnitude above it, so it is left as an
  observation: something in the D3D9 driver's handling of two otherwise
  identical resource sequences.
- **Not done:** a package built with packs and run on x99; the retro-only
  package; the Pi's VC4 has no S3TC, so its packs would need an
  uncompressed or ETC1 variant (the chain is built from the same
  `PreparedTexture`, so that is a bake flag, not a format).

### R5, the plan (2026-10-04): fixed function

Fixed function has no shaders, so it cannot be previewed the way the other
tiers are, through the same HLSL on Metal. The plan keeps the render-and-look
workflow anyway by modelling what texture stages can do as a shader variant
first, and making the D3D9 backend match that.

1. **The `FIXED_FUNCTION` shader variant** (the ff tier's define set, in
   place of `SM2` plus `NO_SHADOWS`): per vertex, `saturate(ambient + sun *
   max(N.L, 0))` with D3D9's plain Lambert (no wrap: the wrap becomes a
   little more ambient), the fog amount and the fog colour; per pixel,
   texture × vertex colour (modulate, or modulate2x where the range needs
   it), alpha test, nothing else. No shadow map, no normal or ORM maps, no
   `ldr_encode`: the grade's exposure is folded into the palette, the sun
   and the ambient on the CPU (`SceneUniforms` for this tier), and the
   result clamps. Renders on Metal, D3D12, Vulkan and D3D9 SM3/SM2 as the
   ff preview. The open question in this file, whether it reads as "the
   game, earlier" or "the game, broken", is answered on this render,
   against the concept targets, before any D3D9 work.
2. **Content the tier needs**, each a `Tier::FixedFunction` branch in
   `gfx/` or the app, checked on the preview:
   - terrain: the material per vertex on the CPU at build (the palette
     blend by slope and height, the patch noise, the sun baked or left to
     the lights), the detail texture as the second stage;
   - sky: the analytic sky baked once at start into a lat-long texture
     (the sun is static), drawn on a dome with one stage, so the look is
     the shader's;
   - foliage: the instances expanded into static chunk buffers (no
     instancing, and one draw per tree is too many draws for a 2001
     driver); the coarse cards only;
   - creatures: CPU skinning into a dynamic vertex buffer (a 6K-triangle
     LOD, 4 influences), the preview may keep skinning on the GPU;
   - shadows: a blob under each creature, an alpha-blended decal on the
     terrain, in the main pass; no shadow pass;
   - water: the alpha-blended plane with the sky texture as its reflection;
   - particles, the HUD, ImGui: as they are (`imgui_impl_dx9` is fixed
     function already).
3. **The D3D9 backend's ff path.** `create_pipeline` without shaders: a
   recipe per stem that maps the pipeline's attributes to real usages
   (POSITION, NORMAL, COLOR, TEXCOORD, not TEXCOORDn), and sets the
   transforms, the light, the material, the stages and the fog from the
   uniform blocks the renderers push unchanged: slot 0 vertex is
   `SceneUniforms` (the view is rebuilt from the camera basis, the
   projection from `view_proj` and it), slot 1 `ModelUniforms` (the world
   matrix, the tint as `TFACTOR`). `bind_fragment_textures` becomes
   `SetTexture` per stage. Software vertex processing when the caps have
   no T&L, for the TNT and Rage class.
4. **The gate**: D3D9 ff on x99's 5060 Ti (a modern driver still runs
   fixed function) against D3D12 at `--tier ff`, under `--loose`: stage
   arithmetic is 8-bit and the fog table is the driver's, so only a wrong
   frame fails. Then the X550, which also does fixed function, and the FF
   cards when the G41 is built.
5. **The 1998 preset** below Very low: 16-bit textures at 256² with no
   DXT (the packs decode their BC chains on the CPU for a card without it;
   `PreparedTexture` gets an uncompressed variant), 16-bit Z, 320×240 to
   640×480, one or two bots, no grass.

#### Step 1, as built (2026-10-04): the FIXED_FUNCTION variant

`--tier ff` now compiles every world shader with `FIXED_FUNCTION`
(`gfx::RenderTier::fixed_function`, `pipeline.cpp`), the ff tier allows
only Shadows Off, and the bake scripts carry the variant (`bake_shaders.sh`,
`bake_d3d9.py --tier ff`, which runs it as vs/ps_2_0 on an SM2 card). The
renders are `artifacts/r5-ff-preview/` (ff left, sm2 right, Low): the
valley, the glide side-on, the head and the landing. **It reads as the
game, earlier**: the same valley with a flat-lit, textured dragon, no
shadows, a flat river, the terrain a shade greener where its material
resolves per vertex. Three things the plan had wrong, each found on a
render:

- **Not D3D9 lights.** The plan was the sun, the hemisphere and the
  ambient as `D3DLIGHT9`s in display space, summed and clamped per vertex
  as the fixed-function pipeline does. Display-encoding the sun (2.3 →
  1.46, clamped to 1) and the ambient (0.76) separately put them nearly
  equal, so every face was lit alike and a dark brown hide read as flat
  grey-pink; `MODULATE2X` on top washed every texture to white. The
  variant now **pre-lights**: the modern `direct_sun` and `ambient_light`
  summed in linear light and `ldr_encode_fast`-ed per vertex into the
  vertex colour, which the texture modulates (plain `MODULATE`). That is
  the 2001 pre-lit pipeline, `D3DRS_LIGHTING` off, and it keeps the modern
  sun-to-ambient ratio. For the backend it means no light setup at all:
  the terrain's colours are baked at build (the sun is static), a
  creature's are computed while its vertices are skinned on the CPU, the
  foliage's at batch time.
- **Textures sample UNORM** on this tier (`upload_prepared` strips the sRGB
  flag): a DX7-class card reads a colour map's 8-bit values as they are.
  The lit result is texture × display-encoded light, which is what those
  cards drew.
- **Uniform slot 0 must stay read.** The first fixed-function fragment
  stage read no field of `scene` (its fog colour came as a varying,
  `scene_out` only clamps), so slot 0 dropped out of reflection and the
  `model` block pushed to slot 1 no longer lined up: `material.x` read 0
  and the dragon drew in its vertex colour alone. The same trap as the
  texture slots, one layer up. The finish reads the fog colour from
  `scene` now. A debug edit that returns before `sm2_finish` falls into
  the texture-slot version of it and samples the shadow map as the hide.

The fog is exp2 on the radial distance toward one constant colour
(`Lighting::fog_color`, display-encoded), no height term.

#### Steps 2 to 4, as built (2026-10-04): the tier on the X550

**The variant is shaped for stages.** The FIXED_FUNCTION vertex stage hands
the pixel side exactly what a fixed-function pipeline takes -- `diffuse :
COLOR0` (pre-lit and finished), `fog : FOG` (D3D's factor, 1 clear),
`uv : TEXCOORD0` -- and each stem's preview fragment stage is its stage
recipe spelled out. In that order, because DXC drops a pixel shader's
unused inputs and packs the rest, and D3D12 then refused every pipeline
whose stage read fewer than all three (the sky, the water, the props, the
lines); a stage that reads a prefix leaves its registers where the vertex
stage put them. Metal and Vulkan link by location and never noticed.

**The content** (step 2), each behind `active_tier().fixed_function`: one
card atlas (leaf and needle maps side by side over a mid-grey strip, so one
stage setup serves cards, bark and solids -- white doubled the rocks under
MODULATE2X; 2w x 2h, since the X550 refused a 256x144 one), the coarse
crowns everywhere, a greyscale terrain detail (a stage modulates by the
whole texture), a 64 x 32 sky dome coloured per vertex (the glow survives,
the disc goes soft), a baked disc texture for the particles, and a 32 x 32
water grid (one quad fogged per vertex from the skirt's corners was fog
from shore to shore). **The terrain's diffuse is baked at build**
(`gfx/prelit.h`, the vertex program line for line in C++): the ground and
the sun never move, and on a card without T&L the 158-slot program ran on
the CPU over 55K vertices a frame. The bake is the program's own value to
1/255; finding the one place it was not moved the material's fit to the
terrain (the snow line) to before the meshes are built.

**The D3D9 path** (step 3): `bake_d3d9.py --tier ff` bakes each stem's
vertex program as vs_2_0 with the legacy registers restored by name
(SPIRV-Cross calls every varying TEXCOORDn -- the uv included, which the
order puts third; forgetting it sampled one corner texel of every
texture), and the stem's texture-stage recipe (`FF_RECIPES`) beside it;
the backend sets no pixel shader, applies the stages, routes each fragment
texture slot to the stage that reads it, enables D3D's fog with the
program's oFog and `D3DRS_FOGCOLOR` display-encoded from the scene block,
and the alpha test for the cards. The terrain's detail is one stage
(MODULATE2X TEXTURE x DIFFUSE): as a second stage behind a texture-less
first the X550 drew nothing, legal by the book and not by that driver, and
one stage is what a single-TMU card has. A card without vs_2_0 runs the
same programs through D3D9's **software vertex processing** (mixed mode
with hardware T&L, software alone without); `DRAGON_D3D9_SOFTWARE_VP=1`
forces it. No fixed-function lighting code anywhere: the colours arrive
pre-lit.

**The gate** (step 4), the X550 through D3D9 against D3D12 at `--tier ff`
on the 5060 Ti, all eight scenes under `--loose`: means 0.35 to 1.13,
0.09 to 0.47% of pixels past 8 -- an order of magnitude inside the gate,
and nearly inside `--cross-compiler`. `artifacts/r5-ff-preview/
x550-d3d9-ff-vs-d3d12-ff-valley.png` is the pair. The X550 at Low,
headless 1280x720: 39 fps; at **Minimal** (the sixth preset, R5 step 5,
below Very low: 1.2K creatures, trees to 250 m at 3.5x, 20-triangle rocks,
64-texel textures, 50% world) 97 fps.

**Software vertex processing** on the X550 in mixed mode, Low: 35 s a frame
at first -- the backend named the whole buffer as every draw's vertex
range, and the software pipeline transforms the range named, so 264 terrain
chunks a frame transformed eleven million vertices. With the indices' real
min..max per draw (read once from the index data the backend keeps): 2.5 s.
With the terrain's diffuse baked (158 slots to 32): 0.6 s. With each
chunk's vertices gathered into a run of their own (a chunk's range had
spanned thousands of the grid's vertices for the two hundred it used):
**66 ms a frame at Minimal, 14 fps**, against 98.6 with the card's own
vertex shaders.

**The second round (2026-10-04, later).** 14 fps on a Xeon is nothing on a
Pentium 4, so Minimal was cut further, each cut judged on a render. What
the measurements said first: the software pipeline costs about **1.3 us
per vertex it is handed, whatever the program's length** (158 to 32 slots
barely moved the frame), per draw it costs nothing worth counting (263
draws to 34 made the frame *slower*, because coarse chunks cull less), and
a pure software device (`DRAGON_D3D9_SOFTWARE_VP=2`, what a card without
T&L gets) costs the same as mixed mode. So the vertex count is the one
lever, and Minimal now sheds:
- the terrain grid, to 48 m, with the river widened to a cell and a
  quarter (a 52 m channel is one 48 m cell, and the water drew no pixel);
- the far terrain, a 96 m mesh on the same 320 m chunk grid, in place of
  the near one past a kilometre, each chunk with a 30 m flange down its
  border (the two meshes disagree by tens of metres on a steep slope, and
  the gap showed sky); the far river keeps the near width and breaks into
  dashes out there, the lesser sight against a sawtooth bank;
- the sky, a 512x256 lat-long map baked once on a 153-vertex dome (the
  sun's disc is back) -- stage 0 SELECTARG1 TEXTURE on D3D9;
- the checkpoint rings, 16 x 6 and none past 1.5 km (twelve 400-vertex
  rings through a 99-slot program had been a quarter of the frame);
- the water grid 16 x 16 and the skirt a 1250 m cell, snapped to a divisor
  of the half extent after a 1153 m one drew its partly-inside cells over a
  kilometre of valley.

On the X550: **Minimal 54 fps with every vertex processed in software, 173
fps with the card's own; Low 9 fps in software**, which is not a preset for
it. `artifacts/r5-ff-preview/ff-low-metal-vs-minimal-x550-softwarevp.png`
is Low beside the X550's software-processed Minimal. A Pentium 4 is three
to four times slower than this Xeon per thread, so Minimal should land in
the teens there; the real cards will say. If they want more: the foliage
pre-batched with its colours baked (one draw per instance is the other
thing software processing cannot avoid), and the creature's far LOD sooner.

## Beyond the three tiers: the extended study (2026-10-03)

What it would take to reach the targets past R5, evaluated against the port
as it stood at `b3b13a2`. Two measurements frame all of it, both from a
headless Mac run at `--tier ff --preset very-low`, one species, three bots
in a match, 600 frames:

- **0.5 ms median a frame, render included** (1.15 s of user CPU for the
  whole 1.6 s run, load included). The simulation is cheap. By rough
  single-thread ratios that is 3 to 4 ms on the G41's E3300, 6 to 8 ms on
  the P4 630 bench and **about 50 ms on a Pi 1B's ARM11** (a Pentium II
  300 with no SIMD). Every PC target fits; the Pi 1 is marginal, not
  hopeless.
- **679 MB peak RSS.** That is the wall for the Pi 1 (512 MB in total), it
  is the 32-bit XP question below, and it is three orders of magnitude off
  any microcontroller. It is load-side: the 4096² textures are decoded
  before the tier halves them, and the 80K mesh is kept for the CPU
  measurements. **Baking per-tier asset packs offline** (LODs, capped and
  compressed textures, no full mesh) is the shared prerequisite for XP,
  the Pi and the G41's load time, so it is its own item (R2b below), ahead
  of any target that needs it.

| Target | Needs | Size | Verdict |
|---|---|---|---|
| **D3D9 fixed function (R5), down to TNT / Rage XL** | The FF material layer in the D3D9 backend (texture-stage recipes for the seven pipelines, CPU skinning, blob shadows, table fog). For the DX6-class cards: **software vertex processing** when `D3DDEVCAPS_HWTRANSFORMANDLIGHT` is absent, 16-bit uncompressed textures at 256² (no S3TC before the GeForce 256), 16-bit Z, and a "1998" preset below Very low | ~2K lines, 3 to 5 sessions, one more for the 1998 preset | Already the plan. TNT and Rage XL stay inside the D3D9 tier: their XP drivers are DDI 7 or later, so D3D9 FF runs on them with the runtime's software T&L. Not a backend, a preset. The risk is the look, which a render decides at R5 |
| **D3D11, FL 11 and 10** (done 2026-10-04, "The D3D11 backend") | A new RHI backend (~1200 lines, simpler than D3D9: cbuffers, no lost device, no pools). The bake adds fxc profiles `vs/ps_4_0` and `5_0` through the existing `hlslc` wrapper and SPIRV-Cross `--shader-model 40/50`. `imgui_impl_dx11` exists | 2 to 3 sessions | Worth more than it looks. SDL3 GPU has no D3D11 backend. Two owned cards have **neither D3D12 nor Vulkan**: the HD 5450 (TeraScale) and the Bay Trail board; the G41's GMA X4500 is FL 10_0. D3D11 is the only path that runs the modern HDR tier on them. And **WARP comes free**: software rendering at every feature level |
| **D3D11 FL 9_x** (done 2026-10-04, "Feature level 9 on D3D11, as built") | The same backend with the `_level_9_1` and `_level_9_3` profiles. 9_3 maps to the sm3 shader tier, 9_1 and 9_2 to sm2. The known 10level9 limits are already met by those tiers (no SV_VertexID, no depth SRV, instancing only at 9_3, and the backend has a non-instanced path). Check 16F targets and 32-bit indices per format with `CheckFormatSupport`; 9_1 has neither | Half a session on top of D3D11 | A cheap curiosity. The X550 is 9_2 **only through a WDDM driver**; Windows 10 loads no XPDM driver at all, so the testsigned driver on x99 must already be the legacy Vista/7 WDDM one, and `dxdiag` will print its feature level. Expect the SM2 picture exactly: it is the same silicon as D3D9 sm2. The 3DMark Ice Storm box gets ticked and nothing new appears on screen |
| **Software rendering** | Nothing, at first: `VK_ICD_FILENAMES=.../lvp_icd.x86_64.json ./dragon` on x99's CachyOS (Mesa lavapipe), SwiftShader's ICD on any OS, WARP once D3D11 exists | An hour to measure | Do the free one first. Lavapipe on the ten-core Xeon at 640×480 sm2 should give 10 to 20 fps. An own rasteriser is justified only by the microcontroller item, where nothing else exists |
| **Pre-DX7: S3 ViRGE/DX, SiS 6326, Riva 128** (all owned) | A D3D7 (or DX6) immediate-mode backend. D3D9 FF is DX7's model, so if R5's material layer is written API-agnostic (stage ops, a vertex layout, light and fog state) this is ~800 lines of device code. Runs only on **98 SE** (XP's in-box ViRGE driver is 2D), so it sits behind R8 and the SiS AGP bench, which is in China. Content a notch below 1998: 2 MB of textures in total, one stage, 320×240 | 3 to 4 sessions plus unknown driver pain | A trophy, not a port. It would start and draw a dragon over a flat-shaded heightfield. Last among the PC items; do it once R5's layer and R8 exist, when its marginal cost is small |
| **Raspberry Pi 1B** | A **GLES2 RHI backend** (~1200 lines; the shaders are SPIRV-Cross GLSL ES 1.00 from the sm2 variants, compiled by the driver at runtime, so nothing is baked; VC4 has no S3TC, so uncompressed 512² or ETC1), R2b's memory diet, and a "pi" preset (one species, one or two bots, 320×240) | 3 to 5 sessions, the CPU unknown | Possible; prove it on a Pi 3 or 4 first. The backend also covers Pi 2 to 4, pre-Vulkan Linux GPUs and the VC4 class in general. The Pi 1 itself is 50 ms of simulation plus a Mesa driver on an in-order 700 MHz core: 10 to 15 fps at best. Develop the backend on x99's Mesa GL, not on the Pi |
| **ESP32-S3, Pico 2 with DVI** | Not a port: a **demake**. Reuse `src/core` and `game/flight.cpp` (589 lines, renderer-free, any GCC 13+), a two- or three-octave heightfield, a hand-authored ~300-triangle dragon on a ten-joint rig, and an own rasteriser at 320×240×16 (a 150 KB framebuffer fits the 520 KB of SRAM; the Pico 2's HSTX does 640×480 doubled) | 5 to 10 sessions, its own directory and toolchain | Feasible as its own project: Quake runs on the RP2350 at those resolutions, so a few thousand gouraud triangles a frame at 20 to 30 fps is in reach. Nothing of the asset pipeline, the rig or the renderer carries over; the flight model and the feel do. The rasteriser doubles as the Pi 1 fallback |

Hardware: no TNT or Rage XL is in `cards.csv`; the FF cards on hand (the
M6, the GeForce4 MX PCI, the Radeon 7000 PCI, the Matrox G550 PCIe, the
Extreme Graphics 2) cover R5, and both are cheap on Xianyu when the 1998
preset is ready for them.

**The 3DMark2001 dragon** (asked 2026-10-04): x99 has 3DMark2001 SE
installed, and its two MAX-FX archives (`data.ras`, `textures.ras`; the
Max Payne engine's) hold their files raw behind an encrypted directory --
148 LightWave objects, 22 `.lws` scenes, 380 JPEG and 356 DDS images,
carved out by their headers with `tools/3dmark/carve_ras.py` into
`assets/alt/3dmark2001/` (gitignored). The Dragothic dragon is there: the
body at 8,838 points and 16,817 polygons (its surfaces named in Finnish,
`drago.siipi` the wing), a head LOD, the eyes, the rider, and a 98K-polygon
`drako_defskin`. `tools/3dmark/lwo2gltf.py` converts it to a static glTF
(15 surfaces, each with its UV set and its clip's name, six weight maps as
extras, a 19.5-unit wingspan against the engine's assumed 19 m); the
filenames are lost with the directory, so its textures are unnamed and must
be matched by eye from the carved set. **The rig is the job left**: the
deform skeleton fitted as the other species' were, briefed in
`tools/3dmark/dragothic_rig.md` for Codex (gpt-6.1-sol, high) when its
window has room; the loader takes nothing unskinned. It is UL's
copyrighted data: a personal study at most, never committed, never
shipped, and `ATTRIBUTION.md` says so.

The rig was fitted on 2026-10-04 (`tools/3dmark/dragothic_rig_report.md`),
with three of the textures matched by UV and the rest left at their
surfaces' flat colours -- which read wrong beside the benchmark: cyan
spikes, pink teeth, an orange belly, a brown head where the benchmark's is
olive green, and a neck that sits the head on the shoulders. **The
reference is the benchmark itself**: x99's install is the Pro edition, and
Game 2 (Dragothic, high detail) runs on the X550 at 1024x768, driven from
SSH through the console desktop (`tools/x99/desktop_act.ps1`: clicks,
window focus, and a GDI capture that does see the fullscreen D3D8 frame).
Ninety frames at 0.7 s cover the test; the close passes over the head and
under the jaw are frames 30 to 33. They live beside the model in
`assets/alt/3dmark2001/reference/` (UL's frames, gitignored). The Pro
edition's Image Quality test, which should write a chosen frame to
`Quality\*.bmp`, ran and wrote nothing on Windows 10.

**The order chosen** (the user's call, 2026-10-03):

1. The acceptance already queued: the G41 for R0, R3 and R4, then R6.
2. **R2b, per-tier asset packs**: the memory diet, which unblocks 32-bit
   XP, the Pi and the G41's load. Done the same day ("R2b, as built").
3. R5, designed for the 1998 floor from the start: software vertex
   processing, 16-bit textures, the API-agnostic material layer.
4. D3D11, with FL 9_2 on the X550 as the free extra once `dxdiag` confirms
   its feature level.
5. Lavapipe and SwiftShader, the next time x99 is on.
6. GLES2 when a Pi 3 or 4 is on the desk; the Pi 1 after that.
7. D3D7 for the ViRGE once R8 exists; the demake as a separate project.

## The D3D11 backend (2026-10-04)

Item 4 of the order chosen above, built in a session: `src/rhi/d3d11/`, a
third `rhi::Device` beside SDL's and D3D9's, reached with `--gpu-driver
direct3d11` (or `direct3d11:N` for DXGI adapter N) and listed in the
Graphics panel's renderer combo. Windows only. Nine hundred lines, and
simpler than the D3D9 one by the amount the API grew up: constant buffers
instead of register copies, views instead of pools, no lost device, a
stride on the buffer bind instead of a declaration, real instancing with a
first instance, and depth textures that are sampled as themselves (typeless
`R32_TYPELESS`, a `D32_FLOAT` depth view and an `R32_FLOAT` shader view).

**Why it exists** when SDL GPU already runs D3D12 and Vulkan on Windows:
two owned boards have neither -- the Radeon HD 5450 (TeraScale, feature
level 11_0 on D3D11, no D3D12 driver) and the Bay Trail IGP -- and the
G41's GMA X4500 is feature level 10_0. D3D11 is the only API that runs the
**modern HDR tier** on them. And WARP, Microsoft's software rasteriser,
comes with it on every Windows (`DRAGON_D3D11_WARP=1`), which is the
software-rendering item of the study for free.

**The shaders** are the same HLSL through one more translation:
`tools/d3d11/bake_d3d11.py OUT [--tier ...] [--profiles sm50,sm40]`
runs shadercross to SPIR-V with the tier's defines (the D3D9 bake's define
sets minus `D3D9`: D3D11 has `SV_VertexID` and sampled depth), SPIRV-Cross
back to shader model 5.0 HLSL, and `d3dcompiler_47` through the D3D9
bake's `hlslc` wrapper (Wine on the Mac, the same environment as the D3D9
bake) to DXBC: `vs/ps_5_0` for feature level 11, `vs/ps_4_0` for 10. The
files are `<stem><variant>.<vertex|fragment>.<sm50|sm40>.dxbc` beside the
other backends' bakes; the device loads the profile its feature level
reached. The whole modern set, 60 stages, bakes in 40 s.

What SPIRV-Cross's SM 5.0 output gives, and the backend relies on: each
uniform block is a `cbuffer` at `register(bN)` with N our slot, laid out by
`packoffset` so the C++ struct still matches byte for byte (no unwrapping,
no register copies -- `push_uniforms` maps a dynamic constant buffer with
DISCARD and binds it); a fragment texture in slot N is `tN` with its
sampler at `sN`; every vertex input is `TEXCOORD<location>`, which the
input layout names by index; `SV_VertexID` is there for the fullscreen
passes. The pushed blocks go into one of five dynamic constant buffers per
stage and slot, sized 256 bytes to 64 KB, the smallest that fits, so the
driver renames a few hundred bytes for the blocks pushed thousands of
times a frame and not 64 KB each time.

**The one piece of content** the backend owns is `shaders/blit.hlsl`: the
RHI's `blit` (the world rendered at a fraction of the window, brought up
to the UI's size) and the present are both a fullscreen triangle that
stretches a texture, since D3D11 has no `StretchRect` and no
`SDL_BlitGPUTexture`. It is baked without a variant suffix, loaded lazily
from the shader directory the first pipeline named, and on D3D9 it is a
disabled recipe (`FF_RECIPES`), since that backend never draws it.

**Measured on x99's 5060 Ti** (feature level 11_1, `sm50`), headless from
the SSH session like D3D12, the eight golden scenes rendered through it:

| Against | valley | dragon-side | dragon-head | ground | fire | run-hud |
|---|---|---|---|---|---|---|
| `metal`, mean /255 | 0.29 | 0.20 | 0.14 | 0.21 | 0.26 | 0.29 |
| `d3d12` (the same card through DXC), mean | 1.13 | 0.59 | 0.36 | 2.25 | 1.11 | 1.13 |
| for scale: the `d3d12` set against `metal` | 1.29 | 0.72 | 0.46 | 2.36 | 1.25 | 1.29 |

So the D3D11 picture sits nearer the Mac's than D3D12's own does -- fxc's
build of SPIRV-Cross's HLSL and Metal's build of the same SPIR-V agree
better than either does with DXC's DXIL, whose difference is a checker of
mip and anisotropy choices over the terrain and the grass (the diff
images). The renders are the new `tests/golden/d3d11` set, and the gate
for a change to this backend is `compare --set d3d11` plus `--set metal
--loose`, as for the others. `DRAGON_D3D11_FEATURE_LEVEL=10_0` caps the
device at 10_0 and loads the `sm40` set: **bit-identical** to the 11_1
frames on this card, all eight scenes, so the 4_0 profiles need no set of
their own. Dear ImGui draws through `imgui_impl_dx11`, which compiles its
two shaders at runtime through `d3dcompiler_47.dll` -- the one DLL the
Windows package now imports beyond the system set (`package_windows.sh`
allows it; every Windows since 8.1 has it).

**WARP, measured** (`DRAGON_D3D11_WARP=1`, Microsoft's software
rasteriser, feature level 11_1 on x99's ten-core Xeon E5): all eight
scenes render, within 0.16 to 0.35/255 of the 5060 Ti's D3D11 set (a
different rasteriser's filtering; `--loose` passes everything). At
1280x720 it runs the modern tier at **1.8 fps at Ultra, 4.8 at Medium and
19.6 at Minimal**, so a machine with no usable GPU driver at all plays the
sixth preset at a frame rate, which was the study's software-rendering
item answered without writing a rasteriser. The X550 at Minimal through
D3D9 is 173 fps, for scale.

**Not done, in order:** a lost-device equivalent (a removed device is
logged and the frame goes on drawing nothing; D3D9's reset is in the same
state); fullscreen; the D3D11 shaders in the Windows package (the bake
needs Wine on the Mac, like D3D9's, and both are deployed by hand to
`D:\dragon-win\shaders` for now); and the HD 5450 and Bay Trail boards,
which the backend was built for and which have not been powered on.
Feature level 9 is the next section.

### Feature level 9 on D3D11, as built (2026-10-04)

The X550 is feature level 9_2 under its WDDM 1.0 driver, and the 3DMark Ice
Storm box is the only reason anyone remembers 10level9 exists. It is ticked:
`--gpu-driver direct3d11:1` on x99 probes the adapter's feature level before
the tier is fixed (`rhi::d3d11::probe_feature_level`), takes the sm2 tier
for anything under 10_0, and draws the sm2 valley on the X550 **within
`--cross-compiler` of the D3D12 sm2 set in all eight scenes** (means 0.36
to 0.90/255; the 5060 Ti forced to 9_2 with `DRAGON_D3D11_FEATURE_LEVEL=9_2`
is within 0.006 of its own D3D12 sm2 render). At 1024x768 the X550 runs
**Low at 20 fps, Very low at 26, Minimal at 48** against the D3D9 backend's
21 to 24, 28 to 33 and 173 (the last at 1280x720): the 10level9 layer costs
a tenth at the shader-bound presets and half at Minimal, where the frame is
draw calls -- a draw per foliage instance -- through two API layers. The ff
shader variant runs the same way (`--tier ff`, about 80 fps at Minimal).
Windowed, on the desktop session, the X550 presents at 31 fps at its saved
settings and the 5060 Ti at the 60 of vsync. Nothing new appears on
screen, as predicted; the point is that the modern code path reaches the
card without the D3D9 backend. (DXGI numbers the adapters per session:
the X550 is `direct3d11:1` from SSH and `direct3d11:0` on the desktop,
where it drives the display.)

**The shaders:** `tools/d3d11/bake_d3d11.py OUT --tier sm2|sm2-noshadows|ff
--profiles fl91` bakes the tier's variant with `LEVEL9` defined through
fxc's `vs/ps_4_0_level_9_1` profiles, which run at 9_1, 9_2 and 9_3 alike.
`LEVEL9` shares D3D9's two macros (no `SV_VertexID`: the fullscreen passes
read the index from the backend's own buffer; no sampled depth: the shadow
map is an R32F colour stand-in the depth-only passes write z/w into) and
adds one of its own: joint indices arrive as UNORM bytes scaled back by
255, since D3D11 type-checks the input layout against the shader and
level 9's shaders have no integer inputs. There is **no level_9_3 set**:
`ps_4_0_level_9_3` still has ps_2_x's 32 constant registers and no
`texlod`, so the sm3 tier's pixel shaders do not fit it ("Invalid const
register num: 32", "texlod not supported on this target"), and 9_3 runs
the sm2 tier like 9_1 and 9_2.

**What level 9 took**, each found from the SSH session by reading the debug
layer back through `ID3D11InfoQueue` into the log (`DRAGON_GPU_DEBUG=1`;
`DRAGON_D3D11_TRACE=1` adds every pass, bind and draw, and drains the
messages after each, which is how a silent `DXGI_ERROR_DRIVER_INTERNAL_ERROR`
was placed on the draw that raised it):

1. **Typed depth.** Level 9 has no typeless resources: a depth texture is
   created as `D24_UNORM_S8_UINT` or `D16_UNORM` itself (D32F is not there
   at all; `gfx::Device` falls back to D24 on its own), and the sampled one
   gets the R32F stand-in, which the X550 renders to but cannot filter, so
   it is point-sampled (the shaders' 2x2 PCF still softens it).
2. **Samplers:** `MaxLOD` must be `FLT_MAX`, anisotropy at most 2 at 9_1.
3. **Constant buffers are loaded at their declared size.** A pixel shader
   that declares the 39-register scene block and reads three members of it
   still has 39 registers set, against the 32 a level-9 pixel shader has,
   and both the X550's driver and the 5060 Ti's answered the first terrain
   draw with `DXGI_ERROR_DRIVER_INTERNAL_ERROR` and a removed device. The
   SM 5.0 HLSL SPIRV-Cross writes declares every member with `packoffset`,
   so the bake rewrites each cbuffer to the members the stage uses, at
   consecutive registers (`compact_cbuffers`), and writes the moves as a
   `.l9copies` sidecar; the backend keeps each pushed block raw and
   assembles the compact layout before the draw (`flush_level9_uniforms`).
   The D3D9 bake solved the same problem by unwrapping cbuffers into
   globals and reading fxc's constant table back.
4. **The skin palette.** With the scene block compacted, both drivers still
   removed the device at the first skinned draw: its cbuffers declared 4 +
   4 + 192 registers (the 64-joint `PACKED_JOINT_LIMIT`), under 256, and
   fxc had accepted them. Declaring the sm2 tier's 50 joints under `LEVEL9`
   (150 registers) ended it. Whatever the layer reserves, 200 declared
   registers is past it and 158 is not; the limit was not measured finer.
5. **No instancing below 9_3,** and no instanced draw call at all, even
   for one instance: an instanced slot binds per draw at that instance's
   element with a stride of 0, the layout reads it per vertex, and the
   draw repeats (the D3D9 backend's loop). Plain `Draw`/`DrawIndexed`
   everywhere else.
6. **Whole-resource copies only:** the screenshot readback copies the
   whole texture into staging and reads the top-left region.
7. **Dear ImGui's D3D11 renderer** asks for `vs_4_0`/`ps_4_0`, which a
   level-9 device refuses, clamps its font sampler's `MaxLOD` to 0, and
   reports a 16384-texel atlas limit. `cmake/imgui.cmake` patches the
   fetched source to the `level_9_1` profiles (they run on every feature
   level) and to `FLT_MAX`, and `imgui_layer.cpp` sets the atlas limit from
   `max_texture_size()`. With those the run HUD draws on the X550.
8. **Stage signatures must match register for register.** DXC drops a
   pixel-stage input the shader never reads and SPIRV-Cross renumbers the
   rest, which the modern backends link by semantic and level 9 refuses
   ("Semantic 'TEXCOORD' is defined for mismatched hardware registers"),
   drawing nothing: the ff sky dome's fragment stage read only the uv of
   its three varyings and the X550 drew a flat sky. It now multiplies by
   the fog and the diffuse alpha (both 1), the trick particles.hlsl
   already used for the same reason on D3D12.

**9_1** runs too, checked on the 5060 Ti forced there
(`DRAGON_D3D11_FEATURE_LEVEL=9_1`, the debug layer on, no messages; the
run HUD at 160 fps). It takes three more things than 9_2: **no R32F
texture**, so no shadow stand-in -- `supports_format` says a depth target
cannot be sampled, `DeviceLimits::sampled_depth` holds the Shadows setting
at Off (the sm2-noshadows variant), and the shadow map becomes a 1x1 white
texture in its slot; **no 32-bit indices**, so the backend keeps a copy of
each index buffer and draws a 32-bit range as a 16-bit buffer rebased to
its smallest index, made once per range (`narrow_indices`; every draw the
game makes spans under 65535 vertices); and **no separate alpha blend**.
Textures cap at 2048 below 9_3.

Not tested: real 9_1 or 9_3 hardware (none owned that runs Windows 10),
and 9_3's instancing path (it reports instancing and takes the native
draws). The GeForce 6200 is SM3 hardware, but it reaches D3D11 at all
only through NVIDIA's last Vista/7 WDDM driver (309.08); under XP, the
G41's OS, there is no D3D11 and it runs the D3D9 backend. Which level that
driver reports -- 9_3 for SM3 parts, by the book -- is a `dxdiag` on a
Windows 7 install to read, not an assumption; the backend runs any of
the three, since all take the same level_9_1 bytecode.

### vs/ps 1.x for the DX8 cards: the vertex side, not the pixel side (2026-10-04)

The question was whether a shader-model-1 tier is worth having for the
GeForce3/4 Ti and Radeon 8500-9250 class, or whether those should simply
run the FF tier.

- **`ps_1.x` buys nothing.** The FF tier's pixel side is one or two
  texture-stage operations -- a texture MODULATE or MODULATE2X with the
  pre-lit diffuse, an alpha test for the cards -- and `ps_1_1` is exactly
  those operations written as eight instructions over four textures, at
  the same fixed-point precision. What the SM2 tier adds over FF (the
  shadow lookup with its 2x2 PCF, the per-pixel grade) needs `ps_2_0`'s
  64 instructions and float precision; `ps_1_4`'s dependent reads do not
  reach it either. A ps_1.x tier would be a third compiler for the same
  picture. So a DX8 card runs the FF recipes for its pixels.
- **`vs_1_1` would help, and is cheap.** The FF tier's vertex programs are
  `vs_2_0`, and a DX8 card has `vs_1_1` in hardware: 128 instructions, 96
  constants, no flow control. Today that card runs the programs through
  software vertex processing, 54 fps at Minimal on x99's Xeon and less on
  the Pentium 4 these boards pair with ("The second round"). The programs
  are already loop-free; what has to give is size: the skinned palette
  must fit 96 constants, so about **25 joints a draw** (the partitioner,
  `anim/skin_partition.h`, already splits to a budget), and the foliage
  program's 188 slots must come down to 128 (skinned is 124, terrain 17:
  `bake_d3d9.py --tier ff --counts`). fxc compiles the same HLSL to
  `vs_1_1`, so it is a bake profile, two register budgets and the
  partition size -- a session, for every vertex transformed by the card
  instead of the CPU.
- **DX7 T&L cards (GeForce2/4 MX, Radeon 7000) have neither**, and
  software VP is per-vertex bound, so the only lever there is to hand the
  *static* geometry -- the terrain, the foliage with its world matrix, the
  water, the sky, nine tenths of the vertices -- to the card's
  fixed-function transform, and process only the skinned dragon in
  software (or skin it on the CPU). That is the API-agnostic material
  layer the R5 plan named and did not build; it would also be the D3D7
  backend's whole vertex side, so it is worth doing once, there.

## Where it is tested

The Mac stays the daily driver. Each other machine has one job, chosen so the
slow-to-reach machines are only used for what nothing else can show.

| Platform | Role | Why it, and not another |
|---|---|---|
| **Mac, Metal** | Daily development; the reference goldens | Hot reload, the whole headless workflow, Blender and the tools |
| **Mac, MinGW-w64 + plain Wine** | Build every Windows binary on the Mac; smoke-run them under Wine, run directly (2026-10-03: Sikarugir's Wine 10 engine, with the template's libraries linked beside it, a scratch prefix, no CrossOver); run `d3dcompiler_47` for the D3D9 bake | No machine to power on. The D3D9 spike draws within 0.017/255 of x99 and the D3D9 backend within 0.29/255, and the modern exe on Vulkan (MoltenVK) within 0.46/255 of the Metal golden. **Not a correctness oracle:** wined3d translates D3D9 to OpenGL, so it answers "does it start and roughly draw", and its caps are a fake GeForce 6800. Two paths do not work: D9VK (DXVK's D3D9 on MoltenVK) will not load in Wine's 32-bit WoW64 mode, and GPTK's D3DMetal loads, then crashes SDL's D3D12 backend on a null call inside D3DMetal |
| **x99, CachyOS** | Vulkan; the Linux release; Linux headless soaks over SSH once P0 removes the window | The only Linux box with a real modern GPU. Power it off after each session |
| **x99-windows, Windows 10** | D3D12, D3D11 (and WARP) and Vulkan on Windows; **the D3D9 debugging platform for all three tiers**, with `--tier` forced on its modern card's driver; apitrace for D3D9 call traces and replays | A real D3D9 driver and runtime, a debugger, and fast iteration. A modern driver still runs D3D9 fixed function. But Windows 10 cannot drive a pre-DX9 card, so this proves the code, not the hardware or the budget |
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
  R2 is separate from the backends, and it is what R2b's asset packs
  answer: a 64-bit `--tier ff` run peaks at 679 MB today because the full
  textures and mesh are decoded before the tier cuts them.
- **Fixed-function look.** Whether it reads as "the game, earlier", or only
  as "the game, broken", is a judgement to make on a render at R5 against the
  concept targets, not something to settle in advance.
