#pragma once

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

    bool wireframe = false;

private:
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle sky_ = INVALID_PIPELINE;
    PipelineHandle terrain_ = INVALID_PIPELINE;
    PipelineHandle terrain_wireframe_ = INVALID_PIPELINE;
    PipelineHandle mesh_ = INVALID_PIPELINE;
    ShadowMap* shadow_map_ = nullptr;
    SceneUniforms scene_ = {};
};

}  // namespace gfx
