#pragma once

#include "anim/skinned_mesh.h"
#include "gfx/device.h"
#include "gfx/mesh.h"
#include "gfx/pipeline.h"
#include "gfx/scene_uniforms.h"
#include "gfx/shadow_map.h"

namespace gfx {

// Draws the world: sky, then opaque geometry. Owns the pipelines and the
// per-frame uniform block, so gameplay code hands it a camera and a mesh and
// does not touch the GPU API.
class WorldRenderer {
public:
    bool init(Device* device, PipelineCache* pipelines);
    void shutdown(Device& device);

    // Call once per frame before drawing, then draw_* inside the main pass.
    void set_scene(const SceneUniforms& scene) { scene_ = scene; }
    const SceneUniforms& scene() const { return scene_; }

    // Fullscreen sky. Draw before anything else: it writes no depth, so opaque
    // geometry simply covers it.
    void draw_sky(Device& device, SDL_GPURenderPass* pass);

    // Terrain samples the shadow map, so the renderer needs to know about it.
    void set_shadow_map(ShadowMap* shadow_map) { shadow_map_ = shadow_map; }

    void draw_terrain(Device& device, SDL_GPURenderPass* pass, const Mesh& mesh);

    // Lit opaque geometry with per-vertex albedo and procedural deformation.
    void draw_mesh(Device& device, SDL_GPURenderPass* pass, const Mesh& mesh,
                   const ModelUniforms& model);

    // Depth-only draw for the shadow pass. Applies the same deformation, so a
    // flapping wing casts a flapping shadow.
    void draw_mesh_depth(Device& device, SDL_GPURenderPass* pass, const Mesh& mesh,
                         const core::Mat4& light_view_proj, const ModelUniforms& model);

    // Skinned geometry. `joints` are skinning matrices, at most anim::MAX_JOINTS.
    // `textures` are base-colour textures indexed by each submesh; pass an empty
    // list to draw untextured.
    void draw_skinned(Device& device, SDL_GPURenderPass* pass, const anim::SkinnedMesh& mesh,
                      const ModelUniforms& model, const std::vector<core::Mat4>& joints,
                      const std::vector<SDL_GPUTexture*>& textures = {},
                      SDL_GPUSampler* sampler = nullptr);
    void draw_skinned_depth(Device& device, SDL_GPURenderPass* pass,
                            const anim::SkinnedMesh& mesh, const core::Mat4& light_view_proj,
                            const ModelUniforms& model, const std::vector<core::Mat4>& joints);

    bool wireframe = false;

private:
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle sky_ = INVALID_PIPELINE;
    PipelineHandle terrain_ = INVALID_PIPELINE;
    PipelineHandle terrain_wireframe_ = INVALID_PIPELINE;
    PipelineHandle mesh_ = INVALID_PIPELINE;
    PipelineHandle skinned_ = INVALID_PIPELINE;
    PipelineHandle skinned_depth_ = INVALID_PIPELINE;
    ShadowMap* shadow_map_ = nullptr;
    // 1x1 white, for submeshes with no base-colour texture. A sampler slot must
    // be filled, and the shadow map cannot serve: it is a depth texture and the
    // shader declares a colour one.
    SDL_GPUTexture* white_ = nullptr;
    SDL_GPUSampler* white_sampler_ = nullptr;
    SceneUniforms scene_ = {};
};

}  // namespace gfx
