#include "gfx/mesh.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/log.h"

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

bool Mesh::upload(rhi::Device& rhi, const MeshData& data, const char* debug_name) {
    if (data.vertices.empty() || data.indices.empty()) {
        LOG_ERROR("Mesh::upload(%s): empty geometry", debug_name);
        return false;
    }

    release(rhi);

    vertex_buffer_ = rhi.create_buffer(rhi::BufferUsage::Vertex, uint32_t(data.vertices.size() * sizeof(MeshVertex)),
                                       data.vertices.data(), debug_name);
    if (!vertex_buffer_) return false;

    index_buffer_ = rhi.create_buffer(rhi::BufferUsage::Index, uint32_t(data.indices.size() * sizeof(uint32_t)),
                                      data.indices.data(), debug_name);
    if (!index_buffer_) {
        rhi.destroy(vertex_buffer_);
        vertex_buffer_ = nullptr;
        return false;
    }

    index_count_ = uint32_t(data.indices.size());
    LOG_INFO("mesh '%s': %zu verts, %u indices (%.1f MB)", debug_name, data.vertices.size(),
             index_count_,
             float(data.vertices.size() * sizeof(MeshVertex) + data.indices.size() * 4) / 1048576.0f);
    return true;
}

void Mesh::release(rhi::Device& rhi) {
    rhi.destroy(vertex_buffer_);
    rhi.destroy(index_buffer_);
    vertex_buffer_ = nullptr;
    index_buffer_ = nullptr;
    index_count_ = 0;
}

void Mesh::bind(rhi::Device& rhi, rhi::Pass* pass) const {
    const rhi::BufferBinding vertices{vertex_buffer_, 0};
    rhi.bind_vertex_buffers(pass, 0, &vertices, 1);
    rhi.bind_index_buffer(pass, rhi::BufferBinding{index_buffer_, 0}, rhi::IndexSize::U32);
}

std::vector<rhi::VertexBufferLayout> Mesh::buffer_descriptions() {
    rhi::VertexBufferLayout vb;
    vb.slot = 0;
    vb.pitch = sizeof(MeshVertex);
    vb.rate = rhi::InputRate::Vertex;
    return {vb};
}

std::vector<rhi::VertexAttribute> Mesh::attributes() {
    std::vector<rhi::VertexAttribute> attributes;
    const uint32_t offsets[4] = {offsetof(MeshVertex, position), offsetof(MeshVertex, normal),
                                 offsetof(MeshVertex, color), offsetof(MeshVertex, uv)};
    for (uint32_t i = 0; i < 4; ++i) {
        rhi::VertexAttribute attribute;
        attribute.location = i;
        attribute.buffer_slot = 0;
        attribute.format = i == 3 ? rhi::VertexFormat::Float2
                                  : rhi::VertexFormat::Float3;
        attribute.offset = offsets[i];
        attributes.push_back(attribute);
    }
    return attributes;
}

std::vector<MeshChunk> chunk_mesh(MeshData& mesh, float chunk_size) {
    std::map<std::pair<int, int>, std::vector<uint32_t>> buckets;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const core::Vec3 a = mesh.vertices[mesh.indices[t]].position;
        const core::Vec3 b = mesh.vertices[mesh.indices[t + 1]].position;
        const core::Vec3 c = mesh.vertices[mesh.indices[t + 2]].position;
        const int cx = int(std::floor((a.x + b.x + c.x) / (3.0f * chunk_size)));
        const int cz = int(std::floor((a.z + b.z + c.z) / (3.0f * chunk_size)));
        std::vector<uint32_t>& bucket = buckets[{cx, cz}];
        bucket.insert(bucket.end(), mesh.indices.begin() + ptrdiff_t(t), mesh.indices.begin() + ptrdiff_t(t + 3));
    }
    std::vector<MeshChunk> chunks;
    std::vector<uint32_t> indices;
    indices.reserve(mesh.indices.size());
    for (const auto& [key, bucket] : buckets) {
        core::Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        for (uint32_t i : bucket) {
            const core::Vec3 p = mesh.vertices[i].position;
            lo = core::Vec3{std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = core::Vec3{std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        MeshChunk chunk;
        chunk.first_index = uint32_t(indices.size());
        chunk.index_count = uint32_t(bucket.size());
        chunk.centre = (lo + hi) * 0.5f;
        chunk.radius = core::length(hi - lo) * 0.5f;
        chunks.push_back(chunk);
        indices.insert(indices.end(), bucket.begin(), bucket.end());
    }
    mesh.indices = std::move(indices);
    return chunks;
}

}  // namespace gfx
