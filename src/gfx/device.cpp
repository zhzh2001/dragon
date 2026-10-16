#include "gfx/device.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "core/log.h"
#include "gfx/render_tier.h"
#ifdef DRAGON_GLES
#include "rhi/gles/gles_backend.h"
#endif

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
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__ANDROID__)
    // On Linux a headless run needs no display server at all: SDL's
    // offscreen video driver makes the window, and it creates Vulkan
    // surfaces through VK_EXT_headless_surface. That is what lets a render
    // run over SSH with nobody logged in. The window must say it is a
    // Vulkan one, or the driver tries to load OpenGL for it.
    const bool offscreen = headless_ && !SDL_getenv("DISPLAY") && !SDL_getenv("WAYLAND_DISPLAY");
    if (offscreen) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#endif
    // GLES 2.0 (rhi/gles): an OpenGL window, ES attributes, and on the Mac
    // SDL's EGL pointed at ANGLE.
    const bool gles = config.gpu_driver == "gles";
#ifdef DRAGON_GLES
    if (gles) rhi::gles::prepare_video();
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return SDL_FAIL("SDL_Init");
#ifdef DRAGON_GLES
    if (gles) rhi::gles::prepare_window();
#endif

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (headless_) flags |= SDL_WINDOW_HIDDEN;
    // D3D9 takes the display mode itself, on an ordinary window.
    const bool d3d9 = config.gpu_driver.rfind("direct3d9", 0) == 0;
    if (config.fullscreen && !headless_ && !d3d9) flags |= SDL_WINDOW_FULLSCREEN;
#ifdef SDL_PLATFORM_ANDROID
    // A phone's game is the whole screen: SDL's fullscreen on Android hides
    // the status and navigation bars (immersive mode).
    if (!headless_) flags |= SDL_WINDOW_FULLSCREEN;
#endif
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__ANDROID__)
    if (offscreen && !gles) flags |= SDL_WINDOW_VULKAN;
#endif
    if (gles) flags |= SDL_WINDOW_OPENGL;

    window_ = SDL_CreateWindow(config.title, config.width, config.height, flags);
    if (!window_) return SDL_FAIL("SDL_CreateWindow");

    hdr_ = active_tier().hdr;
    rhi::DeviceConfig rhi_config;
    rhi_config.headless = headless_;
    rhi_config.driver = config.gpu_driver;
    rhi_config.fixed_function = active_tier().fixed_function;
    rhi_config.fullscreen = config.fullscreen;
    rhi_config.color16 = config.color16;
    rhi_config.vsync = config.vsync;
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
    // An LDR world target in 16 bits where the backend has the format (D3D9).
    color16_ = config.color16 && !hdr_ && rhi_->supports_format(rhi::Format::RGB565, rhi::TEXTURE_COLOR_TARGET);

    // Headless never presents, so nothing else establishes the render size.
    requested_w_ = uint32_t(config.width);
    requested_h_ = uint32_t(config.height);
    if (headless_) {
        uint32_t w = requested_w_, h = requested_h_;
        capped(w, h);
        if (!ensure_targets(w, h)) return false;
    }

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
    // A world drawn smaller than the window is stretched into the UI's
    // target: the frame's own image where the backend hands one out
    // (frame_target), and the frame then presents with no further copy.
    ui_target_ = scene_color_;
    if (rhi::Texture* frame = frame_target()) ui_target_ = frame;
    if (scene_world_) rhi_->blit(scene_world_, world_w_, world_h_, ui_target_, render_w_, render_h_);
}

rhi::Texture* Device::frame_target() const {
    // An LDR tier finishes in 8 bits, so the frame's own image can take the
    // world (at full size) or the UI (over a stretched world) -- the copy
    // into it that scene_color would need was a full-screen textured quad,
    // a third of the frame on the M6. Not on a frame that is read back:
    // the back buffer cannot be.
    if (hdr_ || (!screenshot_path_.empty() && scene_color_)) return nullptr;
    // Nor on a capped frame, which is smaller than the image it stretches to.
    if (!headless_ && (render_w_ != present_w_ || render_h_ != present_h_)) return nullptr;
    return rhi_->frame_target();
}

