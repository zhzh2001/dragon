#include "gfx/device.h"

#include <vector>

#include "core/log.h"

namespace gfx {

bool Device::init(const Config& config) {
    headless_ = config.headless;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return SDL_FAIL("SDL_Init");

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (headless_) flags |= SDL_WINDOW_HIDDEN;

    window_ = SDL_CreateWindow(config.title, config.width, config.height, flags);
    if (!window_) return SDL_FAIL("SDL_CreateWindow");

    // MSL source is what we author, so it is the only format we request. A
    // cross-platform port would add SPIRV/DXIL here.
    gpu_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, /*debug_mode=*/true, nullptr);
    if (!gpu_) return SDL_FAIL("SDL_CreateGPUDevice");

    if (!SDL_ClaimWindowForGPUDevice(gpu_, window_)) return SDL_FAIL("SDL_ClaimWindowForGPUDevice");

    // Mailbox where available (lowest latency), else vsync. Input latency has a
    // direct effect on how responsive flight controls feel.
    if (SDL_WindowSupportsGPUPresentMode(gpu_, window_, SDL_GPU_PRESENTMODE_MAILBOX)) {
        SDL_SetGPUSwapchainParameters(gpu_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                      SDL_GPU_PRESENTMODE_MAILBOX);
    }

    if (!SDL_GPUTextureSupportsFormat(gpu_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                                      SDL_GPU_TEXTURETYPE_2D,
                                      SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        depth_format_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
        LOG_WARN("D32_FLOAT unsupported, falling back to D24_UNORM");
    }

    // Headless never acquires a swapchain, so nothing else establishes the
    // render size.
    if (headless_ && !ensure_targets(uint32_t(config.width), uint32_t(config.height))) return false;

    LOG_INFO("GPU device: %s%s", SDL_GetGPUDeviceDriver(gpu_), headless_ ? " (headless)" : "");
    return true;
}

void Device::shutdown() {
    if (gpu_) {
        SDL_WaitForGPUIdle(gpu_);
        if (scene_color_) SDL_ReleaseGPUTexture(gpu_, scene_color_);
        if (scene_hdr_) SDL_ReleaseGPUTexture(gpu_, scene_hdr_);
        if (bloom_a_) SDL_ReleaseGPUTexture(gpu_, bloom_a_);
        if (bloom_b_) SDL_ReleaseGPUTexture(gpu_, bloom_b_);
        if (depth_) SDL_ReleaseGPUTexture(gpu_, depth_);
        scene_color_ = nullptr;
        depth_ = nullptr;
        if (window_) SDL_ReleaseWindowFromGPUDevice(gpu_, window_);
        SDL_DestroyGPUDevice(gpu_);
        gpu_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

bool Device::ensure_targets(uint32_t w, uint32_t h) {
    if (scene_color_ && depth_ && render_w_ == w && render_h_ == h) return true;
    if (w == 0 || h == 0) return false;

    if (scene_color_) SDL_ReleaseGPUTexture(gpu_, scene_color_);
    if (scene_hdr_) SDL_ReleaseGPUTexture(gpu_, scene_hdr_);
    if (bloom_a_) SDL_ReleaseGPUTexture(gpu_, bloom_a_);
    if (bloom_b_) SDL_ReleaseGPUTexture(gpu_, bloom_b_);
    if (depth_) SDL_ReleaseGPUTexture(gpu_, depth_);
    scene_color_ = nullptr;
    scene_hdr_ = nullptr;
    bloom_a_ = nullptr;
    bloom_b_ = nullptr;
    depth_ = nullptr;
    render_w_ = render_h_ = 0;

    SDL_GPUTextureCreateInfo color_info = {};
    color_info.type = SDL_GPU_TEXTURETYPE_2D;
    color_info.format = SCENE_COLOR_FORMAT;
    color_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    color_info.width = w;
    color_info.height = h;
    color_info.layer_count_or_depth = 1;
    color_info.num_levels = 1;
    color_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    scene_color_ = SDL_CreateGPUTexture(gpu_, &color_info);
    if (!scene_color_) return SDL_FAIL("SDL_CreateGPUTexture(scene_color)");

    // The linear scene, and the two half-size bloom targets.
    SDL_GPUTextureCreateInfo hdr_info = color_info;
    hdr_info.format = SCENE_HDR_FORMAT;
    scene_hdr_ = SDL_CreateGPUTexture(gpu_, &hdr_info);
    if (!scene_hdr_) return SDL_FAIL("SDL_CreateGPUTexture(scene_hdr)");
    SDL_GPUTextureCreateInfo bloom_info = hdr_info;
    bloom_info.width = w / 2 > 0 ? w / 2 : 1;
    bloom_info.height = h / 2 > 0 ? h / 2 : 1;
    bloom_a_ = SDL_CreateGPUTexture(gpu_, &bloom_info);
    bloom_b_ = SDL_CreateGPUTexture(gpu_, &bloom_info);
    if (!bloom_a_ || !bloom_b_) return SDL_FAIL("SDL_CreateGPUTexture(bloom)");

    SDL_GPUTextureCreateInfo depth_info = {};
    depth_info.type = SDL_GPU_TEXTURETYPE_2D;
    depth_info.format = depth_format_;
    depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth_info.width = w;
    depth_info.height = h;
    depth_info.layer_count_or_depth = 1;
    depth_info.num_levels = 1;
    depth_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    depth_ = SDL_CreateGPUTexture(gpu_, &depth_info);
    if (!depth_) {
        SDL_ReleaseGPUTexture(gpu_, scene_color_);
        scene_color_ = nullptr;
        return SDL_FAIL("SDL_CreateGPUTexture(depth)");
    }

    render_w_ = w;
    render_h_ = h;
    LOG_INFO("render targets resized to %ux%u", w, h);
    return true;
}

bool Device::begin_frame() {
    cmd_ = SDL_AcquireGPUCommandBuffer(gpu_);
    if (!cmd_) {
        SDL_FAIL("SDL_AcquireGPUCommandBuffer");
        return false;
    }

    if (headless_) return true;

    // Blocks until a swapchain image is free, which keeps the CPU from running
    // arbitrarily far ahead of the display.
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd_, window_, &swapchain_, &swapchain_w_,
                                               &swapchain_h_)) {
        SDL_FAIL("SDL_WaitAndAcquireGPUSwapchainTexture");
        SDL_SubmitGPUCommandBuffer(cmd_);
        cmd_ = nullptr;
        return false;
    }

    // Legitimately null while minimized or mid-resize.
    if (!swapchain_ || !ensure_targets(swapchain_w_, swapchain_h_)) {
        SDL_SubmitGPUCommandBuffer(cmd_);
        cmd_ = nullptr;
        swapchain_ = nullptr;
        return false;
    }
    return true;
}

SDL_GPURenderPass* Device::begin_main_pass(float r, float g, float b) {
    SDL_GPUColorTargetInfo color = {};
    color.texture = scene_hdr_;
    color.clear_color = SDL_FColor{r, g, b, 1.0f};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    color.cycle = true;

    SDL_GPUDepthStencilTargetInfo depth = {};
    depth.texture = depth_;
    depth.clear_depth = 0.0f;  // reversed-Z: the far plane is 0
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.cycle = true;

    return SDL_BeginGPURenderPass(cmd_, &color, 1, &depth);
}

SDL_GPURenderPass* Device::begin_color_pass(SDL_GPUTexture* target, bool clear) {
    SDL_GPUColorTargetInfo color = {};
    color.texture = target;
    color.clear_color = SDL_FColor{0.0f, 0.0f, 0.0f, 1.0f};
    color.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    color.cycle = clear;
    return SDL_BeginGPURenderPass(cmd_, &color, 1, nullptr);
}

SDL_GPURenderPass* Device::begin_ui_pass() {
    SDL_GPUColorTargetInfo color = {};
    color.texture = scene_color_;
    color.load_op = SDL_GPU_LOADOP_LOAD;
    color.store_op = SDL_GPU_STOREOP_STORE;
    return SDL_BeginGPURenderPass(cmd_, &color, 1, nullptr);
}

void Device::end_pass(SDL_GPURenderPass* pass) {
    if (pass) SDL_EndGPURenderPass(pass);
}

void Device::save_screenshot() {
    const uint32_t w = render_w_, h = render_h_;
    const uint32_t size = w * h * 4;

    SDL_GPUTransferBufferCreateInfo info = {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu_, &info);
    if (!transfer) {
        SDL_FAIL("SDL_CreateGPUTransferBuffer(screenshot)");
        return;
    }

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd_);
    SDL_GPUTextureRegion src = {};
    src.texture = scene_color_;
    src.w = w;
    src.h = h;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst = {};
    dst.transfer_buffer = transfer;
    dst.pixels_per_row = w;
    dst.rows_per_layer = h;
    SDL_DownloadFromGPUTexture(pass, &src, &dst);
    SDL_EndGPUCopyPass(pass);

