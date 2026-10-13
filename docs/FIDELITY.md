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
| **F0** groundwork | Compute in the RHI: compute pipelines, storage buffers and textures, dispatch, indirect draw, on SDL GPU and D3D11 (CS 5.0); none on D3D9. A fidelity golden set at Ultra: the vista, a cliff close, the forest floor, the river, the dragon at a low sun | The current goldens unchanged; one compute pass (the sky LUTs) on every modern backend | 1-2 |
| **F1** sky and air | Hillaire's sky (EGSR 2020): transmittance, multiple-scattering and sky-view LUTs, and an aerial-perspective volume the world shaders sample in `apply_fog`, so haze is the atmosphere's in-scattering and blue-shifts with distance. A sun disc with limb darkening; a sun-elevation dial (late afternoon by default). The valley's height fog stays as an artistic layer on top. The retro tiers get the new sky for free: the ff dome bakes whatever the sky function returns | The vista against the target: blue depth, a warm low sun, a horizon that reads as distance | 2 |
| **F2** terrain shape | Hydraulic and thermal erosion in the generator, on the CPU, deterministic (the "same content on every compiler" rules), cached per seed under `paths::user`. It writes the heightfield and three maps the materials need: flow (gullies, the braided river bed), sediment (scree fans, gravel bars) and wetness. The heightfield stays the one source of truth: `height_at` keeps querying the rendered triangle | Mountains read as rock with gullies from the vista; the E3300 generates a valley in an acceptable time, or the cache covers it | 2-3 |
| **F3** terrain detail | A CDLOD quadtree of instanced patches displaced from the height texture, morphing between levels, so the ground near the dragon has 1 m triangles and the view stays 16 km. Material layers in texture arrays (albedo, normal, roughness, height; PolyHaven CC0: grass, forest floor, scree, gravel, rock with strata, snow), splatted by slope, altitude and F2's maps, height-blended, triplanar on cliffs, parallax on rock at Ultra. A macro colour map so far slopes read painted. Rocks and scree placed from F2's sediment, not from slope alone | The cliff golden against the target's right-hand cliff; the foot-plant still on the floor (`--inspect 60 11 3`) | 3 |
| **F4** light | Cascaded shadows (four cascades to about 3 km) at Ultra and High. **Heightfield shadows**: a compute pass marches the height texture toward the sun into a top-down shadow map for the whole valley, recomputed when the sun moves, for shadows past the last cascade. Sky light from the F1 LUTs into a small cube map, prefiltered, replacing the two-colour hemispheric ambient with the sky that is there, and giving the scales and the water a specular reflection. Horizon occlusion baked from the heightfield | Long mountain shadows across the valley at a low sun; shadow sides lit by the sky's colour | 2-3 |
| **F5** temporal | TAA: a jittered projection, motion vectors (the skinned meshes need last frame's joints, the foliage last frame's wind), history with neighbourhood clamping. It is what makes the noisy techniques after it affordable, and it calms the alpha-tested crowns' shimmer. A temporal upscale on top once it holds | Crowns and fine rock stable in motion; no ghosting behind the dragon in a turn | 2 |
| **F6** clouds and AO | Volumetric clouds: raymarched at quarter resolution with temporal reprojection, from a weather map, lit by the F1 sun and sky, shadowing the ground through a cloud shadow map; flown through, not painted on -- the altitude cue `DIRECTION.md` row 9 asks for. GTAO at half resolution, and screen-space contact shadows for the creatures | The dragon flies into a cloud and out; it sits on the ground when landed | 3 |
| **F7** forest | GPU-driven vegetation: compute culling into indirect draws, five to twenty times the trees so forests are masses, octahedral impostors past the card LOD at Ultra, per-tree colour variety from the palette, grass to a longer distance | The forest golden against the target's middle ground | 2 |
| **F8** water | Flow-mapped normals along the river from F2's flow, depth colour and absorption, shoreline foam, screen-space reflections with the F4 sky cube as their fallback | The river golden: gravel bars, braids, reflections of the banks | 1-2 |
| **F9** hardware ray tracing | A native Metal backend (`src/rhi/metal/`, the size of the D3D11 one), then ray-traced soft shadows, occlusion and water reflections against the creatures and trees, on top of F4's heightfield work. A native backend also brings MetalFX upscaling to the Mac. Vulkan ray queries on x99's 5060 Ti would be a second native backend | The wings' shadow soft on the ground; dragons in the river | 3-5 |

About twenty sessions in all. **The order is by what each buys**: F1
changes every frame for two sessions of work and closes the most of the gap
per session; F2 and F3 together are the clay-mountain fix, the single
largest difference from the target; F4 is what a low sun is for; F5 has to
come before F6 because clouds and GTAO are only affordable with history;
F9 is last because F1-F8 already cover the terrain, which is most of the
frame, and its marginal gain is on the creatures alone.

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
