# Porting: beyond the three tiers

The extended study of 2026-10-03: what each target past R5 would take,
the order chosen, and the 3DMark2001 dragon.

Part of the porting record; [`../PORTING.md`](../PORTING.md) is the index, with the
state of every phase and a map from the section names cited in code
comments ("R6, as built", "The G550 on the G41") to the file that holds them.

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
