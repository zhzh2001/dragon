#pragma once

#include "core/math.h"

namespace gfx {

// A camera as position + orientation, not a matrix. Gameplay code (chase
// cameras, cutscenes, dragon head-cams) wants to reason about where the camera
// is and which way it faces; matrices are derived on demand.
struct Camera {
    core::Vec3 position = core::Vec3::zero();
    core::Quat rotation = core::Quat::identity();

    float fov_y_deg = 60.0f;
    float z_near = 0.1f;

    core::Vec3 forward() const { return core::quat_forward(rotation); }
    core::Vec3 up() const { return core::quat_up(rotation); }
    core::Vec3 right() const { return core::quat_right(rotation); }

    core::Mat4 view() const { return core::view_from_transform(position, rotation); }

    // Reversed-Z with an infinite far plane: nothing ever needs a far-plane
    // tuning pass, and precision stays good across a whole valley.
    core::Mat4 projection(float aspect) const {
        return core::perspective_reverse_z(core::radians(fov_y_deg), aspect, z_near);
    }

    core::Mat4 view_projection(float aspect) const { return projection(aspect) * view(); }

    void look_at(core::Vec3 target, core::Vec3 up_hint = core::Vec3::up()) {
        rotation = core::look_rotation(target - position, up_hint);
    }
};

}  // namespace gfx
