#include "gfx/post.h"

#include "core/log.h"
#include "gfx/device.h"

namespace gfx {

namespace {

PipelineDesc fullscreen_desc(const char* name, const char* shader, rhi::Format format) {
    PipelineDesc desc;
    desc.name = name;
    desc.shader = shader;
    desc.cull = rhi::Cull::None;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.color_format = format;
    desc.depth_format = rhi::Format::Invalid;
    desc.no_depth_target = true;
    return desc;
}

}  // namespace

bool PostProcess::init(Device* device, PipelineCache* pipelines) {
    device_ = device;
    pipelines_ = pipelines;
    bright_ = pipelines_->create(
        fullscreen_desc("post_bright", "post_bright", device->scene_hdr_format()));
    blur_ = pipelines_->create(
        fullscreen_desc("post_blur", "post_blur", device->scene_hdr_format()));
    composite_ = pipelines_->create(
        fullscreen_desc("post_composite", "post_composite", device->scene_color_format()));

    rhi::SamplerDesc info;
    info.min_filter = rhi::Filter::Linear;
    info.mag_filter = rhi::Filter::Linear;
    info.mip_mode = rhi::MipMode::Nearest;
    info.address_u = info.address_v = info.address_w = rhi::Address::Clamp;
    info.max_lod = 0.0f;  // the targets have one level
    sampler_ = device->rhi().create_sampler(info);
    if (!sampler_) return false;
    return bright_ != INVALID_PIPELINE && blur_ != INVALID_PIPELINE &&
           composite_ != INVALID_PIPELINE;
}

void PostProcess::shutdown() {
    if (device_) device_->rhi().destroy(sampler_);
    sampler_ = nullptr;
}

PostUniforms PostProcess::uniforms_for(const PostSettings& s, float w, float h,
                                       core::Vec2 direction) const {
    PostUniforms u;
    u.grade = core::Vec4{s.exposure, s.contrast, s.saturation, s.enabled ? s.bloom_strength : 0.0f};
    u.bloom = core::Vec4{s.bloom_threshold, s.bloom_knee, s.enabled ? s.vignette : 0.0f,
                         s.enabled ? s.hue_preserve : 0.0f};
    u.shadows = core::Vec4{s.shadows_rgb[0], s.shadows_rgb[1], s.shadows_rgb[2],
                           s.enabled ? s.shadows_strength : 0.0f};
    u.highlights = core::Vec4{s.highlights_rgb[0], s.highlights_rgb[1], s.highlights_rgb[2],
                              s.enabled ? s.highlights_strength : 0.0f};
    u.balance = core::Vec4{s.enabled ? s.temperature : 0.0f, s.enabled ? s.tint : 0.0f,
                           s.enabled ? s.lift : 0.0f, s.gamma};
    u.texel = core::Vec4{1.0f / core::maxf(w, 1.0f), 1.0f / core::maxf(h, 1.0f), direction.x,
                         direction.y};
    if (!s.enabled) {
        // Off means the old picture: Reinhard, gamma, nothing else.
        u.grade = core::Vec4{1.0f, 1.0f, 1.0f, 0.0f};
        u.balance.w = 1.0f;
    }
    return u;
}

void PostProcess::run(Device& device, const PostSettings& settings) {
    rhi::Pipeline* bright = pipelines_->get(bright_);
    rhi::Pipeline* blur = pipelines_->get(blur_);
    rhi::Pipeline* composite = pipelines_->get(composite_);
    if (!composite || !device.scene_hdr()) return;

    const float w = float(device.width());
    const float h = float(device.height());
    const float bw = float(device.bloom_width());
    const float bh = float(device.bloom_height());

    auto bind = [&](rhi::Pass* pass, rhi::Texture* texture, uint32_t slot) {
        rhi::TextureBinding binding = {};
        binding.texture = texture;
        binding.sampler = sampler_;
        device.rhi().bind_fragment_textures(pass, slot, &binding, 1);
    };

    // The bloom chain, when the shaders are alive. A broken bloom shader
    // leaves the composite with a black bloom, not a black frame.
    if (bright && blur && settings.enabled && settings.bloom_strength > 0.0f) {
        {
            rhi::Pass* pass = device.begin_color_pass(device.bloom_a(), true);
            device.rhi().bind_pipeline(pass, bright);
            const PostUniforms u = uniforms_for(settings, w, h, core::Vec2{0.0f, 0.0f});
            device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &u, sizeof(u));
            bind(pass, device.scene_hdr(), 0);
            device.rhi().draw(pass, 3, 1, 0, 0);
            device.end_pass(pass);
        }
        {
            rhi::Pass* pass = device.begin_color_pass(device.bloom_b(), true);
            device.rhi().bind_pipeline(pass, blur);
            const PostUniforms u = uniforms_for(settings, bw, bh, core::Vec2{1.0f, 0.0f});
            device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &u, sizeof(u));
            bind(pass, device.bloom_a(), 0);
            device.rhi().draw(pass, 3, 1, 0, 0);
            device.end_pass(pass);
        }
        {
            rhi::Pass* pass = device.begin_color_pass(device.bloom_a(), true);
            device.rhi().bind_pipeline(pass, blur);
            const PostUniforms u = uniforms_for(settings, bw, bh, core::Vec2{0.0f, 1.0f});
            device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &u, sizeof(u));
            bind(pass, device.bloom_b(), 0);
            device.rhi().draw(pass, 3, 1, 0, 0);
            device.end_pass(pass);
        }
    } else {
        // Clear the bloom so the composite adds nothing.
        rhi::Pass* pass = device.begin_color_pass(device.bloom_a(), true);
        device.end_pass(pass);
    }

    rhi::Pass* pass = device.begin_color_pass(device.scene_color(), true);
    device.rhi().bind_pipeline(pass, composite);
    const PostUniforms u = uniforms_for(settings, w, h, core::Vec2{0.0f, 0.0f});
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &u, sizeof(u));
    bind(pass, device.scene_hdr(), 0);
    bind(pass, device.bloom_a(), 1);
    device.rhi().draw(pass, 3, 1, 0, 0);
    device.end_pass(pass);
}

}  // namespace gfx
