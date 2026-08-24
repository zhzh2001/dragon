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

    // Terrain avoidance, applied by moving the goal rather than by seizing the
    // controls. The first version overrode pitch and roll directly, which fought
    // the PD loop below and switched behaviour discontinuously -- it flew into
    // the ground on some courses and not others depending on nothing more than
    // frame timing.
    const float clearance = state.position.y - ground_height;
    const float shortfall = tuning.min_clearance - clearance;
    if (shortfall > 0.0f) target.y += shortfall * tuning.avoid_lift;

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

    // Low on altitude: never dive, and always spend energy on climbing.
    if (shortfall > 0.0f) {
        input.flap = 1.0f;
        input.tuck = 0.0f;
        input.brake = 0.0f;
    }

    // Critically low, and only then, take the bank out directly. In a steep bank
    // the lift vector is mostly horizontal, so pulling back turns instead of
    // climbing -- the classic descending spiral -- and the lifted aim point
    // alone cannot fix that, because the dragon is already pointing where it
    // needs to go. The roll has to be commanded out.
    const float critical =
        core::saturate(1.0f - clearance / core::maxf(tuning.min_clearance * 0.55f, 1.0f));
    if (critical > 0.0f) {
        const float bank = std::atan2(core::dot(state.right(), Vec3::up()),
                                      core::dot(state.up(), Vec3::up()));
        const float level_command = core::clampf(-bank * 1.6f, -1.0f, 1.0f);
        input.roll = core::lerpf(input.roll, level_command, critical);
    }
    return input;
}

}  // namespace game
