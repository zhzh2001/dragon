#include "game/maneuver.h"

using core::Vec3;

namespace game {

bool Maneuver::start(ManeuverKind what, float roll_direction, FlightState& state,
                     const ManeuverTuning& tuning) {
    if (what == ManeuverKind::None || active() || cooldown > 0.0f) return false;
    if (state.grounded) return false;
    if (what == ManeuverKind::Flip && state.airspeed < tuning.flip_min_airspeed) return false;
    kind = what;
    time = 0.0f;
    phase = 0;
    direction = roll_direction >= 0.0f ? 1.0f : -1.0f;
    start_forward = state.forward();
    if (kind == ManeuverKind::Roll) {
        // The dodge: a sideways kick in the direction of the roll, so the
        // roll moves the dragon off the line it was on, not just around it.
        state.velocity = state.velocity + state.right() * (direction * tuning.roll_dodge_impulse);
    }
    return true;
}

void Maneuver::apply(FlightInput& in, FlightState& state, const ManeuverTuning& tuning,
                     float dt) {
    cooldown = core::maxf(cooldown - dt, 0.0f);
    if (!active()) return;
    time += dt;
    in.maneuver = true;

    switch (kind) {
        case ManeuverKind::Roll: {
            in.roll = direction;
            // A touch of nose-up so the roll does not shed height, and no
            // tuck: folded wings do not roll.
            in.pitch = 0.15f;
            in.tuck = 0.0f;
            in.agility = tuning.roll_agility;
            // The dodge continues through the first part of the roll, along
            // the direction the dragon was rolling toward when it started --
            // its own right axis swings round as it rolls, so the push uses
            // the horizontal of the start heading's right.
            if (time < tuning.roll_duration * tuning.roll_dodge_fraction) {
                const Vec3 side = core::normalize_or(
                    core::cross(Vec3{start_forward.x, 0.0f, start_forward.z}, Vec3::up()), state.right());
                state.velocity = state.velocity + side * (direction * tuning.roll_dodge_push * dt);
            }
            if (time >= tuning.roll_duration) {
                kind = ManeuverKind::None;
                cooldown = tuning.cooldown;
            }
            break;
        }
        case ManeuverKind::Flip: {
            in.tuck = 0.0f;
            in.brake = 0.0f;
            in.flap = 1.0f;  // the half loop eats energy; put it back
            in.agility = tuning.flip_agility;
            const Vec3 forward = state.forward();
            const Vec3 up = state.up();
            const Vec3 right = state.right();
            if (phase == 0) {
                // Pull until the heading has reversed.
                in.pitch = 1.0f;
                in.roll = 0.0f;
                if (core::dot(forward, start_forward) < -0.85f || time > tuning.flip_max_duration * 0.6f) {
                    phase = 1;
                }
            } else {
                // Roll out to upright. Inverted, right.y is near zero and says
                // nothing, so commit to one direction until the sky is back
                // overhead; then bring the wings level proportionally. Roll
                // right (+1) drops the right wing, lowering right.y.
                in.pitch = 0.1f;
                in.roll = up.y < 0.3f ? 1.0f : core::clampf(right.y * 4.0f, -1.0f, 1.0f);
                const bool upright = up.y > 0.9f && std::fabs(right.y) < 0.12f;
                if (upright || time > tuning.flip_max_duration) {
                    kind = ManeuverKind::None;
                    cooldown = tuning.cooldown;
                }
            }
            break;
        }
        default:
            kind = ManeuverKind::None;
            break;
    }
}

}  // namespace game
