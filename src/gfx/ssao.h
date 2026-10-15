#pragma once

// Screen-space ambient occlusion (docs/FIDELITY.md, F6), on the modern tier:
// shaders/post_ssao.hlsl at half resolution from the main pass's depth, then
// a depth-aware blur; the composite multiplies the result into the scene
// (post_composite.hlsl). Run after the main pass, before the post stack.

#include "gfx/camera.h"
#include "gfx/pipeline.h"

namespace gfx {

class Device;

struct SsaoSettings {
    bool enabled = true;
    float radius = 3.0f;      // metres
    float intensity = 3.0f;
    float strength = 0.85f;   // how far the composite darkens toward it
    // How far above a surface's tangent plane (as a sine) a sample must
    // stand to occlude it: the terrain's 6 m facets meet at shallow folds,
    // and at 0.06 every fold read as a crease, a grid over each far slope.
    float bias = 0.25f;
    float fade_start = 350.0f;
    float fade_end = 900.0f;
};

class Ssao {
public:
    bool init(Device* device, PipelineCache* pipelines);
    void shutdown();
    bool available() const { return available_; }
    // The blurred occlusion, or null when off or unavailable.
    rhi::Texture* run(Device& device, const Camera& camera, float aspect, const SsaoSettings& settings);

private:
    bool ensure_targets(Device& device, uint32_t width, uint32_t height);
    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle ssao_ = INVALID_PIPELINE;
    PipelineHandle blur_ = INVALID_PIPELINE;
    rhi::Sampler* point_ = nullptr;
    rhi::Sampler* linear_ = nullptr;
    rhi::Texture* raw_ = nullptr;
    rhi::Texture* blurred_ = nullptr;
    uint32_t width_ = 0, height_ = 0;
    bool available_ = false;
};

}  // namespace gfx
