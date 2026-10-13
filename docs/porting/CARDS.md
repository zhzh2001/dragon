# Porting: the retro cards, measured

Every round on real hardware since the XP build first ran (R6): the G41
with the Mobility Radeon M6, the Atom D525's GMA 3150, and the Matrox G550
behind its PCIe bridge. Newest last. The levers each round found (Lowest,
the vertex paths, the bridge settings) are in the code; why they exist is
here.

Part of the porting record; [`../PORTING.md`](../PORTING.md) is the index, with the
state of every phase and a map from the section names cited in code
comments ("R6, as built", "The G550 on the G41") to the file that holds them.

## The G41 with the Mobility Radeon M6, XP SP3

### It runs on XP (2026-10-04)

The G41 (Celeron E3300 at 3.34 GHz, XP
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

### Making 640x480 Minimal playable on the M6 (2026-10-04)

25 fps
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

### Second round: the broken ground and the dips (2026-10-04)

Two reports
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

### Third round: taking a slow frame apart (2026-10-05)

The slow frames
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

### Headless overstates the HUD

Headless waits for the card every frame,
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

### The low LODs, made whole (2026-10-06)

The playtest saw the dragon
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

### For the cards below the M6

A Rage XL (and the DX6/DX7 parts like it)
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

## A second machine: the Atom D525 and its GMA 3150 (2026-10-11)

The
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

### Three vertex paths (2026-10-11)

With the interpreter found, the ff
tier's terrain can now be drawn three ways, picked by
`DRAGON_D3D9_VERTEX_PATH` (unset: the vertex program, as before):

- `ff`: fixed-function vertex processing. view_proj goes in as D3D9's
  transforms with the camera folded into the view (D3D range-fogs on
  camera-space distance), the uv comes from camera-space texture
  generation through a texture matrix, the fog is ff_fog's two modes as
  D3D vertex fog, and the colour is packed to a D3DCOLOR copy of the
  buffer in FVF order. On a card with T&L this runs on the card (the M6's
  driver emulates it); without, in D3D9's software path.
- `cpu`: the backend transforms the draw's vertex range itself with the
  program's arithmetic, culls triangles wholly outside one plane, clips
  against the near plane, and draws pre-transformed vertices (XYZRHW,
  the fog factor in the specular alpha): D3D9 does no vertex work.

The bake marks the terrain's recipe `"transform": "terrain"` with the
registers it reads. On x99 (Windows 10, pure software processing, ff
Minimal) both frames match the program's (cpu within 4 levels, ff within
17 on 0.02% of the pixels) and `DRAGON_D3D9_PROFILE`, which now times
each pipeline, gives:

| ms a frame in the draw calls | program | cpu |
|---|---|---|
| terrain (238 draws, 4,708 vertices) | 7.55 | 0.85 |
| foliage (a draw per tree, 1,257 vertices) | 11.9 | -- |
| skinned (698 vertices) | 5.0 | -- |
| mesh, water, sky | 2.8 | -- |

Path `cpu` now covers every ff program with real vertex work: the terrain,
the foliage (placement, sway and ff_lit per instance, all the trees of a
draw in one batch), the creatures (packed-joint skinning), the props and
the water, from the bake's [slot, register] map of every uniform member
(`"registers"`, written for a recipe with a `"transform"`). Consecutive
draws of one pipeline gather into one pre-transformed draw, flushed before
anything that changes state or order -- the terrain's 240 chunks of ~20
vertices cost more in draws than in arithmetic, and with the batch the
terrain beat even Windows 7's JIT (6.8 ms against 9.2). Only the sky and
the particles still go through D3D9's own vertex processing. On the Atom's
Windows 10 (ff Minimal, 640x480, headless; frames within 5 levels):

| ms a frame | program | `ff` | `cpu` |
|---|---|---|---|
| terrain | 73.6 | 60.4 | 6.9 |
| foliage | 104.9 | (program) | 7.8 |
| creatures | 44.1 | (program) | 4.7 |
| props + water | 22.9 | (program) | 3.4 |
| **median frame** | **264.8** | 239.2 | **29.0** |
| median, 900 frames into a hoard run | 262.4 | -- | 37.9 |

