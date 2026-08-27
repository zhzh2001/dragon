#include "game/bot.h"

#include <cmath>

#include "game/combat.h"

using core::Vec3;

namespace game {

void BotPilot::reset(uint32_t seed) {
    rng_ = seed ? seed : 1u;
    state_ = BotState::Attack;
    state_time_ = 0.0f;
    fire_timer_ = 0.0f;
    jink_phase_ = random_unit() * core::PI;
    snapshot_age_ = 1e9f;
    seen_before_ = false;
    seen_acceleration_ = Vec3::zero();
    // Personality: each pilot runs its rhythm a little fast or slow, so a
    // flight of them breaks formation naturally.
    tempo_ = 1.0f + 0.25f * random_unit();
}

float BotPilot::random_unit() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return float(rng_ & 0xffffffu) / float(0xffffff) * 2.0f - 1.0f;
}

const char* BotPilot::state_name() const {
    switch (state_) {
        case BotState::Attack: return "attack";
        case BotState::Extend: return "extend";
        case BotState::Evade: return "evade";
    }
    return "?";
}

// Quadratic extrapolation of the sampled player: position, velocity and the
// acceleration measured between samples. It leads arcs and brakes; what it
// cannot do -- by design -- is see a manoeuvre CHANGE inside the reaction
// window.
Vec3 BotPilot::predict(float ahead) const {
    return seen_position_ + seen_velocity_ * ahead +
           seen_acceleration_ * (0.5f * ahead * ahead);
}

void BotPilot::notify_hit() {
    // Getting hit interrupts anything. Re-notification refreshes the jink
    // rather than stacking states.
    if (state_ != BotState::Evade) {
        state_ = BotState::Evade;
        state_time_ = 0.0f;
        jink_phase_ = random_unit() * core::PI;
    }
}

