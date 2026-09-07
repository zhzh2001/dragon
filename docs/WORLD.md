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
plants.

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
