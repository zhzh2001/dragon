# The world: terrain, water and vegetation

How the valley is generated, queried and planted.

## Vegetation

`game::Vegetation` places, `gfx::Foliage` draws. Four tree kinds (spruce,
mountain pine, broadleaf, dead snag) and three grass kinds (tuft, waterside
reed, bush), each a generated mesh of rings, cones, lumpy blobs and blade
triangles, each instanced from its own buffer with one `FoliageInstance` per
plant -- position, scale, yaw, shade, sway phase. Trees are planted once per
terrain on a jittered grid, thinned by TWO scales of cover noise (stands at
400 m, clumps at 55 m -- one scale is an even sprinkle) and kept off water,
steep ground and everything above a treeline (`treeline_above_floor`: a
forested peak is a hill). **The kind mix shifts with altitude but is a mix
everywhere** -- half broadleaf on the floor, spruce through the middle, pines
at the top, dead snags in the last stretch and on rough ground -- because a
band of one kind reads as a plantation, which is exactly what the first
all-broadleaf floor looked like. Sizes spread 0.7-1.7x for the same reason.
**Plants sink into slopes** (`slope_sink`, metres per unit of 1 - normal.y)
so a skirt's downhill side meets the ground instead of hanging in the air.
Trees cast shadows through their own depth pipeline and sway in the vertex
shader with the square of their height fraction so roots stay put. **Grass is
re-placed every frame** around the active camera from a stateless hash of the
ground cell, so a tuft is always in the same place when you come back to it,
patchy by a fine noise, and streamed like the particles; no texture, no alpha,
shrinking into the ground over the last third of the radius rather than
blinking out. Both pipelines read the mesh vertex layout plus a second,
instance-rate buffer -- and `instance_step_rate` must be 0: SDL reserves it,
and a 1 fails pipeline creation with an empty message. Plants are visual only;
nothing collides with a tree.

**There is a river now, and it is the only water.** The valley floor sits
above the water line everywhere, so for a long time "water level" coloured a
sand band and nothing else. `analytic_height_at` carves a channel along
`river_center_x` (the corridor's centre plus its own slower wander, so the two
curves differ) down to a flat bed below the water line; `surface_at` is the
ground or the water, whichever is higher, and everything that lands, hovers
or measures clearance uses it. The water is one quad at the water line over
the whole world (`water.msl`: fresnel between a deep colour and the reflected
sky, a small ripple, a sun glint), hidden by the terrain wherever the ground
is above the line -- which is everywhere but the river. Reeds crowd its banks.

**The world does not end at the playable extent.** The analytic height used
to carry on past the last visible triangle: an invisible mountain range you
could fly into and land on, out toward the sky. The terrain now builds a
coarse **skirt** (36 m cells to three times the half extent) from the same
function, drawn with the terrain shader and queried by the same triangle
lookup, so there is ground under the sky all the way to the fog. It gets no
plants. The terrain shader's sky fade over the outermost band now keys off
the skirt's extent, not the playable one: left where it was, it started 500 m
inside the map, and the default course's start sat in it.

The surface query also exposed a frame-time weakness: under the rally test's
4x frame jitter the bank-limited autopilot orbited Canyon Weave's rings that
it flies cleanly at 60 Hz. The real fix was in the flight model: **frames
longer than `max_step` (20 ms) are split into equal substeps** inside
`FlightModel::update`, since the app lets a hitch reach 100 ms and the
explicit integration of a banked, roll-damped turn drifts at that step.
Every dragon benefits, bots included. The autopilot also gained a go-around
(fly back out to an entry point once past a ring's plane), which is a
smaller matter -- an eager version of it, triggering on oblique approaches,
made things worse before the substeps were found.

## Vegetation v2: cards (2026-09-20)

The v1 plants were solid blobs and cones: one facet of flat green per
triangle, lit by their own copy of the lighting, which is why the forest read
as pastel polygon blobs against a scanned dragon. v2 keeps the generator --
it is the reason every tree matches every other -- and changes what it
generates.

- **Crowns are cards.** A card is a textured quad (`card()` in
  `gfx/foliage.cpp`) whose alpha cuts the leaves out of it; a crown is a
  scatter of them over an ellipsoid shell (`card_crown()`), or for the spruce
  three tall cards crossed through the axis with drooping sprays in layers.
  Alpha-TESTED, not blended: sorted-blend foliage is a rabbit hole, and
  alpha-tested cards are what 2005 shipped, so this is a retro asset too.
- **Two textures, grey.** `assets/textures/leaf_cluster.png` (a cluster of
  pointed ellipse leaves) and `needle_spray.png` (seventy thin needles
  radiating), rendered in Blender through the MCP bridge as grey detail maps
  on a transparent film, 256 px. Grey on purpose: the palette colours them in
  the shader (`albedo *= card.r * 2.2`), so the tree greens still come from
  `gfx/palette.h` and the Palette panel, and the per-tree warm push still
  works. They are the game's own, no licence; the Blender script that made
  them is in the commit that added them.
- **The mesh carries a material tag** in the vertex colour's third channel
  (`FoliageMaterial`): plain, leaf card, needle card, bark; plus 10 for a
  DETAIL card. `MeshVertex` grew a UV for it, which every mesh now carries
  and only the foliage shaders read. Bark is streaked in the shader from the
  same value noise the terrain patches with (moved to `scene_common.msl`),
  in u around the trunk and stretched along v, so a trunk is not a flat
  brown pole.
- **Dark undersides and translucency.** A card facing the ground is darkened
  on its geometric normal before the facing flip; sun through a crown from
  behind is `translucent_sun`, bark gets none.
- **Shadows are the cut-out.** The depth pass samples the same textures and
  discards the same texels, so a crown's shadow is leaves, not a quad.
- **The impostor is the same mesh.** Past `lod_distance` (240 m, Vegetation
  panel) the fragment shader discards every detail card, dithered per
  instance over a 30% band with the instance's sway phase as the hash, and
  the crown's few coarse cards -- placed and sized to carry the silhouette
  -- are what a distant tree is. No second mesh, no second draw, no instance
  sorting; the cost is vertex work on cards that discard.

What v1 got right stays: instancing, the sway, the palette, the placement
by slope and altitude. Renders before and after in `artifacts/vegetation-v2/`.

## Trees on the skirt, and cells (2026-09-20)

Placement stopped at the playable edge, and the spawn sits at 90% of the
extent, so the ground behind the start was bare to the horizon. Trees now
continue onto the skirt out to `skirt_trees` times the half extent (1.6),
thinning over a 150 m band to `skirt_cover` (0.6) of the cover; the skirt
grid's `height_at` places them, and the grass follows the same limit. That
took the valley from 6.7 k to 9.9 k trees.

The renderer stopped drawing them all. `Foliage::set_trees` uploads each
kind sorted into 320 m ground cells, each with a bounding sphere over its
bases plus the tallest crown; `draw_trees` extracts the six clip planes from
the view-projection (Gribb/Hartmann, valid for the reversed-Z main
projection and the shadow ortho alike) and issues one instanced draw per
cell the frustum and `tree_draw_distance` (4200 m) admit, and the depth pass
does the same against the light's box. The Vegetation panel prints trees
placed and trees drawn. Six hundred headless frames took the same wall time
before and after with half again as many trees. `artifacts/vegetation-v2/
skirt_trees_behind_start.png` is the view back over the spawn.

