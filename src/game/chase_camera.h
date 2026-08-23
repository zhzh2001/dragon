#pragma once

#include "game/flight.h"
#include "gfx/camera.h"

namespace game {

class Terrain;

// Third-person camera that follows the dragon.
//
// M5 refines this into a spring arm with terrain collision. For now it is the
// minimum needed to judge whether flight feels right: trailing distance that
// grows with speed, partial roll inheritance, and lead into the turn. Full roll
// inheritance is deliberately avoided -- it is nauseating and it hides the
// horizon, which is the main reference for reading your own attitude.
struct ChaseCameraTuning {
    float distance = 17.0f;         // metres behind at rest
    float distance_speed_gain = 0.10f;  // extra metres per m/s of airspeed
    float height = 4.6f;            // metres above the dragon
    float look_ahead = 9.0f;        // metres ahead of the dragon to aim at

    // Half-lives, in seconds. Position lags more than aim so the dragon leads
    // the frame during hard manoeuvres.
    float position_lag = 0.10f;
    float aim_lag = 0.06f;

    // How much of the dragon's bank the camera copies. Some is essential to
    // convey the turn; all of it is sickening.
    float roll_inheritance = 0.35f;

    float fov_base_deg = 62.0f;
    float fov_speed_gain = 0.16f;  // degrees per m/s, for the sense of speed
    float fov_max_deg = 88.0f;

    // Never let the camera end up inside the ground.
    float ground_margin = 3.5f;
};

class ChaseCamera {
public:
    void snap_to(const FlightState& state);
    void update(const FlightState& state, const Terrain* terrain, float dt);

    const gfx::Camera& camera() const { return camera_; }
    ChaseCameraTuning tuning;

private:
    core::Vec3 desired_position(const FlightState& state) const;
    core::Vec3 desired_target(const FlightState& state) const;

    gfx::Camera camera_;
    core::Vec3 position_ = core::Vec3::zero();
    core::Vec3 aim_ = core::Vec3::zero();
    float fov_ = 62.0f;
    bool initialized_ = false;
};

}  // namespace game
