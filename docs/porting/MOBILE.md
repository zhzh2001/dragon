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
| Tier it can carry | **Modern**, Medium to High | Modern, Low to Medium | Modern at Low to Very low, or sm2-class content | ff-class content at Lowest, or the MCU demake's renderer |

Two facts frame all four. **The simulation is portable**: `src/` has no SIMD
intrinsics and no x86 assumption apart from the XP build's `-msse2`, and
arm64 is already the Mac's architecture, so the "same content on every
compiler" rules (one random draw per statement, LOD code without FMA
contraction) already hold on ARM. **The renderer is the split**: the phones
and probably the VisionFive 2 have Vulkan, which SDL GPU already drives;
the Pi has only GLES 2.0, which nothing in the engine drives today -- SDL
GPU has no GL backend.

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

- **Do the phones have USB debugging on**, and is either rooted? `adb
  devices` listed nothing on 2026-10-13. Root is not needed; developer mode
  is.
- **Which image is on the VisionFive 2**, StarFive's Debian with IMG's
  driver, or a mainline distribution with Mesa? It decides M2's first step.
- **Can the Pi have a second SD card** for the game, so the QPU LLM setup
  stays as it is?
- **Touch or tilt as the default** on a phone is a feel question, settled
  by playing both, like every other control in this game.
