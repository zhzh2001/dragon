#pragma once

#include "core/math.h"

#include <cmath>
#include "gfx/camera.h"
#include "gfx/device.h"
#include "gfx/pipeline.h"

namespace gfx {

// Which casters a cascade needs (ShadowMap::caster_cull). Light-space depth
// is ndc z, linear in the orthographic projection.
struct ShadowCasterCull {
    // The cascade's casters: its receivers' depth window, widened toward the
    // sun by how far a caster's shadow runs down the light.
    float z_min = -1e30f;
    float z_max = 1e30f;
    float per_metre = 0.0f;  // metres to ndc z
    // The next finer cascade's receiver box. A caster that sits inside it
    // with its whole shadow (which runs along z only) shades nothing this
    // cascade is sampled for.
    bool has_inner = false;
    core::Mat4 inner_view_proj;
    float inner_xy = 0.0f;  // ndc, as the shaders' margin
    float inner_z_min = 0.0f, inner_z_max = 0.0f, inner_per_metre = 0.0f;
    float reach = 0.0f;     // metres a shadow runs down the light

    // Whether a sphere of casters at `centre` can shade this cascade.
    bool needed(const core::Mat4& view_proj, core::Vec3 centre, float radius) const {
        const float z = core::transform_point(view_proj, centre).z;
        const float rz = radius * per_metre;
        if (z + rz < z_min || z - rz > z_max) return false;
        if (!has_inner) return true;
        const core::Vec3 p = core::transform_point(inner_view_proj, centre);
        const float r = radius * inner_per_metre;
        // The ndc x and y of the box span its full width: 2 / (2 half).
        const float rxy = radius * inner_xy_per_metre;
        return !(std::fabs(p.x) + rxy < inner_xy && std::fabs(p.y) + rxy < inner_xy && p.z - r > inner_z_min &&
                 p.z + r + reach * inner_per_metre < inner_z_max);
    }
    float inner_xy_per_metre = 0.0f;
};

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
    // A point takes a cascade only within this many half-widths of the box's
    // centre along the light (scene_common.hlsl, cascade_visibility). The box
    // is pulled 1.5 km toward the sun, and under a 19 degree sun the finest
    // one's 3 km of depth was a strip 150 m wide and 3 km long across the
    // ground, every crown in it drawn whole: 4 ms a cascade in a forest at
    // 1080p. Kept to the window, a cascade is a box again.
    static constexpr float RECEIVER_DEPTH = 2.0f;
    // Which casters the active cascade needs; `reach` is how far a caster's
    // shadow runs down the light, in metres. On a single map, all of them.
    ShadowCasterCull caster_cull(float reach) const;

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