BotDecision BotPilot::update(float dt, const FlightState& self, const FlightState& player,
                             bool player_alive, float ground_height) {
    BotDecision decision;
    state_time_ += dt;
    fire_timer_ = core::maxf(fire_timer_ - dt, 0.0f);
    jink_phase_ += tuning.jink_rate * dt;

    // ---- perception ----
    // The player is sampled, not streamed: between snapshots the bot flies and
    // aims at an extrapolation, so a break inside the reaction window works.
    snapshot_age_ += dt;
    if (snapshot_age_ >= tuning.reaction_interval) {
        // Acceleration measured between the last two samples: a steady turn or
        // a brake shows up here and gets led. Capped, because a sample straddling
        // a respawn would otherwise predict teleportation -- and the FIRST
        // sample has no history, so it measures nothing rather than measuring
        // against zero.
        if (seen_before_) {
            Vec3 acceleration = (player.velocity - seen_velocity_) /
                                core::maxf(snapshot_age_, 1e-3f);
            const float magnitude = core::length(acceleration);
            if (magnitude > 45.0f) acceleration *= 45.0f / magnitude;
            seen_acceleration_ = acceleration * core::saturate(tuning.lead_curvature);
        }
        seen_before_ = true;
        snapshot_age_ = 0.0f;
        seen_position_ = player.position;
        seen_velocity_ = player.velocity;
    }
    const Vec3 believed = predict(snapshot_age_);

    const float range = core::distance(self.position, believed);

    // ---- state transitions ----
    switch (state_) {
        case BotState::Attack:
            // The attack clock only runs inside gun range, where a stalemated
            // turning fight is possible. Outside it the bot is approaching, not
            // attacking, and timing out of an approach just oscillates: nine
            // seconds of closing, seven seconds of extending away, no progress.
            if (range > tuning.fire_range * 0.8f) state_time_ = 0.0f;
            if (!player_alive || range < tuning.min_attack_range ||
                state_time_ > tuning.attack_duration * tempo_) {
                state_ = BotState::Extend;
                state_time_ = 0.0f;
                // Out past the player and offset to a random side, climbing a
                // little: the classic extension, leaving with energy.
                const Vec3 away = core::normalize_or(self.position - believed,
                                                     self.forward());
                const Vec3 side = core::normalize_or(core::cross(away, Vec3::up()),
                                                     Vec3::right());
                extend_point_ = self.position + away * tuning.extend_distance +
                                side * (tuning.extend_distance * 0.45f * random_unit()) +
                                Vec3{0.0f, 60.0f, 0.0f};
            }
            break;
        case BotState::Extend:
            if (player_alive &&
                (core::distance(self.position, extend_point_) < tuning.steering.arrive_radius *
                                                                    3.0f ||
                 state_time_ > tuning.extend_duration * tempo_)) {
                state_ = BotState::Attack;
                state_time_ = 0.0f;
            }
            break;
        case BotState::Evade:
            if (state_time_ > tuning.evade_duration) {
                state_ = BotState::Extend;
                state_time_ = 0.0f;
                const Vec3 away = core::normalize_or(self.position - believed,
                                                     self.forward());
                extend_point_ = self.position + away * tuning.extend_distance;
            }
            break;
    }

    // ---- steering ----
    Vec3 aim_point = believed;
    switch (state_) {
        case BotState::Attack: {
            // Fly at the firing solution, not at the target: the nose ends up
            // where the shot needs it. Solved on the predicted arc, in the
            // round's own frame -- a fired round inherits the bot's velocity, so
            // the solution must subtract the bot's drift over the flight time or
            // every crossing shot lands one drift-length behind the target.
            float flight_time = range / core::maxf(tuning.projectile_speed, 1.0f);
            for (int i = 0; i < 3; ++i) {
                flight_time = core::length(predict(snapshot_age_ + flight_time) -
                                           self.position - self.velocity * flight_time) /
                              core::maxf(tuning.projectile_speed, 1.0f);
            }
            aim_point = predict(snapshot_age_ + flight_time);
            break;
        }
        case BotState::Extend:
            aim_point = extend_point_;
            break;
        case BotState::Evade: {
            // A weave around the escape direction. The jink is in the aim
            // point, so the PD loop stays one coherent controller.
            const Vec3 away = core::normalize_or(self.position - believed, self.forward());
            const Vec3 side = core::normalize_or(core::cross(away, Vec3::up()), Vec3::right());
            aim_point = self.position + away * 260.0f +
                        side * 150.0f * std::sin(jink_phase_) +
                        Vec3{0.0f, 90.0f * std::sin(jink_phase_ * 0.7f), 0.0f};
            break;
        }
    }
    // The floor: whatever the state wants, the aim point never goes below safe
    // height over the terrain. The steering's own avoidance handles the
    // approach; this stops the doctrine from ordering a descent into the dirt
    // in the first place.
    if (ground_height > -1e8f) {
        aim_point.y = core::maxf(aim_point.y, ground_height + tuning.terrain_floor);
    }
    decision.flight = steer_toward(self, aim_point, tuning.steering, ground_height);

    // ---- gunnery ----
    if (state_ == BotState::Attack && player_alive && fire_timer_ <= 0.0f &&
        range <= tuning.fire_range) {
        // Same arc-aware, drift-compensated solution the steering flies toward.
        float flight_time = range / core::maxf(tuning.projectile_speed, 1.0f);
        for (int i = 0; i < 3; ++i) {
            flight_time = core::length(predict(snapshot_age_ + flight_time) - self.position -
                                       self.velocity * flight_time) /
                          core::maxf(tuning.projectile_speed, 1.0f);
        }
        const Vec3 to_solution =
            core::normalize_or(predict(snapshot_age_ + flight_time) - self.position -
                                   self.velocity * flight_time,
                               self.forward());
        const float aligned = core::dot(to_solution, self.forward());
        if (aligned >= std::cos(core::radians(tuning.fire_cone_deg))) {
            fire_timer_ = tuning.fire_cooldown;
            decision.fire = true;
            // Spread as a random tilt of the firing direction: honest error in
            // the solution, not damage dice.
            const float spread = core::radians(tuning.aim_spread_deg);
            Vec3 direction = to_solution;
            direction += Vec3{random_unit(), random_unit(), random_unit()} * spread;
            decision.fire_velocity =
                self.velocity + core::normalize_or(direction, to_solution) *
                                    tuning.projectile_speed;
        }
    }

    return decision;
}

}  // namespace game
