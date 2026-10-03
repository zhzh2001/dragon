#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "rhi/rhi.h"
#include "anim/skeleton.h"
#include "core/math.h"

namespace anim {

// Vertex for a skinned mesh. Four influences per vertex is the standard
// compromise: enough for a shoulder or a neck joint, and it packs into one
// 16-byte pair of attributes.
struct SkinnedVertex {
    core::Vec3 position;
    core::Vec3 normal;
    core::Vec3 color;
    core::Vec2 uv;
    // xyz tangent, w handedness (+/-1) for reconstructing the bitangent. Needed
    // only for tangent-space normal mapping; a mesh without one shades flat.
    core::Vec4 tangent = core::Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    // Joint indices as bytes: MAX_JOINTS is 256, so a byte is exactly enough.
    uint8_t joints[4] = {0, 0, 0, 0};
    // Should sum to 1. Normalized on insert, because weights that do not sum to
    // one either shrink the mesh or blow it up.
    float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
};

// A run of indices sharing one material, so each can bind its own texture.
struct SkinnedSubmesh {
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
    // Indices into the model's texture list; -1 means the map is absent and the
    // shader falls back to the geometric normal / a default roughness.
    int base_color_texture = -1;
    int normal_texture = -1;
    // glTF packs occlusion in R, roughness in G, metallic in B of one image.
    int orm_texture = -1;
    // Empty: the vertices index the skeleton directly. Otherwise the global
    // joints this batch uses, in the order its vertices index them
    // (anim/skin_partition.h) -- the draw pushes only these matrices.
    std::vector<uint16_t> palette;
};

struct SkinnedMeshData {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SkinnedSubmesh> submeshes;

    void recompute_normals();
    // Adds a vertex, normalizing its weights and dropping any that are zero.
    uint32_t add(core::Vec3 position, core::Vec3 color, core::Vec2 uv,
                 const int (&joint_indices)[4], const float (&joint_weights)[4]);
};

class SkinnedMesh {
public:
    bool upload(rhi::Device& rhi, const SkinnedMeshData& data, const char* debug_name);
    void release(rhi::Device& rhi);
    void bind(rhi::Device& rhi, rhi::Pass* pass) const;

    uint32_t index_count() const { return index_count_; }
    const std::vector<SkinnedSubmesh>& submeshes() const { return submeshes_; }
    bool valid() const { return vertex_buffer_ && index_buffer_; }

    // A distance LOD (docs/PORTING.md, R4): a retro tier gives a creature a
    // cheaper mesh for past `distance` metres, which the renderer picks by
    // itself (gfx::WorldRenderer::draw_skinned). A rival or a grazer far off
    // is a few pixels tall, and on an SM2 card ten thousand triangles each.
    bool upload_far(rhi::Device& rhi, const SkinnedMeshData& data, float distance, const char* debug_name);
    const SkinnedMesh& for_distance(float metres) const {
        return far_ && far_->valid() && metres > far_distance_ ? *far_ : *this;
    }

    static std::vector<rhi::VertexBufferLayout> buffer_descriptions();
    static std::vector<rhi::VertexAttribute> attributes();

private:
    rhi::Buffer* vertex_buffer_ = nullptr;
    rhi::Buffer* index_buffer_ = nullptr;
    uint32_t index_count_ = 0;
    std::vector<SkinnedSubmesh> submeshes_;
    std::shared_ptr<SkinnedMesh> far_;  // shared: models are copied by value
    float far_distance_ = 0.0f;
};

}  // namespace anim
