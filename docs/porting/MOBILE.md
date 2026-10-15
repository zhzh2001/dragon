# Porting: phones and single-board computers (M0-M5)

Planned 2026-10-13. Four devices the user owns, from a 2024 phone down to a
2012 board, and what it takes to put the game on each. Nothing here is
built yet; the numbers are the devices' published figures and this
repository's own measurements, and every estimate is to be replaced by a
measurement on the device.

Part of the porting record; [`../PORTING.md`](../PORTING.md) is the index.

## The devices

| | OnePlus Ace 3V | Redmi K30 5G | VisionFive 2 | Raspberry Pi 1B |
|---|---|---|---|---|
| Year | 2024 | 2019 | 2022 | 2012 |
| SoC | Snapdragon 7+ Gen 3 | Snapdragon 765G | StarFive JH7110 | Broadcom BCM2835 |
| CPU | 1x Cortex-X4, 4x A720, 3x A520 | 1x 2.4 + 1x 2.2 GHz A76-class, 6x A55 | 4x SiFive U74 at 1.5 GHz (RV64GC, in-order, no vector unit) | 1x ARM1176JZF-S at 700 MHz (ARMv6, VFPv2, no NEON) |
| GPU | Adreno 732 | Adreno 620 | Imagination BXE-4-32 | VideoCore IV |
| Graphics API | Vulkan 1.3, GLES 3.2 | Vulkan 1.1, GLES 3.2 | Vulkan 1.2 and GLES 3.2 through IMG's closed driver on StarFive's Debian image; Mesa's open PowerVR driver is the other candidate | GLES 2.0 (and desktop GL 2.1) through Mesa's vc4, with the KMS overlay on |
| Texture formats | ASTC, ETC2 (no BC/DXT) | ASTC, ETC2 (no BC/DXT) | ETC2, ASTC per driver; no BC expected | ETC1 only (no alpha), else uncompressed |
| Memory | 12-16 GB | 6-8 GB | 2/4/8 GB | 512 MB, shared with the GPU |
| OS | Android 14 (ColorOS) | Android 11-12 (MIUI) | Debian riscv64 | Raspberry Pi OS Legacy (Bullseye, 32-bit armhf); see `~/src/rpi` |
| Tier it can carry | **Modern**, Medium to High | sm3 at native (measured, 43-50 fps); modern only at a phone preset, about 30 fps | Modern at Low to Very low, or sm2-class content | ff-class content at Lowest, or the MCU demake's renderer |

Two facts frame all four. **The simulation is portable**: `src/` has no SIMD
intrinsics and no x86 assumption apart from the XP build's `-msse2`, and
arm64 is already the Mac's architecture, so the "same content on every
compiler" rules (one random draw per statement, LOD code without FMA
contraction) already hold on ARM. **The renderer is the split**: the phones
and probably the VisionFive 2 have Vulkan, which SDL GPU already drives;
the Pi has only GLES 2.0, which nothing in the engine drives today -- SDL
GPU has no GL backend.

## Measured first: the Pi 1B against the K30 5G (2026-10-15)

Before any porting, the two ends of the phone-and-board range were put on
the same two benches, each built from one source for every device
(`tools/mobile_bench/`, `build.sh mac|pi|android`, nothing installed on the
devices). The M5 Mac is the third column, as the anchor that the engine's
own frame profile can be scaled from.

- `cpu_bench`: single-threaded. The game's simulation compiled through
  `game-mcu`'s overlay (exactly what `dragon_sim` runs: a hoard run flown
  by the demo pilot, then an arena match against four bots, 3600 frames at
  60 Hz), its 320x240 software renderer drawing those frames, CPU skinning,
  and scalar float and memory. Pi: a static armv6 musl binary from Zig.
  Phone: the NDK's clang, run under `adb shell`, pinned with `taskset`.
- `gles_bench`: GLES 2.0 only, offscreen into an RGBA8 + depth16
  framebuffer from a 1x1 pbuffer context, so no display is involved. Fill
  tests are 32 blended full-screen layers (blended because a GPU with
  hidden-surface removal -- Apple's, and the VisionFive 2's PowerVR --
  shades only the top opaque layer and read 131 Gpix/s; blending is free
  on VC4 and on the Adreno). Vertex tests are a 128x128-vertex grid four
  times a frame (`--grid 128`). The Pi ran it twice: Broadcom's firmware
  driver (`/opt/vc`, as the board is set up for the QPU LLM work) and
  Mesa's vc4 (the KMS overlay switched on for the run, then the original
  `config.txt` restored; `libegl1`, `libgles2` and their `-dev` packages
  stay installed). Mesa's run predates the blended layers; its opaque fill
  rows stand because its blended row matched them. The Mac ran it through
  VS Code's bundled ANGLE (GLES on Apple's GL-over-Metal).

