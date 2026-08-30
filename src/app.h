#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/input.h"
#include "core/math.h"
#include "editor/imgui_layer.h"
#include "anim/dragon_rig.h"
#include "anim/gltf_loader.h"
#include "game/chase_camera.h"
#include "game/bot.h"
#include "game/combat.h"
#include "game/match.h"
#include "game/studio.h"
#include "game/debug_camera.h"
#include "game/autopilot.h"
#include "game/course.h"
#include "game/flight.h"
#include "game/rally.h"
#include "game/terrain.h"
#include "gfx/debug_draw.h"
#include "gfx/particles.h"
#include "audio/audio.h"
#include "gfx/device.h"
#include "gfx/mesh.h"
#include "gfx/pipeline.h"
#include "gfx/scene_uniforms.h"
#include "gfx/shadow_map.h"
#include "gfx/texture.h"
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
    float inspect_distance = 30.0f;

    // --autopilot flies the selected course unattended. Used to verify the whole
    // rally loop headlessly, and it doubles as the seed of the bot AI.
    bool autopilot = false;

    // --combat arms the dragon and spawns a wave, for verifying combat without
    // a human at the keyboard.
    bool combat = false;
    // --studio N drops into the animation studio playing scenario N: the dragon
    // pinned in place flying a scripted, repeatable manoeuvre, for looking at
    // the rig instead of chasing it.
    int studio_scenario = -1;  // -1 = off
    // --attack holds breath and fires continuously. The combat equivalent of
    // --input: it puts the flame and the projectiles on screen for a capture.
    bool attack = false;
    // --bots N replaces the sentinels with N bot dragons at startup.
    int bots = 0;
    // --match starts a deathmatch against the spawned bots immediately.
    bool match = false;
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
    void build_dragon_ui();
    void build_combat_ui();
    game::CombatInput read_combat_input() const;
    void draw_combat(SDL_GPURenderPass* pass);
    void draw_combat_hud();
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

    // Combat. Off until switched on: the rally is still the default activity,
    // and sentinels shooting at a player trying to set a lap time is nobody's
    // idea of a good race.
    // Animation studio: scripted manoeuvres replace the flight state that the
    // rig, the dragon transform and the chase camera see. Flight, rally and
    // combat idle while it is up.
    bool studio_active_ = false;
    int studio_scenario_ = 0;
    float studio_time_ = 0.0f;
    float studio_time_scale_ = 1.0f;
    core::Vec3 studio_centre_ = core::Vec3::zero();
    game::FlightState studio_state_;
    const game::FlightState& dragon_state() const {
        return studio_active_ ? studio_state_ : flight_.state();
    }
    void build_studio_ui();

    game::Combat combat_;
    bool combat_enabled_ = false;

    // M14: bot dragons. Each flies its own FlightModel through a BotPilot and
    // occupies an external hostile slot in Combat, which handles its health,
    // lock-on, hits and respawn timing. Pointers because DragonRig carries
    // simulation state that must not be copied about by vector growth.
    struct BotShip {
        game::FlightModel flight;
        game::BotPilot pilot;
        anim::DragonRig rig;
        int slot = -1;
        bool was_alive = true;
        bool grounded_last_frame = false;
        bool breathing = false;
        float grounded_time = 0.0f;
        float hit_cry_cooldown = 0.0f;
        float last_health = 0.0f;
    };
    std::vector<std::unique_ptr<BotShip>> bots_;
    int bot_count_ = 2;
    // Template applied to every spawned or preset-adjusted pilot.
    game::BotTuning bot_tuning_;
    int bot_skill_ = 1;  // 0 rookie, 1 veteran, 2 ace
    bool manual_aim_ = false;
    float saved_aim_assist_ = 0.9f;
    // Per-tier bot survivability, applied at spawn (health) and live (regen).
    float bot_health_ = 80.0f;

    // M15: the match loop. Countdown, fight to a kill target, results,
    // rematch. Idle means free play, which everything else already was.
    game::Match match_;
    void start_match();
    void apply_bot_skill(int level);
    void spawn_bots(int count);
    void place_bot(BotShip& bot, uint32_t seed);
    void update_bots(float dt);
    gfx::Mesh sphere_mesh_;
    gfx::ParticleSystem particles_;
    audio::Audio audio_;
    // Edge detection for the sounds tied to state transitions.
    float previous_fire_cooldown_ = 0.0f;
    float previous_flap_phase_ = 0.0f;
    float hit_sound_cooldown_ = 0.0f;
    bool was_boosting_ = false;
    float boost_fov_ = 0.0f;
    float base_fov_ = 62.0f;
    // Deterministic jitter for the emitters.
    uint32_t particle_rng_ = 1u;
    float particle_unit();
    void emit_flame(core::Vec3 origin, core::Vec3 direction, float range, bool hostile,
                    float dt);
    void emit_impact(core::Vec3 position, bool hostile, bool on_terrain);
    float hit_marker_ = 0.0f;
    core::Vec3 hit_marker_position_ = core::Vec3::zero();
    float damage_flash_ = 0.0f;
    // Edge detection for gamepad buttons, which core::Input only reports as
    // held state.
    bool cycle_button_was_down_ = false;
    bool cycle_requested_ = false;
    core::Vec3 damage_direction_ = core::Vec3::zero();
    float damage_marker_ = 0.0f;
    // Master switch for every ImGui panel. The tuning UI covers most of the
    // screen, which is fine while tuning and useless while playing.
    bool show_panels_ = true;
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
    std::vector<anim::AnimationClip> dragon_animations_;
    std::vector<SDL_GPUTexture*> dragon_textures_;
    SDL_GPUSampler* model_sampler_ = nullptr;
    bool show_skeleton_ = false;
    gfx::MaterialToggles material_toggles_;

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
        // How hard holding tuck noses the dragon over when the stick is
        // neutral. 0 restores fold-only tuck.
        float tuck_nose_over = 0.45f;
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
        // Free look pitch on the right stick. Inverted by default: pushing the
        // stick away tilts the view up, which is what a stick means to anyone who
        // has flown one.
        bool invert_free_look_y = true;

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
