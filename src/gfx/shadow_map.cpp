#include "gfx/shadow_map.h"

#include <algorithm>

#include "core/log.h"
#include "gfx/mesh.h"
#include "gfx/render_tier.h"
#include "gfx/scene_uniforms.h"

using core::Mat4;
using core::Vec3;

namespace gfx {
namespace {

PipelineDesc make_shadow_mesh_desc(rhi::Format depth_format) {
    PipelineDesc desc;
    desc.name = "shadow_mesh";
    desc.shader = "shadow_depth";
    desc.vertex_buffers = Mesh::buffer_descriptions();
    desc.vertex_attributes = Mesh::attributes();
    desc.no_color_target = true;
    desc.depth_format = depth_format;
    // Conventional depth here, unlike the reversed-Z main pass.
    desc.depth_compare = rhi::Compare::Less;
    desc.depth_test = true;
    desc.depth_write = true;
    // Culling front faces is the usual trick to push acne behind the geometry,
    // but terrain is an open heightfield -- culling either side would punch
    // holes in the shadows. So we keep both and rely on the depth bias.
    desc.cull = rhi::Cull::None;
    return desc;
}

}  // namespace

bool ShadowMap::init(Device* device, PipelineCache* pipelines, uint32_t resolution) {
    pipelines_ = pipelines;
    resolution_ = resolution;
    // A map smaller than 4096 (the Shadows setting, gfx/graphics_settings.h)
    // covers less, to keep about a metre a texel: at the full extent a 512
    // map's 3.5 m texels lose every tree and the dragon. Ridges past the
    // smaller extent go unshadowed; the extent slider still moves it.
    if (base_extent_ <= 0.0f) base_extent_ = extent;
    extent = resolution_ < 4096 ? std::min(base_extent_, float(resolution_) * 0.5f) : base_extent_;
    // Cascaded on the modern tier, where the shaders can choose among them.
    cascades_ = active_tier().tier == Tier::Modern ? MAX_CASCADES : 1;

    rhi::Device& rhi = device->rhi();
    if (!rhi.supports_format(format_, rhi::TEXTURE_DEPTH_TARGET | rhi::TEXTURE_SAMPLED)) {
        format_ = rhi::Format::D16;
        LOG_WARN("shadow map falling back to D16_UNORM");
    }

    rhi::TextureDesc desc;
    desc.width = resolution_;
    desc.height = resolution_;
    desc.format = format_;
    desc.usage = rhi::TEXTURE_DEPTH_TARGET | rhi::TEXTURE_SAMPLED;
    unavailable_ = !rhi.supports_format(format_, desc.usage);
    if (unavailable_) {
        // No depth target can be sampled (D3D11 at feature level 9_1): the
        // settings hold the shadows off (DeviceLimits::sampled_depth), and
        // the shaders' slot gets a 1x1 white texture -- "unshadowed" -- so
        // the bindings stay what every pipeline was built against.
        desc = rhi::TextureDesc{};
        desc.width = desc.height = 1;
        desc.format = rhi::Format::RGBA8;
        desc.usage = rhi::TEXTURE_SAMPLED;
        texture_ = rhi.create_texture(desc, "shadow_map_unavailable");
        const uint8_t white[4] = {255, 255, 255, 255};
        if (!texture_ || !rhi.upload_texture(texture_, white, 1, 1, false)) return false;
        LOG_INFO("shadow map: unavailable on this device (no sampled depth), off");
    } else {
        texture_ = rhi.create_texture(desc, "shadow_map");
        if (!texture_) return false;
    }

    rhi::SamplerDesc sampler_desc;
    // Linear filtering on the depth values themselves, which softens the PCF
    // result further at no cost -- except on SM2, whose cards cannot filter
    // the 32-bit float map D3D9 keeps it in: the tier point-samples on every
    // backend, so it looks the same everywhere it runs.
    const rhi::Filter filter = active_tier().vertex_lighting ? rhi::Filter::Nearest : rhi::Filter::Linear;
    sampler_desc.min_filter = filter;
    sampler_desc.mag_filter = filter;
    sampler_desc.mip_mode = rhi::MipMode::Nearest;
    // Clamping to the border would be ideal; clamp-to-edge plus an explicit
    // in-bounds test in the shader achieves the same thing portably.
    sampler_desc.address_u = sampler_desc.address_v = sampler_desc.address_w = rhi::Address::Clamp;
    sampler_desc.max_lod = 0.0f;
    sampler_ = rhi.create_sampler(sampler_desc);
    if (!sampler_) {
        rhi.destroy(texture_);
        texture_ = nullptr;
        return false;
    }

    // Not built where the map cannot be: on the Pi 1B each pipeline is a
    // tenth of a second of its start-up.
    if (!unavailable_) mesh_pipeline_ = pipelines_->create(make_shadow_mesh_desc(format_));
    if (!unavailable_) {
        LOG_INFO("shadow map: %ux%u %s", resolution_, resolution_, format_ == rhi::Format::D32F ? "D32F" : "D16");
    }
    return true;
}

void ShadowMap::shutdown(Device& device) {
    device.rhi().destroy(texture_);
    device.rhi().destroy(sampler_);
    texture_ = nullptr;
    sampler_ = nullptr;
}

void ShadowMap::update(const Camera& camera, Vec3 sun_direction) {
    Vec3 to_sun = core::normalize_or(sun_direction, Vec3::up());
    if (cascades_ > 1) {
        // Each cascade a quadrant of the atlas: half its texels across.
        for (int c = 0; c < cascades_; ++c) cascade_view_proj_[c] = fit(camera, to_sun, cascade_extent[c], resolution_ / 2);
        light_view_proj_ = cascade_view_proj_[cascades_ - 1];
        return;
    }
    light_view_proj_ = fit(camera, to_sun, extent, resolution_);
}

void ShadowMap::begin_cascade(rhi::Device& rhi, rhi::Pass* pass, int cascade) {
    active_ = cascade;
    if (cascades_ <= 1 || !pass) return;
    const float half = float(resolution_) * 0.5f;
    rhi.set_viewport(pass, float(cascade % 2) * half, float(cascade / 2) * half, half, half);
}

namespace {
// One cascade's receiver window in its light-space depth. fit(): the eye
// `distance` back from the centre, the depth range from 1 m to twice that,
// linear in an orthographic projection.
void receiver_window(float half, float& z_min, float& z_max, float& per_metre) {
    const float distance = std::max(half * 2.2f, 1500.0f);
    per_metre = 1.0f / (distance * 2.0f - 1.0f);
    const float centre = (distance - 1.0f) * per_metre;
    const float window = ShadowMap::RECEIVER_DEPTH * half;
    z_min = centre - window * per_metre;
    z_max = centre + window * per_metre;
}
}  // namespace

ShadowCasterCull ShadowMap::caster_cull(float reach) const {
    ShadowCasterCull cull;
    if (cascades_ <= 1) return cull;
    receiver_window(cascade_extent[active_], cull.z_min, cull.z_max, cull.per_metre);
    cull.z_min -= reach * cull.per_metre;
    cull.reach = reach;
    if (active_ > 0) {
        const float inner = cascade_extent[active_ - 1];
        cull.has_inner = true;
        cull.inner_view_proj = cascade_view_proj_[active_ - 1];
        cull.inner_xy = 0.97f;  // the shaders' margin (cascade_visibility)
        cull.inner_xy_per_metre = 1.0f / inner;
        receiver_window(inner, cull.inner_z_min, cull.inner_z_max, cull.inner_per_metre);
    }
    return cull;
}

void ShadowMap::apply(SceneUniforms& scene) const {
    if (cascades_ <= 1) return;
    for (int c = 0; c < cascades_; ++c) scene.cascade_view_proj[c] = cascade_view_proj_[c];
    // x the count; y, z, w each cascade's texel in metres.
    const float texels = float(resolution_) * 0.5f;
    scene.cascade_params = core::Vec4{float(cascades_), cascade_extent[0] * 2.0f / texels,
                                      cascade_extent[1] * 2.0f / texels, cascade_extent[2] * 2.0f / texels};
}

Mat4 ShadowMap::fit(const Camera& camera, Vec3 to_sun, float half, uint32_t texels) const {
    const float extent = half;
    const uint32_t resolution_ = texels;
    // Centre the volume ahead of the camera rather than on it, so most of the
    // budget goes where the viewer is looking instead of behind them.
    Vec3 centre = camera.position + camera.forward() * (extent * 0.45f);

    // Snap the centre to the shadow map's texel grid in light space. Without
    // this, the map jitters as the camera moves and shadow edges crawl.
    // The light space must be anchored at a fixed point (the world origin):
    // anchored at the centre itself, as it first was, the centre always sat
    // at its own origin, the snap rounded zero to zero, and every tree's
    // shadow shimmered in flight (the playtest of 2026-10-13).
    const float texels_per_metre = float(resolution_) / (extent * 2.0f);
    Vec3 light_up = std::fabs(to_sun.y) > 0.99f ? Vec3::unit_z() : Vec3::up();
    Mat4 snap_view = core::look_at(Vec3{}, -to_sun, light_up);
    Vec3 centre_in_light = core::transform_point(snap_view, centre);
    centre_in_light.x = std::floor(centre_in_light.x * texels_per_metre) / texels_per_metre;
    centre_in_light.y = std::floor(centre_in_light.y * texels_per_metre) / texels_per_metre;
    centre = core::transform_point(core::inverse(snap_view), centre_in_light);

    // Pull the light back far enough that mountains behind the centre still
    // cast into it.
    // At least 1.5 km back, so the finest cascade's light is not inside a
    // tall caster (the map is 32-bit float depth; the range costs nothing).
    const float distance = std::max(extent * 2.2f, 1500.0f);
    Vec3 eye = centre + to_sun * distance;
    Mat4 view = core::look_at(eye, centre, light_up);
    Mat4 projection = core::ortho(-extent, extent, -extent, extent, 1.0f, distance * 2.0f);
    return projection * view;
}

rhi::Pass* ShadowMap::begin_pass(Device& device) {
    if (!texture_) return nullptr;
    rhi::PassDesc pass;
    pass.depth = texture_;
    pass.clear_depth = true;
    pass.clear_depth_value = 1.0f;  // conventional depth: far is 1
    pass.keep_depth = true;         // the world passes sample it
    return device.rhi().begin_pass(pass);
}

rhi::Pipeline* ShadowMap::mesh_pipeline() const {
    return pipelines_ ? pipelines_->get(mesh_pipeline_) : nullptr;
}

}  // namespace gfx
