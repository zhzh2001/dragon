#pragma once

#include <SDL3/SDL_events.h>

#include "gfx/device.h"

struct ImFont;

namespace editor {

// Dear ImGui setup and per-frame plumbing.
//
// ImGui is the engine's editor: it is where flight coefficients, camera feel,
// and AI weights get tuned while the game runs. Frame order matters --
// begin_frame, build UI, then prepare_draw_data BEFORE the render pass opens,
// then render inside it.
class ImGuiLayer {
public:
    bool init(gfx::Device& device);
    // The HUD kit's two faces, or null when no TTF was found (the kit and the
    // panels then draw with ImGui's default).
    ImFont* numeral_font() const { return numeral_font_; }
    ImFont* label_font() const { return label_font_; }
    void shutdown();

    // Feed every SDL event here. Returns true if ImGui consumed it, so gameplay
    // input can be suppressed while the user is dragging a slider.
    bool process_event(const SDL_Event& event);

    // The render target ImGui draws into, in pixels: the framebuffer scale is
    // taken from it rather than from the window, which on a display with a
    // pixel density of 2 reported twice the target and drew every panel and
    // readout at double size, cropped.
    void begin_frame(float target_width, float target_height);

    // Must be called after all UI is built and before Device::begin_main_pass,
    // because the backend issues copy passes to upload vertex data.
    void prepare_draw_data(gfx::Device& device);

    // Call inside the main render pass, after the scene has been drawn.
    void render(gfx::Device& device, SDL_GPURenderPass* pass);

    // True while the mouse is over any ImGui window -- use it to gate camera
    // look and gameplay clicks.
    bool wants_mouse() const;
    bool wants_keyboard() const;

private:
    bool initialized_ = false;
    ImFont* numeral_font_ = nullptr;
    ImFont* label_font_ = nullptr;
    bool has_draw_data_ = false;
};

}  // namespace editor
