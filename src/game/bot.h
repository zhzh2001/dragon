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
    // How much of the player's measured acceleration goes into the prediction.
    // At 0 the bot leads straight lines only and a steady turn defeats it
    // forever -- but a STEADY turn is the most predictable manoeuvre there is,
    // and a pilot who cannot lead one is not a pilot. 1 leads the full arc;
    // what still works on the bot is CHANGING the manoeuvre inside its
    // reaction window, which is the honest counter.
    float lead_curvature = 1.0f;

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
    // Low enough that an attack presses THROUGH the breath envelope instead of
    // breaking off at its outer edge -- 120 left a flame window nobody hit.
    float min_attack_range = 80.0f;
    float attack_duration = 9.0f;
    // The extend leg: fly out, turn around, come back with energy. This is what
    // makes a fight read as passes rather than as two dragons orbiting a point.
    float extend_distance = 520.0f;
    float extend_duration = 7.0f;
    // Getting hit triggers a jink.
    float evade_duration = 2.2f;
    float jink_rate = 2.6f;  // rad/s of the weave

    // ---- breath ----
    // Bots breathe when close and aligned, on a budget: a burst, then a
    // recovery, like the player's meter without the micromanagement.
    float breath_range = 180.0f;
    float breath_cone_deg = 20.0f;
    float breath_burst = 2.2f;    // seconds of flame per burst
    float breath_recovery = 4.0f; // seconds to recharge after a burst

    // ---- melee ----
    // Bots bite too, or the rival-dragon fight is two flame-throwers jousting
    // while the player gets bitten by nobody. Same reach the player has, read
    // off the live position like the flame: a bite is a contact, and
    // pretending not to see a dragon twenty metres away reads as blindness.
    float melee_range = 30.0f;
    float melee_cone_deg = 60.0f;  // half angle
    float strike_range = 16.0f;
    float melee_cooldown = 0.9f;
    // The charge: inside this range and lined up on the player, an attack
    // does not time out, the bot flies at the player's BODY rather than at
    // the firing solution ahead of it, holds flap, and spends a boost to
    // close. Without it a bot closing at three metres a second ran out its
    // attack clock at 300 m and went to extend, so no bot was ever within
    // bite range of a fleeing player; with flap alone one bite landed in a
    // two-minute match, and the playtest still saw a bite once in three.
    float charge_range = 400.0f;
    float charge_boost_duration = 1.1f;
    float charge_boost_cooldown = 6.0f;

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
    // Holding the flame this frame.
    bool breathe = false;
    // Biting or striking this frame (an edge).
    bool melee = false;
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
    // For probes: what the pilot currently believes about the target's motion.
    core::Vec3 seen_acceleration() const { return seen_acceleration_; }
    const char* state_name() const;
    // For probes and the panel: pressing a lined-up attack to bite range.
    bool charging() const { return charging_; }

private:
    float random_unit();  // [-1, 1]

    BotState state_ = BotState::Attack;
    float state_time_ = 0.0f;
    float fire_timer_ = 0.0f;
    float melee_timer_ = 0.0f;
    bool charging_ = false;
    float boost_hold_ = 0.0f;   // seconds of charge boost left
    float boost_timer_ = 0.0f;  // cooldown until the next
    float jink_phase_ = 0.0f;

    // Stale-by-design perception, including the measured acceleration between
    // the last two samples.
    float snapshot_age_ = 1e9f;
    bool seen_before_ = false;
    core::Vec3 seen_position_ = core::Vec3::zero();
    core::Vec3 seen_velocity_ = core::Vec3::zero();
    core::Vec3 seen_acceleration_ = core::Vec3::zero();
    core::Vec3 predict(float ahead) const;

    core::Vec3 extend_point_ = core::Vec3::zero();
    float breath_budget_ = 1.0f;  // 0..1 of a burst
    bool breathing_ = false;
    // Per-pilot personality scale on the rhythm timers.
    float tempo_ = 1.0f;
    uint32_t rng_ = 1u;
};

}  // namespace game
