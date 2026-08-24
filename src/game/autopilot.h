#pragma once

#include "game/flight.h"

namespace game {

// Steers a dragon toward a point in space.
//
// Two jobs. It verifies the rally end to end without a human at the controls,
// and it is the seed of the bot AI: bots will command the same FlightInput the
// player does, through the same flight model, so they are physically honest
// rather than cheating their way around a course.
struct AutopilotTuning {
    // Bank hard for a large bearing error. Radians of error to full deflection.
    float roll_gain = 1.5f;
    float pitch_gain = 2.2f;
    // Derivative damping, against the dragon's own angular velocity. Without
    // these the loop is pure proportional and hunts: it overshoots the target,
    // reverses at full deflection, overshoots again, and never settles.
    float roll_damping = 0.55f;
    float pitch_damping = 0.75f;
    // Speed it tries to hold. Below this it flaps; well above it stops.
    float cruise_speed = 55.0f;
    float max_speed = 95.0f;
    // Tuck into a dive when the target is this far below the nose.
    float dive_threshold = 0.30f;
    // How close counts as arrived, in metres.
    float arrive_radius = 25.0f;
    // When a target has a required approach direction, aim at a point back along
    // that axis instead of straight at the target, so the dragon arrives lined
    // up rather than cutting across the plane at a shallow angle. Scales with
    // range and is capped.
    float axis_lead_fraction = 0.55f;
    float axis_lead_max = 240.0f;
    // Terrain avoidance. Below `min_clearance` the aim point is lifted by the
    // shortfall times `avoid_lift`, rather than the controls being overridden.
    // An override fights the PD loop and switches modes, which oscillates; a
    // lifted target keeps one coherent controller.
    float min_clearance = 90.0f;
    float avoid_lift = 3.0f;
};

// `ground_height` is the terrain height directly below the dragon, or a very
// negative number to disable terrain avoidance.
FlightInput steer_toward(const FlightState& state, core::Vec3 target,
                         const AutopilotTuning& tuning, float ground_height = -1e9f);

// As above, but `approach_direction` is the direction the dragon should be
// travelling when it arrives -- a checkpoint's normal. Pass a zero vector for no
// constraint.
FlightInput steer_through(const FlightState& state, core::Vec3 target,
                          core::Vec3 approach_direction, const AutopilotTuning& tuning,
                          float ground_height = -1e9f);

}  // namespace game
