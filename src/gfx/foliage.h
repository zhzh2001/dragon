#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "gfx/foliage_instance.h"
#include "gfx/mesh.h"
#include "gfx/pipeline.h"
#include "gfx/scene_uniforms.h"

namespace gfx {

class Device;
class ShadowMap;

// Instanced plants: one conifer mesh and one grass tuft, each drawn many
// thousands of times from an instance buffer. Trees are static (uploaded when
// planted, and they cast shadows); grass is streamed every frame around the
// camera, like the particles, and casts none -- a shadow map texel is bigger
// than a blade.
class Foliage {
public:
    bool init(Device* device, PipelineCache* pipelines, ShadowMap* shadow_map);
    void shutdown(Device& device);

    // Load-time upload; replaces the previous forest.
    void set_trees(Device& device, const std::vector<FoliageInstance>& trees);
    // Per-frame staging, before any render pass opens (it is a copy pass).
    void upload_grass(Device& device, const std::vector<FoliageInstance>& grass);

    void draw_trees(Device& device, SDL_GPURenderPass* pass, const SceneUniforms& scene);
    void draw_grass(Device& device, SDL_GPURenderPass* pass, const SceneUniforms& scene);
    void draw_trees_depth(Device& device, SDL_GPURenderPass* pass,
                          const core::Mat4& light_view_proj, float time);

    uint32_t tree_count() const { return tree_count_; }
    uint32_t grass_count() const { return grass_uploaded_; }

    // Sway amplitude in metres at the top of a plant.
    float wind = 1.0f;
    // Grass shrinks to nothing between these camera distances.
    float grass_fade_start = 90.0f;
    float grass_fade_end = 130.0f;
    // Nominal heights at scale 1, for the sway's height fraction.
    float tree_height = 13.0f;
    float tuft_height = 0.9f;

private:
    struct Params {
        core::Vec4 wind_time_fade;  // x wind, y time, z fade start, w fade end
        core::Vec4 extra;           // x nominal height
    };
    void draw(Device& device, SDL_GPURenderPass* pass, const SceneUniforms& scene,
              const Mesh& mesh, SDL_GPUBuffer* instances, uint32_t count, float fade_start,
              float fade_end, float height);
    bool ensure_grass_capacity(uint32_t count);

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    ShadowMap* shadow_map_ = nullptr;
    PipelineHandle pipeline_ = INVALID_PIPELINE;
    PipelineHandle depth_pipeline_ = INVALID_PIPELINE;

    Mesh conifer_;
    Mesh tuft_;
    SDL_GPUBuffer* trees_ = nullptr;
    uint32_t tree_count_ = 0;
    SDL_GPUBuffer* grass_ = nullptr;
    SDL_GPUTransferBuffer* grass_transfer_ = nullptr;
    uint32_t grass_capacity_ = 0;
    uint32_t grass_uploaded_ = 0;
};

// The plant meshes, generated: a conifer of stacked cones on a trunk, and a
// tuft of a few leaning blades. Vertex colour carries the material.
MeshData make_conifer_mesh();
MeshData make_grass_tuft_mesh();

}  // namespace gfx
