#include "gfx/ssao.h"

#include <algorithm>
#include <cmath>

#include "core/math.h"
#include "gfx/device.h"
#include "gfx/render_tier.h"

namespace gfx {

namespace {

PipelineDesc pass_desc(const char* name) {
    PipelineDesc desc;
    desc.name = name;
    desc.shader = name;
    desc.cull = rhi::Cull::None;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.color_format = rhi::Format::RGBA16F;
    desc.no_depth_target = true;
    return desc;
}

}  // namespace

bool Ssao::init(Device* device, PipelineCache* pipelines) {
    device_ = device;
    pipelines_ = pipelines;
    available_ = active_tier().tier == Tier::Modern && device->hdr() && device->depth_sampled();
    if (!available_) return true;
    ssao_ = pipelines->create(pass_desc("post_ssao"));
    blur_ = pipelines->create(pass_desc("post_ssao_blur"));
    rhi::SamplerDesc info;
    info.min_filter = info.mag_filter = rhi::Filter::Nearest;
    info.mip_mode = rhi::MipMode::Nearest;
    info.address_u = info.address_v = info.address_w = rhi::Address::Clamp;
    info.max_lod = 0.0f;
    point_ = device->rhi().create_sampler(info);
    info.min_filter = info.mag_filter = rhi::Filter::Linear;
    linear_ = device->rhi().create_sampler(info);
    available_ = point_ && linear_;
    return true;
}

void Ssao::shutdown() {
    if (!device_) return;
    rhi::Device& rhi = device_->rhi();
    rhi.destroy(raw_);
    rhi.destroy(blurred_);
    rhi.destroy(point_);
    rhi.destroy(linear_);
    raw_ = blurred_ = nullptr;
    point_ = linear_ = nullptr;
    width_ = height_ = 0;
}

bool Ssao::ensure_targets(Device& device, uint32_t width, uint32_t height) {
    if (raw_ && width_ == width && height_ == height) return true;
    device.rhi().destroy(raw_);
    device.rhi().destroy(blurred_);
    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = rhi::Format::RGBA16F;
    desc.usage = rhi::TEXTURE_SAMPLED | rhi::TEXTURE_COLOR_TARGET;
    raw_ = device.rhi().create_texture(desc, "ssao");
    blurred_ = device.rhi().create_texture(desc, "ssao_blurred");
    width_ = width;
    height_ = height;
    return raw_ && blurred_;
}

rhi::Texture* Ssao::run(Device& device, const Camera& camera, float aspect, const SsaoSettings& s) {
    if (!available_ || !s.enabled || !device.depth()) return nullptr;
    rhi::Pipeline* ssao = pipelines_->get(ssao_);
    rhi::Pipeline* blur = pipelines_->get(blur_);
    const uint32_t w = std::max(1u, device.world_width() / 2), h = std::max(1u, device.world_height() / 2);
    if (!ssao || !blur || !ensure_targets(device, w, h)) return nullptr;
    rhi::Device& rhi = device.rhi();

    struct {
        core::Vec4 projection, params, texel;
    } u;
    u.projection = core::Vec4{std::tan(core::radians(camera.fov_y_deg) * 0.5f), aspect, camera.z_near, s.bias};
    u.params = core::Vec4{s.radius, s.intensity, s.fade_start, s.fade_end};
    u.texel = core::Vec4{1.0f / float(device.world_width()), 1.0f / float(device.world_height()), 0.0f, 0.0f};
    {
        rhi::Pass* pass = device.begin_color_pass(raw_, false);
        rhi.bind_pipeline(pass, ssao);
        rhi.push_uniforms(rhi::Stage::Fragment, 0, &u, sizeof(u));
        rhi::TextureBinding depth = {};
        depth.texture = device.depth();
        depth.sampler = point_;
        rhi.bind_fragment_textures(pass, 0, &depth, 1);
        rhi.draw(pass, 3, 1, 0, 0);
        device.end_pass(pass);
    }
    {
        const core::Vec4 texel{1.0f / float(w), 1.0f / float(h), 0.0f, 0.0f};
        rhi::Pass* pass = device.begin_color_pass(blurred_, false);
        rhi.bind_pipeline(pass, blur);
        rhi.push_uniforms(rhi::Stage::Fragment, 0, &texel, sizeof(texel));
        rhi::TextureBinding raw = {};
        raw.texture = raw_;
        raw.sampler = point_;
        rhi.bind_fragment_textures(pass, 0, &raw, 1);
        rhi.draw(pass, 3, 1, 0, 0);
        device.end_pass(pass);
    }
    return blurred_;
}

}  // namespace gfx
