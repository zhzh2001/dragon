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

// A run of a mesh's indices covering one square of ground, with bounds, so a
// large mesh (the terrain) can be drawn chunk by chunk and culled.
struct MeshChunk {
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    core::Vec3 centre;
    float radius = 0.0f;
    // Which square of chunk_mesh's grid: two meshes chunked on the same grid
    // name the same ground by the same pair.
    int grid_x = 0, grid_z = 0;
    // With flanges: the run is the ground's own triangles (body_count
    // indices) and then each side's flange, -x, +x, -z, +z. A flange is only
    // seen where the neighbour is drawn from the other mesh, and on a card
    // that pays per triangle the rest cost 13% of the terrain.
    uint32_t body_count = 0;
    uint32_t side_first[4] = {};
    uint32_t side_count[4] = {};
};

// Reorders `mesh`'s triangles into square chunks of `chunk_size` metres by
// their centroids' x and z, and returns the chunks.
// With `flange` > 0, each chunk's border edges grow a strip hanging that
// many metres straight down: when two meshes of different density meet
// (the fixed-function tier's near and far terrain), the gap their edges
// leave shows the strip's terrain colour instead of the sky. That covers a
// difference in height only: the two meshes must also end on the same line,
// so `chunk_size` must be a whole number of both meshes' cells and `origin`
// (x and z) a grid line of both. Off the grid, each mesh's border cells
// fell to whichever side their centroids did, and the seams opened into
// holes a cell wide.
std::vector<MeshChunk> chunk_mesh(MeshData& mesh, float chunk_size, float flange = 0.0f, float origin = 0.0f);

// GPU-resident mesh.
class Mesh {
public:
    bool upload(rhi::Device& rhi, const MeshData& data, const char* debug_name);
    void release(rhi::Device& rhi);

    void bind(rhi::Device& rhi, rhi::Pass* pass) const;
    uint32_t index_count() const { return index_count_; }
    bool valid() const { return vertex_buffer_ && index_buffer_; }
    // A sphere round the vertices, in the mesh's own space: what the
    // renderer culls against the view (WorldRenderer::draw_mesh).
    core::Vec3 bound_centre() const { return bound_centre_; }
    float bound_radius() const { return bound_radius_; }

    // Vertex layout matching MeshVertex, for PipelineDesc.
    static std::vector<rhi::VertexBufferLayout> buffer_descriptions();
    static std::vector<rhi::VertexAttribute> attributes();

private:
    rhi::Buffer* vertex_buffer_ = nullptr;
    rhi::Buffer* index_buffer_ = nullptr;
    uint32_t index_count_ = 0;
    core::Vec3 bound_centre_;
    float bound_radius_ = 0.0f;
};

}  // namespace gfx
