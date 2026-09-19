#include "game/flight.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "core/log.h"
#include "game/terrain.h"

using core::Quat;
using core::Vec3;

namespace {

// Wing height through one beat, returning +1 fully raised and -1 fully lowered.
//
// Two cosine easings of unequal length: a short fast downstroke and a longer
// recovery. Each segment has zero derivative at its ends, so the wing does not
// visibly jerk at the reversal points.
float wingbeat_curve(float phase, float downstroke_fraction) {
    const float down = core::clampf(downstroke_fraction, 0.05f, 0.95f);
    if (phase < down) {
        const float t = phase / down;
        return std::cos(t * core::PI);  // +1 down to -1
    }
    const float t = (phase - down) / (1.0f - down);
    return -std::cos(t * core::PI);  // -1 back up to +1
}

}  // namespace

namespace game {

void FlightModel::reset(Vec3 position, Quat orientation, float airspeed) {
    state_ = FlightState();
    state_.position = position;
    state_.orientation = core::normalize(orientation);
    state_.velocity = core::quat_forward(state_.orientation) * airspeed;

    // Derived telemetry has to be consistent immediately, not only after the
    // first update. Otherwise anything that samples the state on the spawn frame
    // -- the HUD, a test, a camera snap -- reads zeroes.
    state_.airspeed = airspeed;
    state_.climb_rate = state_.velocity.y;
    state_.specific_energy =
        position.y + core::length_sq(state_.velocity) / (2.0f * core::maxf(tuning.gravity, 0.01f));
    state_.g_load = 1.0f;
    state_.wing_angle = core::radians(tuning.glide_dihedral_deg);
}

void FlightModel::update(const FlightInput& input, const Terrain* terrain, float dt) {
    if (dt <= 0.0f) return;
    const float step_limit = core::maxf(max_step, 1e-3f);
    const int steps = dt > step_limit ? int(std::ceil(dt / step_limit)) : 1;
    const float step_dt = dt / float(steps);
    for (int i = 0; i < steps; ++i) step(input, terrain, step_dt);
}

void FlightModel::step(const FlightInput& input, const Terrain* terrain, float dt) {

    // Smooth the raw input. Control lag is what separates a dragon from a
    // cursor: the body has inertia and the wings take time to bite.
    const float heft_root = std::sqrt(core::maxf(tuning.heft, 0.05f));
    const float lag = tuning.control_lag * heft_root;
    state_.control.x = core::damp(state_.control.x, core::clampf(input.pitch, -1.0f, 1.0f), lag, dt);
    state_.control.y = core::damp(state_.control.y, core::clampf(input.yaw, -1.0f, 1.0f), lag, dt);
    state_.control.z = core::damp(state_.control.z, core::clampf(input.roll, -1.0f, 1.0f), lag, dt);

    // Take-off: the first flap from the ground is a leap. Without it a
    // grounded dragon flapping just slid forward until lift arrived, and the
    // min-airspeed assist is off on the ground, so it often never did.
    const bool flap_pressed = input.flap > 0.5f;
    if (state_.grounded && flap_pressed && !flap_was_down_) {
        state_.velocity += Vec3{0.0f, tuning.takeoff_jump, 0.0f} + state_.forward() * tuning.takeoff_push;
        state_.position.y += 0.05f;
        state_.grounded = false;
    }
    flap_was_down_ = flap_pressed;
    // A standing animal folds its wings without being asked. The studio's
    // grounded scenario has always assumed this -- it sets a full tuck itself --
    // but the game did not, so a landed dragon stood with its wings half open
    // unless the player kept holding the dive key. Lift is moot on the ground,
    // so this costs the force model nothing.
    const float tuck_command =
        state_.grounded ? 1.0f : core::saturate(input.tuck);
    state_.wing_tuck = core::damp(state_.wing_tuck, tuck_command, 0.12f, dt);
    state_.wing_brake = core::damp(state_.wing_brake, core::saturate(input.brake), 0.10f, dt);

    integrate_rotation(input, dt);
    integrate_forces(input, dt);

    state_.position += state_.velocity * dt;
    resolve_ground(terrain, dt);

    // Telemetry that only depends on the final state.
    state_.climb_rate = state_.velocity.y;
    state_.specific_energy =
        state_.position.y + core::length_sq(state_.velocity) / (2.0f * tuning.gravity);
    if (terrain) state_.ground_clearance = terrain->clearance_at(state_.position);
}

void FlightModel::integrate_forces(const FlightInput& input, float dt) {
    const Vec3 forward = state_.forward();
    const Vec3 up = state_.up();
    const Vec3 right = state_.right();

    const float airspeed = core::length(state_.velocity);
    state_.airspeed = airspeed;

    // Relative wind in body axes. Below a threshold there is no meaningful
    // airflow, so aerodynamic forces vanish rather than producing garbage
    // angles from a near-zero vector.
    const bool has_airflow = airspeed > 0.5f;
    const Vec3 flow = has_airflow ? state_.velocity / airspeed : forward;

    const float along = core::dot(flow, forward);
    const float vertical = core::dot(flow, up);
    const float lateral = core::dot(flow, right);

    // Positive angle of attack means the wind arrives from below the wing.
    state_.angle_of_attack = has_airflow ? std::atan2(-vertical, along) : 0.0f;
    state_.sideslip = has_airflow ? std::atan2(lateral, along) : 0.0f;

    const float stall_angle = core::radians(tuning.stall_angle_deg);
    const float aoa = state_.angle_of_attack;
    const float aoa_abs = std::fabs(aoa);
    state_.stalling = has_airflow && aoa_abs > stall_angle;

    // Lift curve: linear to the stall angle, then collapsing to a fraction.
    // Real wings do roughly this, and the shape is what makes a stall feel like
    // losing the air rather than hitting a wall.
    float lift_coefficient =
        core::clampf(aoa / stall_angle, -1.0f, 1.0f) * tuning.lift_coefficient_max;
    const float stall_falloff =
        core::smoothstep(stall_angle, stall_angle * 2.1f, aoa_abs);
    lift_coefficient *= core::lerpf(1.0f, tuning.post_stall_lift, stall_falloff);

    // Wing configuration. Tucking sheds lift and drag together; braking flares
    // for the opposite trade.
    const float tuck = state_.wing_tuck;
    const float brake = state_.wing_brake;
    lift_coefficient *= (1.0f - tuck * tuning.tuck_lift_loss) * (1.0f + brake * tuning.brake_lift_gain);

    // Dynamic pressure times area: the scale of every aerodynamic force.
    const float q_area = 0.5f * tuning.air_density * airspeed * airspeed * tuning.wing_area;

    float drag_coefficient = tuning.parasitic_drag +
                             tuning.induced_drag_factor * lift_coefficient * lift_coefficient;
    drag_coefficient *= (1.0f - tuck * tuning.tuck_drag_loss);
    drag_coefficient *= (1.0f + brake * tuning.brake_drag_gain);

    // Lift acts perpendicular to the relative wind, in the body's plane of
    // symmetry -- not simply along body up, which is what makes banked turns
    // curve the flight path on their own.
    Vec3 lift_direction = up;
    if (has_airflow) {
        Vec3 span = core::cross(flow, up);
        if (core::length_sq(span) > 1e-6f) {
            lift_direction = core::normalize(core::cross(span, flow));
        }
    }

    const float lift_magnitude = lift_coefficient * q_area;
    const float drag_magnitude = drag_coefficient * q_area;
    state_.lift = lift_magnitude;
    state_.drag = drag_magnitude;

    Vec3 lift_force = lift_direction * lift_magnitude;
    Vec3 drag_force = has_airflow ? -flow * drag_magnitude : Vec3::zero();

    // Wingbeats.
    //
    // Amplitude eases toward the command rather than switching, and the phase
    // keeps advancing while any amplitude remains, so releasing the flap key
    // finishes the current beat and settles into a glide instead of freezing the
    // wings mid-stroke.
    float thrust_magnitude = tuning.glide_thrust;
    const float flap_command = core::saturate(input.flap);
    state_.flap_amplitude =
        core::damp(state_.flap_amplitude, flap_command, tuning.flap_blend, dt);

    const float downstroke_fraction = core::clampf(tuning.flap_downstroke_fraction, 0.05f, 0.95f);
    if (state_.flap_amplitude > 0.01f && tuning.flap_period > 0.0f) {
        state_.flap_phase += dt / tuning.flap_period;
        state_.flap_phase -= std::floor(state_.flap_phase);

        // Thrust comes from the downstroke, peaking mid-stroke where the wing is
        // moving fastest. Tying it to the same phase the animation uses means the
        // push you feel always matches the beat you see.
        if (state_.flap_phase < downstroke_fraction) {
            const float t = state_.flap_phase / downstroke_fraction;
            thrust_magnitude +=
                tuning.flap_peak_force * std::sin(t * core::PI) * state_.flap_amplitude;
        }
    } else {
        // Start each new beat from the top of the downstroke.
        state_.flap_phase = 0.0f;
    }

    // Blend between the resting dihedral and the current point in the beat.
    const float up_angle = core::radians(tuning.flap_up_angle_deg);
    const float down_angle = core::radians(tuning.flap_down_angle_deg);
    const float centre = (up_angle + down_angle) * 0.5f;
    const float half_range = (up_angle - down_angle) * 0.5f;
    const float beating =
        centre + half_range * wingbeat_curve(state_.flap_phase, downstroke_fraction);
    state_.wing_angle = core::lerpf(core::radians(tuning.glide_dihedral_deg), beating,
                                    core::saturate(state_.flap_amplitude));

    // Low-speed assist: a gentle forward push so running out of airspeed means
    // a mushy nose rather than an unrecoverable tumble.
    if (airspeed < tuning.min_airspeed && !state_.grounded) {
        const float deficit = 1.0f - airspeed / core::maxf(tuning.min_airspeed, 0.1f);
        thrust_magnitude += tuning.min_airspeed_assist * deficit * deficit;
    }

    // Boost is unconditional: it is an ability with a cooldown, and having it
    // quietly do nothing at low speed or while stalled would make it unreadable.
    thrust_magnitude += tuning.boost_force * core::saturate(input.boost);

    state_.thrust = thrust_magnitude;
    Vec3 thrust_force = forward * thrust_magnitude;
    Vec3 gravity_force = Vec3{0.0f, -tuning.gravity * tuning.effective_mass(), 0.0f};

    debug_lift = lift_force;
    debug_drag = drag_force;
    debug_thrust = thrust_force;
    debug_gravity = gravity_force;

    const Vec3 total = lift_force + drag_force + thrust_force + gravity_force;
    state_.velocity += (total / tuning.effective_mass()) * dt;

    // G-load is what the pilot feels along body up: aerodynamic force only,
    // since gravity is not felt in free fall.
    state_.g_load = core::dot(lift_force + drag_force + thrust_force, up) /
                    (tuning.effective_mass() * tuning.gravity);
}

void FlightModel::integrate_rotation(const FlightInput& input, float dt) {
    const Vec3 up = state_.up();
    const Vec3 right = state_.right();

    const float airspeed = core::length(state_.velocity);
    // Control authority grows with airflow over the wings, but never to zero --
    // a dragon can still throw its weight around at a hover.
    const float authority =
        core::lerpf(tuning.low_speed_authority, 1.0f,
                    core::saturate(airspeed / core::maxf(tuning.authority_reference_speed, 1.0f)));

    // Commanded body rates. Positive pitch about +X raises the nose; yaw right
    // and roll right are both negative about their axes, given forward is -Z.
    const float rate_scale = core::clampf(input.agility, 0.25f, 4.0f) /
                             std::sqrt(core::maxf(tuning.heft, 0.05f));
    Vec3 commanded{state_.control.x * tuning.pitch_rate * rate_scale,
                   -state_.control.y * tuning.yaw_rate * rate_scale,
                   -state_.control.z * tuning.roll_rate * rate_scale};
    commanded *= authority;

    // --- assists, expressed as extra commanded rate ---

    // Bank angle, signed. Rolling right yaws the body so that the right wing
    // drops, which makes dot(right, world up) negative -- so a right bank is a
    // NEGATIVE bank angle here. Both assists below depend on that sign, and
    // both had it backwards: auto-level was a positive feedback loop that rolled
    // the dragon all the way inverted, and turn coordination was yawing out of
    // the turn instead of into it.
    const float bank = std::atan2(core::dot(right, Vec3::up()), core::dot(up, Vec3::up()));

    // Turn coordination: entering a bank should swing the nose around with it,
    // otherwise the turn skids and reads as a slide rather than a turn. Right
    // bank (negative) needs right yaw (negative rate).
    commanded.y += bank * tuning.turn_coordination * core::saturate(airspeed / 30.0f);

    // Auto-level: roll back toward wings-level, but only when the player is not
    // asking for roll, so it assists rather than fights.
    //
    // The error is dot(body right, world up) rather than the bank angle,
    // because that degenerates gracefully: it goes to zero in a vertical dive,
    // where rolling genuinely would not help, instead of commanding a large
    // useless rate. Its one weakness is that it is also zero when exactly
    // inverted, so past ninety degrees we command full deflection and commit to
    // a direction rather than balancing on the singularity.
    const float roll_released =
        input.maneuver ? 0.0f : 1.0f - core::saturate(std::fabs(state_.control.z) * 4.0f);
    if (roll_released > 0.0f) {
        const float right_up = core::dot(right, Vec3::up());
        const float up_up = core::dot(up, Vec3::up());
        float roll_error = -right_up;
        if (up_up < 0.0f) {
            // Inverted. Pick a side and hold it, biased by whichever way is
            // already shorter.
            roll_error = right_up > 0.0f ? -1.0f : 1.0f;
        }
        const float level_rate = core::clampf(roll_error * tuning.auto_level,
                                              -tuning.auto_level_max_rate,
                                              tuning.auto_level_max_rate);
        commanded.z += level_rate * roll_released;
    }

    // Bank limit. Applied after auto-level so it has the final say, and unlike
    // auto-level it works even while the player is holding roll -- that is the
    // whole point.
    if (tuning.bank_limit_deg > 0.0f && !input.maneuver) {
        const float limit = core::radians(tuning.bank_limit_deg);
        // The fade has to start well before the limit. Roll rate is around
        // 170 deg/s and the rate damping has a ~0.4 s half-life, so a narrow
        // window is crossed before the command can reverse -- the first attempt
        // used 14 degrees and overshot to 92, past vertical, which is exactly
        // the failure this assist exists to prevent.
        const float over =
            core::smoothstep(limit - core::radians(30.0f), limit, std::fabs(bank));
        if (over > 0.0f) {
            // commanded.z and bank share a sign when the roll command is
            // deepening the bank rather than recovering from it.
            if (commanded.z * bank > 0.0f) commanded.z *= (1.0f - over);
            commanded.z += -core::signf(bank) * over * tuning.bank_limit_recovery;
        }
    }

    // Weathercock stability: yaw the nose back into the airflow. This is the
    // single term that most makes the dragon feel like it is flying through air.
    const float airflow = core::saturate(airspeed / 25.0f);
    commanded.y += -state_.sideslip * tuning.yaw_stability * airflow;

    // Pitch stability opposes angle of attack, and stall recovery adds a hard
    // nose-down push once the wing has let go.
    commanded.x += -state_.angle_of_attack * tuning.pitch_stability * airflow;
    if (state_.stalling) {
        commanded.x += -core::signf(state_.angle_of_attack) * tuning.stall_recovery * airflow;
    }

    // Pitch attitude assist. Angle-of-attack stability says nothing about which
    // way the nose is pointing, so a hands-off dive is a stable dive. This eases
    // the nose toward the horizon instead, and only while the player is not
    // asking for pitch.
    const float pitch_released = 1.0f - core::saturate(std::fabs(state_.control.x) * 4.0f);
    if (pitch_released > 0.0f) {
        const Vec3 forward = state_.forward();
        const float level_pitch = core::clampf(-forward.y * tuning.pitch_level,
                                              -tuning.pitch_level_max_rate,
                                              tuning.pitch_level_max_rate);
        commanded.x += level_pitch * pitch_released * airflow;
    }

    // Rate damping, so control inputs settle instead of ringing.
    const Vec3 damping{tuning.pitch_damping, tuning.yaw_damping, tuning.roll_damping};
    Vec3 target = commanded;
    state_.angular_velocity.x =
        core::damp(state_.angular_velocity.x, target.x, 1.0f / core::maxf(damping.x, 0.01f), dt);
    state_.angular_velocity.y =
        core::damp(state_.angular_velocity.y, target.y, 1.0f / core::maxf(damping.y, 0.01f), dt);
    state_.angular_velocity.z =
        core::damp(state_.angular_velocity.z, target.z, 1.0f / core::maxf(damping.z, 0.01f), dt);

    // Body rates to world space, then integrate the orientation.
    const Vec3 world_omega = core::rotate(state_.orientation, state_.angular_velocity);
    state_.orientation = core::integrate(state_.orientation, world_omega, dt);
}

void FlightModel::resolve_ground(const Terrain* terrain, float dt) {
    if (!terrain) {
        state_.grounded = false;
        return;
    }

    // The surface, not the ground: over the river that is the water.
    const float ground_height = terrain->surface_at(state_.position.x, state_.position.z);
    const float resting_height = ground_height + tuning.ground_offset;

    if (state_.position.y > resting_height) {
        state_.grounded = false;
        return;
    }

    const bool on_water =
        terrain->height_at(state_.position.x, state_.position.z) < terrain->settings().water_level;
    const Vec3 normal = on_water ? Vec3::up()
                                 : terrain->normal_at(state_.position.x, state_.position.z);
    state_.position.y = resting_height;

    // Standing: once slow, the body settles level on the surface -- yaw kept,
    // pitch and roll eased out -- instead of holding whatever attitude it
    // arrived in. A landed dragon is not a parked aircraft frozen mid-bank.
    const float slow = core::saturate(1.0f - core::length(state_.velocity) / 8.0f);
    if (slow > 0.0f) {
        const Vec3 forward = state_.forward();
        const Vec3 level_forward = core::normalize_or(
            forward - normal * core::dot(forward, normal), Vec3::forward());
        const core::Quat level = core::look_rotation(level_forward, normal);
        state_.orientation = core::normalize(
            core::slerp(state_.orientation, level, core::saturate(slow * 4.0f * dt)));
        state_.angular_velocity = state_.angular_velocity * (1.0f - core::saturate(slow * 6.0f * dt));
    }

    // Remove the velocity going into the surface, keep what slides along it.
    const float into_surface = core::dot(state_.velocity, normal);
    if (into_surface < 0.0f) state_.velocity -= normal * into_surface;

    // Ground friction, frame-rate independent.
    const float retained = std::exp(-tuning.ground_friction * dt);
    state_.velocity *= retained;

    // Static friction: come to a genuine stop rather than creeping downhill.
    if (core::length_sq(state_.velocity) <
        core::sqf(core::maxf(tuning.ground_stop_speed, 0.0f))) {
        state_.velocity = Vec3::zero();
    }

    state_.grounded = true;
    state_.stalling = false;
}

// ---------------------------------------------------------------- presets

FlightTuning tuning_preset_glider() {
    FlightTuning t;
    // Big slow wings: floats, turns lazily, holds energy well.
    t.wing_area = 56.0f;
    t.lift_coefficient_max = 2.5f;
    t.parasitic_drag = 0.036f;
    t.pitch_rate = 1.1f;
    t.roll_rate = 2.1f;
    t.auto_level = 1.1f;
    t.flap_peak_force = 7200.0f;
    t.flap_period = 1.15f;
    return t;
}

FlightTuning tuning_preset_agile() {
    FlightTuning t;
    // Small fast wings: twitchy, bleeds energy in turns, rewards precision.
    t.mass = 620.0f;
    t.wing_area = 30.0f;
    t.lift_coefficient_max = 2.0f;
    t.parasitic_drag = 0.055f;
    t.induced_drag_factor = 0.075f;
    t.pitch_rate = 2.0f;
    t.yaw_rate = 0.75f;
    t.roll_rate = 4.4f;
    t.control_lag = 0.05f;
    t.auto_level = 0.55f;
    t.flap_peak_force = 8200.0f;
    t.flap_period = 0.7f;
    return t;
}

FlightTuning tuning_preset_heavy() {
    FlightTuning t;
    // Enormous and reluctant: high top speed, dreadful low-speed handling.
    t.mass = 1500.0f;
    t.wing_area = 62.0f;
    t.lift_coefficient_max = 2.1f;
    t.parasitic_drag = 0.052f;
    t.pitch_rate = 0.95f;
    t.yaw_rate = 0.4f;
    t.roll_rate = 1.6f;
    t.control_lag = 0.14f;
    t.flap_peak_force = 17000.0f;
    t.flap_period = 1.35f;
    t.min_airspeed = 22.0f;
    return t;
}

// ---------------------------------------------------------------- persistence

namespace {

// One entry per tunable field, so save and load can never disagree about the
// set of keys.
struct Field {
    const char* name;
    float FlightTuning::*member;
};

#define FIELD(name) {#name, &FlightTuning::name}
const Field FIELDS[] = {
    FIELD(mass),                 FIELD(wing_area),           FIELD(air_density),
    FIELD(gravity),              FIELD(lift_coefficient_max), FIELD(stall_angle_deg),
    FIELD(post_stall_lift),      FIELD(parasitic_drag),      FIELD(induced_drag_factor),
    FIELD(flap_peak_force),      FIELD(flap_period),         FIELD(glide_thrust),
    FIELD(boost_force),
    FIELD(flap_up_angle_deg),    FIELD(flap_down_angle_deg),
    FIELD(flap_downstroke_fraction), FIELD(glide_dihedral_deg), FIELD(flap_blend),
    FIELD(tuck_lift_loss),       FIELD(tuck_drag_loss),      FIELD(brake_drag_gain),
    FIELD(brake_lift_gain),      FIELD(pitch_rate),          FIELD(yaw_rate),
    FIELD(roll_rate),            FIELD(control_lag),         FIELD(low_speed_authority),
    FIELD(authority_reference_speed), FIELD(yaw_stability),  FIELD(pitch_stability),
    FIELD(pitch_damping),        FIELD(yaw_damping),         FIELD(roll_damping),
    FIELD(bank_limit_deg),       FIELD(bank_limit_recovery),
    FIELD(auto_level),           FIELD(auto_level_max_rate), FIELD(turn_coordination),
    FIELD(stall_recovery),       FIELD(pitch_level),         FIELD(pitch_level_max_rate),
    FIELD(min_airspeed),         FIELD(min_airspeed_assist), FIELD(ground_offset),
    FIELD(heft),                 FIELD(takeoff_jump),        FIELD(takeoff_push),
    FIELD(ground_friction),      FIELD(ground_stop_speed),   FIELD(safe_landing_speed),
};
#undef FIELD

}  // namespace

bool save_tuning(const FlightTuning& tuning, const char* path) {
    std::string text = "# dragon flight tuning\n";
    char line[128];
    for (const Field& field : FIELDS) {
        std::snprintf(line, sizeof(line), "%s %.6g\n", field.name, tuning.*field.member);
        text += line;
    }
    if (!SDL_SaveFile(path, text.data(), text.size())) {
        LOG_ERROR("could not write '%s': %s", path, SDL_GetError());
        return false;
    }
    LOG_INFO("saved tuning -> %s", path);
    return true;
}

bool load_tuning(FlightTuning& tuning, const char* path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (!data) {
        LOG_WARN("no tuning file at '%s'", path);
        return false;
    }
    std::string text(static_cast<char*>(data), size);
    SDL_free(data);

    int applied = 0;
    size_t cursor = 0;
    while (cursor < text.size()) {
        size_t end = text.find('\n', cursor);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(cursor, end - cursor);
        cursor = end + 1;

        if (line.empty() || line[0] == '#') continue;
        size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        std::string key = line.substr(0, space);
        float value = float(SDL_atof(line.c_str() + space + 1));

        for (const Field& field : FIELDS) {
            if (key == field.name) {
                tuning.*field.member = value;
                ++applied;
                break;
            }
        }
    }
    LOG_INFO("loaded %d tuning values from %s", applied, path);
    return applied > 0;
}

}  // namespace game
