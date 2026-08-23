#pragma once

#include <string>

#include "core/input.h"
#include "core/math.h"
#include "editor/imgui_layer.h"
#include "game/debug_camera.h"
#include "game/terrain.h"
#include "gfx/debug_draw.h"
#include "gfx/device.h"
#include "gfx/mesh.h"
#include "gfx/pipeline.h"
#include "gfx/scene_uniforms.h"
#include "gfx/shadow_map.h"
#include "gfx/world_renderer.h"

namespace app {

struct Options {
    int frames = 0;  // 0 = run until quit
    bool headless = false;
    std::string screenshot;  // empty = none

    // --cam x,y,z,tx,ty,tz places the camera for a verification capture. Without
    // it the app frames the valley itself.
    bool has_camera = false;
    core::Vec3 camera_position;
    core::Vec3 camera_target;

    // Hides the ImGui panels, for captures that should show only the world.
    bool hide_ui = false;
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

    void regenerate_terrain();
    void frame_camera_on_valley();

    Options options_;

    gfx::Device device_;
    gfx::PipelineCache pipelines_;
    gfx::DebugDraw debug_;
    gfx::WorldRenderer world_;
    gfx::ShadowMap shadow_;
    editor::ImGuiLayer ui_;
    core::Input input_;
    game::DebugCamera camera_;

    game::Terrain terrain_;
    game::TerrainSettings terrain_settings_;
    gfx::Mesh terrain_mesh_;
    gfx::Lighting lighting_;
    gfx::TerrainMaterial material_;
    float time_seconds_ = 0.0f;

    bool running_ = false;
    bool mouse_look_ = false;
    int frame_index_ = 0;
    int shader_reloads_ = 0;
    float reload_timer_ = 0.0f;

    // Scene tuning, all live-editable.
    bool show_grid_ = false;
    bool show_probes_ = false;
    bool show_ground_probe_ = true;

    // Rolling frame-time average, so the readout is steady enough to read.
    static constexpr int FRAME_HISTORY = 90;
    float frame_history_[FRAME_HISTORY] = {};
    int frame_cursor_ = 0;
    int frame_filled_ = 0;
    float average_frame_ms() const;
};

}  // namespace app
