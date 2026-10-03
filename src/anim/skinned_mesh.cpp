#include "anim/skinned_mesh.h"

#include <cstddef>

#include "core/log.h"

using core::Vec3;

namespace anim {

uint32_t SkinnedMeshData::add(Vec3 position, Vec3 color, core::Vec2 uv,
                              const int (&joint_indices)[4], const float (&joint_weights)[4]) {
    SkinnedVertex vertex;
    vertex.position = position;
    vertex.normal = Vec3::up();  // replaced by recompute_normals
    vertex.color = color;
    vertex.uv = uv;

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

bool SkinnedMesh::upload(rhi::Device& rhi, const SkinnedMeshData& data, const char* debug_name) {
    if (data.vertices.empty() || data.indices.empty()) {
        LOG_ERROR("SkinnedMesh::upload(%s): empty geometry", debug_name);
        return false;
    }
    release(rhi);

    vertex_buffer_ = rhi.create_buffer(rhi::BufferUsage::Vertex, uint32_t(data.vertices.size() * sizeof(SkinnedVertex)),
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
    submeshes_ = data.submeshes;
    if (submeshes_.empty()) submeshes_.push_back({0, index_count_, -1});
    LOG_INFO("skinned mesh '%s': %zu verts, %u indices, %zu submesh(es)", debug_name,
             data.vertices.size(), index_count_, submeshes_.size());
    return true;
}

bool SkinnedMesh::upload_far(rhi::Device& rhi, const SkinnedMeshData& data, float distance, const char* debug_name) {
    auto far = std::make_shared<SkinnedMesh>();
    if (!far->upload(rhi, data, debug_name)) return false;
    if (far_) far_->release(rhi);
    far_ = far;
    far_distance_ = distance;
    return true;
}

void SkinnedMesh::release(rhi::Device& rhi) {
    if (far_) {
        far_->release(rhi);
        far_.reset();
    }
    rhi.destroy(vertex_buffer_);
    rhi.destroy(index_buffer_);
    vertex_buffer_ = nullptr;
    index_buffer_ = nullptr;
    index_count_ = 0;
}

void SkinnedMesh::bind(rhi::Device& rhi, rhi::Pass* pass) const {
    const rhi::BufferBinding vertices{vertex_buffer_, 0};
    rhi.bind_vertex_buffers(pass, 0, &vertices, 1);
    rhi.bind_index_buffer(pass, rhi::BufferBinding{index_buffer_, 0}, rhi::IndexSize::U32);
}

std::vector<rhi::VertexBufferLayout> SkinnedMesh::buffer_descriptions() {
    rhi::VertexBufferLayout vb;
    vb.slot = 0;
    vb.pitch = sizeof(SkinnedVertex);
    vb.rate = rhi::InputRate::Vertex;
    return {vb};
}

std::vector<rhi::VertexAttribute> SkinnedMesh::attributes() {
    std::vector<rhi::VertexAttribute> attributes;
    auto push = [&attributes](uint32_t location, rhi::VertexFormat format,
                              uint32_t offset) {
        rhi::VertexAttribute attribute;
        attribute.location = location;
        attribute.buffer_slot = 0;
        attribute.format = format;
        attribute.offset = offset;
        attributes.push_back(attribute);
    };
    push(0, rhi::VertexFormat::Float3, offsetof(SkinnedVertex, position));
    push(1, rhi::VertexFormat::Float3, offsetof(SkinnedVertex, normal));
    push(2, rhi::VertexFormat::Float3, offsetof(SkinnedVertex, color));
    push(3, rhi::VertexFormat::Float2, offsetof(SkinnedVertex, uv));
    push(4, rhi::VertexFormat::UByte4, offsetof(SkinnedVertex, joints));
    push(5, rhi::VertexFormat::Float4, offsetof(SkinnedVertex, weights));
    push(6, rhi::VertexFormat::Float4, offsetof(SkinnedVertex, tangent));
    return attributes;
}

}  // namespace anim
