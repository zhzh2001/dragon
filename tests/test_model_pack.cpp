// Model packs (anim/model_pack.h): a pack round-trips byte for byte, a
// texture chain trimmed from the baked cap is the chain built at the smaller
// cap, and a real model's pack holds what the glTF route computes.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "anim/gltf_loader.h"
#include "anim/model_pack.h"
#include "anim/skin_lod.h"
#include "test_paths.h"

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

// A rippled sheet, n x n quads, in two materials, skinned across eight joints.
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
                     core::Vec3{1.0f, 0.5f, 0.25f}, core::Vec2{fx, fz}, joints, weights);
        }
    }
    for (int half = 0; half < 2; ++half) {
        anim::SkinnedSubmesh sub;
        sub.index_offset = uint32_t(mesh.indices.size());
        sub.base_color_texture = 0;
        sub.normal_texture = half == 0 ? 1 : -1;
        sub.orm_texture = half == 1 ? 2 : -1;
        sub.palette = {uint16_t(half), uint16_t(half + 3)};
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

// A deterministic pattern with alpha where `alpha` says, every texel distinct.
gfx::ImageData pattern(int w, int h, bool alpha) {
    gfx::ImageData image;
    image.width = w;
    image.height = h;
    image.rgba.resize(size_t(w) * h * 4);
    uint32_t state = 12345u;
    for (size_t i = 0; i < image.rgba.size(); i += 4) {
        state = state * 1664525u + 1013904223u;
        image.rgba[i] = uint8_t(state >> 24);
        image.rgba[i + 1] = uint8_t(state >> 16);
        image.rgba[i + 2] = uint8_t(state >> 8);
        image.rgba[i + 3] = alpha ? uint8_t(state) : 255;
    }
    return image;
}

bool same_mesh(const anim::SkinnedMeshData& a, const anim::SkinnedMeshData& b) {
    if (a.vertices.size() != b.vertices.size() || a.indices != b.indices || a.submeshes.size() != b.submeshes.size())
        return false;
    if (!a.vertices.empty() &&
        std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(a.vertices[0])) != 0)
        return false;
    for (size_t i = 0; i < a.submeshes.size(); ++i) {
        const anim::SkinnedSubmesh& x = a.submeshes[i];
        const anim::SkinnedSubmesh& y = b.submeshes[i];
        if (x.index_offset != y.index_offset || x.index_count != y.index_count ||
            x.base_color_texture != y.base_color_texture || x.normal_texture != y.normal_texture ||
            x.orm_texture != y.orm_texture || x.palette != y.palette)
            return false;
    }
    return true;
}

bool same_prepared(const gfx::PreparedTexture& a, const gfx::PreparedTexture& b) {
    return a.format == b.format && a.width == b.width && a.height == b.height && a.levels == b.levels;
}

uint32_t fingerprint(const anim::SkinnedMeshData& mesh) {
    uint32_t h = 2166136261u;
    const auto mix = [&h](const void* data, size_t size) {
        for (size_t i = 0; i < size; ++i) h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    };
    mix(mesh.vertices.data(), mesh.vertices.size() * sizeof(mesh.vertices[0]));
    mix(mesh.indices.data(), mesh.indices.size() * sizeof(mesh.indices[0]));
    return h;
}

