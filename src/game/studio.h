#pragma once

#include "anim/dragon_rig.h"
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
    Melee,  // appended, so the numbers in the docs and scripts stay
    Claw,
    Tail,
    Walk,  // on the ground at a walking pace: the stride, on a treadmill
    Count,
};

const char* studio_scenario_name(StudioScenario scenario);
// What to look for while watching -- shown in the panel so an inspection is a
// checklist rather than a vibe.
const char* studio_scenario_notes(StudioScenario scenario);

// The scripted state at time `t`. The dragon stays at `centre` (position is
// pinned; nothing downstream of the rig reads position kinematics), with
// `ground_y` used by the grounded scenario. `ground_offset` is the body-centre
// height above that surface; the default preserves the original studio pose for
// callers that do not have per-model ground tuning.
FlightState studio_state(StudioScenario scenario, float t, core::Vec3 centre, float ground_y,
                         float ground_offset = 2.5f);

// Where the attack scenario's target orbits, for the head aim.
core::Vec3 studio_attack_target(float t, core::Vec3 centre);
// The mark the melee scenario snaps at: close ahead, weaving across the nose.
core::Vec3 studio_melee_target(float t, core::Vec3 centre);

// What the dragon does with its weapons over the interval (previous, t]: the
// attack scenario holds its breath and spits on a schedule, so the jaw, the
// recoil and the talons can be watched on loop. The fire flag is an edge and
// needs the previous time to be detected.
anim::RigAction studio_action(StudioScenario scenario, float previous, float t);

}  // namespace game