`ff` helps little here because Windows 10 interprets fixed function too;
it is for the cards whose driver takes T&L (the M6). Where Windows 7's
`d3d9.dll` sits beside the exe, its JIT is faster than this C++ on the
foliage (1.7 ms against 7.1: per-vertex `pow` and `exp`, four vertices at
a time in the JIT's SSE) and slower on everything batched. Still to
measure: the M6.

Under XP on the Atom the order turns over, because XP's runtime compiles
the programs (ff Minimal, 640x480, headless, median frame):

| | program | `ff` | `cpu` |
|---|---|---|---|
| default view | **13.7** | 14.4 | 23.2 |
| 900 frames into a hoard run | **18.7** | 19.2 | 33.0 |

The terrain costs the same every way (8.8-9.4 ms: the GMA and its driver,
not the vertices), and the C++ is 3-5x slower than XP's SSE on the lit
programs (the creatures 11.8 ms against 3.1, the trees 3.7 against 0.8):
per-vertex `pow` and `exp` where the JIT runs four vertices at once. So
the path follows the runtime: unset, a software-processing device on the
ff tier takes `cpu` where `d3d9.dll` exports `Direct3DCreate9On12`
(Windows 10's, the interpreter) and keeps the programs elsewhere (XP, 7,
and a Windows 7 `d3d9.dll` beside the exe); `DRAGON_D3D9_VERTEX_PATH=
program|ff|cpu` overrides it. Windows 8 and 8.1 are unmeasured and keep
the programs.

So a card the sm2 tier fits by its pixel shaders can still have software
vertices, and then sm2's own default (Medium) runs at 12 fps; the tier's
default should follow the vertex processing, not only the shader model. sm2 Minimal
also hides the river behind the coarse ground, on the Mac's `--tier sm2`
as well; the ff tier's water, built on the wet cells only, shows it.

## Not done: the other cards

The order is the G41 under XP SP3 with
each card (record the exports, then `dragon.exe` with no flags, then the
R4/R5 budgets); XP's own D3D9 drivers will have their quirks, and the
thunks' XP sides have run only when forced on a newer Windows. The tests
link the import libraries directly and are for Wine, not XP. A Pentium III
or Athlon XP has no SSE2, which the build requires.

## The G550 on the G41 (2026-10-12)

The M6 died, and the G41's card is
now a Matrox Millennium G550 PCIe (a PCI G550 behind its own PCIe bridge)
on driver 5.89.004, the last package whose D3D works on that board
(qemu-gpu's `evidence/g550/g41/g550.1-xp-driver-survey`). The caps: vs/ps
0, no hardware T&L, power-of-two textures to 2048, three stages, no
StretchRect from textures, no guard band, no DXT, no dynamic textures, and
for depth D24S8, D32, D16 and D15S1 -- **no D24X8**. Two things stopped it,
each a first-frame failure:

- **Depth.** Every depth surface was D24X8, hard-coded: the G550 refused
  it with INVALIDCALL and the game ended before its first frame. The
  backend now takes the deepest format the card offers that
  `CheckDepthStencilMatch` pairs with both 32- and 16-bit colour, and logs
  the whole set (`choose_depth_format`).
- **The HUD's glyphs drew as solid blocks.** imgui_impl_dx9 makes its font
  atlas `D3DUSAGE_DYNAMIC` in the default pool and assumes 4096 texels; the
  G550 has neither, so the atlas never existed. `cmake/imgui.cmake` patches
  a managed texture in where `D3DCAPS2_DYNAMICTEXTURES` is absent, and the
  layer caps the atlas at the device's size.

Then the ff tier draws the valley as the Mac's preview does
(`artifacts/r5-ff-preview/g550-d3d9-ff-vs-metal-ff-valley.png`), at
**17-18 fps, Minimal, 640x480 headless** (median 54 ms). The models load
from their glTFs (the packs carry DXT only), 25 s to the first frame.

### Where the 54 ms goes: the card fetching its commands

320x240 is no
faster, the process is at 100% of a core, and `sampler.exe` puts 97% of the
samples in the kernel (`KiFastSystemCall`). This first read as the display
driver writing every vertex to the card itself. It is not (2026-10-13, below):
a kernel profile shows the driver's own code at a few per cent, and the
time is the CPU *waiting* for the card. `tools/d3d9/swvp/tris.cpp` takes
D3D9's vertex work out (pre-transformed vertices, 18,050 triangles a frame)
and times each way of sending them to the frame's end on the card. Its
first figures (10 us a triangle indexed, 5.3-7.4 a plain list, 3.8-4.9 in
strips) each carried a 50 ms read-back of the whole target over PCI; ended
by a 16x16 target's read-back instead, on the driver's defaults:

| pre-transformed, tiny triangles | us a triangle | us a vertex |
|---|---|---|
| plain list, position + colour (20 bytes) | 2.74 | 0.91 |
| plain list, + uv (28 bytes) | 3.60 | 1.20 |
| plain list, + specular and uv (32 bytes) | 4.11 | 1.37 |
| strips, one a row, + uv | 1.38 | 1.37 |
| indexed list, + uv, no fog | 7.10 | -- |
| indexed list, + specular and uv, vertex fog | 4.09 | -- |
| the plain list in 1,024 draws, + uv | 4.93 | -- |

The cost follows the bytes sent -- about 27 MB/s, almost nothing a
triangle -- while texturing costs nothing and filling the whole target
adds about 10%. An indexed list with a uv and no fog is a slow path at
twice a plain list; a draw call costs about 23 us.

### The driver's own settings did not change it

The miniport
(`g550dhm.sys`) defines its registry values as category-prefixed names
with defaults, read from the device's video key -- `Engine.BusMastering`,
`Engine.BusMastering2D` and `Engine.BusMasteringOGL` default to **1**
already, `CPU.WriteCombining`, `CPU.AGP` and `User3D.ForceAGP` to 0,
`User3D.GuardBand` to 0. Set on the active key and rebooted, each in turn,
`User.BusMastering` (a wrong guess at the name), then `CPU.WriteCombining`
with `User3D.GuardBand`, left the table within 3% and the reported guard
band at 0; all are removed again. `ForceAGP` on a board with no AGP was
not tried.

### Strips do not pay in the game

Path `cpu` gathers each pipeline's
triangles into one pre-transformed draw, so `meshopt_stripify` on that
batch was tried (with `meshopt_optimizeVertexCacheStrip` first): the
batches are short culled and clipped runs of a few chunks, and the strip
drew **1.65 primitives a triangle**, the rest degenerate joins, which this
driver pays for like any other. Lowest went 27.5 -> 25.8 ms headless,
Minimal no better; the change was not kept.

### Path `cpu` is the G550's default

Its culling and batching cost this
driver less than the runtime's own clipping into it: headless at
640x480, Lowest median 31.8 -> 27.4 ms and Minimal 52-54 -> 46, and in a
real run (hoard run seed 7, autopilot, 3,600 frames, fullscreen 640x480
16-bit, vsync off) Lowest **23.9 -> 29.6 fps**. A Matrox adapter
(`VendorId` 0x102B) now takes it unless `DRAGON_D3D9_VERTEX_PATH` says
otherwise; the frame matches the Mac's
(`artifacts/r5-ff-preview/g550-d3d9-ff-cpu-vs-metal-ff-lowest.png`).

| G550, XP, the run above | Average | Median | 1% low |
|---|---|---|---|
| ff, Lowest, path `cpu` | 29.6 fps | 33.2 ms | 49.8 ms |
| ff, Lowest, programs | 23.9 fps | 49.8 ms | 49.8 ms |
| ff, Minimal, path `cpu` | 15.7 fps | 66.4 ms | 83.0 ms |

The fullscreen medians land on whole 60 Hz refreshes with vsync off, so
the driver appears to flip on the blank whatever the present interval
(its `User3D.FlipOnVBlank`); the headless figures are the real costs.
Lowest is the G550's preset. More would have to come from fewer
triangles -- the game sends about 4,900 a frame at Lowest (terrain 2.9K,
rocks 0.6K, water 0.5K, trees 0.5K, sky 0.25K, the dragon 0.1K), each at
the price above.

### The bottleneck was the bridge, and it tunes (2026-10-13)

The G550
PCIe is a PCI G550 behind a TI XIO2000A PCIe-to-PCI bridge (`104c:8231`,
x1 gen1). Three probes in `tools/d3d9/swvp/` found it:

- `pcicfg.exe` reads configuration space through `NtSystemDebugControl`:
  bus mastering is on for the card and the bridge (command `0007` and
  `0117`).
- `kprof.exe` profiles the kernel the way `kernrate` does
  (`NtCreateProfile`). Under OpenGL the busy core is 100% in one 16-byte
  bucket of `G550DHD.dll`, a loop reading the chip's STATUS register
  (`0x1e14`) until the primary DMA list has ended. Under D3D9 it is d3d9's
  user-mode loop and the kernel's locks around its calls into the driver,
  with the driver's own code at 2-5%. Both wait for the card.
- `gltris.exe` draws the same grid through Matrox's OpenGL driver: 4.2 us
  a textured triangle in a list, 2.3 in strips -- slow too, so not D3D's
  driver alone.

The card reads its DMA list from system memory through the bridge, which
answers an upstream Memory Read Multiple by prefetching 2 cache lines of
its cache-line-size register (8 DWORDs here): 64 bytes a round trip. Two
settings lengthen that, and a third lets the chip ask for it:

| µs a triangle, pos+col+uv | driver default | `Engine.EnableMemAccess=1` | + bridge `PREFETCH_4X` | + bridge cache line 16 DWORDs |
|---|---|---|---|---|
| D3D9 plain list | 3.70 | 2.21 | 1.53 | **1.20** |
| D3D9 strips | 1.42 | 0.87 | 0.63 | **0.57** |
| D3D9 indexed list | 7.24 | 4.32 | 2.98 | **2.35** |
| OpenGL arrays, textured | 4.26 | 2.49 | 1.77 | **1.38** |

- `Engine.EnableMemAccess=1` (a REG_DWORD on the video key, default -1,
  "auto", which left it off) sets bit 22 of the chip's OPTION register
  (`480a1520` -> `484a1520`), as far as can be told the enhanced memory
  access that lets it burst. It survives a reboot; `CPU.BridgeBMControl`
  was set with it and made no difference.
- `PREFETCH_4X` is bit 6 of the bridge's Control and Diagnostic Register 0
  (`C0h`): `pcicfg.exe OUT set 1:0.0 C0 40`.
- The cache line size (`0Ch`) the HAL refuses to write, so `pcicfg.exe OUT
  port 1:0.0 0C 00010010` writes it through ports CF8h/CFCh; 32 DWORDs is
  no faster than 16. `FORCE_MRM` (bit 24 at `D4h`) changes nothing. Both
  bridge values reset at boot and have to be written again.

What did not help: `Engine.BusMastering=0` turns the driver's 3D off
altogether (`CreateDevice` returns D3DERR_NOTAVAILABLE and OpenGL falls to
Microsoft's GDI Generic, which on the Celeron draws this grid 4-13x faster
than the card did); `Engine.SynchronizeEngine=0` moves nothing; vertex
buffers in the default pool cost what user pointers do.

The same run as the table above, tuned:

| G550, XP, tuned | Average | Median | 1% low |
|---|---|---|---|
| ff, Lowest, path `cpu` | **54.9 fps** (29.6) | 16.6 ms | 33.2 ms |
| ff, Lowest, programs | 40.1 fps (23.9) | 16.6 ms | 33.2 ms |
| ff, Minimal, path `cpu` | **40.4 fps** (15.7) | 16.6 ms | 33.2 ms |
| ff, Minimal, programs | 40.2 fps | 16.6 ms | 49.8 ms |

Path `cpu` stays the Matrox default. With the card fed three times faster,
Minimal is now playable and Lowest is at the refresh most of the time. A
PCI card behind such a bridge is worth this check before its triangle
budget is cut: the bridge's prefetch, not the chip, set the G550's.
