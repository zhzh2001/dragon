#include "gfx/device.h"

#include <algorithm>
#include <vector>

#include "core/log.h"
#include "gfx/render_tier.h"

namespace gfx {

bool Device::init(const Config& config) {
    headless_ = config.headless;

    // Headless keeps a hidden window for the UI layer to attach to, but the
    // GPU device never claims it: the frame lives in the offscreen targets and
    // is read back from there, so no swapchain is created. A display server
    // is still needed for the window itself. SDL's "offscreen" video driver
    // would remove that, but on macOS it fails creating the window (it wants
    // OpenGL), and "dummy" leaves SDL GPU with no backend. Which driver
    // Linux-over-SSH needs is a P2 question (docs/PORTING.md), settled on x99.
#if !defined(__APPLE__) && !defined(_WIN32)
    // On Linux a headless run needs no display server at all: SDL's
    // offscreen video driver makes the window, and it creates Vulkan
    // surfaces through VK_EXT_headless_surface. That is what lets a render
    // run over SSH with nobody logged in. The window must say it is a
    // Vulkan one, or the driver tries to load OpenGL for it.
    const bool offscreen = headless_ && !SDL_getenv("DISPLAY") && !SDL_getenv("WAYLAND_DISPLAY");
    if (offscreen) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return SDL_FAIL("SDL_Init");

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (headless_) flags |= SDL_WINDOW_HIDDEN;
#if !defined(__APPLE__) && !defined(_WIN32)
    if (offscreen) flags |= SDL_WINDOW_VULKAN;
#endif

    window_ = SDL_CreateWindow(config.title, config.width, config.height, flags);
    if (!window_) return SDL_FAIL("SDL_CreateWindow");

    hdr_ = active_tier().hdr;
    rhi::DeviceConfig rhi_config;
    rhi_config.headless = headless_;
    rhi_config.driver = config.gpu_driver;
    // The GPU debug layer: on in a development build, off in a package (a
    // player has no use for its validation, and some D3D12 implementations,
    // D3DMetal under Wine among them, crash on asking for it).
    // DRAGON_GPU_DEBUG=0 or 1 overrides either way.
#ifdef DRAGON_SHADERCROSS
    rhi_config.debug = true;
#else
    rhi_config.debug = false;
#endif
    if (const char* forced = SDL_getenv("DRAGON_GPU_DEBUG")) rhi_config.debug = forced[0] == '1';
    rhi_ = rhi::Device::create(window_, rhi_config);
    if (!rhi_) return false;

    if (!rhi_->supports_format(rhi::Format::D32F, rhi::TEXTURE_DEPTH_TARGET)) {
        depth_format_ = rhi::Format::D24;
        LOG_WARN("D32_FLOAT unsupported, falling back to D24_UNORM");
    }

    // Headless never presents, so nothing else establishes the render size.
    if (headless_ && !ensure_targets(uint32_t(config.width), uint32_t(config.height))) return false;

    LOG_INFO("GPU device: %s%s", rhi_->driver_name(), headless_ ? " (headless)" : "");
    return true;
}

void Device::shutdown() {
    if (rhi_) {
        rhi_->wait_idle();
        release_targets();
        rhi_.reset();
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

void Device::release_targets() {
    for (rhi::Texture** target : {&scene_color_, &scene_hdr_, &bloom_a_, &bloom_b_, &depth_, &scene_world_}) {
        rhi_->destroy(*target);
        *target = nullptr;
    }
    render_w_ = render_h_ = 0;
    world_w_ = world_h_ = 0;
}

void Device::set_world_scale(float scale) {
    scale = scale < 0.25f ? 0.25f : scale > 1.0f ? 1.0f : scale;
    if (scale == world_scale_) return;
    world_scale_ = scale;
    if (!rhi_ || render_w_ == 0) return;  // sized at the first frame
    const uint32_t w = render_w_, h = render_h_;
    rhi_->wait_idle();
    release_targets();
    ensure_targets(w, h);
}

void Device::finish_world() {
    if (scene_world_) rhi_->blit(scene_world_, world_w_, world_h_, scene_color_, render_w_, render_h_);
}

bool Device::ensure_targets(uint32_t w, uint32_t h) {
    if (scene_color_ && depth_ && render_w_ == w && render_h_ == h) return true;
    if (w == 0 || h == 0) return false;
    release_targets();
    const uint32_t ww = std::max(1u, uint32_t(float(w) * world_scale_ + 0.5f));
    const uint32_t wh = std::max(1u, uint32_t(float(h) * world_scale_ + 0.5f));

    rhi::TextureDesc color;
    color.width = w;
    color.height = h;
    color.format = scene_color_format();
    color.usage = rhi::TEXTURE_COLOR_TARGET | rhi::TEXTURE_SAMPLED;
    scene_color_ = rhi_->create_texture(color, "scene_color");
    if (!scene_color_) return false;

    rhi::TextureDesc world = color;
    world.width = ww;
    world.height = wh;
    // The linear scene, and the two half-size bloom targets -- unless the tier
    // has no HDR, when the world finishes in 8 bits: straight into
    // scene_color at full size, or into its own target to be stretched.
    if (!hdr_) {
        if (ww != w || wh != h) {
            scene_world_ = rhi_->create_texture(world, "scene_world");
            if (!scene_world_) {
                release_targets();
                return false;
            }
        }
    } else {
        rhi::TextureDesc hdr = world;
        hdr.format = scene_hdr_format();
        scene_hdr_ = rhi_->create_texture(hdr, "scene_hdr");
        if (!scene_hdr_) return false;
        rhi::TextureDesc bloom = hdr;
        bloom.width = ww / 2 > 0 ? ww / 2 : 1;
        bloom.height = wh / 2 > 0 ? wh / 2 : 1;
        bloom_a_ = rhi_->create_texture(bloom, "bloom_a");
        bloom_b_ = rhi_->create_texture(bloom, "bloom_b");
        if (!bloom_a_ || !bloom_b_) return false;
    }

    rhi::TextureDesc depth;
    depth.width = ww;
    depth.height = wh;
    depth.format = depth_format_;
    depth.usage = rhi::TEXTURE_DEPTH_TARGET;
    depth_ = rhi_->create_texture(depth, "scene_depth");
    if (!depth_) {
        release_targets();
        return false;
    }

    render_w_ = w;
    render_h_ = h;
    world_w_ = ww;
    world_h_ = wh;
    LOG_INFO("render targets resized to %ux%u%s, world %ux%u", w, h, hdr_ ? "" : " (LDR)", ww, wh);
    return true;
}

bool Device::begin_frame() {
    uint32_t w = 0, h = 0;
    const rhi::FrameStatus status = rhi_->begin_frame(&w, &h);
    if (status != rhi::FrameStatus::Ready) return false;
    if (!headless_ && !ensure_targets(w, h)) {
        rhi_->end_frame(nullptr, 0, 0, nullptr);  // submit what little there is
        return false;
    }
    in_frame_ = true;
    return true;
}

rhi::Pass* Device::begin_main_pass(float r, float g, float b) {
    rhi::PassDesc pass;
    pass.color = hdr_ ? scene_hdr_ : scene_world_ ? scene_world_ : scene_color_;
    pass.clear_color = true;
    pass.clear_rgba[0] = r;
    pass.clear_rgba[1] = g;
    pass.clear_rgba[2] = b;
    pass.clear_rgba[3] = 1.0f;
    pass.depth = depth_;
    pass.clear_depth = true;
    // The far plane: 0 under reversed-Z, 1 in the conventional layout a
    // retro tier uses (gfx/render_tier.h).
    pass.clear_depth_value = depth_convention().reversed ? 0.0f : 1.0f;
    return rhi_->begin_pass(pass);
}

rhi::Pass* Device::begin_color_pass(rhi::Texture* target, bool clear) {
    rhi::PassDesc pass;
    pass.color = target;
    pass.clear_color = clear;
    return rhi_->begin_pass(pass);
}

rhi::Pass* Device::begin_ui_pass() {
    rhi::PassDesc pass;
    pass.color = scene_color_;
    return rhi_->begin_pass(pass);
}

void Device::end_pass(rhi::Pass* pass) {
    if (pass) rhi_->end_pass(pass);
}

void Device::save_screenshot(const std::vector<uint8_t>& rgba) {
    const uint32_t w = render_w_, h = render_h_;
    if (rgba.size() < size_t(w) * h * 4) {
        LOG_ERROR("screenshot: readback failed");
        return;
    }
    // SDL_PIXELFORMAT_RGBA32 is the byte-order-R,G,B,A alias, which is what
    // the RGBA8 scene target holds in memory.
    SDL_Surface* surface = SDL_CreateSurfaceFrom(int(w), int(h), SDL_PIXELFORMAT_RGBA32,
                                                 const_cast<uint8_t*>(rgba.data()), int(w * 4));
    if (!surface) return;
    if (SDL_SaveBMP(surface, screenshot_path_.c_str())) {
        LOG_INFO("screenshot -> %s (%ux%u)", screenshot_path_.c_str(), w, h);
    } else {
        SDL_FAIL("SDL_SaveBMP");
    }
    SDL_DestroySurface(surface);
}

void Device::end_frame() {
    if (!in_frame_) return;
    in_frame_ = false;
    if (screenshot_path_.empty()) {
        rhi_->end_frame(scene_color_, render_w_, render_h_, nullptr);
        return;
    }
    std::vector<uint8_t> rgba;
    rhi_->end_frame(scene_color_, render_w_, render_h_, &rgba);
    save_screenshot(rgba);
    screenshot_path_.clear();
}

}  // namespace gfx
