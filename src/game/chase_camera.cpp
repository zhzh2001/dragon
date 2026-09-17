#include "game/chase_camera.h"

#include "game/terrain.h"

using core::Quat;
using core::Vec2;
using core::Vec3;

namespace game {
namespace {

// Cheap deterministic wobble for camera shake. Two incommensurable sines per
// axis, so it never settles into a visible rhythm.
//
// Frequencies are deliberately low (roughly 0.7 and 1.5 Hz). The first version
// ran at 2 and 5 Hz, which reads as the camera rattling rather than as air
// moving over a large animal.
float wobble(float t, float seed) {
    return std::sin(t * 4.3f + seed) * 0.62f + std::sin(t * 9.7f + seed * 2.7f) * 0.38f;
}

}  // namespace

ChaseCameraTuning camera_preset_chase() { return ChaseCameraTuning(); }

ChaseCameraTuning camera_preset_action() {
    ChaseCameraTuning t;
    // Close, tight, and committed to the turn. Reads speed and intent best, and
    // is the one most likely to make people motion sick.
    t.distance = 14.0f;
    t.distance_speed_gain = 0.05f;
    t.height = 3.4f;
    t.look_ahead = 16.0f;
    t.look_ahead_turn = 13.0f;
    t.position_lag = 0.07f;
    t.aim_lag = 0.035f;
    t.roll_inheritance = 0.55f;
    t.fov_base_deg = 70.0f;
    t.fov_speed_gain = 0.28f;
    t.shake_speed = 0.020f;
    return t;
}

ChaseCameraTuning camera_preset_cinematic() {
    ChaseCameraTuning t;
    // Far, slow, and level. Shows off the landscape and the dragon's silhouette;
    // poor for precision flying.
    t.distance = 34.0f;
    t.distance_speed_gain = 0.14f;
    t.height = 8.0f;
    t.look_ahead = 6.0f;
    t.look_ahead_turn = 4.0f;
    t.position_lag = 0.26f;
    t.aim_lag = 0.14f;
    t.roll_inheritance = 0.12f;
    t.fov_base_deg = 52.0f;
    t.fov_speed_gain = 0.12f;
    t.shake_speed = 0.005f;
    t.shake_g = 0.0f;
    return t;
}

Vec3 ChaseCamera::compute_pivot(const FlightState& state) const {
    // The pivot sits slightly ahead of the body so the camera orbits the
    // dragon's shoulders rather than its tail.
    const Vec3 up = core::normalize_or(
        core::lerp(Vec3::up(), state.up(), tuning.roll_inheritance), Vec3::up());
    return state.position + state.forward() * tuning.pivot_forward + up * tuning.height;
}

Vec3 ChaseCamera::compute_arm_direction(const FlightState& state) const {
    // Start from straight behind, in the dragon's own frame, then apply free
    // look as a yaw about world up and a pitch about the camera's right.
    Vec3 back = -state.forward();

    const Quat yaw = Quat::from_axis_angle(Vec3::up(), core::radians(free_look_.x));
    back = core::rotate(yaw, back);

    Vec3 right = core::cross(back, Vec3::up());
    if (core::length_sq(right) < 1e-6f) right = state.right();
    right = core::normalize(right);
    const Quat pitch = Quat::from_axis_angle(right, core::radians(free_look_.y));
    back = core::rotate(pitch, back);

    return core::normalize_or(back, -state.forward());
}

Vec3 ChaseCamera::compute_aim(const FlightState& state) const {
    // Lead the turn. Aiming only along `forward` means the camera is always
    // looking at where the dragon has already been pointed; leading by the turn
    // rate puts the space it is about to enter on screen.
    const float turn_rate = state.angular_velocity.y;
    const Vec3 lead = state.right() * (-turn_rate * tuning.look_ahead_turn);
    return state.position + state.forward() * tuning.look_ahead + lead +
           Vec3::up() * tuning.look_down_bias;
}

float ChaseCamera::sweep_arm(Vec3 pivot, Vec3 direction, float desired,
                             const Terrain* terrain) const {
    if (!terrain || desired <= 0.0f) return desired;

    // March outward and stop at the last clear sample. Sweeping rather than only
    // clamping the final position matters: a clamp alone happily places the
    // camera on the far side of a ridge, looking at the inside of a mountain.
    constexpr int STEPS = 14;
    float clear = desired;
    for (int i = 1; i <= STEPS; ++i) {
        const float t = desired * float(i) / float(STEPS);
        const Vec3 sample = pivot + direction * t;
        const float ground = terrain->height_at(sample.x, sample.z) + tuning.collision_margin;
        if (sample.y < ground) {
            clear = desired * float(i - 1) / float(STEPS);
            break;
        }
    }
    return core::maxf(clear, tuning.min_distance);
}

Vec3 ChaseCamera::first_person_eye(const FlightState& state) const {
    if (!head_known_) return state.position + core::rotate(state.orientation, head_offset);
    return head_world_ + state.up() * head_up_ - state.forward() * head_back_;
}

void ChaseCamera::snap_to(const FlightState& state) {
    free_look_ = Vec2{0.0f, 0.0f};
    pivot_ = compute_pivot(state);
    requested_arm_ = tuning.distance + state.airspeed * tuning.distance_speed_gain;
    arm_ = requested_arm_;
    position_ = pivot_ + compute_arm_direction(state) * arm_;
    aim_ = compute_aim(state);
    fov_ = tuning.fov_base_deg;

    if (first_person) {
        position_ = first_person_eye(state);
        aim_ = position_ + state.forward() * 100.0f;
    }

    camera_.position = position_;
    camera_.fov_y_deg = fov_;
    camera_.rotation = core::look_rotation(aim_ - position_, Vec3::up());
    initialized_ = true;
}

void ChaseCamera::update(const FlightState& state, const Terrain* terrain, Vec2 free_look_degrees,
                         float dt) {
    if (!initialized_ || dt <= 0.0f) {
        snap_to(state);
        return;
    }

    shake_time_ += dt;

    // ---- free look ----
    free_look_.x = core::clampf(free_look_.x - free_look_degrees.x, -tuning.free_look_yaw_limit,
                                tuning.free_look_yaw_limit);
    free_look_.y = core::clampf(free_look_.y - free_look_degrees.y, -tuning.free_look_pitch_limit,
                                tuning.free_look_pitch_limit);
    if (tuning.free_look_return > 0.0f) {
        free_look_.x = core::damp(free_look_.x, 0.0f, tuning.free_look_return, dt);
        free_look_.y = core::damp(free_look_.y, 0.0f, tuning.free_look_return, dt);
    }

    // ---- first person ----
    if (first_person) {
        // Rigid to the head: no lag, no arm, no collision. The point of this
        // view is that it does not smooth anything away.
        position_ = first_person_eye(state);
        // Looking down the negated arm direction means free look works here too.
        aim_ = position_ - compute_arm_direction(state) * 200.0f;
        fov_ = core::damp(fov_, core::minf(tuning.fov_base_deg + state.airspeed * tuning.fov_speed_gain,
                                          tuning.fov_max_deg),
                          tuning.fov_lag, dt);
        camera_.position = position_;
        camera_.fov_y_deg = fov_;
        camera_.rotation = core::look_rotation(
            aim_ - position_,
            core::normalize_or(core::lerp(Vec3::up(), state.up(), 0.85f), Vec3::up()));
        return;
    }

    // ---- arm ----
    pivot_ = compute_pivot(state);
    const Vec3 direction = compute_arm_direction(state);
    requested_arm_ = tuning.distance + state.airspeed * tuning.distance_speed_gain;

    const float clear_arm = sweep_arm(pivot_, direction, requested_arm_, terrain);
    // Shorten quickly, extend slowly.
    const float lag =
        clear_arm < arm_ ? tuning.collision_shorten_lag : tuning.collision_extend_lag;
    arm_ = core::damp(arm_, clear_arm, lag, dt);

    Vec3 target_position = pivot_ + direction * arm_;

    // Even a fully shortened arm can end up underground when the dragon is
    // hugging a slope, so the final position is lifted as a last resort.
    if (terrain) {
        const float ground =
            terrain->height_at(target_position.x, target_position.z) + tuning.collision_margin;
        target_position.y = core::maxf(target_position.y, ground);
    }

    position_ = core::damp(position_, target_position, tuning.position_lag, dt);

    // The smoothed position can still drift below ground on a fast climb out of
    // a valley, so it is clamped after smoothing too. Hard clamp, no lag: being
    // inside a mountain for even a few frames is worse than a small jolt.
    if (terrain) {
        const float ground =
            terrain->height_at(position_.x, position_.z) + tuning.collision_margin * 0.75f;
        position_.y = core::maxf(position_.y, ground);
    }

    aim_ = core::damp(aim_, compute_aim(state), tuning.aim_lag, dt);

    // ---- field of view ----
    const float target_fov = core::minf(
        tuning.fov_base_deg + state.airspeed * tuning.fov_speed_gain, tuning.fov_max_deg);
    fov_ = core::damp(fov_, target_fov, tuning.fov_lag, dt);

    // ---- orientation ----
    Vec3 up_reference =
        core::normalize_or(core::lerp(Vec3::up(), state.up(), tuning.roll_inheritance), Vec3::up());

    // Shake grows with speed and g-load, applied as a small rotation so it never
    // moves the camera into geometry.
    const float shake_amount =
        core::minf(state.airspeed * tuning.shake_speed +
                       std::fabs(state.g_load - 1.0f) * tuning.shake_g * 10.0f,
                   tuning.shake_max);
    Vec3 aim_point = aim_;
    if (shake_amount > 0.001f) {
        const Vec3 right = core::normalize_or(core::cross(aim_ - position_, up_reference),
                                              Vec3::right());
        const Vec3 up = core::normalize_or(core::cross(right, aim_ - position_), Vec3::up());
        const float scale = core::radians(shake_amount) * core::length(aim_ - position_);
        aim_point += right * (wobble(shake_time_, 0.0f) * scale) +
                     up * (wobble(shake_time_, 4.1f) * scale);
    }

    camera_.position = position_;
    camera_.fov_y_deg = fov_;
    camera_.rotation = core::look_rotation(aim_point - position_, up_reference);
}

}  // namespace game
