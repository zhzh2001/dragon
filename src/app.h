#pragma once

#include <string>

#include "core/input.h"
#include "editor/imgui_layer.h"
#include "game/debug_camera.h"
#include "gfx/debug_draw.h"
#include "gfx/device.h"
#include "gfx/pipeline.h"

namespace app {

struct Options {
    int frames = 0;  // 0 = run until quit
    bool headless = false;
    std::string screenshot;  // empty = none
};

Options parse_options(int argc, char** argv);

// Owns every subsystem and runs the frame loop. Kept separate from main() so
// the ordering constraints between subsystems live in one readable place.
class App {
public:
    bool init(const Options& options);
    void shutdown();
    void run();

private:
    void pump_events();
    void update(float dt);
    void build_ui(float dt);
    void render();

    Options options_;

    gfx::Device device_;
    gfx::PipelineCache pipelines_;
    gfx::DebugDraw debug_;
    editor::ImGuiLayer ui_;
    core::Input input_;
    game::DebugCamera camera_;

    bool running_ = false;
    bool mouse_look_ = false;
    int frame_index_ = 0;
    int shader_reloads_ = 0;
    float reload_timer_ = 0.0f;

    // Scene tuning, all live-editable.
    float clear_color_[3] = {0.055f, 0.07f, 0.10f};
    float grid_half_extent_ = 200.0f;
    float grid_spacing_ = 5.0f;
    bool show_grid_ = true;
    bool show_probes_ = true;

    // Rolling frame-time average, so the readout is steady enough to read.
    static constexpr int FRAME_HISTORY = 90;
    float frame_history_[FRAME_HISTORY] = {};
    int frame_cursor_ = 0;
    int frame_filled_ = 0;
    float average_frame_ms() const;
};

}  // namespace app
