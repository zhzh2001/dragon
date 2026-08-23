#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <string>

namespace gfx {

// Owns the window, the GPU device, the offscreen scene target, and the depth
// buffer. One per process.
//
// The frame renders into an offscreen color target which is then blitted to the
// swapchain. That indirection costs one blit and buys three things: a stable
// render format independent of the display, a place to hang tonemapping and
// post-processing later, and the ability to capture or render with no visible
// window at all (see `headless`).
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
        // Skips window display and swapchain acquisition entirely. Rendering
        // still happens into the offscreen target, so screenshots work. Used by
        // automated smoke tests.
        bool headless = false;
    };

    bool init(const Config& config);
    void shutdown();

    // Acquires a command buffer and (unless headless) the swapchain texture.
    // Returns false if this frame should be skipped entirely -- the window is
    // minimized or mid-resize. Guarantees the scene and depth targets match the
    // render size on success.
    bool begin_frame();

    // Blits the scene target to the swapchain, services any pending screenshot
    // request, and submits. Only valid after begin_frame() returned true.
    void end_frame();

    // Starts the main render pass into the offscreen scene target.
    SDL_GPURenderPass* begin_main_pass(float r, float g, float b);

    // Starts an overlay pass on the scene target: loads existing color and
    // binds no depth attachment. UI needs its own pass because Dear ImGui's
    // pipelines declare no depth target, and a pipeline can only be bound in a
    // pass whose attachments match it.
    SDL_GPURenderPass* begin_ui_pass();

    void end_pass(SDL_GPURenderPass* pass);

    // Saves the scene target to `path` as a BMP at the end of this frame.
    // Synchronous when it fires (stalls the GPU), so it is a debug tool only.
    void request_screenshot(std::string path) { screenshot_path_ = std::move(path); }

    SDL_GPUDevice* gpu() const { return gpu_; }
    SDL_Window* window() const { return window_; }
    SDL_GPUCommandBuffer* cmd() const { return cmd_; }

    // Pipelines must declare this as their color target format, not the
    // swapchain format, since the main pass renders offscreen.
    SDL_GPUTextureFormat scene_color_format() const { return SCENE_COLOR_FORMAT; }
    SDL_GPUTextureFormat depth_format() const { return depth_format_; }

    uint32_t width() const { return render_w_; }
    uint32_t height() const { return render_h_; }
    float aspect() const { return render_h_ > 0 ? float(render_w_) / float(render_h_) : 1.0f; }

private:
    // RGBA8 keeps the blit to the swapchain trivial. Swap for a 16-bit float
    // format when HDR tonemapping arrives.
    static constexpr SDL_GPUTextureFormat SCENE_COLOR_FORMAT =
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    bool ensure_targets(uint32_t w, uint32_t h);
    void save_screenshot();

    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* gpu_ = nullptr;
    bool headless_ = false;

    SDL_GPUCommandBuffer* cmd_ = nullptr;
    SDL_GPUTexture* swapchain_ = nullptr;
    uint32_t swapchain_w_ = 0, swapchain_h_ = 0;

    SDL_GPUTexture* scene_color_ = nullptr;
    SDL_GPUTexture* depth_ = nullptr;
    uint32_t render_w_ = 0, render_h_ = 0;
    SDL_GPUTextureFormat depth_format_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    std::string screenshot_path_;
};

}  // namespace gfx
