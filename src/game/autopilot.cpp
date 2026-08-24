#include "game/autopilot.h"

using core::Vec3;

namespace game {

FlightInput steer_through(const FlightState& state, Vec3 target, Vec3 approach_direction,
                          const AutopilotTuning& tuning, float ground_height) {
    // Line up on the approach axis when far out, converging on the target itself
    // as the range closes. Flying straight at a ring from off to one side means
    // crossing its plane at a shallow angle and clipping the rim.
    if (core::length_sq(approach_direction) > 1e-6f) {
        const Vec3 axis = core::normalize(approach_direction);
        const float range = core::distance(state.position, target);
        const float lead = core::minf(range * tuning.axis_lead_fraction, tuning.axis_lead_max);
        target -= axis * lead;
    }
    return steer_toward(state, target, tuning, ground_height);
}

FlightInput steer_toward(const FlightState& state, Vec3 target, const AutopilotTuning& tuning,
                         float ground_height) {
    FlightInput input;

    const Vec3 to_target = target - state.position;
    const Vec3 desired = core::normalize_or(to_target, state.forward());

    // Bearing error measured in the dragon's own frame: positive means the
    // target is off to the right.
    const float ahead = core::dot(desired, state.forward());
    const float lateral = core::dot(desired, state.right());
    const float vertical = core::dot(desired, state.up());
    const float bearing = std::atan2(lateral, ahead);

    // Bank into the turn. Roll rather than rudder, because that is how the
    // flight model actually turns -- tilting the lift vector.
    //
    // The damping term uses the dragon's own body roll rate. A roll command of
    // +1 produces a NEGATIVE body rate about Z, so the current rate expressed in
    // command units is -omega_z, and opposing it means adding +omega_z.
    input.roll = core::clampf(
        bearing * tuning.roll_gain + state.angular_velocity.z * tuning.roll_damping, -1.0f, 1.0f);

    // Pitch toward the target. When the target is behind, `vertical` becomes
    // unreliable, so fall back to the raw height difference.
    float pitch_command = vertical * tuning.pitch_gain;
    if (ahead < 0.0f) {
        pitch_command = core::clampf(to_target.y * 0.02f, -1.0f, 1.0f) * tuning.pitch_gain;
    }
    // Pitch command +1 produces a positive body rate about X, so damping
    // subtracts the current rate directly.
    input.pitch = core::clampf(pitch_command - state.angular_velocity.x * tuning.pitch_damping,
                               -1.0f, 1.0f);

    // Flap when slow, or whenever the target is above: climbing costs energy and
    // flapping is the only way to make it.
    const bool needs_energy = state.airspeed < tuning.cruise_speed || vertical > 0.08f;
    input.flap = needs_energy ? 1.0f : 0.0f;

    // Tuck to build speed on the way down, but not past the speed limit.
    if (vertical < -tuning.dive_threshold && state.airspeed < tuning.max_speed) {
        input.tuck = 1.0f;
    }
    // Brake if badly overspeed, so a long dive does not overshoot every ring.
    if (state.airspeed > tuning.max_speed * 1.15f) input.brake = 0.6f;

    // Terrain avoidance overrides everything: pull up hard and flap. Without
    // this an autopilot chasing a low checkpoint flies straight into a hillside.
    const float clearance = state.position.y - ground_height;
    if (clearance < tuning.min_clearance) {
        const float urgency = core::saturate(1.0f - clearance / core::maxf(tuning.min_clearance, 1.0f));
        input.pitch = core::maxf(input.pitch, urgency);
        input.flap = 1.0f;
        input.tuck = 0.0f;
        // Level the wings while climbing away: banking here just delays the
        // recovery.
        input.roll *= 1.0f - urgency;
    }
    return input;
}

}  // namespace game
