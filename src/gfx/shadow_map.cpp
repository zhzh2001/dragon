#include "gfx/shadow_map.h"

#include "core/log.h"
#include "gfx/mesh.h"

using core::Mat4;
using core::Vec3;

namespace gfx {
namespace {

PipelineDesc make_shadow_mesh_desc(SDL_GPUTextureFormat depth_format) {
    PipelineDesc desc;
    desc.name = "shadow_mesh";
    desc.shader_path = "shadow_depth.msl";
    desc.vs_uniform_buffers = 1;
    desc.vertex_buffers = Mesh::buffer_descriptions();
    desc.vertex_attributes = Mesh::attributes();
    desc.no_color_target = true;
    desc.depth_format = depth_format;
    // Conventional depth here, unlike the reversed-Z main pass.
    desc.depth_compare = SDL_GPU_COMPAREOP_LESS;
    desc.depth_test = true;
    desc.depth_write = true;
    // Culling front faces is the usual trick to push acne behind the geometry,
    // but terrain is an open heightfield -- culling either side would punch
    // holes in the shadows. So we keep both and rely on the depth bias.
    desc.cull = SDL_GPU_CULLMODE_NONE;
    return desc;
}

}  // namespace

bool ShadowMap::init(Device* device, PipelineCache* pipelines, uint32_t resolution) {
    pipelines_ = pipelines;
    resolution_ = resolution;

    if (!SDL_GPUTextureSupportsFormat(device->gpu(), format_, SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET |
                                          SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        format_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
        LOG_WARN("shadow map falling back to D16_UNORM");
    }

    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = format_;
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = resolution_;
    info.height = resolution_;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture_ = SDL_CreateGPUTexture(device->gpu(), &info);
    if (!texture_) return SDL_FAIL("SDL_CreateGPUTexture(shadow_map)");
    SDL_SetGPUTextureName(device->gpu(), texture_, "shadow_map");

    SDL_GPUSamplerCreateInfo sampler_info = {};
    // Linear filtering on the depth values themselves, which softens the PCF
    // result further at no cost.
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    // Clamping to the border would be ideal; clamp-to-edge plus an explicit
    // in-bounds test in the shader achieves the same thing portably.
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(device->gpu(), &sampler_info);
    if (!sampler_) {
        SDL_ReleaseGPUTexture(device->gpu(), texture_);
        texture_ = nullptr;
        return SDL_FAIL("SDL_CreateGPUSampler(shadow_map)");
    }

    mesh_pipeline_ = pipelines_->create(make_shadow_mesh_desc(format_));
    LOG_INFO("shadow map: %ux%u %s", resolution_, resolution_,
             format_ == SDL_GPU_TEXTUREFORMAT_D32_FLOAT ? "D32F" : "D16");
    return true;
}

void ShadowMap::shutdown(Device& device) {
    if (texture_) SDL_ReleaseGPUTexture(device.gpu(), texture_);
    if (sampler_) SDL_ReleaseGPUSampler(device.gpu(), sampler_);
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

SDL_GPURenderPass* ShadowMap::begin_pass(Device& device) {
    if (!texture_) return nullptr;

    SDL_GPUDepthStencilTargetInfo depth = {};
    depth.texture = texture_;
    depth.clear_depth = 1.0f;  // conventional depth: far is 1
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.cycle = true;
    return SDL_BeginGPURenderPass(device.cmd(), nullptr, 0, &depth);
}

SDL_GPUGraphicsPipeline* ShadowMap::mesh_pipeline() const {
    return pipelines_ ? pipelines_->get(mesh_pipeline_) : nullptr;
}

}  // namespace gfx
