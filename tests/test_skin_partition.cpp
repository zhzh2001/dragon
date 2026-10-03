// Palette splitting (anim/skin_partition.h): a mesh cut into batches of at
// most N joints must skin to exactly the same positions as the whole.

#include <cstdio>
#include <cstring>
#include <random>

#include "anim/skin_partition.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("  FAIL (line %d): %s\n", __LINE__, #cond);       \
        }                                                                 \
    } while (0)

using core::Mat4;
using core::Vec3;

namespace {

// The shader's blend, on the CPU: the same matrices scaled by the same
// weights and summed in the same order.
Vec3 skin(const anim::SkinnedVertex& v, const Mat4* palette) {
    Mat4 blended;
    for (int c = 0; c < 4; ++c) blended.col[c] = palette[v.joints[0]].col[c] * v.weights[0];
    for (int k = 1; k < 4; ++k) {
        for (int c = 0; c < 4; ++c) blended.col[c] = blended.col[c] + palette[v.joints[k]].col[c] * v.weights[k];
    }
    return core::transform_point(blended, v.position);
}

Mat4 random_joint(std::mt19937& rng) {
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    const core::Quat q = core::normalize(core::Quat{u(rng), u(rng), u(rng), u(rng) + 1.5f});
    return Mat4::translation(Vec3{u(rng) * 3.0f, u(rng) * 3.0f, u(rng) * 3.0f}) * Mat4::from_quat(q);
}

// A mesh like a creature's: vertices weighted to a few joints near each other
// along the chain, so neighbouring triangles share most of their joints.
anim::SkinnedMeshData make_mesh(std::mt19937& rng, int joints, int vertices, int materials) {
    anim::SkinnedMeshData mesh;
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int i = 0; i < vertices; ++i) {
        const int base = int(float(i) / float(vertices) * float(joints - 4));
        int ids[4] = {base, base + 1 + int(u(rng) * 2.0f), base + 3, base};
        float w[4] = {0.5f + u(rng), u(rng), u(rng) * 0.5f, 0.0f};
        mesh.add(Vec3{u(rng) * 10.0f, u(rng) * 10.0f, u(rng) * 10.0f}, Vec3::one(),
                 core::Vec2{u(rng), u(rng)}, ids, w);
    }
    const int per = vertices / materials;
    for (int m = 0; m < materials; ++m) {
        anim::SkinnedSubmesh sub;
        sub.index_offset = uint32_t(mesh.indices.size());
        sub.base_color_texture = m;
        for (int i = m * per; i + 2 < (m + 1) * per; ++i) {
            mesh.indices.push_back(uint32_t(i));
            mesh.indices.push_back(uint32_t(i + 1));
            mesh.indices.push_back(uint32_t(i + 2));
        }
        sub.index_count = uint32_t(mesh.indices.size()) - sub.index_offset;
        mesh.submeshes.push_back(sub);
    }
    return mesh;
}

void test_split_is_exact(uint32_t max_bones) {
    std::printf("a 200-joint mesh split into %u-joint palettes skins identically\n", max_bones);
    std::mt19937 rng(7 + max_bones);
    const anim::SkinnedMeshData mesh = make_mesh(rng, 200, 3000, 3);
    std::vector<Mat4> joints(256);
    for (Mat4& m : joints) m = random_joint(rng);

    const anim::SkinnedMeshData split = anim::partition_palettes(mesh, max_bones);
    CHECK(split.submeshes.size() > mesh.submeshes.size());
    CHECK(split.indices.size() == mesh.indices.size());

    // Every batch within budget, every batch from one source material, and
    // the batches together covering each material's triangles in order.
    size_t triangles = 0;
    bool exact = true, within = true;
    for (const anim::SkinnedSubmesh& batch : split.submeshes) {
        if (batch.palette.empty() || batch.palette.size() > max_bones) within = false;
        Mat4 local[256];
        for (size_t i = 0; i < 256; ++i) local[i] = Mat4::identity();
        for (size_t i = 0; i < batch.palette.size(); ++i) local[i] = joints[batch.palette[i]];
        for (uint32_t k = 0; k < batch.index_count; ++k) {
            const anim::SkinnedVertex& v = split.vertices[split.indices[batch.index_offset + k]];
            for (int s = 0; s < 4; ++s) {
                if (v.weights[s] > 0.0f && v.joints[s] >= batch.palette.size()) within = false;
            }
            // The source vertex is the one at the same position in the
            // unsplit index list: batches keep triangle order.
            const anim::SkinnedVertex& original =
                mesh.vertices[mesh.indices[triangles * 3 + (k / 3) * 3 + (k % 3)]];
            const Vec3 a = skin(original, joints.data());
            const Vec3 b = skin(v, local);
            if (std::memcmp(&a, &b, sizeof(Vec3)) != 0) exact = false;
        }
        triangles += batch.index_count / 3;
    }
    CHECK(within);
    CHECK(exact);
    CHECK(triangles * 3 == mesh.indices.size());
    // Materials survive the split.
    bool materials = true;
    int last = -1;
    for (const anim::SkinnedSubmesh& batch : split.submeshes) {
        if (batch.base_color_texture < last) materials = false;
        last = batch.base_color_texture;
    }
    CHECK(materials);
    std::printf("  %zu batches, %zu -> %zu vertices\n", split.submeshes.size(), mesh.vertices.size(),
                split.vertices.size());
}

void test_fitting_mesh_is_untouched() {
    std::printf("a mesh whose joints fit is returned as it was, with no palettes\n");
    std::mt19937 rng(3);
    const anim::SkinnedMeshData mesh = make_mesh(rng, 40, 600, 2);
    const anim::SkinnedMeshData out = anim::partition_palettes(mesh, 60);
    CHECK(out.vertices.size() == mesh.vertices.size());
    CHECK(out.indices == mesh.indices);
    CHECK(out.submeshes.size() == mesh.submeshes.size());
    CHECK(out.submeshes[0].palette.empty());
}

void test_tiny_budget_is_clamped() {
    std::printf("a budget below one triangle's worst case is raised to it\n");
    std::mt19937 rng(11);
    const anim::SkinnedMeshData mesh = make_mesh(rng, 120, 900, 1);
    const anim::SkinnedMeshData out = anim::partition_palettes(mesh, 2);
    CHECK(out.indices.size() == mesh.indices.size());
    bool within = true;
    for (const anim::SkinnedSubmesh& batch : out.submeshes) {
        if (batch.palette.size() > anim::MIN_PALETTE_BONES) within = false;
    }
    CHECK(within);
}

}  // namespace

int main() {
    test_split_is_exact(60);
    test_split_is_exact(50);
    test_split_is_exact(12);
    test_fitting_mesh_is_untouched();
    test_tiny_budget_is_clamped();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
