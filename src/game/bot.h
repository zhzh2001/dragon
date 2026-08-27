#pragma once

#include "game/autopilot.h"
#include "game/flight.h"

namespace game {

// Bot skill dials. Difficulty is honest imperfection -- stale information and
// scattered aim -- never a different flight model: a bot flies exactly the
// dragon the player flies, through the same FlightInput.
struct BotTuning {
    AutopilotTuning steering;
    BotTuning() {
        // Bots dive at targets on purpose, which the rally autopilot never did,
        // so they need more avoidance authority: a higher floor and a harder
        // pull when they cross it.
        steering.min_clearance = 130.0f;
        steering.avoid_lift = 4.5f;
    }

    // ---- perception ----
    // The bot samples the player this often and extrapolates in between. This
    // is THE fairness dial: a direction change inside the reaction window
    // genuinely defeats its aim, the way it defeats a person's.
    float reaction_interval = 0.30f;

    // ---- gunnery ----
    float fire_range = 560.0f;
    // The nose must actually point at the firing solution: bots aim by flying,
    // not by turrets.
    float fire_cone_deg = 8.0f;
    float fire_cooldown = 1.7f;
    // Aim error, degrees of cone around the true solution.
    float aim_spread_deg = 3.0f;
    float projectile_speed = 230.0f;
    float damage = 11.0f;

    // ---- the fight's rhythm ----
    // Attack runs end: too close (an overshoot about to happen) or too long
    // (a tail chase that is not converging).
    float min_attack_range = 120.0f;
    float attack_duration = 9.0f;
    // The extend leg: fly out, turn around, come back with energy. This is what
    // makes a fight read as passes rather than as two dragons orbiting a point.
    float extend_distance = 520.0f;
    float extend_duration = 7.0f;
    // Getting hit triggers a jink.
    float evade_duration = 2.2f;
    float jink_rate = 2.6f;  // rad/s of the weave

    // Never chase anything below this height over the terrain. The player may
    // fly into the weeds; following them there is how bots die of enthusiasm.
    float terrain_floor = 90.0f;
};

enum class BotState : int { Attack, Extend, Evade };

// One decision per frame: how to fly, and whether to shoot.
struct BotDecision {
    FlightInput flight;
    bool fire = false;
    // World-space velocity for the projectile if fire is set.
    core::Vec3 fire_velocity = core::Vec3::zero();
};

// The pilot. Owns only its own perception and rhythm state; the aircraft is the
// caller's FlightModel and the weapons are the caller's Combat.
class BotPilot {
public:
    BotTuning tuning;

    // `seed` desynchronises a flight of bots: identical pilots entering the
    // same states in lockstep read as a formation of one mind.
    void reset(uint32_t seed);

    // `ground_height` should be the highest terrain the caller can see along
    // the flight path -- below the bot AND ahead of it -- not just directly
    // underneath: avoidance that only looks down flies into rising slopes.
    BotDecision update(float dt, const FlightState& self, const FlightState& player,
                       bool player_alive, float ground_height);

    // The caller saw this bot take damage; the pilot reacts.
    void notify_hit();

    BotState state() const { return state_; }
    const char* state_name() const;

private:
    float random_unit();  // [-1, 1]

    BotState state_ = BotState::Attack;
    float state_time_ = 0.0f;
    float fire_timer_ = 0.0f;
    float jink_phase_ = 0.0f;

    // Stale-by-design perception.
    float snapshot_age_ = 1e9f;
    core::Vec3 seen_position_ = core::Vec3::zero();
    core::Vec3 seen_velocity_ = core::Vec3::zero();

    core::Vec3 extend_point_ = core::Vec3::zero();
    // Per-pilot personality scale on the rhythm timers.
    float tempo_ = 1.0f;
    uint32_t rng_ = 1u;
};

}  // namespace game
