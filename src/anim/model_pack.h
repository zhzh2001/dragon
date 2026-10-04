#pragma once

// A model pack: one creature or prop as a retro tier uploads it, baked ahead
// of time (docs/PORTING.md, R2b).
//
// Loading a species from its glTF decodes five 4096² maps (320 MB), halves
// each down to the tier's cap, builds the mips and DXT-encodes them, then
// simplifies the 80K-triangle mesh to the level's budget. On the Mac that is
// 0.6 s and a 530 MB peak per species; on a 2005 CPU it is seconds, and in a
// 32-bit process the roster does not fit at all. A pack holds the results:
// the skeleton and clips, the authored mesh, the LOD at every model-detail
// level, and each texture's mip chain at the largest retro cap, already in
// its block format. A level below the cap is the same chain with its top
// levels dropped (gfx::PreparedTexture::trim), so one file serves every tier
// and preset; the modern tier never reads one.
//
// A pack is a build product, like a baked shader: `dragon --bake-packs`
// writes `<model>.glb.pack` beside each roster model and prop, the packagers
// run it, and `*.pack` is gitignored. The pack records its source's size and
// a hash of the file's first and last 256 KB (not its modification time,
// which a zip keeps only to two seconds); when the glTF is beside it and
// differs, the loader warns and takes the glTF route, so a re-rigged model is
// never shadowed by a stale pack. Without the glTF (a retro-only package) the
// pack is trusted.
//
// What is baked is what the runtime computes: the same `simplify_skinned`,
// `cap_image`, `build_mips` and `encode_bc`, so a frame drawn from a pack is
// byte for byte the frame drawn from the glTF (tests/test_model_pack.cpp),
// and the LOD fingerprints the Mac and Windows compare are unchanged.

#include <cstdint>
#include <string>
#include <vector>

#include "anim/animation.h"
#include "anim/skeleton.h"
#include "anim/skinned_mesh.h"
#include "core/math.h"
#include "gfx/texture.h"

namespace anim {

struct ModelPackLod {
    uint32_t max_triangles = 0;
    float error = 0.0f;
    // Empty when the authored mesh already fits this budget.
    SkinnedMeshData mesh;
};

struct ModelPackTexture {
    gfx::TextureKind kind = gfx::TextureKind::Colour;
    bool srgb = true;
    gfx::PreparedTexture prepared;
};

struct ModelPack {
    // The glTF this was built from, for the freshness check
    // (source_fingerprint).
    uint64_t source_size = 0;
    uint64_t source_hash = 0;

    // What load_skinned_gltf reports.
    Skeleton skeleton;
    SkinnedMeshData mesh;  // as authored
    std::vector<AnimationClip> animations;
    core::Vec3 bounds_min = core::Vec3::zero();
    core::Vec3 bounds_max = core::Vec3::zero();
    float bind_pose_error = 0.0f;

    std::vector<ModelPackLod> lods;
    std::vector<ModelPackTexture> textures;

    // The LOD baked for exactly `max_triangles`; null when none was.
    const ModelPackLod* lod(uint32_t max_triangles) const;
};

// `<model path>.pack`, beside the model like its profiles.
std::string model_pack_path(const std::string& model_path);

// The texture kind a model's texture `index` holds, from the materials that
// use it: a retro tier compresses each kind its own way (gfx/texture.h).
gfx::TextureKind texture_kind_for(const SkinnedMeshData& mesh, const std::vector<uint8_t>& texture_srgb,
                                  size_t index);

// Builds the pack of `model_path`'s glTF: a LOD for each budget in
// `triangle_budgets` (0 and repeats skipped), the textures capped at
// `texture_cap` and compressed. False with `error` when the glTF does not
// load.
bool build_model_pack(const char* model_path, const std::vector<uint32_t>& triangle_budgets,
                      uint32_t texture_cap, ModelPack& out, std::string* error);

bool write_model_pack(const char* path, const ModelPack& pack);
bool read_model_pack(const char* path, ModelPack& out, std::string* error);

// The size of the file at `path` and FNV-1a over its first and last 256 KB:
// what a pack records of its source. False when the file cannot be read.
bool source_fingerprint(const char* path, uint64_t* size, uint64_t* hash);

// Whether `pack` was built from the file at `source_path` as it is now. True
// when that file is absent: a package may ship the pack alone.
bool model_pack_fresh(const ModelPack& pack, const char* source_path);

}  // namespace anim
