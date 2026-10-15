#pragma once

#include "anim/skinned_mesh.h"
#include "gfx/atmosphere.h"
#include "gfx/device.h"
#include "gfx/mesh.h"
#include "gfx/noise_lattice.h"
#include "gfx/pipeline.h"
#include "gfx/scene_uniforms.h"
#include "gfx/shadow_map.h"
#include "gfx/taa.h"

namespace gfx {

// Draws the world: sky, then opaque geometry. Owns the pipelines and the
// per-frame uniform block, so gameplay code hands it a camera and a mesh and
// does not touch the GPU API.
// Per-map switches for the model's material set, so each can be turned off and
// compared in place. A normal or roughness map is very hard to judge without an
// A/B: the eye accepts almost any plausible surface until it sees the other one.
struct MaterialToggles {
    bool base_colour = true;
    bool normal_map = true;
    bool orm_map = true;
};

class WorldRenderer {
public:
    // Models smaller than `pixels` across their radius, in a world target
    // `height` pixels tall, are skipped (RenderTier::min_draw_pixels; 0: off),
    // and so are those wholly past `view` metres (ContentBudget::view_distance).
    void set_detail_cull(float pixels, float height, float view) {
        min_draw_pixels_ = pixels;
        view_height_ = height;
        view_distance_ = view;
    }
    bool init(Device* device, PipelineCache* pipelines);
    void shutdown(Device& device);

    // Call once per frame before drawing, then draw_* inside the main pass.
    void set_scene(const SceneUniforms& scene) { scene_ = scene; }
    const SceneUniforms& scene() const { return scene_; }

    // Fullscreen sky. Draw before anything else: it writes no depth, so opaque
    // geometry simply covers it.
    void draw_sky(Device& device, rhi::Pass* pass);
    // Fixed function: the sky dome's per-vertex colours from `scene`'s sun
    // and sky (gfx/prelit.h), once; the dome is not drawn until this runs.
    void bake_sky_dome(Device& device, const SceneUniforms& scene);

    // Terrain samples the shadow map, so the renderer needs to know about it.
    void set_shadow_map(ShadowMap* shadow_map) { shadow_map_ = shadow_map; }
    // Every lit world shader reads the atmosphere's atlas on the modern tier.
    void set_atmosphere(const Atmosphere* atmosphere) { atmosphere_ = atmosphere; }

    // Debug only; every map is on in normal use.
    void set_material_toggles(const MaterialToggles& toggles) { material_toggles_ = toggles; }

    // With `chunks` (gfx::chunk_mesh), only the chunks the camera sees.
    void draw_terrain(Device& device, rhi::Pass* pass, const Mesh& mesh,
                      const std::vector<MeshChunk>* chunks = nullptr);
    // The water surface quad (see water.hlsl).
    // With `chunks`, only those in view (the water plane spans the skirt).
    void draw_water(Device& device, rhi::Pass* pass, const Mesh& mesh, const std::vector<MeshChunk>* chunks = nullptr);

    // Lit opaque geometry with per-vertex albedo and procedural deformation.
    void draw_mesh(Device& device, rhi::Pass* pass, const Mesh& mesh,
                   const ModelUniforms& model);

    // Depth-only draw for the shadow pass. Applies the same deformation, so a
    // flapping wing casts a flapping shadow.
    void draw_mesh_depth(Device& device, rhi::Pass* pass, const Mesh& mesh,
                         const core::Mat4& light_view_proj, const ModelUniforms& model,
                         const std::vector<MeshChunk>* chunks = nullptr);

    // Skinned geometry. `joints` are skinning matrices, at most anim::MAX_JOINTS.
    // `textures` are base-colour textures indexed by each submesh; pass an empty
    // list to draw untextured.
    void draw_skinned(Device& device, rhi::Pass* pass, const anim::SkinnedMesh& mesh,
                      const ModelUniforms& model, const std::vector<core::Mat4>& joints,
                      const std::vector<rhi::Texture*>& textures = {},
                      rhi::Sampler* sampler = nullptr);
    // Into the temporal resolve's velocity pass (gfx/taa.h): the creature's
    // screen motion since last frame, from `prev_model` and the matrices.
    void draw_skinned_velocity(Device& device, rhi::Pass* pass, const anim::SkinnedMesh& mesh,
                               const ModelUniforms& model, const core::Mat4& prev_model,
                               const std::vector<core::Mat4>& joints, const std::vector<core::Mat4>& prev_joints,
                               const Taa::VelocityMatrices& matrices);
    void draw_skinned_depth(Device& device, rhi::Pass* pass,
                            const anim::SkinnedMesh& mesh, const core::Mat4& light_view_proj,
                            const ModelUniforms& model, const std::vector<core::Mat4>& joints);

