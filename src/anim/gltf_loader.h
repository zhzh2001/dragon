#pragma once

#include <string>
#include <vector>

#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"

namespace anim {

// Loads the first skinned mesh and its skeleton from a .gltf or .glb file.
//
// Deliberately narrow: this engine needs one skinned character, not a scene
// graph. Every skinned primitive in the file is merged into a single mesh, since
// they share one skin and one draw call is cheaper than several.
//
// Bind-pose inverse matrices are recomputed from the node hierarchy rather than
// read from the file. Both are meant to agree, and computing them keeps the
// loaded skeleton consistent with the one invariant the whole system rests on:
// that the bind pose yields identity skinning matrices.
struct GltfLoadResult {
    bool ok = false;
    std::string error;
    int joint_count = 0;
    size_t vertex_count = 0;
    size_t triangle_count = 0;
    // Bounds of the loaded mesh in its own space, for working out scale and
    // orientation without guessing.
    core::Vec3 bounds_min = core::Vec3::zero();
    core::Vec3 bounds_max = core::Vec3::zero();
    // Fraction of the model's widest extent spanned by the middle half of its
    // vertices. Near 1 is a solid shape; near 0 means the rest pose is a tangle
    // that only its animation resolves.
    float rest_pose_bulk_fraction = 0.0f;
};

GltfLoadResult load_skinned_gltf(const char* path, Skeleton& out_skeleton,
                                 SkinnedMeshData& out_mesh);

}  // namespace anim
