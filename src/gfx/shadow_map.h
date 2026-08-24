#pragma once

#include "core/math.h"
#include "gfx/camera.h"
#include "gfx/device.h"
#include "gfx/pipeline.h"

namespace gfx {

// Single directional shadow map, fitted to a box that follows the camera.
//
// Not cascaded: one map covering a few hundred metres around the viewer is
// enough for a valley, and it keeps the pass count down. The trade-off is that
// distant terrain has no shadows, which fog hides anyway.
//
// The map uses a conventional [0, 1] depth range with a LESS compare, unlike the
// main pass's reversed-Z. It is an independent pass with its own texture, and
// standard depth is easier to reason about when debugging shadow acne.
class ShadowMap {
public:
    bool init(Device* device, PipelineCache* pipelines, uint32_t resolution = 4096);
    void shutdown(Device& device);

    // Recomputes the light matrix for this frame. `sun_direction` points from
    // the surface toward the sun.
    void update(const Camera& camera, core::Vec3 sun_direction);

    // Depth-only pass. Returns nullptr if the shadow map is unavailable.
    SDL_GPURenderPass* begin_pass(Device& device);

    // The pipeline that writes depth for opaque meshes.
    SDL_GPUGraphicsPipeline* mesh_pipeline() const;

    SDL_GPUTexture* texture() const { return texture_; }
    SDL_GPUSampler* sampler() const { return sampler_; }
    const core::Mat4& light_view_proj() const { return light_view_proj_; }
    uint32_t resolution() const { return resolution_; }
    SDL_GPUTextureFormat format() const { return format_; }

    // Half-width of the shadowed region, in metres. Larger covers more of the
    // valley at the cost of resolution.
    float extent = 900.0f;
    // Depth offset applied when comparing, in light-space depth units. Too small
    // gives acne on lit slopes, too large detaches shadows from their caster.
    float depth_bias = 0.0022f;
    // How dark a fully shadowed surface gets. Never 0: fully black shadows lose
    // all terrain shape.
    float strength = 0.78f;
    bool enabled = true;

private:
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle mesh_pipeline_ = INVALID_PIPELINE;
    SDL_GPUTexture* texture_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
    uint32_t resolution_ = 0;
    SDL_GPUTextureFormat format_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    core::Mat4 light_view_proj_;
};

}  // namespace gfx