void test_round_trip() {
    std::printf("round trip\n");
    anim::ModelPack pack;
    pack.source_size = 123456789ull;
    pack.source_hash = 0xDEADBEEFCAFEull;
    for (int j = 0; j < 8; ++j) {
        core::Transform t;
        t.position = core::Vec3{float(j), 0.5f, -1.0f};
        t.rotation = core::Quat{0.1f * float(j), 0.0f, 0.0f, 1.0f};
        t.scale = core::Vec3{1.0f, 2.0f, float(j)};
        pack.skeleton.add_joint("joint_" + std::to_string(j), j == 0 ? anim::NO_PARENT : j - 1, t);
    }
    pack.skeleton.finalize();
    pack.mesh = sheet(12);
    pack.bounds_min = core::Vec3{-1.0f, -2.0f, -3.0f};
    pack.bounds_max = core::Vec3{4.0f, 5.0f, 6.0f};
    pack.bind_pose_error = 0.001f;
    anim::AnimationClip clip;
    clip.name = "graze";
    clip.duration = 2.5f;
    anim::RotationTrack track;
    track.joint = 3;
    track.times = {0.0f, 1.0f, 2.5f};
    track.rotations = {core::Quat{0, 0, 0, 1}, core::Quat{0.5f, 0, 0, 0.8f}, core::Quat{0, 0.5f, 0, 0.8f}};
    clip.tracks.push_back(track);
    pack.animations.push_back(clip);
    {
        anim::ModelPackLod lod;
        lod.max_triangles = 100;
        lod.error = 0.05f;
        lod.mesh = anim::simplify_skinned(pack.mesh, 100).mesh;
        pack.lods.push_back(lod);
        anim::ModelPackLod whole;
        whole.max_triangles = 100000;
        pack.lods.push_back(whole);
    }
    gfx::TextureBudget budget;
    budget.max_size = 64;
    budget.compress = true;
    const gfx::TextureKind kinds[3] = {gfx::TextureKind::Colour, gfx::TextureKind::NormalMap, gfx::TextureKind::Orm};
    for (int i = 0; i < 3; ++i) {
        anim::ModelPackTexture t;
        t.kind = kinds[i];
        t.srgb = i == 0;
        t.prepared = gfx::prepare_texture(pattern(128, 64, i == 0), kinds[i], budget, true);
        pack.textures.push_back(t);
    }
    CHECK(pack.textures[0].prepared.format == rhi::Format::BC3_SRGB);
    CHECK(pack.textures[1].prepared.format == rhi::Format::BC3);
    CHECK(pack.textures[2].prepared.format == rhi::Format::BC1);
    CHECK(pack.textures[0].prepared.width == 64 && pack.textures[0].prepared.height == 32);

    const std::string path = test_temp_path("model_pack.pack");
    CHECK(anim::write_model_pack(path.c_str(), pack));
    anim::ModelPack back;
    std::string error;
    CHECK(anim::read_model_pack(path.c_str(), back, &error));
    if (!error.empty()) std::printf("  read: %s\n", error.c_str());
    CHECK(back.source_size == pack.source_size);
    CHECK(back.source_hash == pack.source_hash);
    CHECK(back.skeleton.count() == 8);
    for (int j = 0; j < 8 && j < back.skeleton.count(); ++j) {
        const anim::Joint& a = pack.skeleton.joint(j);
        const anim::Joint& b = back.skeleton.joint(j);
        CHECK(a.name == b.name && a.parent == b.parent);
        CHECK(std::memcmp(&a.local_bind, &b.local_bind, sizeof a.local_bind) == 0);
        CHECK(std::memcmp(&a.inverse_bind, &b.inverse_bind, sizeof a.inverse_bind) == 0);
    }
    CHECK(same_mesh(back.mesh, pack.mesh));
    CHECK(std::memcmp(&back.bounds_min, &pack.bounds_min, sizeof back.bounds_min) == 0);
    CHECK(std::memcmp(&back.bounds_max, &pack.bounds_max, sizeof back.bounds_max) == 0);
    CHECK(back.bind_pose_error == pack.bind_pose_error);
    CHECK(back.animations.size() == 1);
    if (back.animations.size() == 1) {
        CHECK(back.animations[0].name == "graze" && back.animations[0].duration == 2.5f);
        CHECK(back.animations[0].tracks.size() == 1 && back.animations[0].tracks[0].joint == 3);
        CHECK(back.animations[0].tracks[0].times == track.times);
        CHECK(std::memcmp(back.animations[0].tracks[0].rotations.data(), track.rotations.data(),
                          track.rotations.size() * sizeof(core::Quat)) == 0);
    }
    CHECK(back.lods.size() == 2);
    CHECK(back.lod(100) && back.lod(100)->error == 0.05f && same_mesh(back.lod(100)->mesh, pack.lods[0].mesh));
    CHECK(back.lod(100000) && back.lod(100000)->mesh.indices.empty());
    CHECK(back.lod(7) == nullptr);
    CHECK(back.textures.size() == 3);
    for (size_t i = 0; i < 3 && i < back.textures.size(); ++i) {
        CHECK(back.textures[i].kind == kinds[i] && back.textures[i].srgb == (i == 0));
        CHECK(same_prepared(back.textures[i].prepared, pack.textures[i].prepared));
        CHECK(back.textures[i].prepared.source_width == 128 && back.textures[i].prepared.source_height == 64);
    }

    // Freshness: a source that is not there is trusted; one that differs is
    // not; the fingerprint sees a changed byte, not just a changed size.
    CHECK(anim::model_pack_fresh(back, "/no/such/model.glb"));
    CHECK(!anim::model_pack_fresh(back, path.c_str()));  // the pack file itself: other size and bytes
    {
        uint64_t size = 0, hash = 0, size2 = 0, hash2 = 0;
        CHECK(anim::source_fingerprint(path.c_str(), &size, &hash));
        const std::string edited = test_temp_path("model_pack_edited.pack");
        std::filesystem::copy_file(path, edited, std::filesystem::copy_options::overwrite_existing);
        FILE* f = std::fopen(edited.c_str(), "r+b");
        std::fseek(f, 20, SEEK_SET);
        std::fputc('!', f);
        std::fclose(f);
        CHECK(anim::source_fingerprint(edited.c_str(), &size2, &hash2));
        CHECK(size == size2 && hash != hash2);
        std::filesystem::remove(edited);
    }

    // A truncated file is refused, and so is another version.
    {
        FILE* f = std::fopen(path.c_str(), "rb");
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fclose(f);
        const std::string cut = test_temp_path("model_pack_cut.pack");
        std::filesystem::copy_file(path, cut, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::resize_file(cut, size_t(size) - 100);
        anim::ModelPack junk;
        CHECK(!anim::read_model_pack(cut.c_str(), junk, &error));
        std::printf("  truncated: %s\n", error.c_str());
        std::filesystem::remove(cut);
    }
    std::filesystem::remove(path);
}

void test_trim() {
    std::printf("trim equals the smaller cap\n");
    for (int kind = 0; kind < 3; ++kind) {
        const gfx::TextureKind k = kind == 0   ? gfx::TextureKind::Colour
                                   : kind == 1 ? gfx::TextureKind::NormalMap
                                               : gfx::TextureKind::Orm;
        const gfx::ImageData image = pattern(256, 128, false);
        gfx::TextureBudget big, small;
        big.max_size = 256;
        small.max_size = 32;
        big.compress = small.compress = true;
        gfx::PreparedTexture trimmed = gfx::prepare_texture(image, k, big, true);
        const size_t levels_before = trimmed.levels.size();
        trimmed.trim(32);
        const gfx::PreparedTexture direct = gfx::prepare_texture(image, k, small, true);
        CHECK(trimmed.width == 32 && trimmed.height == 16);
        CHECK(trimmed.levels.size() == levels_before - 3);
        CHECK(same_prepared(trimmed, direct));
        // No cap: untouched. A cap it already fits: untouched.
        gfx::PreparedTexture untouched = direct;
        untouched.trim(0);
        CHECK(same_prepared(untouched, direct));
        untouched.trim(64);
        CHECK(same_prepared(untouched, direct));
    }
    // Uncompressed too (a device without BC).
    {
        const gfx::ImageData image = pattern(64, 64, true);
        gfx::TextureBudget big, small;
        big.max_size = 64;
        small.max_size = 16;
        big.compress = small.compress = true;
        gfx::PreparedTexture trimmed = gfx::prepare_texture(image, gfx::TextureKind::Colour, big, false);
        CHECK(trimmed.format == rhi::Format::RGBA8_SRGB);
        trimmed.trim(16);
        CHECK(same_prepared(trimmed, gfx::prepare_texture(image, gfx::TextureKind::Colour, small, false)));
        CHECK(trimmed.levels.size() == 5);  // 16, 8, 4, 2, 1
    }
}

void test_real_model() {
    const std::filesystem::path glb = test_source_root() / "assets" / "embercrest.glb";
    if (!std::filesystem::exists(glb)) {
        std::printf("real model: assets/embercrest.glb not here, skipped\n");
        return;
    }
    std::printf("real model: %s\n", glb.string().c_str());
    anim::ModelPack pack;
    std::string error;
    const std::vector<uint32_t> budgets = {20000, 3000, 2500, 0, 2500};
    CHECK(anim::build_model_pack(glb.string().c_str(), budgets, 1024, pack, &error));
    if (!error.empty()) std::printf("  build: %s\n", error.c_str());
    CHECK(pack.source_size > 0);
    CHECK(pack.lods.size() == 3);  // 0 and the repeat skipped
    CHECK(pack.lod(2500) && !pack.lod(2500)->mesh.indices.empty());
    CHECK(pack.textures.size() >= 3);
    for (const anim::ModelPackTexture& t : pack.textures) {
        CHECK(t.prepared.compressed());
        CHECK(t.prepared.width <= 1024 && t.prepared.height <= 1024);
        CHECK(t.prepared.source_width >= int(t.prepared.width));
    }

    // The baked LOD is the one the runtime would compute.
    anim::Skeleton skeleton;
    anim::SkinnedMeshData mesh;
    anim::GltfLoadResult loaded = anim::load_skinned_gltf(glb.string().c_str(), skeleton, mesh);
    CHECK(loaded.ok);
    CHECK(same_mesh(mesh, pack.mesh));
    CHECK(skeleton.count() == pack.skeleton.count());
    const anim::LodResult direct = anim::simplify_skinned(mesh, 2500);
    CHECK(fingerprint(direct.mesh) == fingerprint(pack.lod(2500)->mesh));
    // And a texture at the Very low cap is the trimmed chain.
    if (!loaded.textures.empty()) {
        gfx::TextureBudget budget;
        budget.max_size = 128;
        budget.compress = true;
        const gfx::TextureKind kind = anim::texture_kind_for(mesh, loaded.texture_srgb, 0);
        const gfx::PreparedTexture at_128 = gfx::prepare_texture(loaded.textures[0], kind, budget, true);
        gfx::PreparedTexture trimmed = pack.textures[0].prepared;
        trimmed.trim(128);
        CHECK(same_prepared(trimmed, at_128));
    }

    const std::string path = test_temp_path("embercrest.pack");
    CHECK(anim::write_model_pack(path.c_str(), pack));
    anim::ModelPack back;
    CHECK(anim::read_model_pack(path.c_str(), back, &error));
    CHECK(same_mesh(back.mesh, pack.mesh));
    CHECK(back.lods.size() == pack.lods.size());
    for (size_t i = 0; i < back.lods.size() && i < pack.lods.size(); ++i) CHECK(same_mesh(back.lods[i].mesh, pack.lods[i].mesh));
    CHECK(back.textures.size() == pack.textures.size());
    for (size_t i = 0; i < back.textures.size() && i < pack.textures.size(); ++i)
        CHECK(same_prepared(back.textures[i].prepared, pack.textures[i].prepared));
    CHECK(anim::model_pack_fresh(back, glb.string().c_str()));
    std::printf("  %zu triangles, %zu textures, pack %.1f MB\n", pack.mesh.indices.size() / 3,
                pack.textures.size(), double(std::filesystem::file_size(path)) / (1024.0 * 1024.0));
    std::filesystem::remove(path);
}

}  // namespace

int main() {
    test_round_trip();
    test_trim();
    test_real_model();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
