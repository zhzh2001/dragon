#include "anim/skin_partition.h"

#include <array>
#include <bitset>
#include <vector>

namespace anim {
namespace {

// The joints a vertex really depends on: zero-weight slots are padding (add()
// leaves them at joint 0) and must not cost palette space.
template <typename F>
void for_each_influence(const SkinnedVertex& v, F&& f) {
    for (int k = 0; k < 4; ++k) {
        if (v.weights[k] > 0.0f) f(v.joints[k]);
    }
}

}  // namespace

SkinnedMeshData partition_palettes(const SkinnedMeshData& mesh, uint32_t max_bones) {
    if (max_bones < MIN_PALETTE_BONES) max_bones = MIN_PALETTE_BONES;

    // Nothing to do when the whole skeleton the mesh uses fits one palette.
    std::bitset<256> used;
    for (const SkinnedVertex& v : mesh.vertices) for_each_influence(v, [&](uint8_t j) { used.set(j); });
    if (used.count() <= max_bones) return mesh;

    SkinnedMeshData out;
    out.vertices.reserve(mesh.vertices.size() + mesh.vertices.size() / 8);
    out.indices.reserve(mesh.indices.size());

    // Per batch: the global joint -> local slot map, and the global vertex ->
    // batch vertex map. Reset with the lists of what was set, not a sweep.
    std::array<int16_t, 256> slot;
    slot.fill(-1);
    std::vector<uint32_t> remap(mesh.vertices.size(), UINT32_MAX);
    std::vector<uint32_t> touched_vertices;

    std::vector<SkinnedSubmesh> source = mesh.submeshes;
    if (source.empty()) source.push_back({0, uint32_t(mesh.indices.size()), -1, -1, -1});

    for (const SkinnedSubmesh& sub : source) {
        uint32_t tri = sub.index_offset;
        const uint32_t end = sub.index_offset + sub.index_count;
        while (tri < end) {
            // Open a batch, then take triangles while their joints still fit.
            SkinnedSubmesh batch = sub;
            batch.index_offset = uint32_t(out.indices.size());
            batch.index_count = 0;
            batch.palette.clear();
            for (; tri + 2 < end; tri += 3) {
                uint8_t fresh[MIN_PALETTE_BONES];
                uint32_t fresh_count = 0;
                for (uint32_t c = 0; c < 3; ++c) {
                    for_each_influence(mesh.vertices[mesh.indices[tri + c]], [&](uint8_t j) {
                        if (slot[j] >= 0) return;
                        for (uint32_t f = 0; f < fresh_count; ++f) {
                            if (fresh[f] == j) return;
                        }
                        fresh[fresh_count++] = j;
                    });
                }
                if (batch.palette.size() + fresh_count > max_bones) break;
                for (uint32_t f = 0; f < fresh_count; ++f) {
                    slot[fresh[f]] = int16_t(batch.palette.size());
                    batch.palette.push_back(fresh[f]);
                }
                for (uint32_t c = 0; c < 3; ++c) {
                    const uint32_t g = mesh.indices[tri + c];
                    if (remap[g] == UINT32_MAX) {
                        SkinnedVertex v = mesh.vertices[g];
                        for (int k = 0; k < 4; ++k) {
                            v.joints[k] = v.weights[k] > 0.0f ? uint8_t(slot[v.joints[k]]) : 0;
                        }
                        remap[g] = uint32_t(out.vertices.size());
                        out.vertices.push_back(v);
                        touched_vertices.push_back(g);
                    }
                    out.indices.push_back(remap[g]);
                }
                batch.index_count += 3;
            }
            if (batch.index_count > 0) out.submeshes.push_back(batch);
            // Close the batch: forget its palette and its vertex copies.
            for (uint16_t j : batch.palette) slot[j] = -1;
            for (uint32_t g : touched_vertices) remap[g] = UINT32_MAX;
            touched_vertices.clear();
            if (batch.index_count == 0) break;  // degenerate tail; cannot happen with max_bones >= 12
        }
    }
    return out;
}

}  // namespace anim
