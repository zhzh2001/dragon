#pragma once

#include "core/math.h"
#include "gfx/pipeline.h"

namespace gfx {

class Device;

// Must match PostUniforms in shaders/post_common.hlsl.
struct PostUniforms {
    core::Vec4 grade;       // exposure, contrast, saturation, bloom strength
    core::Vec4 bloom;       // threshold, knee, vignette, unused
    core::Vec4 shadows;     // rgb multiplier, a strength
    core::Vec4 highlights;  // rgb multiplier, a strength
    core::Vec4 balance;     // temperature, tint, lift, gamma
    core::Vec4 texel;       // 1/w, 1/h, blur dir x, blur dir y
    core::Vec4 occlusion;   // x the ambient occlusion's strength (0: off)
    core::Vec4 flare;       // xy the sun on the screen (uv), z the lens flare, w the sun's glare (0: off)
};

// The grade and the bloom, as dials. Everything a person would drag while
// looking at the picture; the defaults are the "painted realism, late
// afternoon" target in DIRECTION.md, judged against docs/concept/valley-target.png.
struct PostSettings {
    float exposure = 1.12f;
    float contrast = 1.08f;
    float saturation = 1.04f;
    float bloom_strength = 0.35f;
    // Linear scene units. The sun-lit snow sits near 1; fire and the sun's
    // glow well above. Below the threshold nothing blooms.
    float bloom_threshold = 1.1f;
    float bloom_knee = 0.5f;
    float vignette = 0.12f;
    // How much of the tonemap preserves hue. Reinhard per channel sends every
    // bright colour to white -- a stack of additive fire puffs became cream,
    // frost became a white smear. At 1 the curve maps luminance only and
    // keeps the ratios, so fire stays orange however bright it gets; at 0 it
    // is the old per-channel curve. The default keeps a little of the
    // white-hot core.
    float hue_preserve = 0.8f;
    float temperature = 0.03f;  // a touch warm
    float tint = 0.0f;
    float lift = 0.0f;
    float gamma = 1.0f;
    // Split-toning: cool shadows, warm highlights -- the late-afternoon split
    // the lighting already makes, pushed a little further in the grade.
    float shadows_rgb[3] = {0.88f, 0.93f, 1.04f};
    float shadows_strength = 0.4f;
    float highlights_rgb[3] = {1.04f, 0.98f, 0.90f};
    float highlights_strength = 0.35f;
    bool enabled = true;
    // Sharpening after the temporal resolve (gfx/taa.h), which softens a
    // moving picture; 0 when the resolve is off.
    float sharpen = 0.6f;
    // FXAA in the composite (post_composite.hlsl): the spatial anti-aliasing,
    // for when the temporal resolve is off.
    bool fxaa = false;
    // The modern tier's lens (post_composite.hlsl, GAPS.md G9): ghosts of
    // the bright spots mirrored through the centre, and a glare of rays about
    // the sun when it is in the frame and not behind a ridge.
    float lens_flare = 0.06f;
    float sun_glare = 0.4f;
};

// Renders the HDR scene target to the 8-bit scene colour target through the
// bloom and the grade. Three half-resolution passes (bright, blur across, blur
// down) and one full-resolution composite. All fullscreen triangles, no
// vertex buffers.
class PostProcess {
public:
    bool init(Device* device, PipelineCache* pipelines);
    void shutdown();
    // Between the main pass ending and the UI pass beginning.
    // `scene`: the linear world to bloom and grade -- the device's HDR
    // target, or the temporal resolve's output (gfx/taa.h).
    // `occlusion`: the ambient occlusion to multiply in (gfx/ssao.h), with
    // its strength; null for none.
    void run(Device& device, const PostSettings& settings, rhi::Texture* scene, rhi::Texture* occlusion = nullptr,
             float occlusion_strength = 0.0f);

    // Where the sun is on the screen (uv, y down) and whether it is ahead of
    // the camera at all, for the glare; set each frame before run().
    core::Vec2 sun_uv{0.5f, 0.5f};
    bool sun_ahead = false;

private:
    PostUniforms uniforms_for(const PostSettings& settings, float w, float h,
                              core::Vec2 direction) const;

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle bright_ = INVALID_PIPELINE;
    PipelineHandle blur_ = INVALID_PIPELINE;
    PipelineHandle composite_ = INVALID_PIPELINE;
    rhi::Sampler* sampler_ = nullptr;
};

}  // namespace gfx
