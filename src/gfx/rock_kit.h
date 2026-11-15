#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "gfx/pipeline.h"
#include "gfx/rock_piece.h"
#include "gfx/scene_uniforms.h"

namespace gfx {

class Device;
class ShadowMap;
class Atmosphere;

// The scanned rock kit on the modern tier (docs/WORLD.md, "Rocks";
// tools/rockkit.md for the files): eight textured pieces at three levels of
// detail, drawn instanced through rock.hlsl and lit by the shared path. The
// retro tiers keep gfx::Foliage's untextured rocks.
//
// Where the trees go by ground cells, the kit goes by piece: about 3,000
// instances, so each frame every one is tested against the frustum and the
// draw distances on the CPU (a fraction of a millisecond) and the survivors
// are streamed, sorted by piece and level of detail, like the grass. A draw
// is then one call per piece and level, 24 in all, plus the shadow list.
class RockKit {
public:
    bool init(Device* device, PipelineCache* pipelines, ShadowMap* shadow_map);
    // The 24 files' meshes and the 1024 maps (the _lod1 files'), which
    // every level of a piece shares (one atlas layout). False, and nothing
    // kept, when any file is missing or wrong: the caller falls back.
    bool load(Device& device);
    bool loaded() const { return loaded_; }
    void shutdown(Device& device);

    void set_instances(int piece, const std::vector<RockInstance>& instances);
    uint32_t count() const;

    // What the frame may draw: past `reach` (a big piece's `big_reach`) a
    // piece is gone, shrinking into the ground over the last `fade_band`
    // of it; shadows within `shadow_reach` of the eye.
    struct Reach {
        float small = 1400.0f;
        float big = 2600.0f;
        float fade_band = 0.15f;
        float shadow = 600.0f;
    };
    // Culls, picks each piece's level and streams the frame's instances.
    // Before any render pass opens.
    void prepare(Device& device, const SceneUniforms& scene, const Reach& reach);
    void draw(Device& device, rhi::Pass* pass, const SceneUniforms& scene, const Atmosphere* atmosphere);
    void draw_depth(Device& device, rhi::Pass* pass, const core::Mat4& light_view_proj);
    uint32_t drawn() const { return drawn_; }
    uint32_t drawn_triangles() const { return drawn_triangles_; }

    // ---- dials (the Vegetation panel's Rocks)
    // A level of detail by distance in the piece's own radii (a 3 m
    // boulder at 25 radii is 75 m out; a 30 m cliff piece 750 m).
    float lod1_radii = 22.0f;
    float lod2_radii = 80.0f;
    // The colour: each piece's mean albedo pulled to the terrain's rock
    // (palette Rock and RockDark at `tone`, times `brightness`), by
    // `colour_match` (0 the scan as it is). The scan's contrast stays.
    float colour_match = 1.0f;
    float tone = 0.6f;
    float brightness = 1.0f;
    // The terrain's broken-rock tint on steep ground (terrain.hlsl), the
    // same warm and cool blotches across the world, so a boulder is broken
    // from the face above it.
    float terrain_tint = 1.0f;
    // Moss and grass on what faces up near the valley floor.
    float moss = 1.0f;
    // Where a piece meets the ground: the soil's colour and a darkening
    // over this many metres above it.
    float contact_height = 0.6f;
    float contact = 1.0f;

private:
    struct Level {
        rhi::Buffer* vertices = nullptr;
        rhi::Buffer* indices = nullptr;
        uint32_t index_count = 0;
    };
    struct Piece {
        Level levels[ROCK_LODS];
        rhi::Texture* base = nullptr;
        rhi::Texture* normal = nullptr;
        rhi::Texture* orm = nullptr;
        core::Vec3 mean_albedo = core::Vec3{0.232f, 0.208f, 0.180f};  // linear, measured at load
        core::Vec3 bound_centre;  // of the mesh at scale 1, in its own axes
        float bound_radius = 1.0f;
        std::vector<RockInstance> instances;
    };
    struct Range {
        uint32_t first = 0, count = 0;
    };
    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    ShadowMap* shadow_map_ = nullptr;
    PipelineHandle pipeline_ = INVALID_PIPELINE;
    PipelineHandle depth_pipeline_ = INVALID_PIPELINE;
    rhi::Sampler* sampler_ = nullptr;
    Piece pieces_[ROCK_PIECES];
    bool loaded_ = false;
    rhi::Buffer* frame_instances_ = nullptr;
    uint32_t frame_capacity_ = 0;
    Range main_[ROCK_PIECES][ROCK_LODS];
    Range shadow_[ROCK_PIECES][ROCK_LODS];
    std::vector<RockInstance> scratch_[2][ROCK_PIECES][ROCK_LODS];
    std::vector<RockInstance> frame_;
    uint32_t drawn_ = 0;
    uint32_t drawn_triangles_ = 0;
    void release(Device& device);
};

}  // namespace gfx
