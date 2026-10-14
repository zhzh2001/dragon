#pragma once

// Temporal anti-aliasing (docs/FIDELITY.md, F5, shaders/post_taa.hlsl), on
// the modern tier. Each frame:
//
//   1. begin_frame() picks this frame's sub-pixel jitter (Halton 2,3, eight
//      phases) and remembers the camera unjittered;
//   2. the world draws through jitter() * view_proj;
//   3. resolve(), after the main pass, blends the frame into the history
//      reprojected through the depth buffer, and returns the result for
//      the post stack to bloom and grade.
//
// A camera cut (a jump of more than a few hundred metres, or a turn of
// more than ~25 degrees in a frame) or a change of size starts the history
// afresh.

#include "core/math.h"
#include "gfx/camera.h"
#include "gfx/pipeline.h"

namespace gfx {

class Device;

class Taa {
public:
    bool init(Device* device, PipelineCache* pipelines);
    void shutdown();
    // Off on a retro tier, when the panel says so, or when the device could
    // not keep its depth to read.
    bool active() const { return enabled && available_; }
    bool available() const { return available_; }

    void begin_frame(const Camera& camera, float aspect, uint32_t width, uint32_t height);
    // Clip-space translation by this frame's jitter: identity when inactive.
    core::Mat4 jitter() const { return jitter_; }
    // The frame, resolved against the history; `scene` itself when inactive.
    rhi::Texture* resolve(Device& device, rhi::Texture* scene, uint32_t width, uint32_t height);

    // The creatures' motion (shaders/velocity_skinned.hlsl): after the main
    // pass and before resolve(), a pass on the velocity target, tested
    // against the main depth. Null when inactive. Every frame the resolve
    // runs must open and end it, creatures or not: it is what clears it.
    rhi::Pass* begin_velocity(Device& device, uint32_t width, uint32_t height);
    // The matrices a velocity draw needs, this frame's and the last.
    struct VelocityMatrices {
        core::Mat4 jittered_view_proj;
        core::Mat4 view_proj;
        core::Mat4 prev_view_proj;
    };
    VelocityMatrices velocity_matrices() const { return {jitter_ * full_, full_, prev_full_}; }

    bool enabled = true;
    // How much of the history a pixel keeps each frame (0.9 at 60 fps
    // averages about the last ten).
    float history_weight = 0.9f;

private:
    struct Uniforms {
        core::Mat4 inv_view_proj;
        core::Mat4 prev_view_proj;
        core::Vec4 camera_delta;
        core::Vec4 texel;
    };
    bool ensure_history(Device& device, uint32_t width, uint32_t height);

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle pipeline_ = INVALID_PIPELINE;
    rhi::Sampler* sampler_ = nullptr;
    rhi::Sampler* depth_sampler_ = nullptr;
    rhi::Texture* history_[2] = {nullptr, nullptr};
    rhi::Texture* velocity_ = nullptr;
    uint32_t history_w_ = 0, history_h_ = 0;
    int write_ = 0;
    bool available_ = false;
    bool valid_ = false;  // history holds a frame of this size
    uint32_t frame_ = 0;

    core::Mat4 jitter_ = core::Mat4::identity();
    core::Mat4 view_proj_ = core::Mat4::identity();       // this frame, camera-relative, unjittered
    core::Mat4 prev_view_proj_ = core::Mat4::identity();  // the last frame's
    core::Vec3 camera_ = {}, prev_camera_ = {};
    core::Vec3 forward_ = {}, prev_forward_ = {};
    bool have_prev_ = false;
    core::Mat4 full_ = core::Mat4::identity(), prev_full_ = core::Mat4::identity();
};

}  // namespace gfx
