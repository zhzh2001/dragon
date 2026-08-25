#pragma once

#include <string>
#include <vector>

#include "anim/animation.h"
#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"
#include "gfx/texture.h"

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
    // Average movement of a vertex when skinned at the bind pose, as a fraction
    // of the model's size. Should be ~0: at rest, skinning is the identity.
    float bind_pose_error = 0.0f;

    // Images referenced by the mesh's submeshes, decoded but not yet uploaded --
    // the loader has no GPU device and should not need one.
    std::vector<gfx::ImageData> textures;
    // Parallel to `textures`: whether each is colour (sRGB) or data (linear).
    // The loader knows this from the glTF slot the image was found in; nothing
    // downstream can work it out from the pixels.
    std::vector<uint8_t> texture_srgb;

    // Authored motion, if the file carries any. Used as a base pose that the
    // procedural rig then overrides for whatever flight drives, so secondary
    // detail the artist animated -- toes, jaw, small body motion -- survives.
    std::vector<AnimationClip> animations;
};

GltfLoadResult load_skinned_gltf(const char* path, Skeleton& out_skeleton,
                                 SkinnedMeshData& out_mesh);

}  // namespace anim