**CPU** (ms a frame unless marked; the phone's A76 is the 2.4 GHz prime
core, cpu 7; the A55 is cpu 0):

| | Pi 1B (ARM11, 700 MHz) | K30 A55 | K30 A76 | Mac M5 | K30 A76 / Pi |
|---|---|---|---|---|---|
| game sim, hoard run | 0.520 | 0.063 | 0.011 | 0.002 | **47x** |
| game sim, arena, 4 bots | 0.405 | 0.042 | 0.007 | 0.002 | 58x |
| software raster 320x240, run | 17.3 | 2.31 | 0.73 | 0.21 | **24x** |
| CPU skinning, ms per 1000 vertices (4 bones) | 0.90 | 0.058 | 0.017 | 0.004 | 53x |
| scalar mul+add, Mflop/s | 338 | 3,538 | 4,704 | 7,436 | 14x |
| 4x4 transform, M vertices/s | 5.0 | 115 | 355 | 931 | 71x (NEON) |
| memcpy, MB/s (musl's on the Pi) | 103 | 4,597 | 6,104 | 52,270 | 59x |
| dependent load, ns | 339 | 221 | 135 | 20 | 2.5x |

The game's own code is **25 to 60 times** faster on one K30 big core than
on the Pi; the geometric mean over the five compute rows is 36x. Against
the M5 the Pi is 80x (raster) to 260x (sim) slower. In the units of
`~/.codex`'s device comparison (S = the RP2350's 22.7 ms render at 252 MHz
over the device's), the Pi is S of about 1.3 and the K30's A76 about 31,
both inside the ranges guessed there (1-3 and 15-35).

**GPU** at 640x480 (rates in Mpix/s, Mtri/s or thousand draws/s; `scene`
is a 32K-triangle fogged terrain, 400 alpha-tested cards and a 6K-triangle
skinned creature, in ms):

| | Pi, Broadcom | Pi, Mesa vc4 | K30, Adreno 620 | Mac M5 (ANGLE) | K30 / Pi (Mesa) |
|---|---|---|---|---|---|
| clear, ms | 1.16 | 0.98 | 0.26 | 0.07 | 3.8x |
| fill_flat | 628 | 786 | 4,151 | 75,487 | 5.3x |
| fill_tex | 305 | 319 | 3,942 | 55,853 | 12.4x |
| fill_ff (the ff tier's pixel) | 287 | 316 | 4,070 | 38,630 | **12.9x** |
| fill_heavy (3 textures, specular, exp fog) | 112 | 106 | 2,023 | 20,775 | 19x |
| tris (lit, 32K a draw) | 11.5 | 8.5 | 120 | 995 | 14x |
| skinned, 4 bones from 24 | **does not link** (2 bones: 5.1) | 3.4 | 30.6 | 939 | 9x |
| draws (one small quad each) | 17.3 (58 us each) | 25.8 (39 us) | 261 (3.8 us) | 1,917 | **10x** |
| scene, ms | 5.39 | 7.10 | 0.96 | 0.09 | 7.4x |

The Adreno 620 is **about 11 times the VC4** (the geometric mean of the
nine throughput rows), 5x on flat fill and up to 19x on arithmetic. So on
the codex comparison's GPU scale (K30 = 100) the Pi is about 9, not the
2-6 it guessed. The resolution undoes most of it: the K30's native
2400x1080 is 8.4 times 640x480's pixels, so per frame a fill-bound phone
at native resolution is barely faster than the Pi at 640x480. At
2400x1080 the Adreno held 4,665 Mpix/s textured and 2,693 heavy, and its
draw-call cost did not change with resolution.

**What the Pi's driver choice decides.** Both drivers run the same
silicon and fill identically. They differ where M5 lives:

- **Broadcom's firmware driver refuses the creatures' vertex shader.**
  Indexed uniform reads go through the texture unit, and four bones from
  a palette fail to link ("Too many texture accesses: caused by uniform
  array access"); two bones link. The skinned path on it is CPU skinning,
  as the ff tier already does on D3D9: 0.9 ms per 1000 vertices on the
  ARM11, so a 1.2K-triangle creature (Minimal) is about half a
  millisecond each.
- **Mesa vc4 links the 4-bone shader, and calls are cheaper** (39 us
  against 58): Broadcom's client library marshals every call over VCHIQ
  to the driver on the VPU.
- **Mesa vc4 hangs the GPU on big, dense draws.** Four draws of 51K
  triangles (a 160x160-vertex grid) in a tenth of the screen wedged V3D:
  the kernel reset it every second, and it stayed wedged for every later
  process until a reboot. 128x128 (16K vertices, 32K triangles a draw)
  is fine. The runs do not separate the two candidates -- triangles a
  draw, or triangles a 64x64 tile, which overflows the binner's lists --
  so M5 should cap a draw near 32K triangles and keep the far terrain
  from packing tiles until a test does.
- Bullseye still has `/opt/vc`; Bookworm does not. Mesa is the driver
  with a future, and the one the plan already names.

**What it means for the real game.** The engine's own frame profile
(`DRAGON_FRAME_PROFILE`, `--run 7 --autopilot`, frames 3180-3600, the
stretch `CARDS.md` used) on the M5:

| | update | ui build | render (CPU) | GPU wait | frame |
|---|---|---|---|---|---|
| `--tier ff --preset lowest`, 640x480, one species | 0.03 | 0.02 | 0.04 | 0.23 | 0.3 ms |
| modern, `--preset low`, 1280x720, the roster | 0.05 | 0.02 | 0.10 | 1.75 | 2.0 ms |

- **The K30 is GPU-bound and CPU-rich.** The engine's 0.2 ms of CPU is
  about 1 ms on its A76 (the 5x of the sim row; 3.4x on the raster). The
  M5's GPU is 10-15 times the Adreno's on these tests (fill_tex 15.5x,
  fill_ff 9.4x, fill_heavy 10.7x at 720p), so Low's 1.75 ms of GPU is
  roughly **18-27 ms on the K30 at 1280x720**, before the costs a desktop
  GPU hides and a tiler does not (the 16-bit HDR targets' bandwidth, the
  4096² shadow map, the bloom's passes). Expect Low at 720p at 30 fps,
  Medium needing the world resolution setting, and the native 2400x1080
  out of reach of anything but Very low -- consistent with M1's plan.
- **The Pi is CPU-bound, and closer than `BEYOND.md` feared.** That study
  took the 0.5 ms median frame as the simulation; the CPU part of it is
  0.1 ms, and the rest was the GPU wait. At the measured 80-260x, the
  engine's CPU is **roughly 8-26 ms on the ARM11**, plus Mesa's 39 us a
  draw (100-250 draws after the ff tier's batching, 4-10 ms) and CPU
  skinning if the driver is Broadcom's. The GPU's share at 320x240 to
  480x272 is a few ms (the scene row is 7 ms at 640x480). So **15-40 ms
  of CPU a frame, 25 fps as a working figure**, against the "10-15 fps at
  best" estimated before. The wide range is the honest part: the engine
  was never compiled for a core with 16 KB of L1 and no L2 of its own,
  and nothing short of running it will narrow it. Memory (429 MB to the
  ARM with `gpu_mem=64`) remains the first wall, as `BEYOND.md` said.
- **Per device class**: the K30's one big core does in 1 ms what the Pi
  does in 25-50, and its GPU fills 12x faster at the ff tier's pixel. The
  Pi at 320x240 is a M3/M5 target for the ff tier's content; the K30 is a
  modern-tier device at reduced resolution. The VisionFive 2 is the one
  device in the house expected between them (unmeasured; `gles_bench`
  builds for it with `GB_MESA` or IMG's EGL).

Sustained: three minutes of `fill_heavy` at 2400x1080 on the K30 held
2,696-2,718 Mpix/s in every 15-second window, so its GPU alone does not
throttle in that time (the CPU was idle; a game loads both). The Pi
stayed at 700 MHz, 42 C and `throttled=0x0` throughout. Not measured: the
phones' Vulkan (GLES was the common API; the port uses SDL GPU's Vulkan),
the Ace 3V and the VisionFive 2.

## M1 on the K30, as built so far (2026-10-15)

M1 came first instead of M0 (the K30 is the phone on `adb`), so it took
M0's shared work with it. What exists:

- **No Gradle.** `tools/android/build_apk.sh` builds `libmain.so` through
  the NDK's CMake toolchain (`build-android/`: arm64-v8a, API 29, SDL
  3.4.16 fetched and linked statically, `DRAGON_SHADERCROSS=OFF`), compiles
  SDL's Java activity and ours (`android/src`, `DragonActivity`) with
  `javac` and `d8`, links the manifest with `aapt2`, and signs with the
  debug key: a 2.4 MB APK in about ten seconds after a C++ change. Tools:
  `brew install --cask android-ndk android-commandlinetools`, `brew
  install openjdk@17` (the Temurin cask wants sudo), then `sdkmanager
  "platforms;android-35" "build-tools;35.0.0"`.
- **The data is beside the APK, not in it.** `tools/android/push_assets.sh`
  stages what the Windows package carries (SPIR-V baked by
  `bake_shaders.sh`, textures, fonts, species, props, each species shrunk
  to 2048 textures by `shrink_glb.py`) and `adb push --sync`es it to
  `/sdcard/Android/data/com.paleshell.dragon/files`, which
  `core/paths.cpp` takes as the root on Android. fopen and ifstream read it
  unchanged, so no file access was rewritten. A release APK will still
  need the assets inside it (the open item from "What every mobile target
  needs").
- **Driven from the Mac.** `tools/android/run.sh -- ARGS` launches with a
  command line (the intent's `args` extra; leading `NAME=value` words are
  set as environment variables first), waits for SDL_main to return, and
  pulls back the log and the screenshot as PNG. A launch with arguments
  shows over the lock screen (`setShowWhenLocked`), because the K30 has a
  PIN and SDL never starts the game behind the keyguard; a launch from the
  launcher does not.
- **Fixed on the way:** a headless capture's UI was laid out for the
  window, which Android sizes to the screen (2261x1080), and squeezed into
  the 1280x720 target, so `ImGuiLayer::begin_frame` now lays out in the
  target's pixels when the two shapes differ. The window is fullscreen
  (immersive) and landscape either way up (`SDL_HINT_ORIENTATIONS`), and
  the panels start hidden on Android, which has no F1.
- **`DRAGON_GPU_PASS_PROFILE=N`** (headless, SDL GPU): from frame N every
  render pass ends in a submit and a wait, and each pass's GPU time is
  logged at exit, labelled by its first pipeline. SDL GPU has no timestamp
  queries and this Adreno driver gives Perfetto no render stages, so it is
  the only per-pass timing on the phone. The waits add about 0.3 ms a pass
  there.

The first boot rendered the valley at Ultra identically to the Metal
capture, HUD and panels included, at 4 fps. A hoard run under the demo
pilot, 600 frames, 1280x720, headless (the GPU is about 83% busy
throughout, per `/sys/class/kgsl/kgsl-3d0/gpubusy`):

| Tier, preset | Median frame | fps |
|---|---|---|
| modern Ultra | 258 ms | 3.9 |
| modern High | 132 ms | 7.6 |
| modern Medium | 92 ms | 10.8 |
| modern Low | 58 ms | 17.0 |
| modern Very low | 47 ms | 21.1 |
| sm3 Low | 26 ms | 39 |
| sm2 Low | 14 ms | 70 |

Played live (swapchain, `--demo`): sm3 Low runs at 43-50 fps on the full
2400x1080 screen (a 2040x918 world).

**The terrain.** The pass profile at modern Low (1280x720) puts 36 of a
50 ms GPU frame in the main pass, and the terrain's fragment shader is 22
of them: alone on screen (every other draw ablated) it is 46 ms, more than
with the rest drawn, because the Adreno's low-resolution depth test then
no longer rejects the ground hidden behind trees and water. (Ablation
first missed it entirely: below Medium there is no far terrain mesh, and
the branch that draws the whole mesh had no ablation check.) No single
effect in it is the cost. Removing the micro-relief, the crags, the rock
blotches, the strata, the rock and scree scans, the ground scans or the
fog one at a time each moved the frame by a millisecond or two; removing
the noise and the scans together moved it by 17. It is register pressure:
the 62 KB shader holds too many values for the GPU to keep many pixels in
flight, and it speeds up only once it is small enough. Even the map-edge
sky blend, in a branch and run only at the edge, cost 5 ms of a 45 ms
frame by being in the shader at all (`[branch]` did not change that).

So `TERRAIN_LITE`, the Terrain setting at Low and below on the modern tier
(`gfx/pipeline.cpp`; baked by `bake_shaders.sh` and `bake_d3d11.py`):
no micro-relief, crags, blotches or strata, the patches in two octaves, the
rock's broad scan alone (one triplanar, not three; without it a slope
read as smooth clay) and the ground's meadow scan in one fetch. The three
maps it no longer reads are touched behind a branch that is never taken:
unread, the compiler stripped them, the reflection counted 7 samplers for
slots up to 9, and the K30's Vulkan refused the pipeline -- the terrain
was simply missing, which Metal, binding by index, never shows. Modern Low
at 1280x720 went from 58.5 to 46.1 ms a frame, headless. The goldens
(Ultra) are unchanged.

**At the phone's own resolution** the picture is harsher: 2400x1080 at
Low's 70% world is 2.8 times 720p's pixels, and the modern tier is
pixel-bound. Live, modern Low ran 6.5 fps before the light terrain;
headless at native it is now 91 ms (world pass 53, cloud march 15,
composite 7, uploads 5.6, atmosphere 4.4, SSAO 4.2). With a 50% world,
the painted cloud slab (`--clouds on=0`) and no SSAO it is 44.6 ms
headless, against the sm3 tier's 38.9 at Low; headless frames carry about
9 ms of CPU that a live frame overlaps, so both are near 30 fps live.

So the K30 is **a modern-tier device only at a phone preset** -- Low,
half the world's resolution, no volumetric clouds, no SSAO -- or an sm3
one at native, which plays at 43-50 fps. The device table's "Modern, Low
to Medium" was the published-figure estimate and is wrong by about 3x.

**The Phone preset** (`--preset phone`, the default on Android) is Low's
content plus three settings the level presets all keep whole: Clouds
(volumetric or the painted slab), Ambient occlusion (on, off) and Frame
height (native, 1080, 900, 720 or 540 lines). The frame cap sizes every
target, the HUD's included, to that many lines in the window's shape, and
the present's blit stretches it to the screen (`gfx::Device::
set_frame_height`); ImGui lays out in the window's points and draws at the
frame's scale, so the HUD keeps its layout. A capped frame never draws
into the backbuffer directly (`frame_target`), which is the screen's size.
Phone takes 720 lines and a 70% world in them: on the K30 a 1600x720
frame and a 1120x504 world. Live, the demo pilot, by world scale:

| World scale in the 720-line frame | Live fps | Median |
|---|---|---|
| 100% | 18.2 | 51.7 ms |
| 85% (Low's) | 24.4 | 39.0 ms |
| **70% (Phone)** | **35.7** | **28.0 ms** |

and the shipped default over 3,600 frames (106 s of the demo): 33.8 fps,
median 29.9 ms, 1% low 35.1 ms. M1's exit test asks 30 fps or better
over ten minutes; this is the first two.

**Next**, in order: a ten-minute live soak for the thermal curve; a
default tier picked from the GPU (the Ace 3V should start higher); the
cloud march and the composite, the two fixed costs left for the presets
above Phone; touch controls through the HUD kit; lifecycle (pause on
background, the swapchain on return); the safe area; the assets inside a
release APK.

## M4 and M5, as built so far (2026-10-15): the Pi 1B runs the game

**The GLES 2.0 backend** (`src/rhi/gles`, `--gpu-driver gles`) is the
fourth `rhi::Device`, for the LDR tiers only (VC4 has no float targets, so
`gles` brings the tier down to sm2). What GLES 2.0 lacks is done in it:

- **Shaders** are GLSL ES 1.00 baked by `tools/gles/bake_gles.py`:
  shadercross to SPIR-V with the D3D9 define sets (GLES 2.0 shares D3D9's
  gaps: no `SV_VertexID`, byte joint indices read as floats), then
  SPIRV-Cross with the uniform blocks flattened to vec4 arrays, the
  interface named by location, each stage's uniforms prefixed so the two
  never clash on precision, and a JSON of which array takes which pushed
  slot and which sampler which texture slot. Two idioms ES 1.00 cannot
  compile left the HLSL: the fullscreen triangle's constant array
  (`fullscreen_corner`) and foliage's integer clamp.
- **The y flip.** The bake flips the vertex stage's y, so every pass lands
  upside down in GL's terms, which is the D3D layout in memory: textures
  sample, viewports place and readbacks come out top row first as on every
  other backend; only the present turns over, and the winding with it.
- **No instancing, sampler objects or sRGB**: one draw per instance with
  its attributes set as constants from a CPU copy of the buffer (D3D9's
  Loop mode); sampler state written into the texture's parameters when
  the binding changes; sRGB texels converted to linear at upload.
- **ImGui** through `imgui_impl_opengl3` in ES2 mode, patched
  (`cmake/imgui.cmake`) to turn its projection and scissor over.

On the Mac it runs on ANGLE (`tools/gles/setup_angle_mac.sh` copies it out
of an Electron app; `-DDRAGON_GLES=ON`): the sm2 and ff tiers are within
**0.52/255 mean of Metal's** frame at the same preset.

**The Pi build** (`tools/pi/build.sh`, `tools/pi/deploy.sh`): Zig's clang
for ARMv6 hard-float against a Bullseye sysroot (glibc 2.31) with Zig's own
libc++ linked in, SDL3 from source with KMSDRM and GLES only
(`SDL_UNIX_CONSOLE_BUILD`), `gles` the default driver
(`DRAGON_GLES_DEFAULT`). Two traps: CMake's C++20 module scan wants a
`clang-scan-deps` Zig does not ship (off in `cmake/zig-armv6-pi.cmake`),
and SDL asks 32-bit glibc for a 64-bit `time_t`, which Zig's generic glibc
headers honour although 2.31 has none of its symbols (`-U_TIME_BITS`).
The executable needs only libGLESv2, libc, libm, libpthread and libdl; SDL
opens DRM, GBM and EGL itself. The KMS overlay stays on the Pi's card
(the LLM setup's `config.txt` is `/boot/config.txt.llm-backup`).

**First runs** on the Pi 1B (700 MHz, Mesa vc4, headless, 640x480, ff
tier at Minimal, one species, `--run 7 --autopilot`): **22.9 fps over 900
frames, median 43 ms, 1% low 46 ms**, peak resident memory 86 MB. It is
CPU-bound, 99% of the one core: render submission 33.5 ms (the terrain's
chunk draws alone 12.6, at Mesa's ~39 us a draw), update 6.4, UI 2.1. The
measured-first estimate was 15-40 ms. Start-up is 96 s, half of it the
terrain's erosion (24 s) run twice: once before the preset applies and
once after.

**Optimised the same day** (`perf` 5.10 works on the Pi's 6.1 kernel with
`-e cpu-clock`; the ARM11's counters cannot sample; `DRAGON_GLES_STATS=1`
logs draws, uniform uploads and programs a frame, and the time each
pipeline spends in the driver's draw calls):

| Change | Run, 640x480, ff Minimal |
|---|---|
| As first built | 22.9 fps, median 43.0 ms |
| The GLES backend no longer re-points unchanged vertex attributes (vc4 revalidated its vertex state every draw) | 24.4 fps |
| Grazers posed only when the main view will draw them | 25.8 fps |
| 624 m terrain chunks on GLES ff (312 on D3D9, whose cards pay per vertex): 245 draws a frame to 105. The frame changes by 0.118/255 mean, 0.2% of pixels past 8, where the fine mesh now reaches further (first recorded as 0.017, against a capture made with `DRAGON_TERRAIN_CHUNK` set empty, which is 0) | 31-34 fps, median 30-31 ms |
| 32-bit index buffers kept as 16-bit copies when every index fits: VC4 fetches only 16-bit indices, and Mesa converted a 32-bit buffer on the CPU at every draw, reading it back out of GPU memory (terrain draws 3.95 -> 1.84 ms, skinned 2.3 -> 1.5; frames byte-identical) | **37.0 fps**, median 26.3 ms, 1% low 30.2 |

And start-up: the erosion ran for an arena valley a start into a run never
shows (a launch with no arguments is one), and the fixed-function far
ground ran it again on identical inputs; now once. At Minimal it erodes on
48 m cells, its mesh's size, not 24: 24 s to 5.6 s, which was also a stall
at every valley of a run.

The Pi build's defaults: the ff tier (whose default is Minimal) and the
frame at 540 lines whatever the monitor's mode (Frame height); 30.7 fps
headless at a 1920x1080 window size. A saved configuration or `--tier` and
`--preset` win.

**Next on the Pi**: keyboard and pad through KMSDRM's evdev; audio through
miniaudio's ALSA; a playtest on a monitor; the rest of Mesa's per-draw
cost (foliage, 190 us a per-instance draw, is the most expensive left; a
constant palette index in its vertex stage changed nothing, so indexed
uniforms are not it).

## What every mobile target needs

These are shared, and most of the M0 work is here, not in a renderer.

- **A cross build.** Android: the NDK's clang for arm64-v8a, inside SDL3's
  `android-project` Gradle template (`SDLActivity`). Linux boards: clang
  with a sysroot taken from the board, the way `~/src/rpi/cross_build.sh`
  already builds llama.cpp for the Pi with Zig (`-mcpu=arm1176jzf_s
  -mfpu=vfp -mfloat-abi=hard`, glibc 2.31). Nothing is built on the
  devices. None of the Android tooling is installed on the Mac yet: only
  `adb` and `fastboot`, from Homebrew.
- **Baked shaders.** shadercross does not run on a phone. The SPIR-V a
  Windows package carries (`tools/release/bake_shaders.sh OUT spv`) is what
  an Android or Linux-board build loads; hot reload is off there.
- **File reads that work inside an APK.** An Android app's data is in its
  APK, and `fopen` cannot open it. `src/` reads files 17 ways
  (`fopen`/`ifstream`) and through SDL in 8; cgltf opens its own buffers.
  Either route every asset read through one `core` helper on
  `SDL_IOFromFile` (which reads APK assets on Android) with cgltf's file
  callbacks pointed at it, or copy the assets out to internal storage on
  first launch and keep `fopen`. The helper is the right one; the copy is
  the quick one for a first boot. `paths::user` maps onto
  `SDL_GetPrefPath` already (P0).
- **Textures a mobile GPU can sample.** The texture budget compresses to
  DXT (R2), and the model packs carry DXT only (R2b); no phone GPU here
  exposes BC formats. M0 can upload uncompressed RGBA8 capped at 2048 (the
  Ace 3V has the memory); a real package wants ASTC (both Adrenos) baked
  into a pack variant, with ARM's `astcenc` (Apache-2.0) vendored as the
  encoder per the third-party policy.
- **Touch, tilt and a pad.** A Bluetooth pad already works through SDL's
  gamepad layer and is the fastest route to a playtest. Touch needs an
  on-screen layout drawn through the HUD kit (`ui::Hud`): a left
  thumb area for pitch and roll (absolute, like the left stick, which
  plays best), and right-side buttons for flap, breath, fireball, bite and
  boost, with dive and brake as edge holds. **Tilt steering** through SDL's
  sensor API is worth a dial: a flight game is the case it exists for.
  Every dial behind a slider, as always.
- **Lifecycle.** On Android the app is paused, backgrounded and has its
  surface destroyed. Pause the simulation on `SDL_EVENT_WILL_ENTER_BACKGROUND`,
  recreate the swapchain on return, and save the run's record on the way
  out; a phone app is killed, not quit.
- **The screen.** 20:9 and wider, with a cutout. The HUD is laid out in
  frame units (1/720 of the height), so it scales; the safe-area insets
  (`SDL_GetWindowSafeArea`) must keep it off the cutout. The world
  resolution setting (R7) is the fill-rate lever on a 2400-pixel-wide
  panel.
- **Tile-based GPUs.** Adreno, PowerVR and VideoCore are all tilers: a pass
  that loads what it did not need, or stores a depth buffer nobody reads,
  costs bandwidth a desktop GPU does not notice. Each pass's load and
  store ops want an audit before the first profile.
- **Verification without a person.** `--screenshot` on Android writes to
  the app's files directory and comes back with `adb exec-out run-as
  <package> cat files/shot.bmp`; arguments reach the game through the
  launch intent (`adb shell am start ... --es args "--frames 120 ..."`,
  read by an `SDLActivity.getArguments()` override). The goldens compare
  against Metal's set under `--loose`, as every other backend does.
  Thermal throttling makes a phone's fps a curve, not a number: bench
  for minutes, not frames.

## Phases

| Phase | Work | Exit test | Size |
|---|---|---|---|
| **M0** Android, the Ace 3V | Gradle project from SDL3's template; NDK build of the game, SDL3 and the vendored libraries; baked SPIR-V; APK asset reads; uncompressed textures; a pad, then touch and tilt; lifecycle; safe area | The run plays end to end on the Ace 3V from a pad and from touch; the eight golden scenes within `--loose` of Metal; a 10-minute bench at Medium with its fps curve | 3-4 sessions |
| **M1** the K30 5G | The same APK; ASTC model packs; whatever the Adreno 620's Vulkan 1.1 driver refuses (SDL 3.4 lets a device opt out of clip distance, depth clamp, indirect first instance and anisotropy at creation) | The run at Low, 30 fps or better over 10 minutes | 1-2 sessions |
| **M2** the VisionFive 2 | A riscv64 sysroot from the board; SDL3 and the game cross-built with clang; `vulkaninfo` on the board decides whether SDL GPU's Vulkan backend runs there | A valley at Very low or Low; a headless golden from the board | 1-2 sessions if its Vulkan holds; otherwise it waits for M4 |
| **M3** the Pi 1B, quickly | `~/src/game-mcu`'s desktop simulator (`dragon_sim`: the game's own flight, combat, bots, run and demo pilot, drawn by its 320x240 software rasteriser) built for armv6 Linux with SDL3 on KMSDRM | It boots to the title and plays a run at 30 fps. Expected: the RP2350 runs it at 30 fps on a 150 MHz Cortex-M33; the ARM11 has several times that | 1 session, in `game-mcu` |
| **M4** a GLES backend | `src/rhi/gles/`, a fourth `rhi::Device` beside SDL GPU, D3D9 and D3D11: GLES 3.0 first (UBOs, instancing, 32-bit indices, half-float targets where the extension is present), then a GLES 2.0 subset (uniform arrays in place of UBOs, D3D9's 16-bit index narrowing, the sm2 tier's pre-batched foliage, LDR). Shaders are the sm2 and ff variants through DXC and SPIRV-Cross to GLSL ES 3.00 and 1.00, baked offline. Developed against x99's Mesa (and llvmpipe for a headless golden), not on a Pi | Goldens at `--tier sm2` and `--tier ff` on x99 within `--cross-compiler` of the Metal previews; the VisionFive 2 on it if M2's Vulkan failed | 3-4 sessions |
| **M5** the Pi 1B, really | M4's GLES 2.0 path on vc4; a "pi" budget below Lowest: one species, one or two rivals, 320x240 to 480x272 world, content in RGBA4444/5551 (alpha-tested cards cannot be ETC1) under a 256 MB ceiling | A valley at 10-15 fps; the CPU decides | 2-3 sessions, the CPU unknown |

**The order** is the table's, and M0 first for three reasons: the Ace 3V is
the user's current phone, so it is the one that would actually be played
on; it needs no new renderer, so every problem it finds is a shared one
(packaging, input, lifecycle, textures) that M1 and M2 then get for free;
and it is the first target with a touch screen, which is a design question
as much as a port. M3 can be slotted in at any point as a one-session
trophy: it is a different codebase's renderer, but the game it runs is
this one's code.

## Per device

### The Ace 3V and the K30 5G (M0, M1)

The Adreno 732 is a 2024 upper-midrange part: expect the modern tier at
Medium to High at a 70-85% world resolution, with the 4096² shadow map
the first thing to give (the Shadows setting) and the 80K-triangle
creatures second (the Models setting already has the LODs). The Adreno
620 is roughly a third of that: Low to Medium. Neither needs a new tier;
both are presets.

Memory is not a constraint on either; APK size is a convenience question
(sideloaded, no store limit). A first APK can carry the glTFs and decode
them as the Mac does; the packs (R2b) cut the load from 6.7 s to 0.6 s on
the Mac, and on a phone CPU that difference is the one players feel.

### The VisionFive 2 (M2)

A slow CPU with a real GPU. The four U74s are in-order and dual-issue, in
the class of a Cortex-A55; with no vector unit nothing auto-vectorises.
The simulation is 0.5 ms a frame on the Mac at Very low with three bots
(`BEYOND.md`), which by rough single-thread ratios is 6-10 ms on a U74:
inside a frame, but the rig on the player's 232-joint skeleton and the
per-frame grass placement are the first things to profile. The GPU is the
question: SDL GPU's Vulkan backend has run on desktop drivers only, and
IMG's closed driver on StarFive's image is a 1.2 implementation that few
engines have exercised. `vulkaninfo` first, then a headless golden from
the board, before any game work.

### The Raspberry Pi 1B (M3, M5)

`~/src/rpi` already has the cross toolchain (Zig with a Bullseye sysroot)
and the board (`ssh rpi`, 192.168.2.10; it was off on 2026-10-13). That
board is set up for the VC4 QPU LLM work: `gpu_mem=64`, `cma=300M`, and
direct mailbox access to the QPUs. Mesa's vc4 needs the KMS overlay
(`dtoverlay=vc4-kms-v3d`) and its own CMA, and the two setups should not
share a card: **a second SD card** for the game is the cheap answer.

**M3** is the quick route and almost certain to work: `game-mcu` was
designed for hardware an order of magnitude smaller. Its SDL3 front end
(`platform/sdl/main.cpp`) needs SDL3 built for armv6 with the KMSDRM
video driver, and nothing else.

**M5** is the real game, and its limits are, in order:

1. **The CPU.** About 50 ms of simulation a frame by the rough ratios in
   `BEYOND.md`, before Mesa's driver overhead, which on an in-order ARM11
   is not small. The pi budget cuts what scales with the world (bots,
   prey, grass placement, particles) before anything visual.
2. **Memory.** 512 MB, of which the GPU's CMA takes a share. The packs
   (217 MB for the roster) carry DXT, which VC4 cannot sample; a pi pack
   is one species at 256² in 16-bit formats.
3. **The GPU** is the least of it: VideoCore IV runs real vertex and
   fragment shaders, and the ff tier's pre-lit, one-texture materials are
   well inside it. Its index buffers are 16-bit, which D3D9's narrowing
   path (`D9Buffer::narrow`) already handles for cards without INDEX32.

## Open questions

- **Does the Ace 3V have USB debugging on**, and is either phone rooted?
  The K30 5G is on `adb` (2026-10-15, `picasso`, Android 12, unrooted):
  native binaries run from `/data/local/tmp`, which is enough for every
  bench and for a first headless boot before any APK. Root is not needed.
- **Which image is on the VisionFive 2**, StarFive's Debian with IMG's
  driver, or a mainline distribution with Mesa? It decides M2's first step.
- **Can the Pi have a second SD card** for the game, so the QPU LLM setup
  stays as it is? Not needed to measure: the KMS overlay toggles with one
  line of `config.txt` and a reboot each way (done 2026-10-15, restored).
  M5's development will want it on for days, though, and that is the
  case for the second card.
- **Touch or tilt as the default** on a phone is a feel question, settled
  by playing both, like every other control in this game.
