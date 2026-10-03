// Creature LODs (anim/skin_lod.h): the simplified mesh must keep only
// original vertices -- joints and weights untouched -- and hit its budget.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "anim/skin_lod.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("  FAIL (line %d): %s\n", __LINE__, #cond);       \
        }                                                                 \
    } while (0)

namespace {

// A rippled sheet, n x n quads, in two materials (left and right halves),
// skinned across eight joints along x.
anim::SkinnedMeshData sheet(int n) {
    anim::SkinnedMeshData mesh;
    for (int z = 0; z <= n; ++z) {
        for (int x = 0; x <= n; ++x) {
            const float fx = float(x) / float(n), fz = float(z) / float(n);
            const float along = fx * 7.0f;
            const int j0 = int(along) < 7 ? int(along) : 6;
            const float t = along - float(j0);
            const int joints[4] = {j0, j0 + 1, 0, 0};
            const float weights[4] = {1.0f - t, t, 0.0f, 0.0f};
            mesh.add(core::Vec3{fx * 10.0f, 0.3f * std::sin(fx * 9.0f) * std::cos(fz * 7.0f), fz * 10.0f},
                     core::Vec3{1.0f, 1.0f, 1.0f}, core::Vec2{fx, fz}, joints, weights);
        }
    }
    for (int half = 0; half < 2; ++half) {
        anim::SkinnedSubmesh sub;
        sub.index_offset = uint32_t(mesh.indices.size());
        sub.base_color_texture = half;
        for (int z = 0; z < n; ++z) {
            for (int x = half * n / 2; x < (half + 1) * n / 2; ++x) {
                const uint32_t a = uint32_t(z * (n + 1) + x), b = a + 1, c = a + uint32_t(n + 1), d = c + 1;
                mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
            }
        }
        sub.index_count = uint32_t(mesh.indices.size()) - sub.index_offset;
        mesh.submeshes.push_back(sub);
    }
    mesh.recompute_normals();
    return mesh;
}

std::string bytes(const anim::SkinnedVertex& v) {
    return std::string(reinterpret_cast<const char*>(&v), sizeof v);
}

}  // namespace

int main() {
    const anim::SkinnedMeshData full = sheet(100);  // 20,000 triangles
    CHECK(full.indices.size() / 3 == 20000);

    // Already within budget: unchanged.
    {
        const anim::LodResult same = anim::simplify_skinned(full, 30000);
        CHECK(same.mesh.indices == full.indices);
        CHECK(same.error == 0.0f);
        CHECK(anim::simplify_skinned(full, 0).mesh.indices == full.indices);
    }

    const anim::LodResult lod = anim::simplify_skinned(full, 5000);
    const size_t triangles = lod.mesh.indices.size() / 3;
    std::printf("  20000 -> %zu triangles, %zu -> %zu vertices, error %.3f%%\n", triangles,
                full.vertices.size(), lod.mesh.vertices.size(), double(lod.error) * 100.0);
    CHECK(triangles <= 5000 + 2);
    CHECK(triangles > 2500);  // it did not collapse the sheet away
    CHECK(lod.error < 0.05f);

    // Every vertex is an original, byte for byte: position, normal, joints
    // and weights as the rig authored them.
    std::set<std::string> originals;
    for (const anim::SkinnedVertex& v : full.vertices) originals.insert(bytes(v));
    int foreign = 0;
    for (const anim::SkinnedVertex& v : lod.mesh.vertices) foreign += originals.count(bytes(v)) ? 0 : 1;
    CHECK(foreign == 0);

    // Indices in range, no vertex unreferenced, materials kept in order.
    bool in_range = true;
    std::vector<bool> used(lod.mesh.vertices.size(), false);
    for (uint32_t i : lod.mesh.indices) {
        if (i >= lod.mesh.vertices.size()) in_range = false;
        else used[i] = true;
    }
    CHECK(in_range);
    CHECK(std::find(used.begin(), used.end(), false) == used.end());
    CHECK(lod.mesh.submeshes.size() == 2);
    if (lod.mesh.submeshes.size() == 2) {
        CHECK(lod.mesh.submeshes[0].base_color_texture == 0 && lod.mesh.submeshes[1].base_color_texture == 1);
        CHECK(lod.mesh.submeshes[1].index_offset == lod.mesh.submeshes[0].index_count);
        CHECK(lod.mesh.submeshes[0].index_count + lod.mesh.submeshes[1].index_count == lod.mesh.indices.size());
    }

    std::printf("skin_lod: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
