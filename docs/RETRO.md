# Dragon on old GPUs — a D3D9-era port study

> **Superseded for the port by [`PORTING.md`](PORTING.md) (2026-10-01).** This
> study predates the post stack (there is now an RGBA16F scene target, bloom
> and a composite pass), and it chose GL 2.1 first because nothing else
> debugged on the Mac. CrossOver now runs a D3D9 build on the Mac, so the plan
> is one D3D9 backend with SM3, SM2 and fixed-function tiers. The analysis
> below is kept for its reasoning: reversed-Z, the skinning palette, VRAM.
> The ranked feature list at the end is still current.

A new game for old hardware. This document works out what it would actually
take to run Dragon on a 2005-class GPU, what breaks, what gets *better*, and
how the work phases so the modern Metal build never stops being the daily
driver. It closes with the broader next-steps list (visuals, gameplay) so the
retro track can be weighed against everything else.

## Picking the target

"Old APIs" needs a concrete machine, because the constraints come from the
hardware, not the API name:

| Profile | API | Hardware | OS | Verdict |
|---|---|---|---|---|
| **SM3.0** | D3D9.0c | GeForce 6600 / Radeon X1600, 128–256 MB | WinXP | **Primary target.** Full shader game survives. |
| SM2.0 | D3D9 | GeForce FX / Radeon 9600 | WinXP | Stretch goal. 64-instruction pixel shaders force real cuts. |
| Fixed function | D3D7/8 | GeForce 2–4 MX | Win98/XP | Not this game: the terrain and skinning ARE shaders. |
| GL 2.1 | OpenGL | same era + old Macs/Linux | any | **Sibling backend** — debuggable on the modern Mac, which is worth a lot. |

Recommendation: build the abstraction once, then **GL 2.1 first** (it runs and
debugs on the dev machine), **D3D9/SM3 second** (the real retro trophy, on a
period Windows box or VM). SM2 only if the itch persists.

## What ports cleanly (more than you'd think)

The engine is accidentally period-shaped, because it was built lean:

- **Forward renderer, one directional light, one shadow map, LDR output.**
  This *is* the 2005 pipeline. No deferred, no HDR target, no post chain —
  the tonemap runs inside each fragment shader and SM3 doesn't care.
- **All simulation is plain C++**: flight, chains, pendulum legs, bots, match
  loop, particles (CPU-simulated already). Zero compute shaders to replace.
- **Additive particles, alpha-blended nothing else.** D3D9 blending covers it.
- **Dear ImGui** ships a maintained D3D9 backend; **miniaudio** speaks
  DirectSound/WinMM back to XP.
- **Draw call count is tiny** (terrain + a handful of dragons + rings + bolts
  + one particle batch + UI) — nowhere near D3D9's per-call overhead limits.
- **Tests**: everything under `tests/` is renderer-free and runs anywhere.

## The five real problems

### 1. The API layer — SDL3 GPU has no D3D9 backend

All GPU work already funnels through a narrow surface: `src/gfx/` (~1,900
lines) plus the vertex-upload half of `anim/skinned_mesh`. The actual concepts
used are small:

> device/swapchain, offscreen colour+depth target, blit, graphics pipeline
> (shader pair, vertex layout, depth/blend/cull state), static vertex/index
> buffer, per-frame dynamic vertex buffer (debug lines, particles), 2D texture
> + mips + sRGB, sampler, uniform push (4 blocks), render pass begin/end,
> draw / draw-indexed, screenshot readback.

That is a ~20-entry-point RHI. The port's first move is extracting exactly
that interface from the current code — a pure refactor, Metal keeps working,
and every later backend is an implementation file, not a rewrite. Notes:

- **Windowing/input stays SDL** (SDL3 for Win7+; true WinXP needs an SDL2
  window layer — decide only if XP-on-metal becomes a goal; D3D9 itself is
  happy on modern Windows for development).
- **Uniform push** maps to `SetVertexShaderConstantF` — same mental model.
- **Dynamic buffers** map to `D3DUSAGE_DYNAMIC` + DISCARD/NOOVERWRITE locking,
  the pattern our transfer-buffer code already mirrors.

### 2. Shaders — MSL twins in HLSL, and the noise has to move

Seven shaders, all small (terrain 185 lines is the giant). Hand-written HLSL
twins beat any transpiler at this scale; the discipline is a shared naming
convention so the pairs stay visibly in sync.

The real issue is **cost, not expressibility**: the terrain evaluates 3-octave
fbm plus detail-noise gradients per fragment. SM3 allows it; a GeForce 6600 at
1024×768 will crawl. The period answer is the right answer: **bake the noise
into tiling textures** (a patch/fbm map, a detail-normal map) at asset-build
time. That's not a downgrade — it's what 2005 shipped, it's faster on the
modern build too, and the shader keeps the same structure with texture fetches
replacing procedural calls.

### 3. Reversed-Z does not exist there

