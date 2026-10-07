#pragma once

// Creature LODs: fewer triangles for the retro tiers (docs/PORTING.md, R2).
//
// The roster's creatures are 60 to 90 thousand triangles, which a 2005 card
// draws a few of a frame, not eight. The tier caps a skinned mesh's triangle
// count (gfx::RenderTier::max_skinned_triangles), and the mesh is simplified
// to it at load with meshoptimizer: edges collapse onto existing vertices, so
// every vertex that survives is an original one, its joints and weights
// untouched. Nothing is re-rigged; a LOD skins exactly as the full mesh does
// at the vertices it keeps. Normals and UVs steer the collapse (attribute
// weights); see skin_lod.cpp for why it may cross a texture seam.

#include <cstdint>

#include "anim/skinned_mesh.h"

namespace gfx {
struct ImageData;
}

namespace anim {

struct LodResult {
    SkinnedMeshData mesh;
    // The simplifier's error, relative to the mesh's extent (0.01 is 1% of
    // its size); 0 when the mesh already fit.
    float error = 0.0f;
};

// `mesh` with at most about `max_triangles` triangles in all (each submesh
// cut by the same ratio), unused vertices dropped and the indices ordered for
// the post-transform cache. Returned unchanged when it already fits or when
// `max_triangles` is 0.
//
// At VERTEX_COLOUR_TRIANGLES or fewer, with the model's decoded `textures`,
// the LOD is drawn untextured instead: each vertex carries the mean of the
// base colour over the part of the full surface nearest it, and the
// submeshes' maps are dropped. Reaching a few thousand triangles means
// collapsing across the UV seams of an atlas cut into hundreds of islands,
// and the surviving vertices then map their triangles across islands -- the
// texture came out as a scramble of patches on every triangle, which was the
// "broken" 600- and 1,200-triangle dragon. A sharper texture only showed the
// scramble better.
constexpr uint32_t VERTEX_COLOUR_TRIANGLES = 5000;
LodResult simplify_skinned(const SkinnedMeshData& mesh, uint32_t max_triangles,
                           const std::vector<gfx::ImageData>* textures = nullptr);

}  // namespace anim
