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
    melee_timer_ = 0.0f;
    jink_phase_ = random_unit() * core::PI;
    breath_budget_ = 1.0f;
    breathing_ = false;
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
    melee_timer_ = core::maxf(melee_timer_ - dt, 0.0f);
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
        case BotState::Attack: {
            // The attack clock only runs inside gun range, where a stalemated
            // turning fight is possible. Outside it the bot is approaching, not
            // attacking, and timing out of an approach just oscillates: nine
            // seconds of closing, seven seconds of extending away, no progress.
            if (range > tuning.fire_range * 0.8f) state_time_ = 0.0f;
            // The break-off range shrinks when the bot is lined up on the
            // player: an aligned pass presses through the breath envelope to
            // a bite before it extends. Off-axis, the old range holds -- that
            // is an overshoot about to happen, and pressing it is a collision
            // course, not an attack.
            const Vec3 to_target = core::normalize_or(believed - self.position, self.forward());
            const bool lined_up = core::dot(to_target, self.forward()) > std::cos(core::radians(25.0f));
            const float break_off = lined_up ? core::minf(tuning.min_attack_range, tuning.melee_range * 0.7f)
                                             : tuning.min_attack_range;
            if (!player_alive || range < break_off ||
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
        }
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

    // Ground recovery reflex: triggered by the physics of the pull-out, not by
    // a fixed height or time. Arresting `sink` of vertical speed at roughly
    // 12 m/s^2 of usable pull consumes sink^2/24 metres -- 66 m from a 40 m/s
    // dive -- so the reflex must fire while that much altitude still exists,
    // plus a margin for the PD loop to actually get the nose up.
    const float clearance = ground_height > -1e8f ? self.position.y - ground_height : 1e9f;
    const float sink = core::maxf(-self.climb_rate, 0.0f);
    const float pull_out_altitude = sink * sink / 24.0f + 45.0f;
    const bool recovering =
        clearance < tuning.terrain_floor * 0.55f || clearance < pull_out_altitude;
    if (recovering) {
        const Vec3 level_forward = core::normalize_or(
            Vec3{self.forward().x, 0.0f, self.forward().z}, Vec3::forward());
        aim_point = self.position + level_forward * 120.0f + Vec3{0.0f, 220.0f, 0.0f};
    }
    decision.flight = steer_toward(self, aim_point, tuning.steering, ground_height);
    if (recovering) {
        decision.flight.flap = 1.0f;
        // Braking in a dive adds drag AND lift: it tightens the pull-out the
        // way flaring for a landing does.
        decision.flight.brake = core::saturate(sink / 30.0f);
        decision.flight.tuck = 0.0f;
    }

    // ---- breath ----
    // Close and aligned: hold the flame, on a budget -- in ANY state, because a
    // bot extending past the player rakes them on the way through, and gating
    // this on the attack state left a 20 m window nobody ever saw a flame in.
    // The check uses the LIVE player position, not the stale sample: a flame is
    // continuous and visibly connects or does not, so pretending not to see
    // would read as blindness rather than fairness. Fairness lives in the aim.
    const float live_range = core::distance(self.position, player.position);
    const Vec3 to_player = core::normalize_or(player.position - self.position, self.forward());
    const bool aligned =
        core::dot(to_player, self.forward()) >= std::cos(core::radians(tuning.breath_cone_deg));
    const bool wants_flame =
        player_alive && !recovering && live_range <= tuning.breath_range && aligned;
    // Latched like the player's meter: an empty budget must refill a third of
    // the way before the flame restarts, or it stutters at zero.
    if (wants_flame && (breathing_ ? breath_budget_ > 0.0f : breath_budget_ > 0.35f)) {
        breathing_ = true;
        decision.breathe = true;
        breath_budget_ =
            core::maxf(breath_budget_ - dt / core::maxf(tuning.breath_burst, 0.1f), 0.0f);
    } else {
        breathing_ = false;
        breath_budget_ = core::minf(
            breath_budget_ + dt / core::maxf(tuning.breath_recovery, 0.1f), 1.0f);
    }

    // ---- melee ----
    // In ANY state, like the flame: a bot extending past the player at
    // fifteen metres claws them on the way through. A bite ahead inside the
    // cone, or a strike at anything alongside; one swing, then the cooldown,
    // stretched by the pilot's tempo like its other rhythms.
    if (player_alive && !recovering && melee_timer_ <= 0.0f) {
        const bool in_bite_cone =
            live_range <= tuning.melee_range &&
            core::dot(to_player, self.forward()) >= std::cos(core::radians(tuning.melee_cone_deg));
        const bool in_strike_reach = live_range <= tuning.strike_range;
        if (in_bite_cone || in_strike_reach) {
            melee_timer_ = tuning.melee_cooldown * tempo_;
            decision.melee = true;
        }
    }

    // ---- gunnery ----
    if (state_ == BotState::Attack && player_alive && fire_timer_ <= 0.0f && !recovering &&
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
