#pragma once

// Dynamic resolution (docs/FIDELITY.md, F5): the world's render scale
// steered by the GPU's time a frame (rhi::Device::gpu_frame_ms), so a
// GPU-bound frame holds the display's rate instead of falling between
// two vsync intervals -- which reads as stutter, at 21 ms a frame on a
// 60 Hz display every other frame waits a whole interval more.
//
// The GPU's time is taken to grow with the world's pixels, the square of
// the scale. Over the budget for a moment, the scale drops at once to what
// should fit; under it with room to spare for a while, it climbs a step,
// only if the step should still fit. Each change reallocates the world's
// targets (gfx::Device::set_world_scale), so changes are spaced out.

#include <algorithm>
#include <cmath>

namespace gfx {

struct DynamicResolutionSettings {
    bool enabled = true;
    float target_fps = 60.0f;
    float min_scale = 0.6f;
    float step = 0.05f;      // the scale moves in steps of this
    float headroom = 0.88f;  // of the frame's interval the GPU may use
};

class DynamicResolution {
public:
    // The scale to render at, given the GPU's last smoothed time a frame
    // (0: unknown) and the most the settings allow.
    float update(const DynamicResolutionSettings& s, float gpu_ms, float dt, float max_scale) {
        max_scale = std::clamp(max_scale, 0.25f, 1.0f);
        if (!s.enabled || gpu_ms <= 0.0f || s.target_fps <= 0.0f) {
            scale_ = max_scale;
            over_ = under_ = cooldown_ = 0.0f;
            return scale_;
        }
        const float floor = std::min(std::max(s.min_scale, 0.25f), max_scale);
        scale_ = std::clamp(scale_, floor, max_scale);
        const float budget = 1000.0f / s.target_fps * s.headroom;
        cooldown_ = std::max(cooldown_ - dt, 0.0f);
        over_ = gpu_ms > budget ? over_ + dt : 0.0f;
        under_ = gpu_ms < budget * 0.75f ? under_ + dt : 0.0f;
        if (cooldown_ > 0.0f) return scale_;
        const float step = std::max(s.step, 0.01f);
        if (over_ > 0.4f && scale_ > floor) {
            // Down to what should fit, a little under, in whole steps.
            const float fits = scale_ * std::sqrt(budget * 0.95f / gpu_ms);
            const float down = std::floor(fits / step) * step;
            scale_ = std::clamp(std::min(down, scale_ - step), floor, max_scale);
            over_ = 0.0f;
            cooldown_ = 1.0f;
        } else if (under_ > 2.5f && scale_ < max_scale) {
            const float up = std::min(scale_ + step, max_scale);
            const float predicted = gpu_ms * (up / scale_) * (up / scale_);
            if (predicted < budget * 0.9f) {
                scale_ = up;
                cooldown_ = 2.0f;
            }
            under_ = 0.0f;
        }
        return scale_;
    }
    float scale() const { return scale_; }

private:
    float scale_ = 1.0f;
    float over_ = 0.0f;      // seconds over the budget, in a row
    float under_ = 0.0f;     // seconds well under it, in a row
    float cooldown_ = 0.0f;  // seconds before the next change
};

}  // namespace gfx
