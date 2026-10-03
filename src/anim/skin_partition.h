#pragma once

// Palette splitting: fitting a skeleton's joints into a vertex shader's
// constant budget (docs/PORTING.md, R2).
//
// The modern build pushes all 256 joint matrices per draw as one uniform
// block. A Direct3D 9 vertex shader has 256 float4 constants in total, and a
// joint costs three of them (a 4x3 matrix), so with the scene and model blocks
// beside it a draw can address about 60 joints. Each submesh is therefore cut
// into batches whose triangles touch at most `max_bones` joints between them;
// each batch carries its palette (the global joint indices it uses, in order),
// and its vertices index that palette instead of the skeleton.
//
// The split is exact: a vertex in a batch blends the same matrices with the
// same weights in the same order as before, so the skinned result is
// bit-for-bit what the unsplit mesh gives. tests/test_skin_partition.cpp
// checks exactly that, and so did Frostvein's real mesh (66,514 vertices, 74
// joints, two batches). On the GPU, a single remapped palette renders
// identically to the unsplit mesh (0 pixels differ on Metal). Two batches
// differ in about 300 of 921,600 pixels (at most 18/255). That is the draw
// boundary, not the palette: the same unsplit mesh drawn in two halves
// differs in the same way (221 pixels, at most 17), where triangles at equal
// depth resolve differently across draws on Apple's GPU.

#include <cstdint>

#include "anim/skinned_mesh.h"

namespace anim {

// Joints a single triangle can need: three vertices of four influences.
constexpr uint32_t MIN_PALETTE_BONES = 12;

// Returns `mesh` with every submesh cut into batches of at most `max_bones`
// joints (clamped to at least MIN_PALETTE_BONES). Batches keep their
// submesh's material; vertices shared between batches are duplicated, one copy
// per batch. A mesh whose joint indices are all below `max_bones` is returned
// unchanged, with no palettes, which the renderer reads as "index the skeleton
// directly"; any other mesh gets a palette, even when one batch holds it.
SkinnedMeshData partition_palettes(const SkinnedMeshData& mesh, uint32_t max_bones);

}  // namespace anim
