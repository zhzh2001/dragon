#pragma once

#include "core/math.h"
#include "game/flight.h"

namespace game {

// Scripted aerobatics: a quick aileron roll that doubles as a dodge, and a
// flip -- a half loop and a roll-out that reverses the heading, the way a
// chased dragon turns to face its chaser. One button each. Both are written
// as CONTROL INPUTS fed to the same flight model as the stick, with the
// assists that would fight them (auto-level, the bank limit) stood down for
// the duration and the control rates scaled up: a manoeuvre is the dragon
// throwing its weight around, not a canned animation, so it still costs
// energy, still stalls if entered too slow, and the bots use exactly the same
// code.
enum class ManeuverKind : int { None = 0, Roll, Flip };

struct ManeuverTuning {
    float roll_duration = 0.85f;    // seconds for one full roll
    float roll_agility = 2.4f;      // roll-rate multiplier while rolling
    // The dodge: a sideways kick at the start, then a sustained push for the
    // first part of the roll. The first cut was a 7 m/s kick alone, and the
    // playtest said it did not really dodge -- at 45 m/s forward that is a
    // metre or two of displacement. This is more like ten.
    float roll_dodge_impulse = 14.0f;  // m/s at the start
    float roll_dodge_push = 40.0f;     // m/s^2 through the first `roll_dodge_fraction` of the roll
    float roll_dodge_fraction = 0.45f;
    float flip_agility = 1.9f;      // pitch-rate multiplier through the half loop
    float flip_max_duration = 3.2f; // give up and level out after this
    float flip_min_airspeed = 26.0f;  // below this a flip is a stall, so it is refused
    float cooldown = 1.0f;          // between manoeuvres
};

struct Maneuver {
    ManeuverKind kind = ManeuverKind::None;
    float time = 0.0f;
    float direction = 1.0f;  // roll: +1 right, -1 left
    int phase = 0;           // flip: 0 half loop, 1 roll-out
    float cooldown = 0.0f;
    core::Vec3 start_forward = core::Vec3::forward();

    bool active() const { return kind != ManeuverKind::None; }
    // Begins a manoeuvre if none is running and the cooldown has lapsed.
    // Applies the roll's dodge kick to the state's velocity. Returns whether
    // it started.
    bool start(ManeuverKind what, float roll_direction, FlightState& state,
               const ManeuverTuning& tuning);
    // Overrides the pitch and roll of `in` while a manoeuvre runs, marks the
    // input as a manoeuvre for the flight model, advances the clock, and
    // pushes the roll's dodge into the state's velocity.
    void apply(FlightInput& in, FlightState& state, const ManeuverTuning& tuning, float dt);
};

}  // namespace game
