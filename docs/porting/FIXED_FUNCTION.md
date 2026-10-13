# Porting: the fixed-function tier (R5)

The tier for cards without shaders: the `FIXED_FUNCTION` variant that
previews it on every backend, the texture-stage recipes D3D9 runs, and
why DX8 cards run it rather than a ps_1.x tier.

Part of the porting record; [`../PORTING.md`](../PORTING.md) is the index, with the
state of every phase and a map from the section names cited in code
comments ("R6, as built", "The G550 on the G41") to the file that holds them.

## R5, the plan (2026-10-04): fixed function

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

### Step 1, as built (2026-10-04): the FIXED_FUNCTION variant

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

### Steps 2 to 4, as built (2026-10-04): the tier on the X550

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


## vs/ps 1.x for the DX8 cards: the vertex side, not the pixel side (2026-10-04)

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
