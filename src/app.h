#pragma once

#include <string>
#include <vector>

#include "core/input.h"
#include "core/math.h"
#include "editor/imgui_layer.h"
#include "anim/dragon_rig.h"
#include "anim/gltf_loader.h"
#include "game/chase_camera.h"
#include "game/debug_camera.h"
#include "game/autopilot.h"
#include "game/course.h"
#include "game/flight.h"
#include "game/rally.h"
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

    // --cam-mode chase|action|cinematic|fp selects the camera for a capture.
    int camera_mode = 0;
    bool first_person = false;

    // --input pitch,roll,yaw,flap,tuck,brake holds a constant control input.
    // Lets a specific flight state be captured and inspected without a human at
    // the keyboard.
    bool has_input_override = false;
    float input_override[6] = {};

    // --course N selects a course by index for a capture or an autopilot run.
    int course_index = 0;

    // --bind-pose freezes the rig, so an imported asset can be checked against
    // its authored rest pose before animation is blamed for anything.
    bool bind_pose = false;

    // --inspect frames the dragon closely from a fixed offset, for looking at
    // the rig rather than at the world.
    bool inspect = false;
    float inspect_angle_deg = 35.0f;

    // --autopilot flies the selected course unattended. Used to verify the whole
    // rally loop headlessly, and it doubles as the seed of the bot AI.
    bool autopilot = false;
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
    core::Vec2 read_free_look(float dt) const;
    void apply_camera_preset(int index);
    gfx::ModelUniforms dragon_model_uniforms() const;
    const gfx::Camera& active_camera() const;
    void draw_flight_debug();
    void build_flight_ui();
    void build_rally_ui();
    void draw_hud();
    void select_course(int index);
    void rebuild_courses();
    gfx::ModelUniforms ghost_model_uniforms(const game::GhostSample& sample) const;
    void draw_skeleton_debug();

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
    // The dragon is a real skinned rig now: skeleton, skinned mesh, and
    // procedural animation driven by flight state.
    anim::DragonShape dragon_shape_;
    anim::Skeleton dragon_skeleton_;
    anim::DragonJoints dragon_joints_;
    anim::SkinnedMesh dragon_mesh_;
    anim::DragonRig dragon_rig_;
    // The ghost needs its own rig: the spring chains carry state, so one rig
    // cannot serve two dragons.
    anim::DragonRig ghost_rig_;
    bool show_skeleton_ = false;

    // An imported asset arrives in whatever scale and orientation its author
    // used. Rather than guess at load time, the correction is a live transform
    // so it can be aligned by eye and then written down.
    struct AssetTransform {
        float scale = 1.0f;
        float yaw_deg = 0.0f;
        float pitch_deg = 0.0f;
        float roll_deg = 0.0f;
        core::Vec3 offset = core::Vec3::zero();

        core::Mat4 matrix() const {
            const core::Quat rotation =
                core::from_euler(core::radians(pitch_deg), core::radians(yaw_deg),
                                 core::radians(roll_deg));
            return core::Mat4::trs(offset, rotation, core::Vec3(scale));
        }
    } asset_;
    bool using_imported_dragon_ = false;
    std::string dragon_source_ = "generated";

    // ---- rally ----
    game::Rally rally_;
    std::vector<game::Course> courses_;
    int current_course_ = 0;
    game::BestTimes best_times_;
    gfx::Mesh ring_mesh_;
    // Unit-radius torus, scaled per ring, so one mesh serves every checkpoint.
    static constexpr float RING_MESH_RADIUS = 1.0f;
    bool show_hud_ = true;
    bool show_ghost_ = true;
    bool show_ring_path_ = true;
    bool autopilot_ = false;
    game::AutopilotTuning autopilot_tuning_;

    // Difficulty assists.
    //
    // A tighter bank limit is NOT easier: turn radius goes as v^2/(g tan bank),
    // so 70 degrees turns in 80 m where 55 degrees needs 145 m. What a new pilot
    // needs is a bank deep enough to turn inside a checkpoint gap, plus a barrier
    // that stops the roll continuing past vertical. So the presets keep the bank
    // generous and vary the checkpoint size instead.
    struct Assists {
        bool auto_flap = true;
        // Flap harder the further below this the airspeed is.
        float auto_flap_speed = 48.0f;
        // And always flap when this close to the ground, which is where running
        // out of energy actually kills you.
        float auto_flap_clearance = 75.0f;
        float ring_radius_scale = 1.5f;
    } assists_;
    int assist_preset_ = 0;  // 0 relaxed, 1 standard, 2 expert
    void apply_assist_preset(int index);
    // Time remaining on the split-delta flash after passing a checkpoint.
    float split_flash_ = 0.0f;
    float miss_flash_ = 0.0f;

    // How the player commands the dragon.
    //
    // The stick is driven toward a *target* derived from whatever device is
    // active, never accumulated from motion history. An accumulating stick means
    // the neutral point drifts with everything you have ever done, which makes
    // recovering from a bad attitude a fight against your own input rather than
    // against the air.
    struct Controls {
        // Mouse steering is off by default: it needs accumulation to work at
        // all, and accumulation is exactly what makes it hard to control.
        bool mouse_stick = false;
        float mouse_sensitivity = 0.0022f;
        // Half-life for mouse deflection decaying back to centre. Short, so the
        // mouse behaves like a spring-centred stick rather than a trackball.
        float mouse_return = 0.35f;

        // Half-life for the stick chasing its target. Small enough to feel
        // immediate, large enough not to be a step input.
        float stick_smoothing = 0.055f;

        float gamepad_deadzone = 0.12f;
        // Exponent applied to gamepad deflection. Above 1 this gives fine
        // control near centre and full authority at the edge.
        float gamepad_expo = 1.7f;

        // Default on: W lowers the nose, S raises it, which is the flight-stick
        // convention most people expect from a flying game. It flips the gamepad
        // pitch axis too.
        bool invert_pitch = true;

        // Free look, in degrees. The mouse figure is per pixel of right-drag;
        // the gamepad figure is per second at full stick deflection.
        float free_look_mouse = 0.16f;
        float free_look_gamepad = 110.0f;
    } controls_;
    core::Vec2 stick_ = core::Vec2{0.0f, 0.0f};
    core::Vec2 mouse_deflection_ = core::Vec2{0.0f, 0.0f};
    bool mouse_captured_ = false;
    void set_mouse_captured(bool captured);

    // Free-fly is for surveying the world; the chase camera is the game.
    bool free_camera_ = false;
    int camera_preset_ = 0;  // 0 chase, 1 action, 2 cinematic
    bool show_camera_rig_ = false;
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
