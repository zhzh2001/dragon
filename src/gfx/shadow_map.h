#pragma once

#include "core/math.h"
#include "gfx/camera.h"
#include "gfx/device.h"
#include "gfx/pipeline.h"

namespace gfx {

// The directional shadow map, fitted to boxes that follow the camera.
//
// On the modern tier it is cascaded (docs/FIDELITY.md, F4): three boxes, 75 m,
// 380 m and 1.9 km about a point ahead of the camera, each a quadrant of one
// atlas (drawn through a viewport, rhi::Device::set_viewport), so the dragon's
// own shadow has 7 cm texels and a forest's reaches 4 km. The shaders take
// the finest box a point is in (scene_common.hlsl). The terrain's shadow past
// all of them comes from the heightfield (shaders/terrain_sun.hlsl). A retro
// tier keeps one map over `extent`.
//
// The map uses a conventional [0, 1] depth range with a LESS compare, unlike the
// main pass's reversed-Z. It is an independent pass with its own texture, and
// standard depth is easier to reason about when debugging shadow acne.
class ShadowMap {
public:
    // Again with another resolution after shutdown() to resize (the Shadows
    // setting).
    bool init(Device* device, PipelineCache* pipelines, uint32_t resolution = 4096);
    void shutdown(Device& device);

    // Recomputes the light matrix for this frame. `sun_direction` points from
    // the surface toward the sun.
    void update(const Camera& camera, core::Vec3 sun_direction);

    // Depth-only pass. Returns nullptr if the shadow map is unavailable.
    // Draw each cascade after begin_cascade(i), i < cascades(): it points the
    // pass at that cascade's quadrant and makes light_view_proj() and
    // active_extent() that cascade's.
    rhi::Pass* begin_pass(Device& device);
    int cascades() const { return cascades_; }
    void begin_cascade(rhi::Device& rhi, rhi::Pass* pass, int cascade);
    float active_extent() const { return cascades_ > 1 ? cascade_extent[active_] : extent; }
    // The cascades into the scene block (cascade_view_proj, cascade_params);
    // nothing on a single map.
    void apply(struct SceneUniforms& scene) const;
    static constexpr int MAX_CASCADES = 3;
    // Each cascade's half-width in metres, finest first.
    float cascade_extent[MAX_CASCADES] = {75.0f, 380.0f, 1900.0f};

    // The pipeline that writes depth for opaque meshes.
    rhi::Pipeline* mesh_pipeline() const;

    rhi::Texture* texture() const { return texture_; }
    rhi::Sampler* sampler() const { return sampler_; }
    const core::Mat4& light_view_proj() const { return cascades_ > 1 ? cascade_view_proj_[active_] : light_view_proj_; }
    uint32_t resolution() const { return resolution_; }
    rhi::Format format() const { return format_; }

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
    // No sampled depth on this device: a stand-in texture, never drawn into.
    bool available() const { return !unavailable_; }

private:
    bool unavailable_ = false;
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle mesh_pipeline_ = INVALID_PIPELINE;
    rhi::Texture* texture_ = nullptr;
    rhi::Sampler* sampler_ = nullptr;
    uint32_t resolution_ = 0;
    float base_extent_ = 0.0f;  // the extent at full resolution
    rhi::Format format_ = rhi::Format::D32F;
    core::Mat4 light_view_proj_;
    int cascades_ = 1;
    int active_ = 0;
    core::Mat4 cascade_view_proj_[MAX_CASCADES];
    // One box's light matrix: `half` metres either side of a point ahead of
    // the camera, snapped to its texel grid of `texels` across.
    core::Mat4 fit(const Camera& camera, core::Vec3 to_sun, float half, uint32_t texels) const;
};

}  // namespace gfx
