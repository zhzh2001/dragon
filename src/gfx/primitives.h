#pragma once

#include "core/math.h"
#include "gfx/mesh.h"

namespace gfx {

// Procedural meshes for things that do not warrant an asset: checkpoint rings,
// greybox props, debug volumes.

// Torus lying in the XY plane, so its axis runs along Z. That matches the engine
// convention where an orientation's forward is -Z: a ring's orientation then
// describes the direction you fly through it.
MeshData make_torus(float radius, float tube_radius, core::Vec3 color, int ring_segments = 32,
                    int tube_segments = 10);

// Flat annulus in the XY plane. Cheaper than a torus and reads better at long
// distance, where a thin tube disappears entirely.
MeshData make_annulus(float inner_radius, float outer_radius, core::Vec3 color,
                      int segments = 48);

// UV sphere centred on the origin. Used for anything roughly round that does not
// deserve an asset: projectiles, target drones, blast markers.
MeshData make_sphere(float radius, core::Vec3 color, int segments = 16, int rings = 10);

}  // namespace gfx