bool Device::ensure_targets(uint32_t w, uint32_t h) {
    if ((scene_color_ || no_color_targets_) && depth_ && render_w_ == w && render_h_ == h) return true;
    if (w == 0 || h == 0) return false;
    release_targets();
    const uint32_t ww = std::max(1u, uint32_t(float(w) * world_scale_ + 0.5f));
    const uint32_t wh = std::max(1u, uint32_t(float(h) * world_scale_ + 0.5f));

    rhi::TextureDesc color;
    color.width = w;
    color.height = h;
    color.format = scene_color_format();
    color.usage = rhi::TEXTURE_COLOR_TARGET | rhi::TEXTURE_SAMPLED;
    scene_color_ = no_color_targets_ ? nullptr : rhi_->create_texture(color, "scene_color");
    if (!scene_color_) {
        // A card that cannot render into a texture (the oldest D3D9 drivers):
        // the frame's own image takes everything (frame_target), the world
        // at full size. No screenshot, which reads scene_color back.
        if (headless_ || hdr_) return false;
        if (!no_color_targets_) LOG_WARN("no render-target textures: drawing straight into the back buffer");
        no_color_targets_ = true;
    }

    rhi::TextureDesc world = color;
    world.width = ww;
    world.height = wh;
    // The linear scene, and the two half-size bloom targets -- unless the tier
    // has no HDR, when the world finishes in 8 bits: straight into
    // scene_color at full size, or into its own target to be stretched.
    if (!hdr_) {
        if ((ww != w || wh != h) && !no_color_targets_) {
            if (color16_) world.format = rhi::Format::RGB565;
            scene_world_ = rhi_->create_texture(world, "scene_world");
            // No world target: the world at full size instead of stretched.
            if (!scene_world_) LOG_WARN("no %ux%u world target: the world at full size", ww, wh);
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
    depth.width = scene_world_ ? ww : w;
    depth.height = scene_world_ ? wh : h;
    depth.format = depth_format_;
    depth.usage = rhi::TEXTURE_DEPTH_TARGET | (depth_sampled_ ? rhi::TEXTURE_SAMPLED : 0);
    depth_ = rhi_->create_texture(depth, "scene_depth");
    if (!depth_) {
        release_targets();
        return false;
    }

    render_w_ = w;
    render_h_ = h;
    world_w_ = scene_world_ ? ww : w;
    world_h_ = scene_world_ ? wh : h;
    LOG_INFO("render targets resized to %ux%u%s, world %ux%u", w, h, hdr_ ? "" : " (LDR)", ww, wh);
    return true;
}

void Device::capped(uint32_t& w, uint32_t& h) const {
    if (frame_height_ == 0 || h <= frame_height_) return;
    w = std::max(1u, uint32_t(uint64_t(w) * frame_height_ / h));
    h = frame_height_;
}

void Device::fit_display_mode() {
    // On a desktop or a phone the window is a guest of the display and its
    // frame is stretched by the present's blit. On a console the window IS
    // the display, at the desktop mode until told otherwise: the Pi 1B
    // scanned a 1920x1080 32-bit buffer out of the memory its CPU and GPU
    // share (500 MB/s at 60 Hz) and the blit filled all of it each frame --
    // a 960x540 frame ran at 8-12 fps there against 30 headless. A mode the
    // frame's size moves the stretch into the monitor.
    const char* video = SDL_GetCurrentVideoDriver();
    if (!window_ || !video || SDL_strcmp(video, "kmsdrm") != 0) return;
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
    if (!desktop) return;
    // Frame height caps the mode itself: the largest mode no taller than
    // it (any shape: on the Pi 1B a 16:9 1280x720 was 19 fps where 4:3
    // 640x480 is 24), or the smallest if every mode is taller.
    const SDL_DisplayMode* best = desktop;
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
    if (frame_height_ > 0) {
        const auto area = [](const SDL_DisplayMode* m) { return m->w * m->h; };
        const SDL_DisplayMode* fits = nullptr;
        const SDL_DisplayMode* smallest = nullptr;
        for (int i = 0; modes && i < count; ++i) {
            const SDL_DisplayMode* m = modes[i];
            if (m->refresh_rate > desktop->refresh_rate + 1.0f) continue;
            const auto better = [&](const SDL_DisplayMode* than, bool larger) {
                if (!than) return true;
                if (area(m) != area(than)) return larger ? area(m) > area(than) : area(m) < area(than);
                return m->refresh_rate > than->refresh_rate;
            };
            if (m->h <= int(frame_height_) && better(fits, true)) fits = m;
            if (better(smallest, false)) smallest = m;
        }
        if (fits) best = fits;
        else if (smallest) best = smallest;
    }
    SDL_DisplayMode chosen = *best;
    SDL_free(modes);
    if (!SDL_SetWindowFullscreenMode(window_, &chosen) || !SDL_SetWindowFullscreen(window_, true)) {
        LOG_WARN("display mode %dx%d: %s", chosen.w, chosen.h, SDL_GetError());
        return;
    }
    SDL_SyncWindow(window_);
    LOG_INFO("display mode %dx%d @ %.0f Hz (desktop %dx%d)", chosen.w, chosen.h, chosen.refresh_rate,
             desktop->w, desktop->h);
}

void Device::set_frame_height(uint32_t lines) {
    if (lines == frame_height_) return;
    frame_height_ = lines;
    if (!headless_) fit_display_mode();
    // A window's frame is sized at the next begin_frame; a headless one now.
    if (!headless_ || !rhi_ || render_w_ == 0) return;
    uint32_t w = requested_w_, h = requested_h_;
    capped(w, h);
    rhi_->wait_idle();
    release_targets();
    ensure_targets(w, h);
}

bool Device::begin_frame() {
    uint32_t w = 0, h = 0;
    const rhi::FrameStatus status = rhi_->begin_frame(&w, &h);
    if (status != rhi::FrameStatus::Ready) return false;
    present_w_ = w;
    present_h_ = h;
    capped(w, h);
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
    if (!scene_world_) {
        if (rhi::Texture* frame = frame_target()) pass.color = frame;
    }
    // The fixed-function tier's sky dome covers every pixel the world leaves,
    // so a colour clear is fill for nothing (DRAGON_CLEAR=1 restores it).
    pass.clear_color = !active_tier().fixed_function || SDL_getenv("DRAGON_CLEAR") != nullptr;
    pass.clear_rgba[0] = r;
    pass.clear_rgba[1] = g;
    pass.clear_rgba[2] = b;
    pass.clear_rgba[3] = 1.0f;
    pass.depth = depth_;
    pass.clear_depth = true;
    // The far plane: 0 under reversed-Z, 1 in the conventional layout a
    // retro tier uses (gfx/render_tier.h).
    pass.clear_depth_value = depth_convention().reversed ? 0.0f : 1.0f;
    pass.keep_depth = depth_sampled_;
    return rhi_->begin_pass(pass);
}

rhi::Texture* Device::scene_target() const {
    if (hdr_) return scene_hdr_;
    return scene_world_ ? scene_world_ : scene_color_;
}

rhi::Pass* Device::resume_main_pass() {
    rhi::PassDesc pass;
    pass.color = scene_target();
    pass.depth = depth_;
    pass.keep_depth = depth_sampled_;
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
    pass.color = ui_target_ ? ui_target_ : scene_color_;
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
    rhi::Texture* source = ui_target_ ? ui_target_ : scene_color_;
    ui_target_ = nullptr;
    if (!screenshot_path_.empty() && !scene_color_) {
        LOG_WARN("screenshot: no render-target texture to read back on this device");
        screenshot_path_.clear();
    }
    if (screenshot_path_.empty()) {
        rhi_->end_frame(source, render_w_, render_h_, nullptr);
        return;
    }
    std::vector<uint8_t> rgba;
    rhi_->end_frame(scene_color_, render_w_, render_h_, &rgba);
    save_screenshot(rgba);
    screenshot_path_.clear();
}

}  // namespace gfx
