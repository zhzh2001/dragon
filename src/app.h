#pragma once

#include <string>

#include "core/input.h"
#include "core/math.h"
#include "editor/imgui_layer.h"
#include "game/chase_camera.h"
#include "game/debug_camera.h"
#include "game/dragon_proxy.h"
#include "game/flight.h"
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
    void respawn_dragon();
    void update_stick(float dt);
    game::FlightInput read_flight_input() const;
    gfx::ModelUniforms dragon_model_uniforms() const;
    const gfx::Camera& active_camera() const;
    void draw_flight_debug();
    void build_flight_ui();

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

    game::FlightModel flight_;
    game::ChaseCamera chase_;
    game::DragonProxyDims dragon_dims_;
    gfx::Mesh dragon_mesh_;

    // How the player commands the dragon. A flight game controlled only by
    // keyboard misrepresents its own flight model, so the mouse drives a virtual
    // stick and is the default.
    struct Controls {
        bool mouse_stick = true;
        float mouse_sensitivity = 0.0040f;  // stick units per pixel
        // Half-life for the stick returning to centre. Keeps the stick from
        // drifting off-centre over a long session without forcing constant
        // mouse motion. Zero disables the return entirely.
        float stick_return = 0.85f;
        bool invert_pitch = false;
        // Roll is the right choice for a winged creature; yaw is offered because
        // some players expect it and it costs nothing to try.
        bool mouse_yaws = false;
        float keyboard_pitch_rate = 2.4f;  // stick units per second
        float keyboard_roll_rate = 3.2f;
    } controls_;
    core::Vec2 stick_ = core::Vec2{0.0f, 0.0f};
    bool mouse_captured_ = false;
    void set_mouse_captured(bool captured);

    // Free-fly is for surveying the world; the chase camera is the game.
    bool free_camera_ = false;
    bool show_forces_ = false;
    bool show_flight_path_ = true;

    // Rolling telemetry for the ImGui plots. Reading these while flying is how
    // the flight model actually gets tuned.
    static constexpr int TELEMETRY_SAMPLES = 240;
    struct Telemetry {
        float airspeed[TELEMETRY_SAMPLES] = {};
        float altitude[TELEMETRY_SAMPLES] = {};
        float aoa_deg[TELEMETRY_SAMPLES] = {};
        float g_load[TELEMETRY_SAMPLES] = {};
        float climb[TELEMETRY_SAMPLES] = {};
        float energy[TELEMETRY_SAMPLES] = {};
        int cursor = 0;
    } telemetry_;
    void push_telemetry(const game::FlightState& state);

    // Breadcrumb trail of where the dragon has been, so a manoeuvre can be
    // inspected after the fact instead of only felt.
    static constexpr int TRAIL_POINTS = 400;
    core::Vec3 trail_[TRAIL_POINTS];
    int trail_count_ = 0;
    int trail_cursor_ = 0;
    float trail_timer_ = 0.0f;
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
