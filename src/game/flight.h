#pragma once

#include "core/math.h"

namespace game {

class Terrain;

// Flight coefficients. Every value here is meant to be dragged in the ImGui
// panel while flying -- that is how the feel gets found. Defaults are a
// starting point, not a design.
//
// The model is an energy model, not an arcade one: thrust, lift, drag and
// gravity are integrated as real forces, so diving buys speed, climbing spends
// it, and hard turns bleed energy. That is what makes both racing lines and
// dogfighting deep, and it is the single decision the rest of the game leans on.
struct FlightTuning {
    // ---- body ----
    float mass = 800.0f;         // kg
    float wing_area = 40.0f;     // m^2, fully extended
    float air_density = 1.225f;  // kg/m^3 at sea level
    float gravity = 9.81f;       // m/s^2

    // ---- aerodynamics ----
    // Peak lift coefficient, reached at the stall angle.
    float lift_coefficient_max = 2.2f;
    float stall_angle_deg = 16.0f;
    // How much lift survives past the stall. 0 would be a brick.
    float post_stall_lift = 0.32f;
    // At 0.048 the dragon sank noticeably at cruise; 0.038 keeps a real
    // speed-to-fly decision without making fast flight feel like falling.
    // Best glide lands near 21 m/s, cruise around 45 m/s costs real height, and
    // a tucked dive tops out near 130 m/s.
    float parasitic_drag = 0.038f;      // CD0
    float induced_drag_factor = 0.062f; // k in CD = CD0 + k*CL^2

    // ---- propulsion ----
    float flap_peak_force = 9000.0f;  // N at the peak of a downstroke
    float flap_period = 0.9f;         // seconds per wingbeat
    float glide_thrust = 0.0f;        // N, free forward push while gliding

    // ---- wingbeat shape ----
    // A real wingbeat is not a sine. The downstroke is the fast, powered half
    // and the recovery is slower, and the wing travels further above the body
    // than below it. Getting this wrong is immediately visible.
    float flap_up_angle_deg = 54.0f;
    float flap_down_angle_deg = -32.0f;
    float flap_downstroke_fraction = 0.40f;
    // Resting dihedral: gliding wings sit slightly raised, not dead flat.
    float glide_dihedral_deg = 9.0f;
    // Half-life for blending between gliding and beating, so starting and
    // stopping a flap eases instead of snapping.
    float flap_blend = 0.16f;

    // ---- wing states ----
    // Tucking trades lift for a much cleaner shape: the dive control, and the
    // core of energy management.
    float tuck_lift_loss = 0.80f;
    // 0.62 gave a 480 km/h tucked dive, which outran the world. 0.35 keeps the
    // dive dramatic at around 410 km/h without turning a valley into a corridor.
    float tuck_drag_loss = 0.35f;
    // Air-braking flares the wings: enormous drag, some extra lift.
    float brake_drag_gain = 3.4f;
    float brake_lift_gain = 0.35f;

    // ---- control authority (rad/s of commanded body rate) ----
    float pitch_rate = 1.45f;
    // Rudder was near-useless at 0.55: weathercock stability cancelled almost
    // all of it. It needs enough authority to be worth reaching for.
    float yaw_rate = 0.95f;
    float roll_rate = 3.0f;
    // Control response half-life. Lower is twitchier.
    float control_lag = 0.075f;
    // Fraction of authority available at zero airspeed. Real surfaces would
    // give none, but a dragon flaps, and a helpless nose is not fun.
    float low_speed_authority = 0.40f;
    float authority_reference_speed = 34.0f;

    // ---- stability (what makes it feel like flight, not a spaceship) ----
    // Weathercock: yaws the nose back into the airflow, killing sideslip.
    float yaw_stability = 1.9f;
    // Pitch stability opposes angle of attack. Too much fights the player.
    float pitch_stability = 0.85f;
    float pitch_damping = 1.5f;
    float yaw_damping = 1.4f;
    float roll_damping = 2.6f;

    // ---- assists ----
    // Soft ceiling on bank angle, in degrees. Zero disables it.
    //
    // Without this, holding roll to turn keeps rolling: past vertical you end up
    // inverted with lift pointing at the ground. That is the single biggest
    // source of crashes for someone who has not flown a flight sim before.
    // Approaching the limit fades roll authority out; past it the wings are
    // actively levelled even while roll is held, so a turn settles into a bank
    // instead of becoming a barrel roll.
    float bank_limit_deg = 68.0f;
    float bank_limit_recovery = 1.7f;

    // Rolls the wings level when the player lets go of roll.
    float auto_level = 2.4f;
    // Ceiling on the auto-level roll rate, so recovering from inverted is brisk
    // without the game visibly yanking the controls away.
    float auto_level_max_rate = 2.2f;
    // Adds yaw into a bank so turns come out coordinated rather than skidding.
    // 0.30 was found by sweep: below it turns skid outward, above it the nose
    // over-yaws into the turn. Either way the turn *rate* barely changes, so
    // this value buys cleanliness rather than performance.
    float turn_coordination = 0.30f;
    // Pitches the nose down when stalled, so a stall is recoverable rather than
    // terminal.
    float stall_recovery = 1.4f;
    // Eases the nose toward the horizon when the player is not commanding pitch.
    // Without this, hands-off in a dive stays in the dive forever -- correct for
    // an aircraft, but it makes an accidental attitude into a crash.
    float pitch_level = 1.1f;
    float pitch_level_max_rate = 0.7f;
    // Below this airspeed a gentle forward push prevents a helpless tumble.
    float min_airspeed = 14.0f;
    float min_airspeed_assist = 3200.0f;  // N

