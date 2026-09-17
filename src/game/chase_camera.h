#pragma once

#include "game/flight.h"
#include "gfx/camera.h"

namespace game {

class Terrain;

// Third-person camera that follows the dragon.
//
// This is the second-hardest thing in the project after animating a dragon, and
// for the same reason: there is no formula for it, only a lot of coupled
// parameters that have to be dragged until the result stops feeling wrong. So
// every one of them is exposed, and the presets exist to compare whole
// configurations rather than argue about individual numbers.
//
// The arm is swept against the terrain rather than merely clamped, because
// clamping alone puts the camera on the far side of a ridge and shows you the
// inside of a mountain. Full roll inheritance is deliberately unavailable: it is
// nauseating, and it hides the horizon, which is the player's main reference for
// reading their own attitude.
struct ChaseCameraTuning {
    // ---- arm geometry ----
    float distance = 22.0f;             // metres behind at rest
    float distance_speed_gain = 0.09f;  // extra metres per m/s of airspeed
    float height = 5.2f;                // metres above the dragon
    float pivot_forward = 2.0f;         // pivot ahead of the body centre

    // ---- aim ----
    float look_ahead = 12.0f;      // metres ahead of the dragon
    float look_ahead_turn = 9.0f;  // extra lead per rad/s of turn rate
    float look_down_bias = 0.0f;   // metres; negative aims lower

    // ---- lag, in seconds of half-life ----
    // Position lags more than aim, so the dragon leads the frame during hard
    // manoeuvres instead of being dragged behind the view.
    float position_lag = 0.11f;
    float aim_lag = 0.055f;
    float fov_lag = 0.30f;

    // How much of the dragon's bank the camera copies. Some is essential to
    // convey the turn; all of it is sickening.
    float roll_inheritance = 0.32f;

    // ---- field of view ----
    float fov_base_deg = 62.0f;
    float fov_speed_gain = 0.22f;  // degrees per m/s -- most of the felt speed
    float fov_max_deg = 92.0f;

    // ---- terrain collision ----
    // How far the camera keeps off the ground.
    float collision_margin = 4.0f;
    // Arm shortening is fast (clipping is immediately ugly) and recovery is slow
    // (snapping back out is jarring).
    float collision_shorten_lag = 0.02f;
    float collision_extend_lag = 0.45f;
    // Shortest the arm may become before the camera gives up and rises instead.
    float min_distance = 5.0f;

    // ---- free look ----
    // Orbiting the view without changing where the dragon is going. Essential in
    // a flight game: you need to look at where you are about to go, and later at
    // whatever is shooting at you. The delta arrives already in degrees, so the
    // camera does not need to know which device produced it.
    // ---- first person ----
    // The composition every species is held to, measured off the original
    // dragon's view (horn tips at the bottom of the frame, wing roots in the
    // corners). The eye sits this many head WIDTHS behind the rear of the
    // head's own mesh -- width, because a head that close fills the frame
    // sideways before it does vertically -- and high enough that the highest
    // point of head or neck in front of it appears this far down the frame
    // (fraction of the half-height below the view axis: 0 is the centre, 1
    // the bottom edge).
    float first_person_setback = 1.0f;
    float first_person_head_line = 0.67f;

    float free_look_return = 1.2f;  // half-life back to centre; 0 = holds
    float free_look_yaw_limit = 150.0f;
    float free_look_pitch_limit = 72.0f;

    // ---- shake ----
    // Scaled by speed and g-load. A little sells velocity; a lot is unreadable,
    // and the first pass was far too much -- at cruise it sat pinned at the
    // ceiling, so the shake was effectively constant instead of expressive.
    // These values are barely perceptible at cruise and noticeable in a dive.
    float shake_speed = 0.010f;
    float shake_g = 0.015f;
    float shake_max = 0.45f;  // degrees
};

// Whole-camera configurations, for comparing feels rather than parameters.
ChaseCameraTuning camera_preset_chase();
ChaseCameraTuning camera_preset_action();
ChaseCameraTuning camera_preset_cinematic();

class ChaseCamera {
public:
    void snap_to(const FlightState& state);

