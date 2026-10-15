# Fidelity: the modern build at its top presets

Planned 2026-10-13. What it takes to move the picture from where it is to
`concept/valley-target.png`, on the modern tier at Ultra and High, now that
the retro tiers have their own content path and no longer limit what the
modern one may use (CLAUDE.md: "no need to limit usage of modern
features"). The art direction is `DIRECTION.md` section 3 and stays
authoritative for the *look*; this file is the *techniques* and their
order. Its "Not recommended: deferred shading, HDR render targets,
screen-space reflections, volumetric fog. They fight the retro track" was
right when the modern and retro builds were one content path. R2 split
them (`porting/D3D9.md`), and the HDR target has existed since the post
stack, so that line no longer holds for the modern tier.

The baseline is in `artifacts/fidelity-baseline/`: a run, a valley vista
and the dragon close, at Ultra on the Mac, 2026-10-13, beside the target.

## Where the picture is

Read off those captures against the target, worst first:

1. **The mountains are clay.** Smooth, uniformly tinted slopes with rocks
   sprinkled on them like dots. The target's mountains are rock: cliffs,
   strata, gullies, scree fans, sharp ridges. The generator has a ridge
   octave and strata (`WORLD.md`, "Row 7"), but the terrain mesh is 6 m
   cells and the shading is a tint plus a detail tile, so nothing below
   about 12 m exists, and erosion -- the thing that makes a mountain look
   like a mountain -- is not modelled at all.
2. **The sky is a gradient.** No clouds, no sun disc to speak of, and the
   haze is an exponential fog lerped toward the sky colour, so distance
   reads grey instead of blue and nothing is lit by the sky that is
   actually there.
3. **The forest is sparse.** Trees stand singly on a lawn; the target's
   forests are masses, coloured tree by tree, with the ground under them
   dark.
4. **Shadows stop.** One 4096² map following the camera covers a few
   hundred metres (`gfx/shadow_map.h`); past it nothing shadows anything,
   so a low sun gives no long mountain shadows -- the thing a low sun is
   for.
5. **The river is a flat ribbon**, without gravel bars, braids, depth
   colour or a reflection of anything but an analytic sky.
6. **The dragon floats.** It is the finished asset, but with no ambient
   occlusion and no contact with the light around it, it sits on the
   scene rather than in it.

## What the platform allows

The modern tier runs on SDL GPU (Metal, Vulkan, D3D12) and D3D11. SDL GPU
3.4 has **compute pipelines, storage buffers and textures, indirect draws,
texture arrays, 3D textures and cube maps**. It has **no tessellation, no
geometry shaders, no mesh shaders, no ray tracing, no timestamp queries,
and no access to the native device** (`SDL_gpu.h`, checked 2026-10-13).
The RHI (`src/rhi/rhi.h`) exposes none of compute yet.

So:

- **"Tessellation" is done the way current terrain renderers do it
  anyway**: a quadtree of instanced grid patches displaced in the vertex
  shader from a height texture (CDLOD or geometry clipmaps). It gives
  triangles where the camera is and works on every modern backend.
  Hardware hull and domain shaders would buy nothing over it for a
  heightfield, and Metal's own tessellation is compute-driven.
- **"Ray tracing" splits in two.** The terrain is a heightfield, and a
  heightfield can be ray-marched in an ordinary compute or fragment pass:
  long mountain shadows, horizon occlusion and sky visibility for the 90%
  of the frame that is ground, on every modern backend, with no RT
  hardware. Hardware ray tracing adds the rest -- the dragon's wings
  shadowing softly, occlusion under the body, dragons reflected in the
  river -- and needs a native backend, because SDL GPU has no ray tracing
  and no way to reach the native device. The M5 has hardware RT through
  Metal; x99's 5060 Ti through Vulkan and D3D12.
- **GPU cost is measured by ablation**, as the retro track already does
  (`DRAGON_FREEZE_AT` with `DRAGON_ABLATE`): without timestamp queries,
  a pass's cost is the frame without it.

Every feature below goes behind a Graphics setting (R7) at levels only the
modern tier offers, so the sm3, sm2 and ff goldens do not move.

## Phases

| | Work | Exit test | Sessions |
|---|---|---|---|
| **F0** groundwork (deferred: F1 needed none of it) | Compute in the RHI: compute pipelines, storage buffers and textures, dispatch, indirect draw, on SDL GPU and D3D11 (CS 5.0); none on D3D9. A fidelity golden set at Ultra: the vista, a cliff close, the forest floor, the river, the dragon at a low sun | The current goldens unchanged; one compute pass (the sky LUTs) on every modern backend | 1-2 |
| **F1** sky and air (**done 2026-10-13**, below) | Hillaire's sky (EGSR 2020): transmittance, multiple-scattering and sky-view LUTs, and an aerial-perspective volume the world shaders sample in `apply_fog`, so haze is the atmosphere's in-scattering and blue-shifts with distance. A sun disc with limb darkening; a sun-elevation dial (late afternoon by default). The valley's height fog stays as an artistic layer on top. The retro tiers get the new sky for free: the ff dome bakes whatever the sky function returns | The vista against the target: blue depth, a warm low sun, a horizon that reads as distance | 2 |
| **F2** terrain shape (**done 2026-10-13**, below) | Hydraulic and thermal erosion in the generator, on the CPU, deterministic (the "same content on every compiler" rules), cached per seed under `paths::user`. It writes the heightfield and three maps the materials need: flow (gullies, the braided river bed), sediment (scree fans, gravel bars) and wetness. The heightfield stays the one source of truth: `height_at` keeps querying the rendered triangle | Mountains read as rock with gullies from the vista; the E3300 generates a valley in an acceptable time, or the cache covers it | 2-3 |
| **F3** terrain detail (procedural and scanned rock, 2026-10-13, below; the CDLOD mesh to come) | A CDLOD quadtree of instanced patches displaced from the height texture, morphing between levels, so the ground near the dragon has 1 m triangles and the view stays 16 km. Material layers in texture arrays (albedo, normal, roughness, height; PolyHaven CC0: grass, forest floor, scree, gravel, rock with strata, snow), splatted by slope, altitude and F2's maps, height-blended, triplanar on cliffs, parallax on rock at Ultra. A macro colour map so far slopes read painted. Rocks and scree placed from F2's sediment, not from slope alone | The cliff golden against the target's right-hand cliff; the foot-plant still on the floor (`--inspect 60 11 3`) | 3 |
| **F4** light (**heightfield shadows and cascades done 2026-10-13/14**, below; sky light done in F1) | Cascaded shadows (four cascades to about 3 km) at Ultra and High. **Heightfield shadows**: a compute pass marches the height texture toward the sun into a top-down shadow map for the whole valley, recomputed when the sun moves, for shadows past the last cascade. Sky light from the F1 LUTs into a small cube map, prefiltered, replacing the two-colour hemispheric ambient with the sky that is there, and giving the scales and the water a specular reflection. Horizon occlusion baked from the heightfield | Long mountain shadows across the valley at a low sun; shadow sides lit by the sky's colour | 2-3 |
| **F5** temporal (**TAA done 2026-10-13**, below; upscaling to come) | TAA: a jittered projection, motion vectors (the skinned meshes need last frame's joints, the foliage last frame's wind), history with neighbourhood clamping. It is what makes the noisy techniques after it affordable, and it calms the alpha-tested crowns' shimmer. A temporal upscale on top once it holds | Crowns and fine rock stable in motion; no ghosting behind the dragon in a turn | 2 |
| **F6** clouds and AO (**volumetric clouds and SSAO done 2026-10-14**, below) | Volumetric clouds: raymarched at quarter resolution with temporal reprojection, from a weather map, lit by the F1 sun and sky, shadowing the ground through a cloud shadow map; flown through, not painted on -- the altitude cue `DIRECTION.md` row 9 asks for. GTAO at half resolution, and screen-space contact shadows for the creatures | The dragon flies into a cloud and out; it sits on the ground when landed | 3 |
| **F7** forest (Ultra's density tripled 2026-10-13, below) | GPU-driven vegetation: compute culling into indirect draws, five to twenty times the trees so forests are masses, octahedral impostors past the card LOD at Ultra, per-tree colour variety from the palette, grass to a longer distance | The forest golden against the target's middle ground | 2 |
| **F8** water (**done 2026-10-13**, below, by heightfield rather than screen space) | Flow-mapped normals along the river from F2's flow, depth colour and absorption, shoreline foam, screen-space reflections with the F4 sky cube as their fallback | The river golden: gravel bars, braids, reflections of the banks | 1-2 |
| **F9** hardware ray tracing | A native Metal backend (`src/rhi/metal/`, the size of the D3D11 one), then ray-traced soft shadows, occlusion and water reflections against the creatures and trees, on top of F4's heightfield work. A native backend also brings MetalFX upscaling to the Mac. Vulkan ray queries on x99's 5060 Ti would be a second native backend | The wings' shadow soft on the ground; dragons in the river | 3-5 |

About twenty sessions in all. **The order is by what each buys**: F1
changes every frame for two sessions of work and closes the most of the gap
per session; F2 and F3 together are the clay-mountain fix, the single
largest difference from the target; F4 is what a low sun is for; F5 has to
come before F6 because clouds and GTAO are only affordable with history;
F9 is last because F1-F8 already cover the terrain, which is most of the
frame, and its marginal gain is on the creatures alone.

## F1, as built (2026-10-13)

`gfx::Atmosphere` (`src/gfx/atmosphere.h`) and `shaders/atmosphere_*.hlsl`,
on the modern tier only (the `ATMOSPHERE` define). Renders in
`artifacts/fidelity-f1/`: the vista, toward the sun, from 2.5 km up, a 4
degree sun, a 55 degree sun, and the run's chase view.

- **No compute after all.** Every table is a fullscreen fragment pass into
  an RGBA16F target, so it runs on SDL GPU and D3D11 with no RHI change and
  F0 waits until something needs it. The RHI has neither 3D textures nor
  viewports, so the per-frame tables share one 256x232 **atlas** that one
  pass fills, branching on the row: the aerial-perspective froxels (32
  slices of 32x32, tiled 8x4), the sky-view map (256x96), and the sky light
  on nine normals. A world shader reads it through one texture slot, the
  `ATMOSPHERE_SLOT` each shader defines before including
  `scene_common.hlsl` (terrain 2, water 0, mesh 1, skinned 4, foliage 3,
  sky 0 with the transmittance table at 1).
- **The transmittance and multiple-scattering tables** are rebuilt only
  when a dial that shapes them changes, or a hot reload replaces their
  pipelines.
- **The one lighting path holds.** `sky_color`, `apply_fog` and the sky half
  of `ambient_light` read the atlas when `scene.atmosphere.x > 0`; with the
  atmosphere off (the Lighting panel's "physical sky", `--atmosphere on=0`)
  they are the analytic functions again. That frame matches the build
  before F1 to 1/255 in one pixel. The sm3, sm2 and ff tiers render
  bit-identically to it (checked against a build of the parent commit).
- **Sky light is integrated, not guessed.** Row 224 holds the sky's
  radiance averaged over the hemisphere about an upward normal and eight
  horizontal ones (cosine-weighted, the ground below the horizon left to
  the bounce). That replaced the zenith/horizon colour pair. Calibrated so
  the dragon's own lighting barely moved (`tests/golden/metal/dragon-head`).
- **The world past the terrain.** A sky-view ray below the horizon first
  ended on nothing and drew a dark band like a sea from altitude. It ends on
  a lit ground tinted like the valley now (`ground_tint`), hazed by the air
  before it.
- **Dials** (the Lighting panel's Atmosphere section, and `--atmosphere
  key=value,...` for captures; `--sun elevation[,azimuth]` sets the time of
  day): sky brightness 9, aerial distance 5x, haze 1.2x Earth's aerosol,
  glow 0.7, valley fog kept 0.35, sky light 1, sun disc 60. The aerial
  scale is the artistic lever: Earth's air over a 15 km valley is barely
  blue, and 8x read as fog. Pushing the haze (Mie) instead greys the
  distance rather than blueing it.
- **Cost**: about 0.4 ms a frame at 1920x1080 on the M5 (600 frames with
  and without it).
- **Not checked yet**: Vulkan, D3D12 and D3D11. MoltenVK is not available
  to SDL on this Mac, and x99 was off. Their golden sets (`vulkan`, `d3d12`,
  `d3d11`) predate F1 and fail for the modern scenes until they are
  recaptured there; compare them `--set metal --loose` first. All 156
  stages bake for MSL, SPIR-V and DXIL (`bake_shaders.sh`).
- **What it did not do at first**: shadow the air. It does now (F4, "The
  shadow in the air"). No volumetric clouds either (F6). The surfaces' sun colour is still the
  Lighting panel's, not the transmittance's, so a 55 degree sun is still
  lit late-afternoon warm.

## F2, as built (2026-10-13)

`game/erosion.h` and `Terrain::run_erosion`. Renders in
`artifacts/fidelity-f2/`: the vista, from altitude, top-down on the gullied
face, the face close, and the run's chase view.

- **Stream power, not droplets.** The first version ran Beyer's droplet
  erosion. On faces this steep it left ripples across the flow, fish
  scales from above, and no channels. The stream power law, solved
  implicitly (Braun and Willett 2013), cuts in proportion to the area
  draining through a cell, so it carves valleys where the drainage
  gathers. A talus pass softens the cuts and lays the scree.
- **Three things made it read as mountains rather than combed clay:**
  - Priority-flood routing (Barnes 2014): the ridged noise is full of
    closed basins that split the drainage. Before the flood, the widest
    catchment was 2,400 cells (0.35 km²); after it, 66,000 (9.5 km²).
  - A^0.75 instead of sqrt(A): big catchments cut far more than rills, so
    valleys form between faces that keep their shape. With sqrt(A), every
    slope combed into the same parallel grooves.
  - A 1-2-1 blur on the result: a one-cell channel upsampled onto the 6 m
    mesh was a string of beads wherever it ran diagonally.
- **One eroded valley on every tier.** The grid is its own: 12 m cells over
  1.6 times the playable extent, sampled by each tier's mesh whatever its
  cell, so `analytic_height_at` is the noise plus the erosion's change and
  `height_at` still reads the rendered triangle. The vertex-lit tiers erode
  on a 24 m grid, a quarter of the work, for CPUs a tenth of the M5's.
- **Gameplay's ground is kept.** The valley floor is the base level the
  mountains drain to and is never changed; the river's banks are masked;
  the change fades out over the grid's outer sixth, where the uneroded
  skirt carries on.
- **What it leaves for the materials**: drainage area and scree depth, in
  the terrain's vertex colour on the per-pixel tiers (the vertex-lit ones
  tint by that colour, so theirs stays white). The terrain shader bares
  rock in the gullies, darkens the stream beds, and lays paler scree where
  the talus settled. With the rock still one flat colour (F3), they read
  more as shape than as material.
- **Deterministic**: no random numbers, the traversal fixed by grid order,
  only sqrt, and `-ffp-contract=off` (`tests/test_erosion.cpp` checks that
  two runs agree, and that the base levels are untouched). The noise it
  starts from is *not* built FMA-free, though, and erosion is chaotic
  enough to magnify an ulp: a seed may differ in detail between the Mac
  and Windows. If that matters, terrain.cpp and core/noise.cpp join the
  no-contract list.
- **Cuts are eased past 150 m** (`CUT_LIMIT`, with a third talus pass):
  the biggest catchments carved slots near 400 m deep and a few cells
  wide, a trench rather than a valley, seen first behind the dragon in the
  close inspection view.
- **Cost**: 1.0 s on the M5 (12 m), 0.2 s at 24 m, and paid twice on a
  `--run` start, which generates the arena's default terrain before the
  run's seed. On the G41 it wants measuring before the next XP package.

## F4, first part, as built (2026-10-13): the terrain's shadow at every distance

`shaders/terrain_sun.hlsl`, owned by `gfx::Atmosphere`. Renders in
`artifacts/fidelity-f4/`.

- **What it fixes.** The shadow map covers 1.8 km about the camera, but a
  900 m ridge under a 14 degree sun throws its shadow 3.6 km. Past the map
  nothing shadowed anything, so the low sun -- the whole point of the
  late-afternoon direction -- gave no long shadows.
- **A map of shadow heights, not of shadow.** For every column of the
  world (2048² over the terrain's 15 km, 7 m a texel), a fragment pass
  marches a 1024² height texture toward the sun and stores the shadow's top
  there, max over t of (ground(t) - t tan(elevation)), with the distance to
  the ridge that sets it. A point at any height compares itself against
  that: the ground, a crown, a dragon flying over a shadowed valley, all
  from one lookup, with a penumbra that widens with the ridge's distance.
  `sun_visibility` takes the darker of it and the shadow map, so every lit
  shader has it; the water's glint reads it too.
- **Rebuilt only when the sun or the terrain changes**, so the sun slider
  sweeps the shadows live.
- **What it shows.** With erosion (F2), this is the frame the target is
  about: lit crests, faces banded light and dark, the valley floor in the
  ridges' shadow with patches of sun. **It was darker to play in**, and
  the first playtest (2026-10-13) said so: the modern tier's sun now
  stands at 19 degrees rather than 14, the sky light's gain is 1.4, and
  exposure is 1.18 (the retro tiers, without long shadows, keep theirs).
  The run's lower frame went from a mean of 60 to 73 of 255, against 91
  before any of this.
- **Retro tiers**: not there (no `ATMOSPHERE`). The ff tier bakes terrain
  shadows into its vertex colour already; doing the same from this map is
  the cheap way to give it the long ones too.
- **The terrain's sky occlusion rides along** (the same pass, the map's b
  and a channels): the horizon's elevation in eight directions out to
  1.5 km, cos² of each, gives the share of a cosine-weighted sky each
  column sees, and the ground's height is stored beside it. Every lit
  shader's ambient is scaled by it near the ground (`terrain_sky`), fading
  out 60 m above, so a gully and a valley bottom see less sky than a ridge.
  That matters because, with the ridges' shadows, the sky's light is most
  of what lights the valley. Squared, it read as murk, so it is the plain
  share, and the sky light's gain went from 1 to 1.2 to keep shaded ground
  readable.

## F3, first step (2026-10-13): the rock

In the modern tier's terrain shader (`shaders/terrain.hlsl`, under
`ATMOSPHERE`, so SM3's budget is untouched). Renders in
`artifacts/fidelity-f3/`. After erosion, the faces had the right shape and
read as sand: one flat warm beige, bent smoothly by a 6 m mesh. Three
terms now, all fading out with distance before they would shimmer:

- **Crags**: two octaves of value-noise bumps (24 m and 9 m) on the
  vertical world planes, blended like a triplanar map, bend the normal on
  rocky ground out to 3.5 km. Ridged noise in the face's own frame came
  first and drew wood grain: its ridges ring like contours, and the frame
  turns with the normal.
- **Broken rock**: grey and warm blotches over the rocky ground.
- **Scanned rock and scree**: Poly Haven's `rock_face_03` and
  `gray_rocks` (CC0, `tools/build_terrain_rock.py`), baked to a normal map
  plus the scan's luminance. The palette still colours the ground, as the
  detail tile does, so the grade stays in one place. They are sampled on
  the three world planes with a whiteout normal blend, rock at 7 m and
  26 m mixed, scree at 5 m where the erosion's talus settled. They fade by
  2.5 km and 1.5 km; past that, the crags carry the faces. Terrain slots
  4 and 5, after the atmosphere's two maps.
- **Bedded cliffs, tried and removed**: 11 m ledges with lit tops,
  jointed risers and grey-to-warm bed colours, after the target's
  right-hand cliff. This terrain has few faces steep enough to be cliffs,
  and on the rest, even limited to faces over 52 degrees, the beds read
  first as contour lines and then as a ploughed field. Real strata want
  geometry: the generator's own strata, or the CDLOD mesh.

Up close it now reads as broken rock rather than sand. At a kilometre,
the shape (F2) and the crags do the work. Next for F3 is the CDLOD mesh.
Cost: about 1 ms a frame at 1080p on the M5 for the crags, the mottling
and the scans together (600 frames: 6.80 s before, 7.39 s after).

## F4, second part (2026-10-13): the shadow in the air

Every metre of haze was sunlit, so the air over a valley the ridges shade
glowed as brightly as the air in the sun, and the shade read as fog. The
aerial perspective's integral (`atmosphere_common.hlsl`, `atmo_integrate`)
now looks up the terrain-sun map at each step of a froxel's ray and cuts
the sun's single scattering where a ridge blocks it. The multiple
scattering stays. The air over shade goes dark and blue, and toward the sun
the haze is lit only where it is in sun. The froxels are 32 across the
screen, so it is soft, as shafts in haze are. Only the froxels: the
sky-view map looks over the ridges, and the sky light is the whole sky's.
Cost: within the noise of the 600-frame timing.

## F4, third part (2026-10-14): cascaded shadows

`gfx/shadow_map.h`. Renders in `artifacts/fidelity-f4/cascades-*`. One
4096 map over 1.8 km gave 44 cm texels near the dragon and no shadows
past 1.8 km, so the forest stopped casting halfway across the valley.
Now the modern tier has three boxes, 75 m, 380 m and 1.9 km in half-width,
each a quadrant of the same 4096 atlas, drawn through a viewport (the new
`rhi::Device::set_viewport`, on SDL GPU and D3D11). Their texels are 7 cm,
37 cm and 1.9 m. The shaders take the finest box a point is in, with the
normal offset and the bias scaled to its texel. The far box draws the
crowns' coarse cards only, and no terrain (the heightfield shadow covers
the terrain there): the full set into all three cost 3 ms a frame, and
this costs about 1.3 ms. The retro tiers keep their single map.

The shadow map's texel snap had never worked: the light view it snapped
in was anchored at the map's own centre, so the centre rounded from zero
to zero, and shadows shimmered in flight at every height (the playtest).
Anchored at the world origin, the snap holds, on every tier.

## F6, first part (2026-10-14): ambient occlusion

`gfx/ssao.h`, `shaders/post_ssao*.hlsl`. Renders in `artifacts/fidelity-f6/`.
Half resolution from the main depth: positions reconstructed from the
infinite reversed-Z projection, normals from the nearer-agreeing
neighbours, twelve samples on a per-pixel-rotated Vogel spiral within
3 m, fading out from 350 to 900 m. A 5x5 depth-aware blur follows, and the
composite multiplies it in before the bloom (`ATMOSPHERE` only, so SM3's
composite is untouched). It shows inside the crowns, where the dragon
meets the ground, and at the grass bases: up to 20 levels, subtle by
design. Applied to the whole light at first, which the playtest found
wrong (below, "After the second playtest"): it now darkens only the
ambient share each world shader leaves in the scene target's alpha.
Dials: the Grade & bloom panel's "ambient occlusion" and its radius,
intensity, strength and angle bias; `DRAGON_ABLATE=ssao`. Cost: within
the timing's noise at 1080p.

## F5, as built (2026-10-13): temporal anti-aliasing

`gfx/taa.h`, `shaders/post_taa.hlsl`, `shaders/velocity_skinned.hlsl`; the
modern tier only. Renders in `artifacts/fidelity-f5/`.

- **Jitter, depth, resolve.** The world draws through an eight-phase
  Halton sub-pixel jitter. The main pass keeps its depth (now sampleable,
  `Device::set_depth_sampled`), and a resolve pass before the bloom blends
  each frame into a ping-ponged history: reprojected through the depth,
  sampled bicubic (Catmull-Rom in five taps), and clipped toward the
  current 3x3 neighbourhood's mean in YCoCg within 1.25 standard
  deviations. The matrices are camera-relative, and the sky, at infinite
  depth, reprojects as directions. Fast motion keeps less history. A
  camera cut, a resize or switching it back on starts afresh.
- **Creatures carry their own motion.** Depth reprojection takes every
  pixel for a fixed point of the world. The chase camera's dragon is not
  one, and it smeared and darkened into the ground behind it. After the
  main pass, the player and the rivals are drawn again into a velocity
  target, depth-tested against what the main pass left and not writing
  it. Each pixel there gets the screen motion of its surface point from
  this frame's pose, model and camera against last frame's, both skin
  palettes included, so the wingbeat moves too. The resolve uses that
  motion wherever a creature was drawn.
- **Sharpening** in the composite (`PostSettings::sharpen`, 0.6, on the
  luminance, limited) offsets the softness TAA brings to a moving picture.
  It is on only while the resolve is.
- **Bugs on the way**, for the next temporal pass:
  - A debug return that skipped a texture shrank the shader's reflected
    sampler count, so the depth in the slot after it was never bound and
    read as zero (`common.hlsl` warns of exactly this).
  - Debug colours go through the tonemap, so read them inverted.
- **Dials**: the Grade & bloom panel's "temporal AA" and "sharpen";
  `--no-taa`; `DRAGON_TAA_WEIGHT` sets the history weight (0.9).
- **Not covered**: prey, props and projectiles reproject by depth, with
  the clamp as their only guard; particles too. Vulkan, D3D12 and D3D11
  are unchecked: the main depth is now sampled there, and D3D11's depth
  views want checking first.
- Cost: 1.2 ms a frame at 1080p on the M5, the depth store and the
  velocity pass included.

## After the first TAA playtest (2026-10-13)

"Very smeared on trees, increasing sharpness doesn't help, kind of remind
me of Witcher 3 vegetation". It doesn't solve the distant river edges
either. And the river seems static. What was found:

- **The smear was a bug.** The velocity target was cleared with the
  pass's default colour, whose alpha is 1, which marked every pixel a
  creature standing still. The history was never reprojected, and in
  motion the whole frame dragged. Cleared to 0, the forest holds its
  shape in flight. TAA still softens thin, sub-pixel crowns: averaging
  their coverage is what anti-aliasing them means.
- **FXAA is the default now** (`--aa off|fxaa|taa`, the panel's
  "anti-aliasing"). Lottes' filter in the composite, on the tonemapped
  luma: crisp crowns, smooth long edges, some shimmer left in motion. TAA
  stays a click away.
- **The river's edge was geometry, not pixels.** Where the bank's 6 m
  facets rise just above the water line, the plane was not drawn, so the
  visible shore was the facets' outline, a few pixels a segment at 1.5 km.
  No AA rounds a polygon. The modern plane now stands half a metre proud,
  and the shader shows water by the smooth heightfield's depth,
  feathering the last metre and discarding where there is none, so no
  depth is written there.
- **The river flows.** The water grid carries the downstream direction at
  each vertex (from the generator's course), and the ripples, stretched
  along it, drift at 2.4 m/s with a two-phase flow map. Foam streaks
  follow.
- **Second round (the same day): "full of lines with enough height",
  "still a bit unnatural and still".** The value-noise ripples, stretched
  along the current, drew lines at a distance, finer than a pixel and
  crawling, which broke the anti-aliasing. The surface is now twelve
  travelling wave trains about the downstream direction, 14 m down to
  0.4 m, each at deep water's speed for its length plus the current's, with
  rough and calm patches drifting over them. Each train fades once it is
  shorter than a few pixels (the fragment's metres per pixel), and the foam
  streaks keep to where they are wider than one.
- **Third round**: the long trains summed into a regular diamond lattice,
  which the sun's glitter drew across the far river. The waves' domain is
  now warped by a slow noise drifting with the current, the wavelengths
  are detuned, the slopes flatten where a pixel covers a metre or more,
  and the glint broadens there to a sheen.
- **The tree shadows' shimmer** was the shadow map's texel snap, which did
  nothing: the snapping view was anchored at the map's own centre, so the
  centre always rounded from zero to zero. Anchored at the world origin,
  shadow edges hold still as the camera moves.
- Debugging lessons, twice over: a debug return that leaves one texture
  unread shifts every later slot (`common.hlsl`), and debug colours pass
  through the tonemap.

## After the second playtest (2026-10-14)

Renders in `artifacts/fidelity-playtest2/`. Three complaints, each fixed:

- **"Ambient occlusion leaves unintended shadows over the image."** Two
  causes. The composite multiplied the occlusion into the whole light, so
  sunlit ground beside a tree went grey in a blot the sun had not cast;
  and the 6 m terrain facets meet at shallow folds that a 0.06 angle bias
  counted as creases, a grid over every far slope. Now each world shader
  writes, in the scene target's alpha, the share of its pixel the
  occlusion may darken (`occlusion_share` in `scene_common.hlsl`): the
  ambient light's part of the light it received, times what is left of
  the surface after the fog (`apply_fog` keeps it). The composite scales
  the occlusion by it, so the AO falls on ambient light alone, where it
  belongs. Blended draws (the water, the particles) leave the alpha alone
  on SDL GPU and D3D11 (D3D11's level 9_1 cannot, and runs no AO), and
  TAA passes it through. The angle bias is 0.25 and a dial. What is left
  is the contact: under the dragon, inside the crowns, at the base of the
  bushes, and nothing in the sun.
- **"The river looks off in mountain shadows."** Several things the sun's
  glitter had hidden. The bed showed through two metres of clear water lit
  by the whole sky, so from altitude a shaded river glowed a pool's teal,
  brighter than the valley around it; the extinction is now a silty
  mountain river's (1.2, 0.75, 0.65 per metre), the bed is wet and darker,
  and the light under the surface sees only the valley's sky
  (`terrain_sky`). The wave trains started at 14 m and ran within 35
  degrees of downstream, so they summed into long crests from bank to bank,
  a wave pool; they now run from 5 m down to 0.15 m in every direction (a
  river's chop is the bed's turbulence and the wind's, not a swell), with
  glassy stretches between rough ones and slack water along the banks.
  The foam streaks were a noise's high band, soft grey blots metres
  across that read as fog lying on the water; they are thin contour lines
  now. And the reflection traced only the ground, so a wooded bank
  mirrored as bare: `App::upload_canopy` splats each tree's crown onto the
  heightfield's grid as cover and top height (the heights texture's g and
  b), and the reflected ray passes through the crowns as through a haze
  as thick as their cover.
- **"Tree LOD pop-in is quite obvious."** At 240 m each tree switched from
  its whole crown to its impostor, dithered tree by tree over 30%, and the
  impostor -- a few large cards with the trunk showing -- turned a slender
  spruce into a square blob (`tree-lod-ref-old-first.png`). Making the
  impostor thicker made the blobs worse. Whole crowns cost only 1.3 ms at
  every distance on the M5 at 1080p, so the modern tier keeps them to
  900 m and dissolves them card by card (each card its own threshold, the
  tree's hash plus the card's shade) to the impostor by 1800 m, where a
  tree is a few pixels tall; the coarse cards cut out slightly less and
  darken as the band passes, and the far shadow cascade's coarse crowns
  match. Trees also shrink into the ground over the last 15% of the draw
  distance, rather than whole ground cells appearing at its edge. 0.6 ms
  over the old LOD. Dials: Terrain > Vegetation's "crown detail
  distance", "crown detail band" and "tree fade band". The retro tiers keep
  240 m and their 30% band.

## F8, as built (2026-10-13): the river

`shaders/water.hlsl`, the modern path. Renders in `artifacts/fidelity-f8/`.
The river was a flat mirror of the sky, the one colour of the sky at
every angle. Everything new reads the heightfield the sun is marched over
(the terrain-sun map keeps the ground's height in its alpha, under the
water too), so none of it needs a second view or a copy of the screen:

- **Reflections by heightfield tracing**: the reflected ray marches the
  ground in 40 steps out to 3 km. Where it hits, the reflection is that
  ground, roughly shaded (dark forest low, grass and rock and snow above),
  lit by the sun where the terrain lets it through, and hazed. Where it
  escapes, it reflects the sky. A ray that only just clears the ground
  takes some of it, by how narrowly. Hit-or-miss alone left hard dark
  flecks wherever a ripple tipped a ray onto the bank.
- **Ripples**: three drifting octaves of value noise, two flowing down
  the valley, calming with distance.
- **Depth**: the bed seen through the water in the shallows, fading to
  the deep colour, reds first; foam where the water is a hand deep.
- **Shadows**: the shadow map is bound to the water now, so trees, banks
  and the dragon shade it, and the glint.
- **Not reflected**: trees and creatures (a screen-space pass would add
  them), and the river does not yet follow its course: the ripples drift
  down -z, not along the bends.
- A bug found on the way: the heightfield had been filled with the
  water's surface where there is water, which made the river zero deep
  everywhere. It holds the ground now.
- Cost: 0.5 ms a frame at 1080p on the M5, with the river in view.

## A first cloud layer (2026-10-13)

Not yet F6's volumetric clouds: a slab of cumulus from 2.4 to 3.3 km,
marched in the sky shader alone (`shaders/sky.hlsl`, `atmo_clouds`), with
its coverage a function of the ground position (`scene_common.hlsl`,
`cloud_coverage`). That one choice is what keeps it cheap:

- **The sky** marches the slab in sixteen dithered steps. Each step is lit
  by the sun through the column of cloud above it, at that step's own
  coverage, rather than by a second march toward the sun. The sky lights
  the heap from above, darker at the base. A Henyey-Greenstein lobe gives
  the silver edge toward the sun, and the air hazes the result like any
  surface (`atmo_aerial`).
- **The ground's cloud shadows** are one coverage lookup where the sun's ray
  crosses the layer, inside `sun_visibility`. So every lit surface, trees
  and dragon included, darkens under a cloud, and the shadows drift with
  it at 9 m/s.
- **Limits**: seen from below only (above the base the sky shows none);
  clouds never cover terrain, since the sky draws only where nothing else
  did; and the water's reflection is the cloudless sky. F6 is the real
  thing, with history (F5) to afford it.
- **Dials** in the Atmosphere section (cover 0.42, base, thickness,
  density), and `--atmosphere clouds=0,...`. Cost: 0.6 ms a frame at
  1080p on the M5.

## F6, second part (2026-10-14): volumetric clouds

`gfx/clouds.h`, `shaders/clouds_*.hlsl`. Renders in
`artifacts/fidelity-f6-clouds/`. The slab the sky painted is now a volume
marched after the opaque world, so clouds stand in front of mountains, are
seen from above as a floor, and are flown into and out of. Between the
opaque draws and the blended ones the main pass pauses (the clouds read
its depth) and resumes for the breath and the particles, so fire in front
of a cloud stays in front. Four passes, no compute:

- **The weather map**, 512² over 80 km about the camera: the coverage the
  sky and the ground shadows already cut (`cloud_field`, with a softer
  ramp), and a broad "how tall" noise, so the march reads one texel a step
  instead of four octaves.
- **The march**, at half resolution, from the slab's entry to its exit or
  the nearest surface of the four pixels (so a ridge never lets cloud past
  its edge), up to 48 steps with a per-frame dither. Density is
  Schneider's Nubis arrangement: the coverage cut into billows by a tiling
  64³ Perlin-Worley volume (built on the CPU at init in 14 ms, in a 2D
  atlas of slices with wrapped borders, since the RHI has no 3D textures),
  under a domed height profile the weather raises, frayed by a finer
  cellular volume -- wisps at the base, billows above. Each step is lit by
  a six-step march toward the sun starting 15 m out (the face a heap shows
  is its outer twenty metres; starting at 40 m every sunlit face read as
  shadowed), three octaves of fainter, wider scattering for the light that
  bounces inside (Wrenninge), powder on the sunward edges, and the sky's
  light from above, darker toward the flat base. The phase is scaled by
  4π so that a thick cloud returns what a white wall would in the world
  shaders' units; normalised per steradian, the heaps were a fifth as
  bright as the snow under them. The air in front hazes the result at the
  cloud's light-weighted depth (`atmo_aerial`).
- **The resolve** blends it into a history reprojected through where the
  ray enters the slab (a plane, so that depth needs no storage), clamped
  to the new frame's neighbourhood.
- **The apply** blends it over the scene at full resolution, except where
  a surface is nearer than the slab -- the whole valley under the layer --
  so the half-resolution clouds never bleed over anything in front.

The layer is thicker (1.4 km) so heaps can tower. Cost against no clouds
at 1080p on the M5: 1.4 ms from the start, 2.1 ms from above the layer
(the old slab: 0.6). Dials under Atmosphere > clouds ("volumetric clouds",
steps, extinction, shape and detail scales, erosion, softness, sun, sky,
history) and `--clouds key=value,...`; `DRAGON_ABLATE=clouds` falls back
to the sky's slab. The river reflects the layer too, cheaply: the
coverage where the reflected ray crosses its middle, lit and hazed
(`water_reflected_sky`). Not yet: the clouds' shadows in the air (the
froxels carry the terrain's only), a cheaper path for High.

**The forest floor** came with it, from the canopy map the river's
reflection reads (`App::upload_canopy`): past 150 m, the ground under the
crowns takes a litter tint and less sky, and past the shadow cascades
(1.4-2 km), where no crown shades the ground any more, the cover stands
in for the shade (`terrain.hlsl`). Subtle in this valley, whose trees
are sparse; the density is F7's.

## The Ultra forest (2026-10-13)

Ultra's tree spacing is 0.55 of High's (`TREE_SPACING`,
`gfx/graphics_settings.cpp`): 32,000 trees instead of 9,800, and the forest
reads as masses rather than an orchard. That costs 1.4 ms a frame at 1080p
on the M5. F7's GPU-driven culling is what would make it cheaper.

## Budgets

The M5 (10-core GPU) at 1440p, and x99's 5060 Ti at 4K, both at 60 fps at
Ultra, measured on the fidelity goldens and a ten-minute `--demo` soak.
The world resolution setting (R7) is the lever between them. Each phase
records its cost by ablation in its section below as it is built, the way
the retro track records the cards in `porting/CARDS.md`.

## What it touches that already exists

- **One lighting path** (CLAUDE.md): F1's aerial perspective replaces the
  inside of `apply_fog`, F4's sky light the inside of `ambient_light`; no
  shader grows its own. The seam rule is why.
- **Terrain queries from the mesh** (CLAUDE.md): F2 and F3 change what the
  mesh is, not that `height_at` reads it. F3's finest level near the
  camera must be the triangles `height_at` interpolates, or the invisible
  bump returns.
- **Every pose change ends with a render**: F3 moves the ground under the
  feet, so its exit test includes the ground-level foot renders.
- **The retro tiers share the heightfield.** F2's erosion runs on every
  tier, so its generation time is measured on the G41 too, not only on the
  Mac.

## Open questions

- **Erosion's cost on the slow machines.** A seed's valley is generated at
  run start; if the E3300 takes a minute, the cache helps only on the second
  run of a seed. Measure before choosing the resolution.
- **A day-night cycle** is one dial away after F1 and F4, and it is a
  gameplay question (hunting at dusk) as much as a visual one.
- **A native Metal backend** could come before F9 for MetalFX alone, if
  the M5's budget at 1440p is tight after F6.
