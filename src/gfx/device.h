#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>

#include "rhi/rhi.h"

namespace gfx {

// Owns the window, the RHI device (rhi/rhi.h), the offscreen scene targets and
// the depth buffer. One per process. This is the engine's policy over the
// RHI: which targets a frame has, how they are sized, and which passes draw
// into them. Every GPU call itself goes through rhi().
//
// The frame renders into an offscreen color target which is then presented
// (scaled) to the window. That indirection costs one blit and buys three
// things: a stable render format independent of the display, a place for the
// post stack, and the ability to capture or render with no visible window at
// all (see `headless`).
//
// Depth uses a reversed-Z layout (near = 1, far = 0) paired with
// core::perspective_reverse_z, which buys the depth precision a flight game
// needs at multi-kilometre view distances. So the depth clear is 0 and the
// compare op is GREATER, not the usual 1 / LESS.
class Device {
public:
    struct Config {
        const char* title = "Dragon";
        int width = 1280;
        int height = 720;
        // Skips window display and presentation entirely. Rendering still
        // happens into the offscreen target, so screenshots work. Used by
        // automated smoke tests.
        bool headless = false;
        // A backend driver name ("vulkan", "direct3d12", "metal"), or empty
        // for the platform's first: D3D12 on Windows, Vulkan on Linux, Metal
        // on macOS.
        std::string gpu_driver;
    };

    bool init(const Config& config);
    void shutdown();

    // Starts recording the frame. Returns false if this frame should be
    // skipped entirely -- the window is minimized or mid-resize. Guarantees
    // the scene and depth targets match the render size on success.
    bool begin_frame();

    // Presents the scene target, services any pending screenshot request, and
    // submits. Only valid after begin_frame() returned true.
    void end_frame();

    // Starts the main render pass into the HDR scene target with depth.
    rhi::Pass* begin_main_pass(float r, float g, float b);

    // Starts an overlay pass on the scene colour target: loads existing colour
    // and binds no depth attachment. UI needs its own pass because Dear
    // ImGui's pipelines declare no depth target, and a pipeline can only be
    // bound in a pass whose attachments match it.
    rhi::Pass* begin_ui_pass();

    // A colour-only pass on any of the targets below, cleared or loaded.
    rhi::Pass* begin_color_pass(rhi::Texture* target, bool clear);

    void end_pass(rhi::Pass* pass);

    // Saves the scene target to `path` as a BMP at the end of this frame.
    // Synchronous when it fires (stalls the GPU), so it is a debug tool only.
    void request_screenshot(std::string path) { screenshot_path_ = std::move(path); }

    rhi::Device& rhi() const { return *rhi_; }
    // False until init() has a device (and after shutdown()).
    bool ready() const { return rhi_ != nullptr; }
    SDL_Window* window() const { return window_; }

    // The world renders LINEAR into a 16-bit float target; the post-process
    // composite tonemaps and grades it into the 8-bit scene colour target,
    // which the UI then draws onto and the present and the screenshot read.
    // World pipelines declare the HDR format (the pipeline cache's default);
    // only the composite and Dear ImGui declare the 8-bit one.
    rhi::Format scene_color_format() const { return rhi::Format::RGBA8; }
    rhi::Format scene_hdr_format() const { return rhi::Format::RGBA16F; }
    // What the main pass renders into: the HDR scene for the post stack, or,
    // for a tier without HDR (gfx/render_tier.h), the 8-bit colour target
    // directly, finished by the world shaders themselves.
    rhi::Format main_color_format() const { return hdr_ ? scene_hdr_format() : scene_color_format(); }
    bool hdr() const { return hdr_; }
    rhi::Format depth_format() const { return depth_format_; }
    rhi::Texture* scene_hdr() const { return scene_hdr_; }
    rhi::Texture* scene_color() const { return scene_color_; }
    // Half-resolution ping-pong targets for the bloom.
    rhi::Texture* bloom_a() const { return bloom_a_; }
    rhi::Texture* bloom_b() const { return bloom_b_; }
    uint32_t bloom_width() const { return world_w_ / 2; }
    uint32_t bloom_height() const { return world_h_ / 2; }

    // The UI's size: scene_color, the present and the screenshot.
    uint32_t width() const { return render_w_; }
    uint32_t height() const { return render_h_; }
    float aspect() const { return render_h_ > 0 ? float(render_w_) / float(render_h_) : 1.0f; }

    // The world renders at a fraction of that (the Resolution setting,
    // gfx/graphics_settings.h): a fill-bound card's pixel work falls with the
    // square of it, while the HUD stays sharp. The HDR tiers' composite
    // samples the smaller scene by UV and so stretches it on its own; an LDR
    // tier's world is stretched into scene_color by finish_world().
    void set_world_scale(float scale);
    float world_scale() const { return world_scale_; }
    uint32_t world_width() const { return world_w_; }
    uint32_t world_height() const { return world_h_; }
    // After the world's last pass, before the UI's.
    void finish_world();

private:
    bool ensure_targets(uint32_t w, uint32_t h);
    void release_targets();
    void save_screenshot(const std::vector<uint8_t>& rgba);

    SDL_Window* window_ = nullptr;
    std::unique_ptr<rhi::Device> rhi_;
    bool headless_ = false;
    bool in_frame_ = false;
    bool hdr_ = true;

    rhi::Texture* scene_color_ = nullptr;
    rhi::Texture* scene_hdr_ = nullptr;
    rhi::Texture* bloom_a_ = nullptr;
    rhi::Texture* bloom_b_ = nullptr;
    rhi::Texture* depth_ = nullptr;
    // An LDR tier's world at its scaled size; null at full size, when the
    // world draws straight into scene_color.
    rhi::Texture* scene_world_ = nullptr;
    uint32_t render_w_ = 0, render_h_ = 0;
    uint32_t world_w_ = 0, world_h_ = 0;
    float world_scale_ = 1.0f;
    rhi::Format depth_format_ = rhi::Format::D32F;

    std::string screenshot_path_;
};

}  // namespace gfx
