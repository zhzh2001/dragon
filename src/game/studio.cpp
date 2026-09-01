#include "game/studio.h"

#include <cmath>

using core::Quat;
using core::Vec3;

namespace game {

namespace {

// Orientation of each manoeuvre as a pure function of time. Everything else --
// body-frame angular velocity in particular -- is derived from this by finite
// difference, so a scenario cannot lie about its own rotation: whatever the
// orientation curve does, the chains and pendulums feel exactly that.
Quat orientation_at(StudioScenario scenario, float t) {
    switch (scenario) {
        case StudioScenario::TurnLeft:
        case StudioScenario::TurnRight: {
            // Coordinated turn: bank chosen for the radius, yaw rate v/R.
            const float sign = scenario == StudioScenario::TurnLeft ? 1.0f : -1.0f;
            const float speed = 35.0f, radius = 90.0f;
            const float bank = std::atan2(speed * speed, 9.81f * radius);  // ~54 deg
            const float yaw_rate = speed / radius;
            return Quat::from_axis_angle(Vec3::unit_y(), sign * yaw_rate * t) *
                   Quat::from_axis_angle(Vec3::unit_z(), sign * bank);
        }
        case StudioScenario::STurns: {
            // Rolling reversals: bank swings between the two sides, heading
            // follows. The reversal moment is the point of the scenario -- it is
            // where the tail whip and the leg swing are most visible.
            const float period = 5.0f;
            const float phase = core::TWO_PI * t / period;
            const float bank = core::radians(52.0f) * std::sin(phase);
            const float heading = 0.9f * -std::cos(phase);
            return Quat::from_axis_angle(Vec3::unit_y(), heading) *
                   Quat::from_axis_angle(Vec3::unit_z(), bank);
        }
        case StudioScenario::Attack: {
            // A weaving pursuit: gentle rolling turns while the head stays
            // pinned on the mark. The contrast between a busy body and a locked
            // head is the whole point of the scenario.
            const float phase = core::TWO_PI * t / 4.0f;
            const float bank = core::radians(30.0f) * std::sin(phase);
            const float heading = 0.28f * -std::cos(phase);
            const float pitch = core::radians(6.0f) * std::sin(phase * 0.5f);
            return Quat::from_axis_angle(Vec3::unit_y(), heading) *
                   Quat::from_axis_angle(Vec3::unit_x(), pitch) *
                   Quat::from_axis_angle(Vec3::unit_z(), bank);
        }
        case StudioScenario::Dive:
            return Quat::from_axis_angle(Vec3::unit_x(), core::radians(-55.0f));
        case StudioScenario::PullOut: {
            // 7 s cycle: dive, then a ~3 g pull back through level, then ease
            // over into the next dive. The pitch curve is smooth (cosine eases)
            // so the finite-differenced rates are too.
            const float cycle = std::fmod(t, 7.0f);
            float pitch_deg = -55.0f;
            if (cycle < 2.5f) {
                pitch_deg = -55.0f;
            } else if (cycle < 5.0f) {
                const float s = core::smoothstep(2.5f, 5.0f, cycle);
                pitch_deg = core::lerpf(-55.0f, 15.0f, s);
            } else {
                const float s = core::smoothstep(5.0f, 7.0f, cycle);
                pitch_deg = core::lerpf(15.0f, -55.0f, s);
            }
            return Quat::from_axis_angle(Vec3::unit_x(), core::radians(pitch_deg));
        }
        default:
            return Quat::identity();
    }
}

float speed_at(StudioScenario scenario, float t) {
    switch (scenario) {
        case StudioScenario::Glide: return 26.0f;
        case StudioScenario::Flap: return 32.0f + 1.5f * std::sin(core::TWO_PI * t / 0.9f);
        case StudioScenario::TurnLeft:
        case StudioScenario::TurnRight: return 35.0f;
        case StudioScenario::STurns: return 38.0f;
        case StudioScenario::Dive: return 95.0f;
        case StudioScenario::PullOut: {
            const float cycle = std::fmod(t, 7.0f);
            if (cycle < 2.5f) return core::lerpf(70.0f, 95.0f, cycle / 2.5f);
            if (cycle < 5.0f) return core::lerpf(95.0f, 60.0f, (cycle - 2.5f) / 2.5f);
            return core::lerpf(60.0f, 70.0f, (cycle - 5.0f) / 2.0f);
        }
        case StudioScenario::Brake:
            // Ping-pong between fast and slow, so the deceleration the legs and
            // chains feel is smooth and periodic rather than a loop-point snap.
            return 29.0f + 11.0f * std::cos(core::TWO_PI * t / 4.0f);
        case StudioScenario::Attack: return 36.0f;
        case StudioScenario::Grounded: return 0.0f;
        default: return 26.0f;
    }
}

}  // namespace

const char* studio_scenario_name(StudioScenario scenario) {
    switch (scenario) {
        case StudioScenario::Glide: return "glide";
        case StudioScenario::Flap: return "flap";
        case StudioScenario::TurnLeft: return "turn left";
        case StudioScenario::TurnRight: return "turn right";
        case StudioScenario::STurns: return "s-turns";
        case StudioScenario::Dive: return "dive";
        case StudioScenario::PullOut: return "pull-out";
        case StudioScenario::Brake: return "brake";
        case StudioScenario::Attack: return "attack";
        case StudioScenario::Grounded: return "grounded";
        default: return "?";
    }
}

const char* studio_scenario_notes(StudioScenario scenario) {
    switch (scenario) {
        case StudioScenario::Glide:
            return "calm baseline: idle at full strength, tail with a little sag";
        case StudioScenario::Flap:
            return "wingbeat: neck/tail bob with the thrust pulses";
        case StudioScenario::TurnLeft:
        case StudioScenario::TurnRight:
            return "tail swings to the outside, legs hang outward, not stiff";
        case StudioScenario::STurns:
            return "reversals: tail whips across, legs swing through, then settle";
        case StudioScenario::Dive:
            return "arrow shape: tucked, then untucked at speed -- wings still sweep, tips flutter";
        case StudioScenario::PullOut:
            return "the pull: wings bow up under g, tail sweeps low, head leads";
        case StudioScenario::Brake:
            return "flare: legs swing forward under the deceleration";
        case StudioScenario::Attack:
            return "neck and head hold the mark; jaw gapes on the breath, rears back on the spit";
        case StudioScenario::Grounded:
            return "wings stowed, legs planted, idle clip at full strength";
        default: return "";
    }
}

Vec3 studio_attack_target(float t, Vec3 centre) {
    // Orbits across the dragon's nose, wide and quick enough that the head has
    // to sweep visibly to hold it while the body weaves the other way.
    return centre + Vec3{85.0f * std::sin(0.8f * t), 25.0f * std::sin(1.3f * t),
                         -120.0f};
}

anim::RigAction studio_action(StudioScenario scenario, float previous, float t) {
    anim::RigAction action;
    if (scenario != StudioScenario::Attack) return action;
    // An 8 s cycle: spit at 0.5 s, breathe from 2 to 4.5 s, spit again at 6 s.
    const float period = 8.0f;
    const float cycle = std::fmod(t, period);
    action.breath = (cycle > 2.0f && cycle < 4.5f) ? 1.0f : 0.0f;
    for (const float shot : {0.5f, 6.0f}) {
        // Crossed if the shot time lies in (previous, t], allowing for the wrap.
        const float last = std::fmod(previous, period);
        const bool crossed = last < cycle ? (shot > last && shot <= cycle)
                                          : (shot > last || shot <= cycle);
        if (crossed && t > previous) action.fire = true;
    }
    return action;
}

FlightState studio_state(StudioScenario scenario, float t, Vec3 centre, float ground_y) {
    FlightState state;

    const Quat orientation = orientation_at(scenario, t);
    const float speed = speed_at(scenario, t);

    // Body-frame angular velocity by central difference of the orientation
    // curve. dq = q(t+h) * conj(q(t-h)) is the world-frame rotation across 2h.
    const float h = 1.0f / 240.0f;
    const Quat dq = core::normalize(orientation_at(scenario, t + h) *
                                    core::conjugate(orientation_at(scenario, t - h)));
    const float half_angle = std::acos(core::clampf(std::fabs(dq.w), -1.0f, 1.0f));
    Vec3 omega_world = Vec3::zero();
    const Vec3 axis{dq.x, dq.y, dq.z};
    if (core::length_sq(axis) > 1e-12f && half_angle > 1e-6f) {
        omega_world = core::normalize(axis) * (dq.w < 0.0f ? -1.0f : 1.0f) *
                      (2.0f * half_angle / (2.0f * h));
    }

    state.position = centre;
    state.orientation = orientation;
    state.velocity = core::rotate(orientation, Vec3::forward()) * speed;
    state.angular_velocity = core::rotate(core::conjugate(orientation), omega_world);
    state.airspeed = speed;
    state.climb_rate = state.velocity.y;
    state.ground_clearance = centre.y - ground_y;
    state.g_load = 1.0f;
    state.wing_angle = core::radians(9.0f);  // glide dihedral

    switch (scenario) {
        case StudioScenario::Flap: {
            state.flap_amplitude = 1.0f;
            state.flap_phase = std::fmod(t / 0.9f, 1.0f);
            // Fast powered downstroke, slower recovery -- the same asymmetry the
            // flight model uses.
            const float phase = state.flap_phase;
            const float beat = phase < 0.4f ? std::cos(core::PI * phase / 0.4f)
                                            : -std::cos(core::PI * (phase - 0.4f) / 0.6f);
            state.wing_angle = core::radians(8.0f) + core::radians(36.0f) * beat;
            break;
        }
        case StudioScenario::TurnLeft:
        case StudioScenario::TurnRight: {
            const float sign = scenario == StudioScenario::TurnLeft ? -1.0f : 1.0f;
            state.g_load = 1.0f / std::cos(std::atan2(35.0f * 35.0f, 9.81f * 90.0f));
            // Held stick: rudder and a little roll toward the turn.
            state.control = Vec3{0.15f, sign * 0.5f, sign * 0.35f};
            break;
        }
        case StudioScenario::STurns: {
            const float phase = core::TWO_PI * t / 5.0f;
            state.control = Vec3{0.1f, 0.6f * std::cos(phase), 0.8f * std::cos(phase)};
            state.g_load = 1.0f + 0.8f * std::fabs(std::sin(phase));
            break;
        }
        case StudioScenario::Dive: {
            // Tucked for the first half of each cycle, released for the second:
            // the release shows what speed alone does to the wings.
            const float cycle = std::fmod(t, 10.0f);
            state.wing_tuck = 1.0f - core::smoothstep(4.5f, 5.5f, cycle) +
                              core::smoothstep(9.3f, 10.0f, cycle);
            state.wing_tuck = core::saturate(state.wing_tuck);
            state.control = Vec3{-0.2f, 0.0f, 0.0f};
            state.g_load = 0.4f;
            break;
        }
        case StudioScenario::PullOut: {
            const float cycle = std::fmod(t, 7.0f);
            const float pulling = core::smoothstep(2.4f, 3.2f, cycle) *
                                  (1.0f - core::smoothstep(4.6f, 5.4f, cycle));
            state.wing_tuck = 1.0f - core::smoothstep(2.2f, 3.0f, cycle) +
                              core::smoothstep(5.0f, 7.0f, cycle);
            state.wing_tuck = core::saturate(state.wing_tuck);
            state.control = Vec3{pulling, 0.0f, 0.0f};
            // The pull itself is what loads the wings.
            state.g_load = 1.0f + 2.2f * pulling;
            break;
        }
        case StudioScenario::Brake: {
            // Braking during the decelerating half of the ping-pong.
            const float decelerating = -std::sin(core::TWO_PI * t / 4.0f);
            state.wing_brake = core::saturate(decelerating * 1.4f);
            state.control = Vec3{0.35f * state.wing_brake, 0.0f, 0.0f};
            break;
        }
        case StudioScenario::Attack: {
            const float phase = core::TWO_PI * t / 4.0f;
            state.control = Vec3{0.1f * std::sin(phase * 0.5f), 0.3f * std::cos(phase),
                                 0.55f * std::cos(phase)};
            state.g_load = 1.0f + 0.35f * std::fabs(std::sin(phase));
            break;
        }
        case StudioScenario::Grounded:
            state.position.y = ground_y + 2.5f;  // hips above the feet
            state.velocity = Vec3::zero();
            state.airspeed = 0.0f;
            state.grounded = true;
            state.ground_clearance = 0.0f;
            // Wings stow along the body on the ground -- the tuck fold is
            // exactly that shape.
            state.wing_tuck = 1.0f;
            state.wing_angle = core::radians(18.0f);
            break;
        default:
            break;
    }

    return state;
}

}  // namespace game
