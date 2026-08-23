#pragma once

#include "core/input.h"
#include "gfx/camera.h"

namespace game {

// Free-fly camera for inspecting the world: WASD to move, mouse to look, and no
// physics. It is a diagnostic tool, not gameplay -- when the dragon's chase
// camera looks wrong, flying the same path with this one tells you whether the
// problem is the camera or the world.
//
// Yaw and pitch are stored as scalars rather than a quaternion so the horizon
// stays level no matter how long you fly. Roll is deliberately unreachable.
class DebugCamera {
public:
    void set_position(core::Vec3 position, core::Vec3 look_target);
    void update(const core::Input& input, float dt, bool mouse_captured);

    const gfx::Camera& camera() const { return camera_; }
    gfx::Camera& camera() { return camera_; }

    float speed = 20.0f;              // metres/second
    float boost_multiplier = 6.0f;    // while shift is held
    float look_sensitivity = 0.12f;   // degrees per pixel
    float move_smoothing = 0.04f;     // velocity half-life, seconds

private:
    void apply_orientation();

    gfx::Camera camera_;
    core::Vec3 velocity_ = core::Vec3::zero();
    float yaw_ = 0.0f;    // radians, around +Y
    float pitch_ = 0.0f;  // radians, positive looks up
};

}  // namespace game
