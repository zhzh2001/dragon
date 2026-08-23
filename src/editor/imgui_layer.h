#pragma once

#include <SDL3/SDL_events.h>

#include "gfx/device.h"

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
    void shutdown();

    // Feed every SDL event here. Returns true if ImGui consumed it, so gameplay
    // input can be suppressed while the user is dragging a slider.
    bool process_event(const SDL_Event& event);

    void begin_frame();

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
    bool has_draw_data_ = false;
};

}  // namespace editor
