#include "anim/skin_lod.h"

#include <algorithm>
#include <cfloat>

#include "meshoptimizer.h"

namespace anim {

LodResult simplify_skinned(const SkinnedMeshData& mesh, uint32_t max_triangles) {
    LodResult result;
    const size_t triangles = mesh.indices.size() / 3;
    if (max_triangles == 0 || triangles <= max_triangles || mesh.vertices.empty()) {
        result.mesh = mesh;
        return result;
    }
    const double ratio = double(max_triangles) / double(triangles);
    const size_t vertex_count = mesh.vertices.size();

    // What a collapse is charged for besides moving the surface: bending the
    // normal (a fold in the hide costs more than a flat run of it) and
    // sliding the texture. The collapse may cross a UV seam (permissive, below)
    // -- the generated creatures are cut into hundreds of atlas islands, and
    // a simplifier that holds every seam stalls at 15 to 20 thousand
    // triangles and then tears the wing membranes to go further -- so the
    // UV error is what keeps it from smearing an island across another.
    constexpr size_t ATTRIBUTES = 5;
    std::vector<float> attributes(vertex_count * ATTRIBUTES);
    for (size_t v = 0; v < vertex_count; ++v) {
        float* a = &attributes[v * ATTRIBUTES];
        a[0] = mesh.vertices[v].normal.x;
        a[1] = mesh.vertices[v].normal.y;
        a[2] = mesh.vertices[v].normal.z;
        a[3] = mesh.vertices[v].uv.x;
        a[4] = mesh.vertices[v].uv.y;
    }
    const float attribute_weights[ATTRIBUTES] = {0.5f, 0.5f, 0.5f, 4.0f, 4.0f};

    std::vector<SkinnedSubmesh> source = mesh.submeshes;
    if (source.empty()) source.push_back({0, uint32_t(mesh.indices.size()), -1, -1, -1});

    std::vector<uint32_t> indices;
    indices.reserve(size_t(double(mesh.indices.size()) * ratio) + 3 * source.size());
    std::vector<SkinnedSubmesh> submeshes;
    std::vector<uint32_t> scratch;
    for (const SkinnedSubmesh& sub : source) {
        const size_t target = std::max<size_t>(3, size_t(double(sub.index_count) * ratio) / 3 * 3);
        scratch.resize(sub.index_count);
        float error = 0.0f;
        // No error bound: the tier's count is the budget. Sparse, because a
        // submesh indexes a slice of the shared vertex buffer.
        size_t count = meshopt_simplifyWithAttributes(
            scratch.data(), &mesh.indices[sub.index_offset], sub.index_count, &mesh.vertices[0].position.x,
            vertex_count, sizeof(SkinnedVertex), attributes.data(), ATTRIBUTES * sizeof(float),
            attribute_weights, ATTRIBUTES, nullptr, target, FLT_MAX,
            meshopt_SimplifySparse | meshopt_SimplifyPermissive, &error);
        meshopt_optimizeVertexCache(scratch.data(), scratch.data(), count, vertex_count);
        result.error = std::max(result.error, error);

        SkinnedSubmesh out = sub;
        out.index_offset = uint32_t(indices.size());
        out.index_count = uint32_t(count);
        out.palette.clear();
        indices.insert(indices.end(), scratch.begin(), scratch.begin() + ptrdiff_t(count));
        if (count > 0) submeshes.push_back(out);
    }

    // Keep only the vertices still referenced, in first-use order.
    std::vector<uint32_t> remap(vertex_count);
    const size_t unique = meshopt_optimizeVertexFetchRemap(remap.data(), indices.data(), indices.size(), vertex_count);
    result.mesh.vertices.resize(unique);
    meshopt_remapVertexBuffer(result.mesh.vertices.data(), mesh.vertices.data(), vertex_count,
                              sizeof(SkinnedVertex), remap.data());
    result.mesh.indices.resize(indices.size());
    meshopt_remapIndexBuffer(result.mesh.indices.data(), indices.data(), indices.size(), remap.data());
    result.mesh.submeshes = std::move(submeshes);
    return result;
}

}  // namespace anim
