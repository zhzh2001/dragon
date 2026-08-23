#include "gfx/mesh.h"

#include "core/log.h"
#include "gfx/buffer.h"

using core::Vec3;

namespace gfx {

void MeshData::recompute_normals() {
    for (MeshVertex& v : vertices) v.normal = Vec3::zero();

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        Vec3 p0 = vertices[i0].position;
        Vec3 p1 = vertices[i1].position;
        Vec3 p2 = vertices[i2].position;
        // Unnormalized cross product has length proportional to twice the
        // triangle area, which gives area weighting for free.
        Vec3 face = core::cross(p1 - p0, p2 - p0);
        vertices[i0].normal += face;
        vertices[i1].normal += face;
        vertices[i2].normal += face;
    }

    for (MeshVertex& v : vertices) v.normal = core::normalize_or(v.normal, Vec3::up());
}

bool Mesh::upload(SDL_GPUDevice* gpu, const MeshData& data, const char* debug_name) {
    if (data.vertices.empty() || data.indices.empty()) {
        LOG_ERROR("Mesh::upload(%s): empty geometry", debug_name);
        return false;
    }

    release(gpu);

    vertex_buffer_ = create_buffer_with_data(
        gpu, data.vertices.data(), uint32_t(data.vertices.size() * sizeof(MeshVertex)),
        SDL_GPU_BUFFERUSAGE_VERTEX, debug_name);
    if (!vertex_buffer_) return false;

    index_buffer_ = create_buffer_with_data(gpu, data.indices.data(),
                                            uint32_t(data.indices.size() * sizeof(uint32_t)),
                                            SDL_GPU_BUFFERUSAGE_INDEX, debug_name);
    if (!index_buffer_) {
        SDL_ReleaseGPUBuffer(gpu, vertex_buffer_);
        vertex_buffer_ = nullptr;
        return false;
    }

    index_count_ = uint32_t(data.indices.size());
    LOG_INFO("mesh '%s': %zu verts, %u indices (%.1f MB)", debug_name, data.vertices.size(),
             index_count_,
             float(data.vertices.size() * sizeof(MeshVertex) + data.indices.size() * 4) / 1048576.0f);
    return true;
}

void Mesh::release(SDL_GPUDevice* gpu) {
    if (vertex_buffer_) SDL_ReleaseGPUBuffer(gpu, vertex_buffer_);
    if (index_buffer_) SDL_ReleaseGPUBuffer(gpu, index_buffer_);
    vertex_buffer_ = nullptr;
    index_buffer_ = nullptr;
    index_count_ = 0;
}

void Mesh::bind(SDL_GPURenderPass* pass) const {
    SDL_GPUBufferBinding vertex_binding = {};
    vertex_binding.buffer = vertex_buffer_;
    SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);

    SDL_GPUBufferBinding index_binding = {};
    index_binding.buffer = index_buffer_;
    SDL_BindGPUIndexBuffer(pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

std::vector<SDL_GPUVertexBufferDescription> Mesh::buffer_descriptions() {
    SDL_GPUVertexBufferDescription vb = {};
    vb.slot = 0;
    vb.pitch = sizeof(MeshVertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    return {vb};
}

std::vector<SDL_GPUVertexAttribute> Mesh::attributes() {
    std::vector<SDL_GPUVertexAttribute> attributes;
    const uint32_t offsets[3] = {offsetof(MeshVertex, position), offsetof(MeshVertex, normal),
                                 offsetof(MeshVertex, color)};
    for (uint32_t i = 0; i < 3; ++i) {
        SDL_GPUVertexAttribute attribute = {};
        attribute.location = i;
        attribute.buffer_slot = 0;
        attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        attribute.offset = offsets[i];
        attributes.push_back(attribute);
    }
    return attributes;
}

}  // namespace gfx