    bool wireframe = false;

private:
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle sky_ = INVALID_PIPELINE;
    // The fixed-function tier's sky: a unit sphere about the camera, coloured
    // per vertex (sky.hlsl, FIXED_FUNCTION), in place of the fullscreen
    // triangle a pixel stage cannot shade.
    Mesh sky_dome_;
    // ... and the sky baked into a lat-long map the dome wears (16 x 8
    // vertices carry it; the texture carries the gradient and the sun).
    rhi::Texture* sky_map_ = nullptr;
    rhi::Sampler* sky_sampler_ = nullptr;
    PipelineHandle terrain_ = INVALID_PIPELINE;
    PipelineHandle terrain_wireframe_ = INVALID_PIPELINE;
    PipelineHandle water_ = INVALID_PIPELINE;
    PipelineHandle mesh_ = INVALID_PIPELINE;
    PipelineHandle skinned_ = INVALID_PIPELINE;
    PipelineHandle skinned_depth_ = INVALID_PIPELINE;
    PipelineHandle skinned_velocity_ = INVALID_PIPELINE;
    ShadowMap* shadow_map_ = nullptr;
    const Atmosphere* atmosphere_ = nullptr;
    MaterialToggles material_toggles_;
    // 1x1 white, for submeshes with no base-colour texture. A sampler slot must
    // be filled, and the shadow map cannot serve: it is a depth texture and the
    // shader declares a colour one.
    rhi::Texture* white_ = nullptr;
    // The terrain's tiling detail (assets/textures/terrain_detail.png: R rock,
    // G grass, B dirt, A snow), and a mid-grey stand-in that leaves the
    // ground as it was when the file is missing.
    rhi::Texture* terrain_detail_ = nullptr;
    rhi::Texture* terrain_rock_ = nullptr;
    rhi::Texture* terrain_scree_ = nullptr;
    rhi::Texture* ground_scans_[3] = {nullptr, nullptr, nullptr};
    rhi::Texture* neutral_ = nullptr;
    // The baked noise lattice, on a tier that bakes it (null otherwise).
    const NoiseLattice* noise_lattice_ = nullptr;
public:
    void set_terrain_detail(rhi::Texture* texture) { terrain_detail_ = texture; }
    // The modern tier's scanned rock and scree (tools/build_terrain_rock.py),
    // in the terrain's slots 4 and 5.
    void set_terrain_scans(rhi::Texture* rock, rhi::Texture* scree) {
        terrain_rock_ = rock;
        terrain_scree_ = scree;
    }
    // The ground's scans (terrain.hlsl): meadow grass, pine litter, gravel.
    void set_ground_scans(rhi::Texture* grass, rhi::Texture* forest, rhi::Texture* gravel) {
        ground_scans_[0] = grass;
        ground_scans_[1] = forest;
        ground_scans_[2] = gravel;
    }
    void set_noise_lattice(const NoiseLattice* lattice) { noise_lattice_ = lattice; }
    // Whether draw_skinned would draw `mesh` at `model` in the main view:
    // asked before posing a skeleton nobody will see.
    bool skinned_in_view(const anim::SkinnedMesh& mesh, const ModelUniforms& model) const {
        return in_view(model, mesh.bound_centre(), mesh.bound_radius(), 1.5f);
    }
private:
    rhi::Sampler* white_sampler_ = nullptr;
    float camera_distance(const ModelUniforms& model) const;
    bool in_view(const ModelUniforms& model, core::Vec3 centre, float radius, float pad) const;
    float min_draw_pixels_ = 0.0f;
    float view_height_ = 720.0f;
    float view_distance_ = 0.0f;
    SceneUniforms scene_ = {};
};

}  // namespace gfx
