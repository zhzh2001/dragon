#include "anim/skin_lod.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

#include "gfx/texture.h"
#include "meshoptimizer.h"

namespace anim {
namespace {

// The LOD's own normals. The vertices that survive keep the normals of the
// full sculpt, which follow its scales and ridges; on a surface a hundred
// times coarser they point every which way, and lit per vertex (the
// fixed-function tier pre-lights by them) the hide broke into dark creased
// facets. Recomputed here from the LOD's faces, area-weighted, and shared
// by every copy of a position, so a UV seam does not crease the shading.
void smooth_normals(SkinnedMeshData& mesh) {
    std::vector<uint32_t> first(mesh.vertices.size());
    {
        // Copies of a position share one accumulator: sort by position.
        std::vector<uint32_t> order(mesh.vertices.size());
        for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
        const auto less = [&](uint32_t a, uint32_t b) {
            const core::Vec3 pa = mesh.vertices[a].position, pb = mesh.vertices[b].position;
            if (pa.x != pb.x) return pa.x < pb.x;
            if (pa.y != pb.y) return pa.y < pb.y;
            if (pa.z != pb.z) return pa.z < pb.z;
            return a < b;
        };
        std::sort(order.begin(), order.end(), less);
        for (size_t i = 0; i < order.size(); ++i) {
            const bool same = i > 0 && mesh.vertices[order[i]].position.x == mesh.vertices[order[i - 1]].position.x &&
                              mesh.vertices[order[i]].position.y == mesh.vertices[order[i - 1]].position.y &&
                              mesh.vertices[order[i]].position.z == mesh.vertices[order[i - 1]].position.z;
            first[order[i]] = same ? first[order[i - 1]] : order[i];
        }
    }
    std::vector<core::Vec3> sum(mesh.vertices.size(), core::Vec3::zero());
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const core::Vec3 a = mesh.vertices[mesh.indices[t]].position;
        const core::Vec3 b = mesh.vertices[mesh.indices[t + 1]].position;
        const core::Vec3 c = mesh.vertices[mesh.indices[t + 2]].position;
        const core::Vec3 n = core::cross(b - a, c - a);  // length = twice the area
        for (int k = 0; k < 3; ++k) sum[first[mesh.indices[t + size_t(k)]]] = sum[first[mesh.indices[t + size_t(k)]]] + n;
    }
    for (size_t v = 0; v < mesh.vertices.size(); ++v) {
        const core::Vec3 n = sum[first[v]];
        const float len = core::length(n);
        if (len > 1e-12f) mesh.vertices[v].normal = n * (1.0f / len);
    }
}

// The base colour at `uv`, linear (the texture is sRGB): nearest texel,
// wrapped. One sample a vertex; the mean over a cluster does the filtering.
core::Vec3 sample_linear(const gfx::ImageData& image, core::Vec2 uv) {
    const float u = uv.x - std::floor(uv.x), v = uv.y - std::floor(uv.y);
    const int x = std::min(image.width - 1, int(u * float(image.width)));
    const int y = std::min(image.height - 1, int(v * float(image.height)));
    const uint8_t* p = &image.rgba[(size_t(y) * size_t(image.width) + size_t(x)) * 4];
    const auto lin = [](uint8_t c) { return std::pow(float(c) / 255.0f, 2.2f); };
    return core::Vec3{lin(p[0]), lin(p[1]), lin(p[2])};
}

// simplify_skinned's vertex colours (skin_lod.h): every full-mesh vertex
// gives its base colour to the nearest LOD vertex drawn with the same
// texture, and each LOD vertex takes the mean of what it was given.
void bake_vertex_colours(const SkinnedMeshData& full, SkinnedMeshData& lod, const std::vector<gfx::ImageData>& textures) {
    const auto texture_of = [&](int slot) -> const gfx::ImageData* {
        return slot >= 0 && size_t(slot) < textures.size() && textures[size_t(slot)].valid() ? &textures[size_t(slot)]
                                                                                            : nullptr;
    };
    // The texture each vertex is drawn with (its first submesh's).
    const auto groups = [](const SkinnedMeshData& m) {
        std::vector<int> group(m.vertices.size(), -2);
        for (const SkinnedSubmesh& s : m.submeshes) {
            for (uint32_t i = s.index_offset; i < s.index_offset + s.index_count; ++i) {
                if (group[m.indices[i]] == -2) group[m.indices[i]] = s.base_color_texture;
            }
        }
        return group;
    };
    const std::vector<int> full_group = groups(full), lod_group = groups(lod);

    core::Vec3 lo{FLT_MAX, FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const SkinnedVertex& v : lod.vertices) {
        lo = core::Vec3{std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
        hi = core::Vec3{std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
    }
    constexpr int GRID = 32;
    const core::Vec3 size = hi - lo;
    const float cell = std::max(std::max(size.x, size.y), std::max(size.z, 1e-6f)) / float(GRID);
    const auto cell_of = [&](core::Vec3 p, int axis) {
        const float c = axis == 0 ? (p.x - lo.x) : axis == 1 ? (p.y - lo.y) : (p.z - lo.z);
        return std::clamp(int(c / cell), 0, GRID);
    };
    // LOD vertices bucketed by cell (GRID + 1 per axis: the top edge's own).
    constexpr int SIDE = GRID + 1;
    std::vector<std::vector<uint32_t>> buckets(size_t(SIDE) * SIDE * SIDE);
    for (uint32_t i = 0; i < lod.vertices.size(); ++i) {
        const core::Vec3 p = lod.vertices[i].position;
        buckets[(size_t(cell_of(p, 0)) * SIDE + size_t(cell_of(p, 1))) * SIDE + size_t(cell_of(p, 2))].push_back(i);
    }

    std::vector<core::Vec3> sum(lod.vertices.size(), core::Vec3::zero());
    std::vector<float> count(lod.vertices.size(), 0.0f);
    for (size_t v = 0; v < full.vertices.size(); ++v) {
        if (full_group[v] == -2) continue;  // not drawn
        const SkinnedVertex& fv = full.vertices[v];
        const gfx::ImageData* image = texture_of(full_group[v]);
        const core::Vec3 colour = image ? sample_linear(*image, fv.uv) : core::Vec3::one();
        const core::Vec3 tint{colour.x * fv.color.x, colour.y * fv.color.y, colour.z * fv.color.z};
        // The nearest LOD vertex of the same texture: rings of cells outward
        // until one holds a candidate, then one ring more (a nearer vertex
        // can sit in the next ring out), lowest index on a tie.
        const int cx = cell_of(fv.position, 0), cy = cell_of(fv.position, 1), cz = cell_of(fv.position, 2);
        uint32_t best = UINT32_MAX;
        float best_d = FLT_MAX;
        int found_at = -1;
        for (int r = 0; r <= SIDE && (found_at < 0 || r <= found_at + 1); ++r) {
            for (int x = cx - r; x <= cx + r; ++x) {
                for (int y = cy - r; y <= cy + r; ++y) {
                    for (int z = cz - r; z <= cz + r; ++z) {
                        if (std::max(std::abs(x - cx), std::max(std::abs(y - cy), std::abs(z - cz))) != r) continue;
                        if (x < 0 || y < 0 || z < 0 || x >= SIDE || y >= SIDE || z >= SIDE) continue;
                        for (uint32_t i : buckets[(size_t(x) * SIDE + size_t(y)) * SIDE + size_t(z)]) {
                            if (lod_group[i] != full_group[v]) continue;
                            const float d = core::length_sq(lod.vertices[i].position - fv.position);
                            if (d < best_d || (d == best_d && i < best)) {
                                best_d = d;
                                best = i;
                            }
                        }
                    }
                }
            }
            if (best != UINT32_MAX && found_at < 0) found_at = r;
        }
        if (best == UINT32_MAX) continue;
        sum[best] = sum[best] + tint;
        count[best] += 1.0f;
    }
    for (size_t i = 0; i < lod.vertices.size(); ++i) {
        if (count[i] > 0.0f) {
            lod.vertices[i].color = sum[i] * (1.0f / count[i]);
        } else if (lod_group[i] != -2) {
            const gfx::ImageData* image = texture_of(lod_group[i]);
            const core::Vec3 c = image ? sample_linear(*image, lod.vertices[i].uv) : core::Vec3::one();
            lod.vertices[i].color = core::Vec3{c.x * lod.vertices[i].color.x, c.y * lod.vertices[i].color.y,
                                               c.z * lod.vertices[i].color.z};
        }
    }
    for (SkinnedSubmesh& s : lod.submeshes) {
        s.base_color_texture = -1;
        s.normal_texture = -1;
        s.orm_texture = -1;
    }
}

// A vertex-coloured LOD draws no texture, so the copies of a vertex its UV
// seams made are one vertex again where the position and the skinning
// agree: their colours averaged (the normals are already shared,
// smooth_normals). On a card that transforms each vertex on the CPU and
// sends it, the 5,000-triangle watchtower was 9,500 vertices for this.
void weld_untextured(SkinnedMeshData& mesh) {
    const auto key_less = [&](uint32_t a, uint32_t b) {
        const SkinnedVertex& va = mesh.vertices[a];
        const SkinnedVertex& vb = mesh.vertices[b];
        if (va.position.x != vb.position.x) return va.position.x < vb.position.x;
        if (va.position.y != vb.position.y) return va.position.y < vb.position.y;
        if (va.position.z != vb.position.z) return va.position.z < vb.position.z;
        for (int i = 0; i < 4; ++i) {
            if (va.joints[i] != vb.joints[i]) return va.joints[i] < vb.joints[i];
            if (va.weights[i] != vb.weights[i]) return va.weights[i] < vb.weights[i];
        }
        return a < b;
    };
    std::vector<uint32_t> order(mesh.vertices.size());
    for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), key_less);
    // key_less breaks ties by index, so compare the keys without it.
    const auto same = [&](uint32_t a, uint32_t b) {
        const SkinnedVertex& va = mesh.vertices[a];
        const SkinnedVertex& vb = mesh.vertices[b];
        if (va.position.x != vb.position.x || va.position.y != vb.position.y || va.position.z != vb.position.z) return false;
        for (int i = 0; i < 4; ++i)
            if (va.joints[i] != vb.joints[i] || va.weights[i] != vb.weights[i]) return false;
        return true;
    };
    std::vector<uint32_t> remap(mesh.vertices.size());
    std::vector<SkinnedVertex> welded;
    std::vector<float> count;
    for (size_t k = 0; k < order.size(); ++k) {
        const uint32_t v = order[k];
        if (k == 0 || !same(order[k - 1], v)) {
            welded.push_back(mesh.vertices[v]);
            count.push_back(1.0f);
        } else {
            SkinnedVertex& w = welded.back();
            w.color = w.color + mesh.vertices[v].color;
            count.back() += 1.0f;
        }
        remap[v] = uint32_t(welded.size() - 1);
    }
    for (size_t i = 0; i < welded.size(); ++i) welded[i].color = welded[i].color * (1.0f / count[i]);
    for (uint32_t& i : mesh.indices) i = remap[i];
    // First-use order again, for the post-transform cache's sake.
    std::vector<uint32_t> fetch(welded.size());
    const size_t unique = meshopt_optimizeVertexFetchRemap(fetch.data(), mesh.indices.data(), mesh.indices.size(), welded.size());
    mesh.vertices.resize(unique);
    meshopt_remapVertexBuffer(mesh.vertices.data(), welded.data(), welded.size(), sizeof(SkinnedVertex), fetch.data());
    meshopt_remapIndexBuffer(mesh.indices.data(), mesh.indices.data(), mesh.indices.size(), fetch.data());
}

}  // namespace

LodResult simplify_skinned(const SkinnedMeshData& mesh, uint32_t max_triangles,
                           const std::vector<gfx::ImageData>* textures) {
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
    // Untextured at the low budgets (skin_lod.h), and lit by its own normals:
    // the light per vertex is then all the shading there is.
    if (textures && max_triangles <= VERTEX_COLOUR_TRIANGLES) {
        bake_vertex_colours(mesh, result.mesh, *textures);
        smooth_normals(result.mesh);
        weld_untextured(result.mesh);
    }
    return result;
}

}  // namespace anim