    // `free_look_degrees` is a yaw/pitch delta in degrees for this frame,
    // already gated by whatever owns the input. `dt` must be > 0.
    void update(const FlightState& state, const Terrain* terrain, core::Vec2 free_look_degrees,
                float dt);

    const gfx::Camera& camera() const { return camera_; }
    ChaseCameraTuning tuning;

    // First person, from just behind and above the dragon's head. Cheap to
    // support and a completely different way to feel the same flight model.
    //
    // Deliberately not at the eyes: from out at the snout you see no dragon at
    // all, which reads as a disembodied camera. Sitting back a little puts the
    // snout ahead and the wing roots in peripheral vision, so the creature you
    // are flying is present in the frame.
    //
    // The eye is placed relative to the ANIMATED HEAD, not the body origin.
    // A fixed body offset was tuned on one asset, whose head sits 1.5 m above
    // the origin; the generated species carry theirs two to five metres up
    // and a further two metres forward, and the same offset put the camera
    // inside the neck with the skull filling the frame. Whoever owns the rig
    // hands over the head's world position each frame with
    // set_first_person_head(), together with the species' offsets from it
    // (RigTuning::first_person_up / _back, body-frame metres: up along the
    // body's up to clear the skull and crest, back along its aft axis to keep
    // the snout in frame). Neither offset follows the head's own rotation: a
    // neck that lags a turn would otherwise roll and swing the whole view.
    // Until the head is supplied, the eye falls back to `head_offset` from
    // the body origin.
    bool first_person = false;
    core::Vec3 head_offset = core::Vec3{0.0f, 1.5f, -3.2f};
    void set_first_person_head(core::Vec3 world_position, float up_metres, float back_metres) {
        head_world_ = world_position;
        head_up_ = up_metres;
        head_back_ = back_metres;
        head_known_ = true;
    }
    void clear_first_person_head() { head_known_ = false; }
    // Put the first-person camera on the head it was last given, with no
    // smoothing. Called again after the rig has posed this frame's head, so
    // the eye and the mesh agree; the eye otherwise trails the head by a
    // frame, and with uneven frame times that trail changes length every
    // frame and the horns twitch.
    void place_first_person(const FlightState& state);

    // Diagnostics, for the tuning panel and debug draw.
    float arm_length() const { return arm_; }
    float requested_arm_length() const { return requested_arm_; }
    bool arm_is_blocked() const { return requested_arm_ - arm_ > 0.5f; }
    core::Vec3 pivot() const { return pivot_; }
    core::Vec2 free_look_angles() const { return free_look_; }

private:
    core::Vec3 compute_pivot(const FlightState& state) const;
    // Where the first-person eye sits this frame, in world space.
    core::Vec3 first_person_eye(const FlightState& state) const;
    // Arm direction in world space, including the free-look offset.
    core::Vec3 compute_arm_direction(const FlightState& state) const;
    core::Vec3 compute_aim(const FlightState& state) const;
    // Longest arm that keeps the camera clear of the ground.
    float sweep_arm(core::Vec3 pivot, core::Vec3 direction, float desired,
                    const Terrain* terrain) const;

    gfx::Camera camera_;
    core::Vec3 head_world_ = core::Vec3::zero();
    float head_up_ = 0.0f;
    float head_back_ = 0.0f;
    bool head_known_ = false;
    core::Vec3 position_ = core::Vec3::zero();
    core::Vec3 aim_ = core::Vec3::zero();
    core::Vec3 pivot_ = core::Vec3::zero();
    float fov_ = 62.0f;
    float arm_ = 22.0f;
    float requested_arm_ = 22.0f;
    core::Vec2 free_look_ = core::Vec2{0.0f, 0.0f};  // degrees, yaw then pitch
    float shake_time_ = 0.0f;
    bool initialized_ = false;
};

}  // namespace game
