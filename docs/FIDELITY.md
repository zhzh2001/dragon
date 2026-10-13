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
| **F3** terrain detail (procedural rock, a first step, 2026-10-13, below) | A CDLOD quadtree of instanced patches displaced from the height texture, morphing between levels, so the ground near the dragon has 1 m triangles and the view stays 16 km. Material layers in texture arrays (albedo, normal, roughness, height; PolyHaven CC0: grass, forest floor, scree, gravel, rock with strata, snow), splatted by slope, altitude and F2's maps, height-blended, triplanar on cliffs, parallax on rock at Ultra. A macro colour map so far slopes read painted. Rocks and scree placed from F2's sediment, not from slope alone | The cliff golden against the target's right-hand cliff; the foot-plant still on the floor (`--inspect 60 11 3`) | 3 |
| **F4** light (**heightfield shadows done 2026-10-13**, below; cascades and sky light to come) | Cascaded shadows (four cascades to about 3 km) at Ultra and High. **Heightfield shadows**: a compute pass marches the height texture toward the sun into a top-down shadow map for the whole valley, recomputed when the sun moves, for shadows past the last cascade. Sky light from the F1 LUTs into a small cube map, prefiltered, replacing the two-colour hemispheric ambient with the sky that is there, and giving the scales and the water a specular reflection. Horizon occlusion baked from the heightfield | Long mountain shadows across the valley at a low sun; shadow sides lit by the sky's colour | 2-3 |
| **F5** temporal | TAA: a jittered projection, motion vectors (the skinned meshes need last frame's joints, the foliage last frame's wind), history with neighbourhood clamping. It is what makes the noisy techniques after it affordable, and it calms the alpha-tested crowns' shimmer. A temporal upscale on top once it holds | Crowns and fine rock stable in motion; no ghosting behind the dragon in a turn | 2 |
| **F6** clouds and AO (a first cloud layer done 2026-10-13, below) | Volumetric clouds: raymarched at quarter resolution with temporal reprojection, from a weather map, lit by the F1 sun and sky, shadowing the ground through a cloud shadow map; flown through, not painted on -- the altitude cue `DIRECTION.md` row 9 asks for. GTAO at half resolution, and screen-space contact shadows for the creatures | The dragon flies into a cloud and out; it sits on the ground when landed | 3 |
| **F7** forest (Ultra's density tripled 2026-10-13, below) | GPU-driven vegetation: compute culling into indirect draws, five to twenty times the trees so forests are masses, octahedral impostors past the card LOD at Ultra, per-tree colour variety from the palette, grass to a longer distance | The forest golden against the target's middle ground | 2 |
| **F8** water | Flow-mapped normals along the river from F2's flow, depth colour and absorption, shoreline foam, screen-space reflections with the F4 sky cube as their fallback | The river golden: gravel bars, braids, reflections of the banks | 1-2 |
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
- **What it does not do**: no shadowing inside the air (no light shafts
  behind a ridge), and no clouds (F6). The surfaces' sun colour is still the
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
- **Cost**: 0.9 s on the M5 (12 m), 0.2 s at 24 m, and paid twice on a
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
  ridges' shadow with patches of sun. **It is darker to play in.** The
  dragon flies in shade more of the time, lit by the sky alone. That is a
  playtest call: the sun's elevation is the dial.
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

## F3, first step (2026-10-13): procedural rock

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
- **Beds**: 11 m ledges, a lit top and a shaded riser, broken by
  vertical joints, each bed's colour between grey and warm. Only on cliffs
  steeper than about 52 degrees: on the 35-45 degree faces most of these
  mountains are, beds read as contour lines.
- **Broken rock**: grey and warm blotches over the rest of the rocky
  ground.

It is an improvement, not the target: rock that reads as rock wants
scanned textures with normal maps (PolyHaven's CC0 rock and scree sets,
`DIRECTION.md` section 5's policy), in the terrain's slots after the
detail tile, blended by these same slope and erosion rules. That is F3's
next step, with the CDLOD mesh after it. Cost so far: 0.4 ms a frame at
1080p on the M5.

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
