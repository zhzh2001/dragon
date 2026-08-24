#include "anim/skinned_mesh.h"

#include <cstddef>

#include "core/log.h"
#include "gfx/buffer.h"

using core::Vec3;

namespace anim {

uint32_t SkinnedMeshData::add(Vec3 position, Vec3 color, const int (&joint_indices)[4],
                              const float (&joint_weights)[4]) {
    SkinnedVertex vertex;
    vertex.position = position;
    vertex.normal = Vec3::up();  // replaced by recompute_normals
    vertex.color = color;

    float total = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (joint_indices[i] >= 0 && joint_weights[i] > 0.0f) total += joint_weights[i];
    }

    if (total <= 0.0f) {
        // No influence at all would collapse the vertex to the origin. Bind it
        // rigidly to the root instead, which at least keeps it attached.
        vertex.joints[0] = 0;
        vertex.weights[0] = 1.0f;
    } else {
        int slot = 0;
        for (int i = 0; i < 4 && slot < 4; ++i) {
            if (joint_indices[i] < 0 || joint_weights[i] <= 0.0f) continue;
            vertex.joints[slot] = uint8_t(joint_indices[i]);
            vertex.weights[slot] = joint_weights[i] / total;
            ++slot;
        }
        for (; slot < 4; ++slot) {
            vertex.joints[slot] = 0;
            vertex.weights[slot] = 0.0f;
        }
    }

    vertices.push_back(vertex);
    return uint32_t(vertices.size() - 1);
}

void SkinnedMeshData::recompute_normals() {
    for (SkinnedVertex& v : vertices) v.normal = Vec3::zero();
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        const Vec3 face = core::cross(vertices[i1].position - vertices[i0].position,
                                      vertices[i2].position - vertices[i0].position);
        vertices[i0].normal += face;
        vertices[i1].normal += face;
        vertices[i2].normal += face;
    }
    for (SkinnedVertex& v : vertices) v.normal = core::normalize_or(v.normal, Vec3::up());
}

bool SkinnedMesh::upload(SDL_GPUDevice* gpu, const SkinnedMeshData& data, const char* debug_name) {
    if (data.vertices.empty() || data.indices.empty()) {
        LOG_ERROR("SkinnedMesh::upload(%s): empty geometry", debug_name);
        return false;
    }
    release(gpu);

    vertex_buffer_ = gfx::create_buffer_with_data(
        gpu, data.vertices.data(), uint32_t(data.vertices.size() * sizeof(SkinnedVertex)),
        SDL_GPU_BUFFERUSAGE_VERTEX, debug_name);
    if (!vertex_buffer_) return false;

    index_buffer_ = gfx::create_buffer_with_data(gpu, data.indices.data(),
                                                 uint32_t(data.indices.size() * sizeof(uint32_t)),
                                                 SDL_GPU_BUFFERUSAGE_INDEX, debug_name);
    if (!index_buffer_) {
        SDL_ReleaseGPUBuffer(gpu, vertex_buffer_);
        vertex_buffer_ = nullptr;
        return false;
    }

    index_count_ = uint32_t(data.indices.size());
    LOG_INFO("skinned mesh '%s': %zu verts, %u indices", debug_name, data.vertices.size(),
             index_count_);
    return true;
}

void SkinnedMesh::release(SDL_GPUDevice* gpu) {
    if (vertex_buffer_) SDL_ReleaseGPUBuffer(gpu, vertex_buffer_);
    if (index_buffer_) SDL_ReleaseGPUBuffer(gpu, index_buffer_);
    vertex_buffer_ = nullptr;
    index_buffer_ = nullptr;
    index_count_ = 0;
}

void SkinnedMesh::bind(SDL_GPURenderPass* pass) const {
    SDL_GPUBufferBinding vertex_binding = {};
    vertex_binding.buffer = vertex_buffer_;
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
    SDL_GPUBufferBinding index_binding = {};
    index_binding.buffer = index_buffer_;
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

std::vector<SDL_GPUVertexBufferDescription> SkinnedMesh::buffer_descriptions() {
    SDL_GPUVertexBufferDescription vb = {};
    vb.slot = 0;
    vb.pitch = sizeof(SkinnedVertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    return {vb};
}

std::vector<SDL_GPUVertexAttribute> SkinnedMesh::attributes() {
    std::vector<SDL_GPUVertexAttribute> attributes;
    auto push = [&attributes](uint32_t location, SDL_GPUVertexElementFormat format,
                              uint32_t offset) {
        SDL_GPUVertexAttribute attribute = {};
        attribute.location = location;
        attribute.buffer_slot = 0;
        attribute.format = format;
        attribute.offset = offset;
        attributes.push_back(attribute);
    };
    push(0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(SkinnedVertex, position));
    push(1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(SkinnedVertex, normal));
    push(2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(SkinnedVertex, color));
    push(3, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, offsetof(SkinnedVertex, joints));
    push(4, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(SkinnedVertex, weights));
    return attributes;
}

}  // namespace anim