    // The download has to complete before we can read it, so this frame's
    // submit becomes a blocking one.
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd_);
    cmd_ = nullptr;
    swapchain_ = nullptr;
    if (fence) {
        SDL_WaitForGPUFences(gpu_, true, &fence, 1);
        SDL_ReleaseGPUFence(gpu_, fence);
    }

    void* pixels = SDL_MapGPUTransferBuffer(gpu_, transfer, false);
    if (pixels) {
        // SDL_PIXELFORMAT_RGBA32 is the byte-order-R,G,B,A alias, which is what
        // R8G8B8A8_UNORM gives us in memory.
        SDL_Surface* surface = SDL_CreateSurfaceFrom(int(w), int(h), SDL_PIXELFORMAT_RGBA32,
                                                     pixels, int(w * 4));
        if (surface) {
            if (SDL_SaveBMP(surface, screenshot_path_.c_str())) {
                LOG_INFO("screenshot -> %s (%ux%u)", screenshot_path_.c_str(), w, h);
            } else {
                SDL_FAIL("SDL_SaveBMP");
            }
            SDL_DestroySurface(surface);
        }
        SDL_UnmapGPUTransferBuffer(gpu_, transfer);
    }
    SDL_ReleaseGPUTransferBuffer(gpu_, transfer);
    screenshot_path_.clear();
}

void Device::end_frame() {
    if (!cmd_) return;

    if (swapchain_) {
        SDL_GPUBlitInfo blit = {};
        blit.source.texture = scene_color_;
        blit.source.w = render_w_;
        blit.source.h = render_h_;
        blit.destination.texture = swapchain_;
        blit.destination.w = swapchain_w_;
        blit.destination.h = swapchain_h_;
        blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
        blit.filter = SDL_GPU_FILTER_LINEAR;
        SDL_BlitGPUTexture(cmd_, &blit);
    }

    if (!screenshot_path_.empty()) {
        save_screenshot();  // submits the command buffer itself
        return;
    }

    if (!SDL_SubmitGPUCommandBuffer(cmd_)) SDL_FAIL("SDL_SubmitGPUCommandBuffer");
    cmd_ = nullptr;
    swapchain_ = nullptr;

    // Windowed frames are throttled by swapchain acquisition. Headless frames
    // are not, so without this the loop submits work as fast as the CPU can
    // build it and lets GPU resources pile up unboundedly.
    if (headless_) SDL_WaitForGPUIdle(gpu_);
}

}  // namespace gfx
