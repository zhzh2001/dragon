#include "gfx/mesh_lod.h"

#include <cfloat>
#include <cstddef>

#include "meshoptimizer.h"

namespace gfx {

MeshData simplify_mesh(const MeshData& mesh, uint32_t max_triangles) {
    if (max_triangles == 0 || mesh.indices.size() / 3 <= max_triangles || mesh.vertices.empty()) return mesh;

    // Weld by position: a flat-shaded mesh repeats each corner once per face,
    // and the simplifier sees separate faces as separate pieces it may not
    // join. Each welded corner keeps one face's colour (the rocks' colours
    // differ per face by their baked shade, which the LOD gives up).
    const meshopt_Stream streams[] = {
        {&mesh.vertices[0].position, sizeof(core::Vec3), sizeof(MeshVertex)},
    };
    std::vector<uint32_t> remap(mesh.vertices.size());
    const size_t unique = meshopt_generateVertexRemapMulti(remap.data(), mesh.indices.data(), mesh.indices.size(),
                                                           mesh.vertices.size(), streams, 1);
    MeshData welded;
    welded.vertices.resize(unique);
    welded.indices.resize(mesh.indices.size());
    meshopt_remapVertexBuffer(welded.vertices.data(), mesh.vertices.data(), mesh.vertices.size(), sizeof(MeshVertex),
                              remap.data());
    meshopt_remapIndexBuffer(welded.indices.data(), mesh.indices.data(), mesh.indices.size(), remap.data());

    std::vector<uint32_t> indices(welded.indices.size());
    const size_t count = meshopt_simplify(indices.data(), welded.indices.data(), welded.indices.size(),
                                          &welded.vertices[0].position.x, welded.vertices.size(), sizeof(MeshVertex),
                                          size_t(max_triangles) * 3, FLT_MAX, 0, nullptr);
    indices.resize(count);
    meshopt_optimizeVertexCache(indices.data(), indices.data(), count, welded.vertices.size());

    MeshData out;
    std::vector<uint32_t> compact(welded.vertices.size());
    const size_t kept = meshopt_optimizeVertexFetchRemap(compact.data(), indices.data(), count, welded.vertices.size());
    out.vertices.resize(kept);
    meshopt_remapVertexBuffer(out.vertices.data(), welded.vertices.data(), welded.vertices.size(), sizeof(MeshVertex),
                              compact.data());
    out.indices.resize(count);
    meshopt_remapIndexBuffer(out.indices.data(), indices.data(), count, compact.data());
    out.recompute_normals();
    return out;
}

}  // namespace gfx
