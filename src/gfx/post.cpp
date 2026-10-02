#include "gfx/post.h"

#include "core/log.h"
#include "gfx/device.h"

namespace gfx {

namespace {

PipelineDesc fullscreen_desc(const char* name, const char* shader, SDL_GPUTextureFormat format) {
    PipelineDesc desc;
    desc.name = name;
    desc.shader = shader;
    desc.cull = SDL_GPU_CULLMODE_NONE;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.color_format = format;
    desc.depth_format = SDL_GPU_TEXTUREFORMAT_INVALID;
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

    SDL_GPUSamplerCreateInfo info = {};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_ = SDL_CreateGPUSampler(device->gpu(), &info);
    if (!sampler_) {
        LOG_ERROR("SDL_CreateGPUSampler(post) failed: %s", SDL_GetError());
        return false;
    }
    return bright_ != INVALID_PIPELINE && blur_ != INVALID_PIPELINE &&
           composite_ != INVALID_PIPELINE;
}

void PostProcess::shutdown() {
    if (device_ && sampler_) SDL_ReleaseGPUSampler(device_->gpu(), sampler_);
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
    SDL_GPUGraphicsPipeline* bright = pipelines_->get(bright_);
    SDL_GPUGraphicsPipeline* blur = pipelines_->get(blur_);
    SDL_GPUGraphicsPipeline* composite = pipelines_->get(composite_);
    if (!composite || !device.scene_hdr()) return;

    const float w = float(device.width());
    const float h = float(device.height());
    const float bw = float(device.bloom_width());
    const float bh = float(device.bloom_height());

    auto bind = [&](SDL_GPURenderPass* pass, SDL_GPUTexture* texture, uint32_t slot) {
        SDL_GPUTextureSamplerBinding binding = {};
        binding.texture = texture;
        binding.sampler = sampler_;
        SDL_BindGPUFragmentSamplers(pass, slot, &binding, 1);
    };

    // The bloom chain, when the shaders are alive. A broken bloom shader
    // leaves the composite with a black bloom, not a black frame.
    if (bright && blur && settings.enabled && settings.bloom_strength > 0.0f) {
        {
            SDL_GPURenderPass* pass = device.begin_color_pass(device.bloom_a(), true);
            SDL_BindGPUGraphicsPipeline(pass, bright);
            const PostUniforms u = uniforms_for(settings, w, h, core::Vec2{0.0f, 0.0f});
            SDL_PushGPUFragmentUniformData(device.cmd(), 0, &u, sizeof(u));
            bind(pass, device.scene_hdr(), 0);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            device.end_pass(pass);
        }
        {
            SDL_GPURenderPass* pass = device.begin_color_pass(device.bloom_b(), true);
            SDL_BindGPUGraphicsPipeline(pass, blur);
            const PostUniforms u = uniforms_for(settings, bw, bh, core::Vec2{1.0f, 0.0f});
            SDL_PushGPUFragmentUniformData(device.cmd(), 0, &u, sizeof(u));
            bind(pass, device.bloom_a(), 0);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            device.end_pass(pass);
        }
        {
            SDL_GPURenderPass* pass = device.begin_color_pass(device.bloom_a(), true);
            SDL_BindGPUGraphicsPipeline(pass, blur);
            const PostUniforms u = uniforms_for(settings, bw, bh, core::Vec2{0.0f, 1.0f});
            SDL_PushGPUFragmentUniformData(device.cmd(), 0, &u, sizeof(u));
            bind(pass, device.bloom_b(), 0);
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            device.end_pass(pass);
        }
    } else {
        // Clear the bloom so the composite adds nothing.
        SDL_GPURenderPass* pass = device.begin_color_pass(device.bloom_a(), true);
        device.end_pass(pass);
    }

    SDL_GPURenderPass* pass = device.begin_color_pass(device.scene_color(), true);
    SDL_BindGPUGraphicsPipeline(pass, composite);
    const PostUniforms u = uniforms_for(settings, w, h, core::Vec2{0.0f, 0.0f});
    SDL_PushGPUFragmentUniformData(device.cmd(), 0, &u, sizeof(u));
    bind(pass, device.scene_hdr(), 0);
    bind(pass, device.bloom_a(), 1);
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    device.end_pass(pass);
}

}  // namespace gfx
