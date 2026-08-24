#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

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
    // Joint indices as bytes: MAX_JOINTS is 64, so a byte is ample.
    uint8_t joints[4] = {0, 0, 0, 0};
    // Should sum to 1. Normalized on insert, because weights that do not sum to
    // one either shrink the mesh or blow it up.
    float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
};

struct SkinnedMeshData {
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;

    void recompute_normals();
    // Adds a vertex, normalizing its weights and dropping any that are zero.
    uint32_t add(core::Vec3 position, core::Vec3 color, const int (&joint_indices)[4],
                 const float (&joint_weights)[4]);
};

class SkinnedMesh {
public:
    bool upload(SDL_GPUDevice* gpu, const SkinnedMeshData& data, const char* debug_name);
    void release(SDL_GPUDevice* gpu);
    void bind(SDL_GPURenderPass* pass) const;

    uint32_t index_count() const { return index_count_; }
    bool valid() const { return vertex_buffer_ && index_buffer_; }

    static std::vector<SDL_GPUVertexBufferDescription> buffer_descriptions();
    static std::vector<SDL_GPUVertexAttribute> attributes();

private:
    SDL_GPUBuffer* vertex_buffer_ = nullptr;
    SDL_GPUBuffer* index_buffer_ = nullptr;
    uint32_t index_count_ = 0;
};

}  // namespace anim
