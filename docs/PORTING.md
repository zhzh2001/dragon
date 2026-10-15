# Porting roadmap: Windows, Linux, Direct3D 9, and mobile

Where the game goes after macOS, in order, and where each step is tested.
This supersedes the port study in `RETRO.md`. That study was written before
the post stack, the hoard run and the roster existed, and two of its premises
no longer hold: "no HDR target, no post chain", and "GL 2.1 first, because it
debugs on the Mac". A D3D9 build now debugs on the Mac too, under Wine
(see [Where it is tested](#where-it-is-tested)).

The short answer to "Windows and Linux shouldn't be hard, right?": **yes, on
modern hardware.** SDL3's GPU API already has Direct3D 12 and Vulkan backends,
the game uses nothing exotic from it, and every system above the renderer is
plain C++. The real work was translating the shaders, plus one packaging fix
that every platform needed, the Mac included. **Direct3D 9 is a different
size of project**, roughly as big as everything before it put together,
because SDL3 GPU has no D3D9 backend and fixed function has no shaders at all.
It is laid out here as one D3D9 backend with three capability tiers.

This file is the index. Until 2026-10-13 it was also the whole record, 2,400
lines of it; the record now lives in `docs/porting/`, one file per part, and
this file keeps what a new session needs first: where each phase stands,
the loose ends, the machines, and where each section went.

## Where each section went

Code comments cite sections by name (`docs/PORTING.md, R6`, `"The G550 on
the G41"`). Those names are unchanged; this is where to find them.

| Cited as | File |
|---|---|
| "What the code depends on today", P0, P1, P2, "Using x99 for this" | [`porting/MODERN.md`](porting/MODERN.md) |
| R0, R1, R2, R3, R4, R2b ("model packs") | [`porting/D3D9.md`](porting/D3D9.md) |
| R5 (its plan, "Step 1", "Steps 2 to 4", "The second round"), "vs/ps 1.x for the DX8 cards" | [`porting/FIXED_FUNCTION.md`](porting/FIXED_FUNCTION.md) |
| R6, the XP platform layer: toolchain, SDL3 subset, thunks, the import gate | [`porting/XP.md`](porting/XP.md) |
| "R6, as built" on real cards: "It runs on XP", the M6 rounds, "Headless overstates the HUD", "For the cards below the M6", "A second machine" (the Atom D525), "Three vertex paths", "The G550 on the G41", the bridge | [`porting/CARDS.md`](porting/CARDS.md) |
| R7, "Budgets to design to", "A preset renders the same on every backend", "Played, not benchmarked" | [`porting/PRESETS.md`](porting/PRESETS.md) |
| "The D3D11 backend", "Feature level 9 on D3D11" | [`porting/D3D11.md`](porting/D3D11.md) |
| "Beyond the three tiers", "The 3DMark2001 dragon", "The order chosen" | [`porting/BEYOND.md`](porting/BEYOND.md) |
| The mobile track: Android phones, the VisionFive 2, the Raspberry Pi 1B (M0-M5) | [`porting/MOBILE.md`](porting/MOBILE.md) |

The modern build's own direction -- more fidelity at the top presets -- is
not a port and lives in [`FIDELITY.md`](FIDELITY.md).

## Where the port stands (2026-10-13)

The mirror was last exported at `4d90e0b` (2026-10-03, through R7's
graphics settings; `docs/RELEASE.md`). Everything below that is newer is
local.

| Phase | State | What is left |
|---|---|---|
| P0-P2 | Done | A Linux package (an older-glibc container) |
| R0 | Done | -- |
| R1, R2 | Done | -- |
| R2b (model packs) | Done: `--bake-packs`, pixel-identical at sm3 and sm2. The packs carry DXT only | A retro-only package that ships packs without the glTFs; an uncompressed or ETC pack for cards without DXT (the G550 loads glTFs, 25 s to the first frame; the Pi has no DXT either) |
| R3 (SM3) | Matches D3D12 at sm3 on x99's 5060 Ti (`tests/golden/d3d12-sm3`). Lost-device reset, exclusive fullscreen | The G41 with the GeForce 6200 TC |
| R4 (SM2) | Runs on x99's X550 within `--cross-compiler` of D3D12's sm2 render; on the Atom D525's GMA 3150 under XP, sm2 Minimal at 60 fps with software vertices | The G41 with the X550: "a valley at 1024x768" |
| R5 (FF) | Done through the gate (`porting/FIXED_FUNCTION.md`): the X550 through D3D9 fixed function within a mean of 1.1/255 of D3D12 in all eight scenes. Minimal and Lowest presets; three vertex paths (`DRAGON_D3D9_VERTEX_PATH=program\|ff\|cpu`), path `cpu` the default on Windows 10's interpreting runtime and on Matrox | The GeForce4 MX and Radeon 7000 on the G41; blob shadows |
| R6 (XP) | **Runs on XP** on three cards (`porting/CARDS.md`): the M6 (died since) to 52.6 fps at 640x480 Minimal fullscreen; the Atom's GMA 3150, ff Lowest at 119 fps; the Matrox G550 PCIe at 55 fps Lowest, 40 Minimal, once its PCIe-to-PCI bridge's prefetch was tuned | The other cards; keyboard and gamepad play by hand |
| R7 (presets) | Seven presets, Ultra to Lowest; the Graphics panel; `tools/bench/presets.py` | Particle caps and fewer bots at the low presets; 30 fps on the SM3 floor card |
| R8 (98 SE) | Not started | -- |
| D3D11 | Done at feature levels 11, 10 and 9 (`porting/D3D11.md`) | The HD 5450 and Bay Trail boards; the shaders in the package |
| **M0-M5 (mobile)** | Planned 2026-10-13; the Pi 1B and the K30 5G benched 2026-10-15 (`porting/MOBILE.md`, "Measured first") | Everything: start with M0, Android on the OnePlus Ace 3V |

**Loose ends a new session should know:**

- **x99's display.** The X550 (testsigned, XP-era driver) is the primary
  adapter on VGA, and the 5060 Ti is still installed.
  - SDL's D3D12 driver takes DXGI's first high-performance adapter, which is
    now the X550, and refuses the machine. `DRAGON_GPU_LOW_POWER=1` reaches
    the 5060 Ti anyway: with nothing to rank a WDDM 1.0 driver on, the
    low-power order is adapter order reversed, "the one not driving the
    display". Vulkan finds the 5060 Ti on its own.
  - D3D11 at 9_2 runs on the X550 (`--gpu-driver direct3d11:1` from the
    SSH session, `direct3d11:0` on the desktop: DXGI numbers the adapters
    per session).
  - The `d3d12`, `d3d12-sm3` and `d3d12-sm2` golden sets predate the
    sequenced random draws (`porting/PRESETS.md`, "A preset renders the
    same on every backend") and fail strict until recaptured.
- **The X550 can hang the machine.** Its driver model has no timeout
  recovery: a GPU hang freezes x99 outright. Do not run High or Ultra on
  it, and keep soaks short.
- **Running D3D9 on x99** needs the desktop session: run it through
  `tools/x99/run_interactive.ps1`, log to a file with `DRAGON_LOG_FILE`
  (the exe is GUI-subsystem); `DRAGON_D3D9_PROFILE=1` gives a per-pipeline
  profile with written pixels; `--restart-every N` with `--telemetry` soaks
  resets against the `gpu:` resource line.
- **Never measure a software-vertex card on Windows 10.** Its `d3d9.dll`
  interprets vertex shaders: a frame the Atom draws in 28 ms under XP took
  2 s there (`porting/CARDS.md`,
  "A second machine"). Measure on XP.
- **D3D9 and D3D11 shaders are deployed by hand.** Bake them with
  `tools/d3d9/bake_d3d9.py OUT --tier sm3|sm2|sm2-noshadows|ff`, which
  needs Wine on the Mac, and copy them into `D:\dragon-win\shaders`. The
  Wine is Sikarugir's engine, and it finds its libraries only with the
  Frameworks directory on the fallback path:
  `WINE=~/Applications/Sikarugir/Test.app/Contents/SharedSupport/wine/bin/wine
  DYLD_FALLBACK_LIBRARY_PATH=~/Applications/Sikarugir/Test.app/Contents/Frameworks
  WINEPREFIX=<a scratch dir>`. The modern variants a Windows build loads
  (`bake_shaders.sh OUT dxil spv`) and the D3D11 set
  (`tools/d3d11/bake_d3d11.py OUT`) go the same way. The release packagers
  do not bake D3D9 or D3D11 yet.
- **A preset must build the same content on every compiler.** Keep one
  random draw per statement, and build the LOD code without FMA
  contraction (CLAUDE.md, "One random draw per statement"). The LOD log
  line's fingerprints compare platforms directly. This now covers ARM and
  RISC-V as well (`porting/MOBILE.md`).
- **x99 has the gold and silver studies** in `D:\dragon-win` and in
  `D:\dragon`, the source copy the cross-built tests read through
  `DRAGON_SOURCE_ROOT`. That is a personal copy. Their profiles are public
  in `assets/species/`; the models are not (`ATTRIBUTION.md`).
- **A PCI card behind a PCIe bridge**: check the bridge's prefetch before
  cutting the card's triangle budget (`porting/CARDS.md`, "The bottleneck
  was the bridge").

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
| **R0** spike | A cross-compiled D3D9 window, one textured and fogged triangle, plus miniaudio and a gamepad, built on the Mac | It runs in Wine, on x99-windows and on the G41 under XP |
| **R1** RHI | Extract the roughly 20-entry-point interface. The RHI owns the depth convention. `app.cpp` and `skinned_mesh` stop calling SDL GPU directly | Metal, D3D12 and Vulkan all match the goldens. Pure refactor |
| **R2** content tiers | Bake the terrain noise; DXT compression and per-tier texture caps at import; creature LODs; palette splitting; a standard-Z path behind the RHI; an LDR fallback in the post stack | The modern build looks unchanged at normal distances, and `--tier` changes only what it should |
| **R3** D3D9 SM3 | The backend plus HLSL `vs_3_0`/`ps_3_0` builds of the P1 shaders, compiled offline with `d3dcompiler_47` | Goldens within tolerance on x99-windows; a hoard run on the G41 with the GeForce 6200 TC |
| **R4** SM2 | The `#if` tier cuts, for `ps_2_0`'s 64 arithmetic instructions | The Radeon X550 on the G41 runs a valley at 1024×768 |
| **R5** fixed function | Materials as texture-stage state, CPU skinning, blob shadows, vertex fog | The FF cards fly a valley |
| **R6** XP platform layer | SDL3 does not support XP: an SDL3 subset on Win32 plus thunks | It installs and runs on a clean XP SP3 |
| **R7** retro presets | Presets down to 640×480, particle caps, fewer bots and less grass | 30 fps on the SM3 floor card at 1024×768 |
| **R8** Windows 98 SE | [gcc-for-Windows98](https://github.com/fsb4000/gcc-for-Windows98) (GCC 11.1, win32 threads: no `std::thread`, `std::mutex` or `std::filesystem`, which `src/` does not use), R6's layer limited to 98's APIs, miniaudio on DirectSound or WinMM | It boots into a valley on 98 SE |

## The mobile track

Four devices, from a 2024 phone to a 2012 single-board computer
(`porting/MOBILE.md` has the plan, the device table and what each step
costs):

| Phase | Target | Route |
|---|---|---|
| **M0** | OnePlus Ace 3V (Snapdragon 7+ Gen 3, Adreno 732) | Android, SDL GPU's Vulkan backend as-is: the modern tier. Packaging, APK asset reads, baked SPIR-V, touch and tilt controls, lifecycle |
| **M1** | Redmi K30 5G (Snapdragon 765G, Adreno 620) | The same APK at a lower preset; ASTC or ETC2 in place of DXT; the Vulkan 1.1 driver's quirks |
| **M2** | VisionFive 2 (JH7110, four U74 cores, IMG BXE-4-32) | Linux riscv64, cross-built on the Mac; SDL GPU's Vulkan backend on the board's PowerVR driver if it holds up |
| **M3** | Raspberry Pi 1B, the quick route | `game-mcu`'s 320x240 software renderer, which already runs the game's own code at 30 fps on a 150 MHz RP2350, built for armv6 Linux |
| **M4** | A GLES RHI backend | GLES 3.0 for anything whose Vulkan fails (the VisionFive 2's fallback), with a GLES 2.0 subset |
| **M5** | Raspberry Pi 1B, the real port | The GLES 2.0 backend on Mesa's vc4, a "pi" content budget under 512 MB |

## Where it is tested

The Mac stays the daily driver. Each other machine has one job, chosen so the
slow-to-reach machines are only used for what nothing else can show.

| Platform | Role | Why it, and not another |
|---|---|---|
| **Mac, Metal** (M5) | Daily development; the reference goldens | Hot reload, the whole headless workflow, Blender and the tools |
| **Mac, MinGW-w64 + plain Wine** | Build every Windows binary on the Mac; smoke-run them under Wine (Sikarugir's Wine 10 engine, a scratch prefix, no CrossOver); run `d3dcompiler_47` for the D3D9 bake | No machine to power on. The D3D9 backend draws within 0.29/255 of x99, and the modern exe on Vulkan (MoltenVK) within 0.46/255 of the Metal golden. **Not a correctness oracle:** wined3d translates D3D9 to OpenGL, so it answers "does it start and roughly draw", and its caps are a fake GeForce 6800. D9VK will not load in Wine's 32-bit WoW64 mode, and GPTK's D3DMetal crashes SDL's D3D12 backend |
| **x99, CachyOS** | Vulkan; the Linux release; Linux headless soaks over SSH | The only Linux box with a real modern GPU. Power it off after each session |
| **x99-windows, Windows 10** | D3D12, D3D11 (and WARP) and Vulkan on Windows; **the D3D9 debugging platform for all three tiers**, with `--tier` forced on its modern card's driver; apitrace for D3D9 call traces | A real D3D9 driver and runtime, a debugger, and fast iteration. But Windows 10 cannot drive a pre-DX9 card and interprets software vertex shaders, so this proves the code, not the hardware or the budget |
| **G41, Windows XP, driven by `agent.ahk`** | **Acceptance for each retro tier.** Its card today is the Matrox G550 PCIe (the M6 died) | The only place budgets, driver quirks and the XP platform layer are real. The qemu-gpu harness copies files over SMB, runs jobs and takes OBS captures |
| **PT55D, Atom D525 + GMA 3150** | A second XP machine (the G41's own disk, in IDE mode) and a Windows 10 install, `ssh windows` | Software vertex processing on a slow in-order CPU, the netbook class |
| The AGP benches (865G P4; the SiS universal-AGP board) | The AGP cards: the FX 5200 boards, the Radeon 7000 and 9200 LE, the TNT2 | Not yet driven remotely |
| **The phones** (OnePlus Ace 3V, Redmi K30 5G) | M0, M1: Android on Vulkan, over `adb` | The only Vulkan drivers in the house that are not desktop ones |
| **VisionFive 2** | M2: Linux riscv64, PowerVR | The one non-x86, non-ARM machine; a slow CPU with a real Vulkan GPU |
| **Raspberry Pi 1B** (`ssh rpi`) | M3, M5: armv6 Linux, VC4 | The floor of the mobile track. The cross toolchain and the sysroot already exist in `~/src/rpi` |
| qemu-gpu's emulated M6 and NV18 | Later: the FF tier as a workload for the emulator, and the emulator as a way to replay a failing FF frame | Not a dependency of this roadmap |

### The G41 loop

`--screenshot` already exists, so the G41 is driven exactly like a headless
run, with a game job alongside `hl2-timedemo.ahk`:

1. The Mac cross-builds and copies the build plus the job into the G41's
   `bench` share (`mount_smbfs` over the direct cable, as in qemu-gpu's
   `docs/G41_TESTBENCH.md`).
2. A job runs
   `dragon.exe --tier ff --frames 600 --screenshot results\g550.bmp --telemetry 60`
   and copies its log out.
3. The Mac pulls the BMP, the log and the fps line back, then diffs the
   frame against the tier's golden. The D3D9 screenshot path is a
   `GetRenderTargetData` readback, so it is the game's own frame and not the
   VGA capture, which qemu-gpu notes is not a pixel oracle.

The agent itself stays in qemu-gpu, where it is maintained.

## Open questions

- **Is the CPU the wall on the slow machines?** Answered for the PC side:
  no. The E3300 and even the Atom D525 run the simulation well inside a
  frame; every slow frame so far was the vertex path or the bus
  (`porting/CARDS.md`). It is open again for the Pi 1B's ARM11 (about
  50 ms of simulation, `porting/BEYOND.md`) and the VisionFive 2's U74.
- **The 32-bit address space** on XP: answered by R2b's packs (217 MB for
  the roster). The Pi 1B's 512 MB, shared with the GPU, is the same
  question one size down, and the packs carry DXT, which neither the Pi
  nor the phones can sample.
- **Fixed-function look.** Settled on renders against the concept targets
  at R5: it reads as the game, earlier.
- **Does the Mac's toolchain produce XP-clean binaries?** Answered: llvm-mingw's
  msvcrt build, gated by `tools/xp/check_imports.py` (`porting/XP.md`).
