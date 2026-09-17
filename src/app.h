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
#include "game/vegetation.h"
#include "gfx/foliage.h"
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
    // --model PATH loads a different rigged glTF in place of assets/dragon.glb,
    // for trying alternative dragons without touching the tree.
    std::string model;
    // --models A.glb,B.glb,... loads a whole roster. The player flies the
    // first and the bots are dealt the rest in turn, so one match can field
    // more than one species instead of four hue-pushed copies of one mesh.
    std::vector<std::string> models;
    // --hue r,g,b,strength recolours the player's dragon (see ModelUniforms::recolour).
    bool has_hue = false;
    core::Vec4 hue;

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
    // Camera elevation above the dragon's horizontal plane; 90 looks straight
    // down, which is the only view that shows a lateral tail wave.
    float inspect_elevation_deg = 14.5f;
    // --inspect-head orbits the animated head instead of the body centre, for
    // looking at the jaw and the aim.
    bool inspect_head = false;
    // --skeleton draws the posed joints as lines, for telling a rig problem
    // from a skinning one in a headless capture.
    bool skeleton = false;

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
    // --bot-range N spawns the bots N metres out instead of 650, so a capture
    // can hold the player and every rival in one frame.
    float bot_range = 0.0f;  // 0 = leave the default
    // --cycle-models N swaps the player onto the next roster entry every N
    // frames. It exists so the model-swap path -- which re-initialises both
    // rigs and every per-species profile -- can be soaked without a human
    // pressing M, and so a sweep of one scenario across every species is one
    // command.
    int cycle_models = 0;  // 0 = off
    // Headless only: alternate the fixed 1/60 s step between (1+j) and (1-j)
    // times its length on even and odd frames. Reproduces the uneven frame
    // pacing of a live window, which is what exposed the first-person camera
    // lagging the head by a frame.
    float frame_jitter = 0.0f;
    // --match starts a deathmatch against the spawned bots immediately.
    bool match = false;

    // --telemetry [N] logs one line of flight state every N frames (default
    // 60, so once a second at the headless fixed step). A screenshot shows a
    // pose; this shows the state machine behind it -- whether the dragon
    // actually touched down, how long it took, and what the assists were
    // doing while it did. Landing is the case that needed it.
    int telemetry_interval = 0;  // 0 = off
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
    void log_telemetry() const;
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
    // The frame before, so the studio's scripted fireballs can be edges.
    float studio_time_previous_ = 0.0f;
    float studio_time_scale_ = 1.0f;
    core::Vec3 studio_centre_ = core::Vec3::zero();
    game::FlightState studio_state_;
    const game::FlightState& dragon_state() const {
        return studio_active_ ? studio_state_ : flight_.state();
    }
    void build_studio_ui();
    // Hand the chase camera this frame's head and the species' eye offsets.
    void feed_first_person_head(float dt);
    // The solved eye offsets, smoothed: they change only with the head's
    // rotation, and the breath tremor would otherwise shake the whole view.
    float first_person_up_smoothed_ = 0.0f;
    float first_person_back_smoothed_ = 0.0f;
    bool first_person_offsets_valid_ = false;

    game::Combat combat_;
    bool combat_enabled_ = false;
    // Melee bookkeeping for the telemetry line: a capture cannot show whether
    // a bite connected, the counters can.
    int bites_landed_ = 0;
    int bites_taken_ = 0;
    int bites_swung_ = 0;

    // M14: bot dragons. Each flies its own FlightModel through a BotPilot and
    // occupies an external hostile slot in Combat, which handles its health,
    // lock-on, hits and respawn timing. Pointers because DragonRig carries
    // simulation state that must not be copied about by vector growth.
    struct BotShip {
        game::FlightModel flight;
        game::BotPilot pilot;
        anim::DragonRig rig;
        // Which entry of models_ this bot wears. A flight of rivals reads as a
        // flight of rivals only if they differ in silhouette; the hue below
        // cannot do that on its own.
        int model = 0;
        int slot = -1;
        bool was_alive = true;
        bool grounded_last_frame = false;
        bool breathing = false;
        // Body colour, so a flight of bots is not four copies of one dragon:
        // a hue the hide is recoloured toward, at its own luminance.
        core::Vec3 hue{1.9f, 0.55f, 0.35f};
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
    // How far out a bot spawns, and how much that varies. Pulled out of
    // place_bot because it decides whether a fight starts as a chase or as a
    // merge -- and because a short range is the only way to get the whole
    // roster into one headless capture.
    float bot_spawn_range_ = 650.0f;
    float bot_spawn_jitter_ = 150.0f;

    // M15: the match loop. Countdown, fight to a kill target, results,
    // rematch. Idle means free play, which everything else already was.
    game::Match match_;
    void start_match();
    void apply_bot_skill(int level);
    void spawn_bots(int count);
    // How far bot hides are recoloured toward their hue; 0 leaves the texture.
    float bot_recolour_ = 0.8f;
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
    void emit_impact(core::Vec3 position, bool hostile, bool on_terrain);
    float hit_marker_ = 0.0f;
    core::Vec3 hit_marker_position_ = core::Vec3::zero();
    float damage_flash_ = 0.0f;
    // Edge detection for gamepad buttons, which core::Input only reports as
    // held state.
    bool cycle_button_was_down_ = false;
    // What the player's rig should be doing with its mouth this frame; filled
    // by combat or the studio, applied where the rig updates.
    anim::RigAction rig_action_;
    bool cycle_requested_ = false;
    core::Vec3 damage_direction_ = core::Vec3::zero();
    float damage_marker_ = 0.0f;
    // Master switch for every ImGui panel. The tuning UI covers most of the
    // screen, which is fine while tuning and useless while playing.
    bool show_panels_ = true;
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
    };

    // One rigged creature, and everything that belongs to it rather than to
    // whoever is flying it. Split out so a match can field more than one
    // species: the mesh, its textures, the skeleton the joint mapper found and
    // the per-model tuning files are all properties of the asset, while the
    // spring state that animates them belongs to each DragonRig.
    struct LoadedModel {
        std::string path = "generated";  // what was loaded, for the UI and logs
        anim::DragonShape shape;         // only meaningful for the generated fallback
        anim::Skeleton skeleton;
        anim::DragonJoints joints;
        anim::SkinnedMesh mesh;
        std::vector<SDL_GPUTexture*> textures;
        std::vector<anim::AnimationClip> animations;
        // Which clip serves as the ground idle, and whether it is held at one time.
        int idle_clip = -1;
        float idle_clip_hold = -1.0f;
        AssetTransform asset;
        // <model>.rig.cfg and <model>.flight.cfg, already resolved. A rig
        // profile changes only the visible pose; the flight profile changes
        // forces and control response, which is why a wyvern can be given the
        // handling its shape implies.
        anim::RigTuning rig_tuning;
        game::FlightTuning flight_tuning;
        std::string rig_tuning_path;
        std::string flight_tuning_path;
        std::string breath_path;
        bool imported = false;
        // What this species' breath does and looks like, from
        // <model>.breath.cfg. The breath is the clearest place a species reads
        // as elemental, so it is a property of the creature, not of combat.
        game::BreathProfile breath;
        // The outermost wing joint per side, for the wingtip vortex trails.
        // -1 when the rig has no wings.
        int wingtip_joint[2] = {-1, -1};
        // Bounds of the mesh the head carries (every vertex weighted mostly to
        // the head joint or a bone under it: skull, jaw, horns, crest), in
        // body-frame metres RELATIVE TO THE HEAD JOINT, measured in the bind
        // pose. The first-person eye is placed off this box, so a species with
        // a tall frill or a long skull is framed like one with neither.
        core::Vec3 head_box_min = core::Vec3::zero();
        core::Vec3 head_box_max = core::Vec3::zero();
        bool head_box_valid = false;
        // A top edge seen from the side: the highest vertex in each of BINS
        // slices along the part's length (z0 front, z1 rear), in body-frame
        // metres relative to the head joint, bind pose. What the eye must
        // clear is the highest point AHEAD of it, which depends on where the
        // eye is, so a box top alone is not enough: horns sweeping straight
        // up put the eye so high that nothing showed. Two parts are measured
        // because the eye sits over the neck, and a neck crest (rimefang's
        // spines, blightmaw's horns are rigged to the last neck bone) rises
        // into the view exactly as the skull does.
        struct TopProfile {
            static constexpr int BINS = 32;
            float z0 = 0.0f;
            float z1 = 0.0f;
            float top[BINS] = {};
            bool valid = false;
            float z_at(int bin) const { return z0 + (float(bin) + 0.5f) / BINS * (z1 - z0); }
        };
        TopProfile head_profile;
        TopProfile neck_profile;
    };
    // Held by pointer because a LoadedModel owns GPU handles and is referred to
    // by index from every bot; growth must not move one under a live reference.
    std::vector<std::unique_ptr<LoadedModel>> models_;
    int player_model_ = 0;
    LoadedModel& player_model() { return *models_[size_t(player_model_)]; }
    const LoadedModel& player_model() const { return *models_[size_t(player_model_)]; }
    // A model index, clamped. Bots store an index rather than a pointer so the
    // roster can be rebuilt without walking them.
    LoadedModel& model_at(int index) {
        if (index < 0 || size_t(index) >= models_.size()) return player_model();
        return *models_[size_t(index)];
    }
    bool load_model(const std::string& path, LoadedModel& out);
    void find_wingtips(LoadedModel& model);
    // `profile` is the breathing species' breath; null keeps the shared
    // player/hostile fire.
    void emit_flame(core::Vec3 origin, core::Vec3 direction, float range, bool hostile, float dt,
                    const game::BreathProfile* profile = nullptr);
    // Re-points the player at another entry in the roster. The rigs carry
    // spring state tied to a specific skeleton, so both are re-initialised.
    void set_player_model(int index);

    anim::DragonRig dragon_rig_;
    // The ghost needs its own rig: the spring chains carry state, so one rig
    // cannot serve two dragons.
    anim::DragonRig ghost_rig_;
    void choose_idle_clip(LoadedModel& model) const;
    void apply_idle_clip(const LoadedModel& model, anim::DragonRig& rig) const;
    SDL_GPUSampler* model_sampler_ = nullptr;
    bool show_skeleton_ = false;
    gfx::MaterialToggles material_toggles_;
    // Trees and grass: placed by the terrain rules, drawn instanced.
    game::Vegetation vegetation_;
    game::VegetationSettings vegetation_settings_;
    gfx::Foliage foliage_;
    std::vector<game::PlantInstance> grass_scratch_[gfx::GRASS_KINDS];
    void replant();
    // The coarse ground beyond the playable extent.
    gfx::Mesh terrain_skirt_mesh_;
    gfx::Mesh water_mesh_;
    std::string model_tuning_path_;
    std::string model_rig_tuning_path_;
    // The player's hide colour: a hue the texture is pushed toward at its own
    // luminance, and how far. Strength 0 is the texture as authored.
    core::Vec3 player_hue_{1.0f, 1.0f, 1.0f};
    float player_recolour_ = 0.0f;


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
