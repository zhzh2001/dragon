# Porting: Windows, Linux and the shader source (P0-P2)

The modern ports: a build that runs on someone else's machine, one HLSL
source for every backend, and Windows and Linux on SDL GPU's D3D12 and
Vulkan. All three were done by 2026-10-02.

Part of the porting record; [`../PORTING.md`](../PORTING.md) is the index, with the
state of every phase and a map from the section names cited in code
comments ("R6, as built", "The G550 on the G41") to the file that holds them.

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
