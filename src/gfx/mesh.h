#pragma once

#include <cstdint>
#include <vector>

#include "rhi/rhi.h"
#include "core/math.h"

namespace gfx {

// Standard vertex for world geometry. Normals are needed for lighting; the
// color channel lets procedural geometry (terrain strata, greybox props) carry
// its own material without a texture pipeline yet.
struct MeshVertex {
    core::Vec3 position;
    core::Vec3 normal;
    core::Vec3 color;
    // Texture coordinate. Plants use it for their leaf cards and their bark;
    // terrain and props leave it zero and their shaders never read it.
    core::Vec2 uv = core::Vec2{0.0f, 0.0f};
};

// CPU-side geometry, before upload. Kept around after upload when the data is
// still needed for queries (terrain height sampling, collision).
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    // Recomputes vertex normals by area-weighted averaging of face normals.
    // Area weighting matters on terrain, where triangles vary wildly in size.
    void recompute_normals();
};

// GPU-resident mesh.
class Mesh {
public:
    bool upload(rhi::Device& rhi, const MeshData& data, const char* debug_name);
    void release(rhi::Device& rhi);

    void bind(rhi::Device& rhi, rhi::Pass* pass) const;
    uint32_t index_count() const { return index_count_; }
    bool valid() const { return vertex_buffer_ && index_buffer_; }

    // Vertex layout matching MeshVertex, for PipelineDesc.
    static std::vector<rhi::VertexBufferLayout> buffer_descriptions();
    static std::vector<rhi::VertexAttribute> attributes();

private:
    rhi::Buffer* vertex_buffer_ = nullptr;
    rhi::Buffer* index_buffer_ = nullptr;
    uint32_t index_count_ = 0;
};

}  // namespace gfx
