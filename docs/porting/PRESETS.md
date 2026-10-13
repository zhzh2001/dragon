# Porting: graphics settings and presets (R7)

The settings, the seven presets from Ultra to Lowest, the benchmark, and
the budgets the retro content was designed to.

Part of the porting record; [`../PORTING.md`](../PORTING.md) is the index, with the
state of every phase and a map from the section names cited in code
comments ("R6, as built", "The G550 on the G41") to the file that holds them.

## R7, in progress: graphics settings and presets

The tier is what the hardware can do: HDR, per-pixel or per-vertex
lighting, packed joints, the depth layout. How much it is asked to draw is
eight settings (`gfx/graphics_settings.h`). Each setting is a level, Ultra
(0) to Very low (4), and each step down roughly halves its vertex load:

| Setting | Ultra | High | Medium | Low | Very low | Applies |
|---|---|---|---|---|---|---|
| Model detail (near, far LOD) | as authored | 20K, 3K past 150 m | 10K, 1.5K past 100 m | 5K, 800 past 80 m | 2.5K, 400 past 60 m | restart |
| Terrain grid | 6 m | 12 m | 18 m | 24 m | 24 m (36 m lost the river) | reload |
| Trees (distance, spacing) | 4.2 km, 0.55× (since 2026-10-13, `FIDELITY.md`) | 2.5 km, 1× | 800 m, 1.8× | 600 m, 2.2× | 400 m, 2.8× | reload |
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

## Budgets to design to

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
