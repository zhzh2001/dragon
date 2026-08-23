#include "game/chase_camera.h"

#include "game/terrain.h"

using core::Vec3;

namespace game {

Vec3 ChaseCamera::desired_position(const FlightState& state) const {
    const float trail = tuning.distance + state.airspeed * tuning.distance_speed_gain;

    // Trail along the dragon's own back rather than straight up in world space,
    // so a roll or a loop keeps the camera behind the body instead of swinging
    // under it.
    const Vec3 back = -state.forward();
    const Vec3 up = core::lerp(Vec3::up(), state.up(), tuning.roll_inheritance);
    return state.position + back * trail + core::normalize_or(up, Vec3::up()) * tuning.height;
}

Vec3 ChaseCamera::desired_target(const FlightState& state) const {
    // Aim ahead of the dragon, which keeps it low in frame and puts the space it
    // is flying into on screen -- the thing the player actually needs to see.
    return state.position + state.forward() * tuning.look_ahead;
}

void ChaseCamera::snap_to(const FlightState& state) {
    position_ = desired_position(state);
    aim_ = desired_target(state);
    fov_ = tuning.fov_base_deg;
    camera_.position = position_;
    camera_.fov_y_deg = fov_;
    camera_.rotation = core::look_rotation(aim_ - position_, Vec3::up());
    initialized_ = true;
}

void ChaseCamera::update(const FlightState& state, const Terrain* terrain, float dt) {
    if (!initialized_) {
        snap_to(state);
        return;
    }

    position_ = core::damp(position_, desired_position(state), tuning.position_lag, dt);
    aim_ = core::damp(aim_, desired_target(state), tuning.aim_lag, dt);

    // Keep the camera out of the terrain. A proper swept test comes with the
    // spring arm in M5; a height clamp already prevents the worst of it.
    if (terrain) {
        const float ground = terrain->height_at(position_.x, position_.z);
        position_.y = core::maxf(position_.y, ground + tuning.ground_margin);
    }

    // FOV widens with speed. This is most of the felt sense of velocity --
    // considerably more than the actual number of metres per second.
    const float target_fov =
        core::minf(tuning.fov_base_deg + state.airspeed * tuning.fov_speed_gain,
                   tuning.fov_max_deg);
    fov_ = core::damp(fov_, target_fov, 0.35f, dt);

    // Roll the camera partway with the dragon by blending the up reference.
    const Vec3 up_reference =
        core::normalize_or(core::lerp(Vec3::up(), state.up(), tuning.roll_inheritance), Vec3::up());

    camera_.position = position_;
    camera_.fov_y_deg = fov_;
    camera_.rotation = core::look_rotation(aim_ - position_, up_reference);
}

}  // namespace game
