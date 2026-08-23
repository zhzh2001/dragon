#include "game/debug_camera.h"

using core::Quat;
using core::Vec3;

namespace game {

void DebugCamera::set_position(Vec3 position, Vec3 look_target) {
    camera_.position = position;

    Vec3 direction = core::normalize_or(look_target - position, Vec3::forward());
    pitch_ = std::asin(core::clampf(direction.y, -1.0f, 1.0f));
    // Rotating (0,0,-1) by yaw about +Y gives (-sin yaw, 0, -cos yaw), so
    // recovering yaw from a direction needs both components negated.
    yaw_ = std::atan2(-direction.x, -direction.z);
    velocity_ = Vec3::zero();
    apply_orientation();
}

void DebugCamera::apply_orientation() {
    // Yaw then pitch, never roll: the horizon stays level.
    camera_.rotation = core::normalize(Quat::from_axis_angle(Vec3::unit_y(), yaw_) *
                                       Quat::from_axis_angle(Vec3::unit_x(), pitch_));
}

void DebugCamera::update(const core::Input& input, float dt, bool mouse_captured) {
    if (mouse_captured) {
        core::Vec2 delta = input.mouse_delta();
        yaw_ -= core::radians(delta.x * look_sensitivity);
        // Clamped just short of straight up/down, where yaw becomes meaningless.
        constexpr float LIMIT = core::HALF_PI - 0.02f;
        pitch_ = core::clampf(pitch_ - core::radians(delta.y * look_sensitivity), -LIMIT, LIMIT);
        apply_orientation();
    }

    Vec3 wish = camera_.forward() * input.axis(SDL_SCANCODE_S, SDL_SCANCODE_W) +
                camera_.right() * input.axis(SDL_SCANCODE_A, SDL_SCANCODE_D) +
                Vec3::up() * input.axis(SDL_SCANCODE_Q, SDL_SCANCODE_E);

    float current_speed = speed;
    if (input.down(SDL_SCANCODE_LSHIFT) || input.down(SDL_SCANCODE_RSHIFT)) {
        current_speed *= boost_multiplier;
    }

    // Smoothing the velocity rather than the position keeps the camera from
    // drifting after the keys are released.
    Vec3 target_velocity = core::normalize_or(wish, Vec3::zero()) * current_speed;
    velocity_ = core::damp(velocity_, target_velocity, move_smoothing, dt);
    camera_.position += velocity_ * dt;
}

}  // namespace game
