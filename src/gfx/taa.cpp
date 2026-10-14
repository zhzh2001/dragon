#include "gfx/taa.h"

#include <SDL3/SDL_stdinc.h>

#include "core/log.h"
#include "gfx/device.h"
#include "gfx/render_tier.h"

namespace gfx {

namespace {

float halton(uint32_t index, uint32_t base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= float(base);
        r += f * float(index % base);
        index /= base;
    }
    return r;
}

}  // namespace

bool Taa::init(Device* device, PipelineCache* pipelines) {
    device_ = device;
    pipelines_ = pipelines;
    available_ = active_tier().tier == Tier::Modern && device->hdr() && device->depth_sampled();
    if (!available_) return true;
    PipelineDesc desc;
    desc.name = "post_taa";
    desc.shader = "post_taa";
    desc.cull = rhi::Cull::None;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.color_format = device->scene_hdr_format();
    desc.no_depth_target = true;
    pipeline_ = pipelines->create(desc);
    // DRAGON_TAA_WEIGHT=w over the history's weight, for taking it apart.
    if (const char* w = SDL_getenv("DRAGON_TAA_WEIGHT")) history_weight = float(SDL_atof(w));
    rhi::SamplerDesc info;
    info.min_filter = info.mag_filter = rhi::Filter::Linear;
    info.mip_mode = rhi::MipMode::Nearest;
    info.address_u = info.address_v = info.address_w = rhi::Address::Clamp;
    info.max_lod = 0.0f;
    sampler_ = device->rhi().create_sampler(info);
    // Depth, point-sampled: a 32-bit float depth texture is not filterable
    // everywhere (Metal), and an average of two depths is no depth at all.
    info.min_filter = info.mag_filter = rhi::Filter::Nearest;
    depth_sampler_ = device->rhi().create_sampler(info);
    available_ = sampler_ != nullptr && depth_sampler_ != nullptr;
    return true;
}

void Taa::shutdown() {
    if (!device_) return;
    device_->rhi().destroy(history_[0]);
    device_->rhi().destroy(history_[1]);
    device_->rhi().destroy(velocity_);
    velocity_ = nullptr;
    device_->rhi().destroy(sampler_);
    device_->rhi().destroy(depth_sampler_);
    history_[0] = history_[1] = nullptr;
    sampler_ = depth_sampler_ = nullptr;
    history_w_ = history_h_ = 0;
    valid_ = false;
}

void Taa::begin_frame(const Camera& camera, float aspect, uint32_t width, uint32_t height) {
    prev_view_proj_ = view_proj_;
    prev_full_ = full_;
    full_ = camera.view_projection(aspect);
    prev_camera_ = camera_;
    prev_forward_ = forward_;
    const bool had = have_prev_;

    view_proj_ = camera.projection(aspect) * core::view_from_transform(core::Vec3{}, camera.rotation);
    camera_ = camera.position;
    forward_ = camera.forward();
    have_prev_ = true;
    // A cut: the history shows somewhere else.
    if (!had || core::distance(camera_, prev_camera_) > 300.0f || core::dot(forward_, prev_forward_) < 0.9f) {
        valid_ = false;
    }

    jitter_ = core::Mat4::identity();
    if (!active() || width == 0 || height == 0) {
        valid_ = false;  // switched back on, it starts afresh
        return;
    }
    ++frame_;
    const uint32_t phase = frame_ % 8u + 1u;
    const float jx = halton(phase, 2) - 0.5f;
    const float jy = halton(phase, 3) - 0.5f;
    // x' = x + j w: a shift of the whole image by j in NDC, after projection.
    jitter_.col[3].x = jx * 2.0f / float(width);
    jitter_.col[3].y = jy * 2.0f / float(height);
}

bool Taa::ensure_history(Device& device, uint32_t width, uint32_t height) {
    if (history_[0] && history_w_ == width && history_h_ == height) return true;
    device.rhi().destroy(history_[0]);
    device.rhi().destroy(history_[1]);
    device.rhi().destroy(velocity_);
    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = device.scene_hdr_format();
    desc.usage = rhi::TEXTURE_SAMPLED | rhi::TEXTURE_COLOR_TARGET;
    history_[0] = device.rhi().create_texture(desc, "taa_history_a");
    history_[1] = device.rhi().create_texture(desc, "taa_history_b");
    velocity_ = device.rhi().create_texture(desc, "taa_velocity");
    history_w_ = width;
    history_h_ = height;
    valid_ = false;
    return history_[0] && history_[1] && velocity_;
}

rhi::Pass* Taa::begin_velocity(Device& device, uint32_t width, uint32_t height) {
    if (!active() || !device.depth() || !ensure_history(device, width, height)) return nullptr;
    rhi::PassDesc pass;
    pass.color = velocity_;
    pass.clear_color = true;
    // rgba 0: no creature, reproject by depth. The pass's default clear has
    // an alpha of 1, which marked every pixel a creature standing still: the
    // history was never reprojected, and everything smeared in motion.
    pass.clear_rgba[3] = 0.0f;
    pass.depth = device.depth();
    pass.clear_depth = false;
    pass.keep_depth = true;   // the resolve reads it next
    return device.rhi().begin_pass(pass);
}

rhi::Texture* Taa::resolve(Device& device, rhi::Texture* scene, uint32_t width, uint32_t height) {
    if (!active() || !scene || !device.depth()) return scene;
    rhi::Pipeline* pipeline = pipelines_->get(pipeline_);
    if (!pipeline || !ensure_history(device, width, height)) return scene;

    Uniforms u;
    u.inv_view_proj = core::inverse(view_proj_);
    u.prev_view_proj = prev_view_proj_;
    const core::Vec3 delta = camera_ - prev_camera_;
    u.camera_delta = core::Vec4{delta.x, delta.y, delta.z, valid_ ? history_weight : 0.0f};
    u.texel = core::Vec4{1.0f / float(width), 1.0f / float(height), 0.0f, 0.0f};

    rhi::Texture* target = history_[write_];
    rhi::Texture* past = history_[1 - write_];
    rhi::Pass* pass = device.begin_color_pass(target, false);
    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &u, sizeof(u));
    rhi::TextureBinding bindings[4] = {};
    bindings[0].texture = scene;
    bindings[1].texture = valid_ ? past : scene;
    bindings[2].texture = device.depth();
    bindings[3].texture = velocity_;
    bindings[0].sampler = bindings[1].sampler = sampler_;
    bindings[2].sampler = bindings[3].sampler = depth_sampler_;
    device.rhi().bind_fragment_textures(pass, 0, bindings, 4);
    device.rhi().draw(pass, 3, 1, 0, 0);
    device.end_pass(pass);

    write_ = 1 - write_;
    valid_ = true;
    return target;
}

}  // namespace gfx
