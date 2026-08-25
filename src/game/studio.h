#pragma once

#include "game/flight.h"

namespace game {

// The animation studio: scripted flight states for looking at the rig.
//
// Judging an animation from live flight means chasing a manoeuvre with the
// camera while also flying it, and no two takes match. The studio pins the
// dragon at one spot and plays a repeatable, dynamically consistent manoeuvre:
// velocity, angular velocity and g-load all agree with the scripted motion, so
// every physically based response -- chains, pendulum legs, load flex -- reacts
// exactly as it would in flight, but under a camera that can sit still and look.
enum class StudioScenario : int {
    Glide = 0,
    Flap,
    TurnLeft,
    TurnRight,
    STurns,
    Dive,
    PullOut,
    Brake,
    Attack,
    Grounded,
    Count,
};

const char* studio_scenario_name(StudioScenario scenario);
// What to look for while watching -- shown in the panel so an inspection is a
// checklist rather than a vibe.
const char* studio_scenario_notes(StudioScenario scenario);

// The scripted state at time `t`. The dragon stays at `centre` (position is
// pinned; nothing downstream of the rig reads position kinematics), with
// `ground_y` used by the grounded scenario.
FlightState studio_state(StudioScenario scenario, float t, core::Vec3 centre, float ground_y);

// Where the attack scenario's target orbits, for the head aim.
core::Vec3 studio_attack_target(float t, core::Vec3 centre);

}  // namespace game
