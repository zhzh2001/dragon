#include "gfx/shadow_map.h"

#include "core/log.h"
#include "gfx/mesh.h"

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
    texture_ = rhi.create_texture(desc, "shadow_map");
    if (!texture_) return false;

    rhi::SamplerDesc sampler_desc;
    // Linear filtering on the depth values themselves, which softens the PCF
    // result further at no cost.
    sampler_desc.min_filter = rhi::Filter::Linear;
    sampler_desc.mag_filter = rhi::Filter::Linear;
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

    mesh_pipeline_ = pipelines_->create(make_shadow_mesh_desc(format_));
    LOG_INFO("shadow map: %ux%u %s", resolution_, resolution_,
             format_ == rhi::Format::D32F ? "D32F" : "D16");
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

    // Centre the volume ahead of the camera rather than on it, so most of the
    // budget goes where the viewer is looking instead of behind them.
    Vec3 centre = camera.position + camera.forward() * (extent * 0.45f);

    // Snap the centre to the shadow map's texel grid in light space. Without
    // this, the map jitters as the camera moves and shadow edges crawl.
    const float texels_per_metre = float(resolution_) / (extent * 2.0f);
    Vec3 light_up = std::fabs(to_sun.y) > 0.99f ? Vec3::unit_z() : Vec3::up();
    Mat4 snap_view = core::look_at(centre, centre - to_sun, light_up);
    Vec3 centre_in_light = core::transform_point(snap_view, centre);
    centre_in_light.x = std::floor(centre_in_light.x * texels_per_metre) / texels_per_metre;
    centre_in_light.y = std::floor(centre_in_light.y * texels_per_metre) / texels_per_metre;
    centre = core::transform_point(core::inverse(snap_view), centre_in_light);

    // Pull the light back far enough that mountains behind the centre still
    // cast into it.
    const float distance = extent * 2.2f;
    Vec3 eye = centre + to_sun * distance;
    Mat4 view = core::look_at(eye, centre, light_up);
    Mat4 projection = core::ortho(-extent, extent, -extent, extent, 1.0f, distance * 2.0f);
    light_view_proj_ = projection * view;
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