Our depth precision at 5 km hangs on reversed-Z + float depth. D3D9 gives a
24-bit fixed-point buffer and no reversed-Z convention. Mitigations, in order:

1. Standard Z with the near plane pushed out (2–4 m in flight; the dragon is
   never centimetres from the lens).
2. Camera-relative rendering (subtract the camera position on the CPU) — we
   already pass world positions through one uniform path, so this is cheap
   and kills the large-coordinate half of the precision problem.
3. If distant terrain still shimmers: split the far range into two passes.
   Probably unnecessary at 5 km with 1+2.

The RHI must therefore own the depth convention (clear value, compare op,
projection builder) instead of the shaders assuming reversed-Z. That's a
small, honest generalization worth doing during extraction.

### 4. Skinning — 256 joints do not fit in SM3 constants

The imported dragon uses 232 joints; a skinning palette of 256 × 3 rows = 768
`float4` registers, three times what an SM3 vertex shader has *in total*.
Options:

- **Palette splitting** (recommended): partition each submesh's triangles by
  the set of bones they touch, ≤ 60 bones per batch (180 registers, room for
  the rest). Period-authentic, keeps GPU skinning, done once at load. The
  dragon becomes ~4–6 draw calls instead of 3 — irrelevant.
- Software skinning: 23k verts × (player + 4 bots + ghost) ≈ 140k
  verts/frame on the CPU. A 2005 Athlon could; it's the fallback, not the plan.

### 5. VRAM — the textures are 40× over budget

Five 4096² RGBA8 maps ≈ 110 MB of the card's 128–256 MB, before the terrain,
shadow map and framebuffers. The fix is the standard one and helps every
build: **compress to DXT1/DXT5 at import** (a small offline step or
stb-based bake) and cap retro texture size at 1024². Budget sketch for a
128 MB card: dragon maps ~4 MB compressed, terrain bakes ~6 MB, shadow map
R16/depth 4 MB, framebuffers ~8 MB — comfortable.

## Phasing (each phase leaves the Metal build green)

| Phase | Work | Exit test |
|---|---|---|
| **R1** | Extract the RHI; gfx code and `skinned_mesh` talk only to it; RHI owns the depth convention | Metal build pixel-identical (golden screenshots), all suites green |
| **R2** | GL 2.1 backend + GLSL 120 shader twins, developed on the Mac | same golden screenshots within tolerance, on GL |
| **R3** | Noise → baked textures; DXT + 1K asset pipeline; palette-split skinning | modern build unchanged visually at normal distances |
| **R4** | D3D9 backend + HLSL SM3 twins, on a Windows machine/VM | the game runs on real or emulated period hardware |
| **R5** | Retro presets: 4:3, vertex fog option, 16-bit dither, low particle caps | 30 fps on a GeForce 6600 at 1024×768 |

R1 is the load-bearing phase and is pure refactor — it can start any time and
is worth doing even if the port stalls, because it decouples the game from
SDL3 GPU. R2 gives a debuggable second backend without leaving the desk. The
Windows-hardware dependency only starts at R4.

**A cheap first taste**: a "2005 mode" toggle on the *Metal* build — render at
640×480 into the offscreen target with bilinear upscale, ordered dithering to
16-bit colour, vertex-fog-style banding, particle caps. One session of work,
zero porting, and it validates the aesthetic before any RHI exists.

## The rest of the map — visuals and gameplay

Ranked by feel-per-session, independent of the retro track:

1. **Loadouts** (finishes M15): heavy / skirmisher / sniper as tuning presets
   over the same flight model — cheap, adds matchup variety, and bots can use
   them too.
2. ~~**Water**~~ — **done**: a river is carved along the corridor with a
   fresnel/sky-reflection surface, and the flight model lands on it. Still
   open is water *gameplay* (skimming, dousing a burning dragon) and a second
   body of water — a lake needs the valley floor to dip below the water line
   somewhere. (Retro note: planar reflections are *the* 2005 water tech, so
   an upgrade from the current analytic reflection still ports.)
3. **Thermals**: updraft columns marked by circling debris particles; rewards
   reading terrain in both rally and combat. Pure flight-model + emitter work.
4. **Team matches**: wingman bots that fly *your* side — BotPilot already
   takes an arbitrary target; it needs a team field and target selection.
5. **Clouds**: a few billboard layers for altitude sense. Pairs with thermals.
6. **Campaign shape**: the ROADMAP's phase-4 question (boss duels vs hoard
   roguelite vs novel vignettes) is now unblocked — the mechanics exist. The
   cheapest probe is one scripted boss duel reusing the bot chassis with a
   big health pool and phase-driven tuning changes.
7. **Photo/replay mode**: the ghost recorder already stores poses; a free
   camera over a recorded fight is mostly UI.

Deliberately not recommended next: multiplayer (a different project), deferred
rendering or HDR pipelines (fights the retro track and the game doesn't need
them), more terrain size (5 km is already sparse without more gameplay in it).