    // ---- ground ----
    float ground_offset = 2.2f;   // body centre height when resting
    float ground_friction = 1.8f;
    // Static friction. Sliding friction alone is multiplicative, so it can never
    // fully cancel the component of gravity along a slope -- a landed dragon
    // slides downhill forever, slowly. Below this speed it simply stops.
    float ground_stop_speed = 1.6f;
    // Landing softer than this keeps you intact; harder is a crash.
    float safe_landing_speed = 18.0f;
};

// Player intent, all normalized. Produced by keyboard, gamepad, or an AI pilot
// -- bots fly through this same struct so they are physically honest.
struct FlightInput {
    float pitch = 0.0f;  // +1 nose up
    float yaw = 0.0f;    // +1 nose right
    float roll = 0.0f;   // +1 roll right
    float flap = 0.0f;   // 0..1, hold to beat wings
    float tuck = 0.0f;   // 0..1, fold wings and dive
    float brake = 0.0f;  // 0..1, flare and slow
};

// Everything the flight model produces. The derived fields exist for telemetry
// and for driving procedural animation, and are cheap to keep.
struct FlightState {
    core::Vec3 position = core::Vec3::zero();
    core::Quat orientation = core::Quat::identity();
    core::Vec3 velocity = core::Vec3::zero();
    // Body-frame angular velocity, rad/s: x pitch, y yaw, z roll.
    core::Vec3 angular_velocity = core::Vec3::zero();

    // ---- derived, refreshed every update ----
    float airspeed = 0.0f;
    float angle_of_attack = 0.0f;  // radians, + means wind from below
    float sideslip = 0.0f;         // radians, + means wind from the right
    float lift = 0.0f;             // newtons
    float drag = 0.0f;
    float thrust = 0.0f;
    float g_load = 1.0f;           // multiples of gravity felt along body up
    float climb_rate = 0.0f;       // m/s, + is up
    float ground_clearance = 0.0f;
    // Total specific energy (height + v^2/2g), in metres. The number a pilot
    // actually manages: it can be traded between altitude and speed but only
    // spent, never conjured.
    float specific_energy = 0.0f;

    bool stalling = false;
    bool grounded = false;

    // Wingbeat phase in [0,1), 0 at the top of the downstroke.
    float flap_phase = 0.0f;
    // How much of a full beat is currently being flown: 0 gliding, 1 full.
    float flap_amplitude = 0.0f;
    // Final wing angle in radians, positive up. The renderer uses this
    // directly, so the visible wing and the thrust it produces can never
    // disagree -- and bots and replays get the same animation for free.
    float wing_angle = 0.0f;
    // Smoothed control positions, for animating control surfaces and for the
    // camera to lead into turns.
    core::Vec3 control = core::Vec3::zero();  // x pitch, y yaw, z roll
    float wing_tuck = 0.0f;
    float wing_brake = 0.0f;

    core::Vec3 forward() const { return core::quat_forward(orientation); }
    core::Vec3 up() const { return core::quat_up(orientation); }
    core::Vec3 right() const { return core::quat_right(orientation); }
};

// Integrates one dragon's flight. Holds no rendering or input state, so the same
// model runs the player and every bot.
class FlightModel {
public:
    void reset(core::Vec3 position, core::Quat orientation, float airspeed);

    // Fixed-step is preferable but a clamped variable step is stable here.
    // `terrain` may be null, in which case there is no ground.
    void update(const FlightInput& input, const Terrain* terrain, float dt);

    const FlightState& state() const { return state_; }
    FlightState& state() { return state_; }
    FlightTuning tuning;

    // Forces from the last update, in world space, for debug visualisation.
    // Seeing these is how a flight model actually gets debugged.
    core::Vec3 debug_lift = core::Vec3::zero();
    core::Vec3 debug_drag = core::Vec3::zero();
    core::Vec3 debug_thrust = core::Vec3::zero();
    core::Vec3 debug_gravity = core::Vec3::zero();

private:
    void integrate_forces(const FlightInput& input, float dt);
    void integrate_rotation(const FlightInput& input, float dt);
    void resolve_ground(const Terrain* terrain, float dt);

    FlightState state_;
};

// Named starting points, so feel can be compared rather than argued about.
FlightTuning tuning_preset_glider();
FlightTuning tuning_preset_agile();
FlightTuning tuning_preset_heavy();

// Flat `key value` text, one per line. Deliberately not JSON: no dependency,
// trivially diffable, and hand-editable while the game is running.
bool save_tuning(const FlightTuning& tuning, const char* path);
bool load_tuning(FlightTuning& tuning, const char* path);

}  // namespace game
