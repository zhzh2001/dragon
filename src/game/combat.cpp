#include "game/combat.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "game/terrain.h"

using core::Vec3;

namespace game {

namespace {

// Where the fire comes from: forward of the body origin and slightly above it,
// roughly the dragon's mouth. Kept in one place so the flame drawn, the cone
// that damages, and the fireball spawn all agree.
constexpr float MUZZLE_FORWARD = 7.5f;
constexpr float MUZZLE_UP = 1.6f;

Vec3 muzzle_of(const FlightState& player) {
    return player.position + player.forward() * MUZZLE_FORWARD + player.up() * MUZZLE_UP;
}

// Where a sentinel sits at its current orbit phase. Used both while flying the
// orbit and at spawn: a sentinel whose position was never initialised sits at
// the world origin, which is a live, shootable target in the middle of the map.
Vec3 orbit_position(const Sentinel& sentinel) {
    return sentinel.centre + Vec3{std::cos(sentinel.phase) * sentinel.orbit_radius,
                                  std::sin(sentinel.phase * 1.7f) * sentinel.bob,
                                  std::sin(sentinel.phase) * sentinel.orbit_radius};
}

}  // namespace

bool point_in_cone(Vec3 point, Vec3 tip, Vec3 axis, float half_angle_radians, float length) {
    const Vec3 offset = point - tip;
    const float along = core::dot(offset, axis);
    if (along <= 0.0f || along > length) return false;
    // Comparing against the radius at this distance rather than an angle keeps
    // it to one multiply and avoids an acos.
    const float radius = along * std::tan(half_angle_radians);
    const Vec3 perpendicular = offset - axis * along;
    return core::length_sq(perpendicular) <= radius * radius;
}

float closest_point_fraction(Vec3 from, Vec3 to, Vec3 point) {
    const Vec3 segment = to - from;
    const float squared = core::length_sq(segment);
    if (squared <= 1e-6f) return 0.0f;
    return core::clampf(core::dot(point - from, segment) / squared, 0.0f, 1.0f);
}

Vec3 intercept_point(Vec3 origin, Vec3 target, Vec3 target_velocity, float speed,
                     int iterations) {
    if (speed <= 1e-3f) return target;
    float flight_time = core::length(target - origin) / speed;
    for (int i = 0; i < iterations; ++i) {
        const Vec3 predicted = target + target_velocity * flight_time;
        flight_time = core::length(predicted - origin) / speed;
    }
    return target + target_velocity * flight_time;
}

float Combat::random_unit() {
    // xorshift32: the sentinels only need spread that does not repeat visibly,
    // and a deterministic sequence makes a headless run reproducible.
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return float(rng_ & 0xffffffu) / float(0xffffff) * 2.0f - 1.0f;
}

Vec3 Combat::muzzle(const FlightState& player) const {
    return has_muzzle_override_ ? muzzle_override_ : muzzle_of(player);
}

Vec3 Combat::lock_position() const {
    if (locked_ < 0 || size_t(locked_) >= sentinels_.size()) return Vec3::zero();
    return sentinels_[size_t(locked_)].position;
}

// Blends the nose toward a point. Rotating between the two directions rather
// than lerping the vectors keeps the result a rotation of fixed magnitude, so
// the assist feels the same at every range instead of stronger up close.
Vec3 Combat::assisted_direction(const FlightState& player, Vec3 target,
                                float max_turn_deg) const {
    const Vec3 nose = player.forward();
    if (locked_ < 0) return nose;
    const Vec3 toward = target - muzzle(player);
    if (core::length_sq(toward) < 1e-6f) return nose;

    const core::Quat full = core::rotation_between(nose, core::normalize(toward));
    float fraction = core::saturate(tuning.aim_assist);
    // Cap by angle as well as by strength, so a target far off the nose is
    // helped by a plausible amount rather than by whatever it takes.
    const float angle = 2.0f * std::acos(core::clampf(std::fabs(full.w), -1.0f, 1.0f));
    const float limit = core::radians(core::maxf(max_turn_deg, 0.0f));
    if (angle * fraction > limit && angle > 1e-4f) fraction = limit / angle;

    return core::normalize_or(
        core::rotate(core::slerp(core::Quat::identity(), full, fraction), nose), nose);
}

Vec3 Combat::fireball_direction(const FlightState& player) const {
    // A fireball's turn is invisible -- it leaves and is gone -- so the only
    // limit that matters is the lock cone that let the target be picked at all.
    return assisted_direction(player, lock_intercept_, tuning.lock_hold_cone_deg);
}

Vec3 Combat::breath_direction_for(const FlightState& player) const {
    // Breath aims at where the target *is*, not where it will be: the stream is
    // continuous, so there is nothing to lead.
    return assisted_direction(player, lock_position(), tuning.breath_assist_max_deg);
}

// Picks and holds a target. Sticky by design: acquired only inside a narrow cone
// off the nose, but kept until it falls well outside a much wider one, so a
// target does not blink out the moment a turn swings the nose past it.
void Combat::update_lock(const FlightState& player) {
    const Vec3 origin = muzzle(player);

    // Lead the target, then raise the aim by however far the shot will fall on
    // the way. Without this the assist is still wrong at range for a reason the
    // player cannot see: at 700 m the flight time is 2.7 s and the drop is 15 m,
    // which is larger than the target, so every long shot passes underneath.
    // The fireball inherits the player's velocity, so the intercept is solved
    // for the drift-compensated round -- without this every crossing shot lands
    // a drift-length behind, and the assist looks like it is not helping.
    auto solve_aim = [&](const Sentinel& target) {
        float flight_time =
            core::distance(origin, target.position) / core::maxf(tuning.fireball_speed, 1.0f);
        Vec3 aim = target.position;
        for (int i = 0; i < 3; ++i) {
            aim = target.position + target.velocity * flight_time;
            aim.y += 0.5f * tuning.fireball_gravity * flight_time * flight_time;
            flight_time = core::length(aim - origin - player.velocity * flight_time) /
                          core::maxf(tuning.fireball_speed, 1.0f);
        }
        // The point handed to the assist is offset so that aiming the nose at it
        // makes the INHERITED round arrive: the drift is baked into the target.
        return aim - player.velocity * flight_time;
    };
    const Vec3 nose = player.forward();
    const float hold = std::cos(core::radians(core::clampf(tuning.lock_hold_cone_deg, 1.0f, 179.0f)));

    auto angle_to = [&](const Sentinel& sentinel, float& out_range) {
        const Vec3 offset = sentinel.position - origin;
        out_range = core::length(offset);
        if (out_range < 1e-3f) return 0.0f;
        return core::degrees(std::acos(core::clampf(core::dot(offset / out_range, nose),
                                                    -1.0f, 1.0f)));
    };
    // Lower is better: degrees off the nose plus a distance penalty, so a close
    // target slightly off the nose beats a speck on the horizon dead ahead.
    auto score_of = [&](const Sentinel& sentinel) {
        float range = 0.0f;
        const float angle = angle_to(sentinel, range);
        if (angle > tuning.lock_cone_deg || range > tuning.lock_range) return 1e9f;
        return angle + range * tuning.lock_distance_weight;
    };

    // Manual relock: jump to the next candidate by score, wrapping, so tapping
    // the button walks every target in the cone.
    if (want_cycle_) {
        want_cycle_ = false;
        std::vector<std::pair<float, int>> candidates;
        for (size_t i = 0; i < sentinels_.size(); ++i) {
            if (!sentinels_[i].alive) continue;
            const float score = score_of(sentinels_[i]);
            if (score < 1e8f) candidates.emplace_back(score, int(i));
        }
        if (!candidates.empty()) {
            std::sort(candidates.begin(), candidates.end());
            int position = -1;
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (candidates[i].second == locked_) position = int(i);
            }
            locked_ = candidates[size_t((position + 1)) % candidates.size()].second;
            lock_intercept_ = solve_aim(sentinels_[size_t(locked_)]);
            return;
        }
    }

    // Keep the current lock if it is still worth keeping.
    if (locked_ >= 0 && size_t(locked_) < sentinels_.size()) {
        const Sentinel& current = sentinels_[size_t(locked_)];
        float range = 0.0f;
        const float angle = angle_to(current, range);
        if (current.alive && std::cos(core::radians(angle)) >= hold &&
            range <= tuning.lock_range * 1.25f) {
            lock_intercept_ = solve_aim(current);
            return;
        }
        locked_ = -1;
    }

    // Otherwise the best-scoring candidate in the acquisition cone.
    float best = 1e8f;
    int best_index = -1;
    for (size_t i = 0; i < sentinels_.size(); ++i) {
        if (!sentinels_[i].alive) continue;
        const float score = score_of(sentinels_[i]);
        if (score < best) {
            best = score;
            best_index = int(i);
        }
    }
    locked_ = best_index;
    if (locked_ >= 0) {
        lock_intercept_ = solve_aim(sentinels_[size_t(locked_)]);
    } else {
        lock_intercept_ = origin + nose * 400.0f;
    }
}

void Combat::reset(const Terrain* terrain, Vec3 arena_centre, uint32_t seed) {
    terrain_ = terrain;
    arena_centre_ = arena_centre;
    rng_ = seed ? seed : 1u;
    projectiles_.clear();
    sentinels_.clear();
    kills_ = 0;
    locked_ = -1;
    revive();
    spawn_wave(5);
}

void Combat::revive() {
    health_ = tuning.max_health;
    breath_ = 1.0f;
    breathing_ = false;
    time_since_damage_ = 0.0f;
    time_since_breath_ = 0.0f;
    fire_timer_ = 0.0f;
    boost_timer_ = 0.0f;
    boost_cooldown_timer_ = 0.0f;
    // Incoming shots die with the player, so a respawn is never instantly
    // undone by a projectile that was already in the air.
    for (Projectile& projectile : projectiles_) {
        if (projectile.team == Team::Hostile) projectile.alive = false;
    }
}

void Combat::spawn_wave(int count) {
    for (int i = 0; i < count; ++i) {
        Sentinel sentinel;
        const float angle = core::TWO_PI * float(i) / float(count > 0 ? count : 1);
        const float distance = 260.0f + 90.0f * float(i % 3);
        sentinel.centre = arena_centre_ + Vec3{std::cos(angle) * distance, 0.0f,
                                               std::sin(angle) * distance};
        // Well clear of the ground: this milestone is about air-to-air, and a
        // target that clips a hillside just reads as broken.
        const float ground = terrain_ ? terrain_->height_at(sentinel.centre.x, sentinel.centre.z)
                                      : 0.0f;
        sentinel.centre.y = ground + 220.0f + 40.0f * float(i % 4);
        sentinel.orbit_radius = 90.0f + 40.0f * float(i % 3);
        sentinel.orbit_speed = 0.22f + 0.08f * float(i % 4);
        sentinel.phase = angle;
        sentinel.bob = 14.0f + 8.0f * float(i % 3);
        sentinel.health = tuning.sentinel_health;
        sentinel.max_health = tuning.sentinel_health;
        sentinel.alive = true;
        // Staggered, so a fresh wave does not open with a simultaneous volley.
        sentinel.fire_timer = tuning.sentinel_fire_interval * (0.4f + 0.6f * float(i) /
                                                               float(count > 0 ? count : 1));
        sentinel.position = orbit_position(sentinel);
        sentinels_.push_back(sentinel);
    }
}

int Combat::spawn_external(float health, float radius) {
    Sentinel bot;
    bot.external = true;
    bot.radius = radius;
    bot.health = health;
    bot.max_health = health;
    bot.alive = true;
    // The caller drives position before the first update; parked far away so a
    // one-frame-old slot cannot be shot at the origin.
    bot.position = arena_centre_ + Vec3{0.0f, 4000.0f, 0.0f};
    sentinels_.push_back(bot);
    return int(sentinels_.size()) - 1;
}

void Combat::drive_external(int index, Vec3 position, Vec3 velocity) {
    if (index < 0 || size_t(index) >= sentinels_.size()) return;
    sentinels_[size_t(index)].position = position;
    sentinels_[size_t(index)].velocity = velocity;
}

void Combat::fire_hostile(Vec3 position, Vec3 velocity, float damage) {
    fire_projectile(position, velocity, damage, 2.5f, 0.0f, Team::Hostile);
}

void Combat::kill_external(int index) {
    if (index < 0 || size_t(index) >= sentinels_.size()) return;
    Sentinel& bot = sentinels_[size_t(index)];
    if (!bot.alive) return;
    CombatEvents ignored;
    damage_sentinel(bot, bot.health + 1.0f, ignored);
}

void Combat::damage_external(int index, float amount) {
    if (index < 0 || size_t(index) >= sentinels_.size()) return;
    CombatEvents ignored;
    damage_sentinel(sentinels_[size_t(index)], amount, ignored);
}

void Combat::clear_hostiles() {
    sentinels_.clear();
    locked_ = -1;
}

void Combat::hostile_breath(Vec3 origin, Vec3 direction, int source, BreathScales scales) {
    hostile_breaths_pending_.push_back(
        {origin, core::normalize_or(direction, Vec3::forward()), source, scales});
}

int Combat::sentinels_alive() const {
    int count = 0;
    for (const Sentinel& sentinel : sentinels_) {
        if (sentinel.alive) ++count;
    }
    return count;
}

float Combat::fire_cooldown() const {
    if (tuning.fireball_cooldown <= 0.0f) return 0.0f;
    return core::clampf(fire_timer_ / tuning.fireball_cooldown, 0.0f, 1.0f);
}

float Combat::boost_cooldown() const {
    if (tuning.boost_cooldown <= 0.0f) return 0.0f;
    return core::clampf(boost_cooldown_timer_ / tuning.boost_cooldown, 0.0f, 1.0f);
}

void Combat::fire_projectile(Vec3 position, Vec3 velocity, float damage, float radius, float blast,
                             Team team) {
    Projectile projectile;
    projectile.position = position;
    projectile.velocity = velocity;
    projectile.life = tuning.fireball_lifetime;
    projectile.damage = damage;
    projectile.radius = radius;
    projectile.blast_radius = blast;
    projectile.team = team;
    projectile.alive = true;

    // Reuse a dead slot before growing: projectiles are spawned constantly and
    // the vector is handed to the renderer every frame.
    for (Projectile& slot : projectiles_) {
        if (!slot.alive) {
            slot = projectile;
            return;
        }
    }
    projectiles_.push_back(projectile);
}

void Combat::damage_sentinel(Sentinel& sentinel, float amount, CombatEvents& events) {
    if (!sentinel.alive || amount <= 0.0f) return;
    sentinel.health -= amount;
    sentinel.hit_flash = 1.0f;
    sentinel.time_since_damage = 0.0f;
    ++events.hits_dealt;
    events.last_hit = sentinel.position;
    events.had_hit = true;
    if (sentinel.health <= 0.0f) {
        sentinel.alive = false;
        sentinel.health = 0.0f;
        sentinel.respawn_timer = tuning.sentinel_respawn;
        ++kills_;
        ++events.kills;
    }
}

void Combat::update_projectiles(float dt, const FlightState& player, CombatEvents& events) {
    for (Projectile& projectile : projectiles_) {
        if (!projectile.alive) continue;

        projectile.life -= dt;
        if (projectile.life <= 0.0f) {
            projectile.alive = false;
            continue;
        }

        const Vec3 previous = projectile.position;
        projectile.velocity.y -= tuning.fireball_gravity * dt;
        projectile.position += projectile.velocity * dt;

        // Swept against each candidate rather than point-tested: at 210 m/s a
        // fireball covers three metres per frame and would tunnel straight
        // through a target it visibly struck.
        auto sweep_hit = [&](Vec3 target, float target_radius, float& out_distance) {
            const float t = closest_point_fraction(previous, projectile.position, target);
            const Vec3 nearest = previous + (projectile.position - previous) * t;
            out_distance = core::length(nearest - target);
            return out_distance <= target_radius + projectile.radius;
        };

        bool consumed = false;
        if (projectile.team == Team::Player) {
            for (Sentinel& sentinel : sentinels_) {
                if (!sentinel.alive) continue;
                float distance = 0.0f;
                const float body = sentinel.radius > 0.0f ? sentinel.radius
                                                          : tuning.sentinel_radius;
                if (!sweep_hit(sentinel.position, body, distance)) continue;
                damage_sentinel(sentinel, projectile.damage, events);
                consumed = true;
                break;
            }
            if (!consumed) {
                // A near miss still counts, falling off to nothing at the edge
                // of the blast. Without this a 3D dogfight rewards luck.
                for (Sentinel& sentinel : sentinels_) {
                    if (!sentinel.alive) continue;
                    float distance = 0.0f;
                    if (!sweep_hit(sentinel.position, tuning.fireball_blast_radius, distance)) {
                        continue;
                    }
                    const float reach = tuning.fireball_blast_radius + projectile.radius;
                    const float falloff =
                        1.0f - core::clampf((distance - tuning.sentinel_radius) /
                                                core::maxf(reach - tuning.sentinel_radius, 1e-3f),
                                            0.0f, 1.0f);
                    damage_sentinel(sentinel, projectile.damage * falloff * 0.5f, events);
                    consumed = true;
                    break;
                }
            }
        } else if (health_ > 0.0f) {
            float distance = 0.0f;
            // The player's hit sphere is the dragon's body, not its wingspan --
            // being clipped through a wing membrane feels arbitrary.
            if (sweep_hit(player.position, 6.5f, distance)) {
                health_ -= projectile.damage;
                events.damage_taken += projectile.damage;
                // Where the round came from, not where it hit: the HUD has to
                // point the player at the shooter.
                events.damage_from = previous - core::normalize_or(projectile.velocity, Vec3::zero()) * 400.0f;
                events.took_damage = true;
                time_since_damage_ = 0.0f;
                consumed = true;
                if (health_ <= 0.0f) {
                    health_ = 0.0f;
                    events.player_died = true;
                }
            }
        }

        if (consumed) {
            projectile.alive = false;
            impacts_.push_back({projectile.position, projectile.team, false});
            continue;
        }

        if (terrain_ && projectile.position.y <=
                            terrain_->height_at(projectile.position.x, projectile.position.z)) {
            projectile.alive = false;
            impacts_.push_back({projectile.position, projectile.team, true});
        }
    }
}

void Combat::update_sentinels(float dt, const FlightState& player, CombatEvents& events) {
    (void)events;
    for (Sentinel& sentinel : sentinels_) {
        sentinel.hit_flash = core::maxf(sentinel.hit_flash - dt * 4.0f, 0.0f);
        sentinel.time_since_damage += dt;

        // External hostiles regenerate after a lull, exactly like the player:
        // pressing the attack matters, and half-dead bots do not accumulate.
        if (sentinel.external && sentinel.alive &&
            sentinel.time_since_damage >= tuning.hostile_regen_delay) {
            sentinel.health =
                core::minf(sentinel.health + tuning.hostile_regen * dt, sentinel.max_health);
        }

        if (!sentinel.alive) {
            sentinel.respawn_timer -= dt;
            if (sentinel.respawn_timer <= 0.0f) {
                sentinel.alive = true;
                sentinel.health = sentinel.max_health;
                sentinel.fire_timer = tuning.sentinel_fire_interval;
                if (!sentinel.external) {
                    // Back on its orbit, not at the origin: this branch returns
                    // early, so nothing else would place it this frame.
                    sentinel.position = orbit_position(sentinel);
                    sentinel.velocity = Vec3::zero();
                }
                // An external slot is repositioned by its owner, which watches
                // for the alive flag coming back.
            }
            continue;
        }

        // External hostiles fly and fight through their own pilot.
        if (sentinel.external) continue;

        // Fixed orbit, no steering. Motion exists to make the target lead a
        // shot, not to be clever.
        const Vec3 previous = sentinel.position;
        sentinel.phase += sentinel.orbit_speed * dt;
        sentinel.position = orbit_position(sentinel);
        sentinel.velocity = dt > 0.0f ? (sentinel.position - previous) / dt : Vec3::zero();

        if (health_ <= 0.0f) continue;

        sentinel.fire_timer -= dt;
        if (sentinel.fire_timer > 0.0f) continue;
        sentinel.fire_timer = tuning.sentinel_fire_interval;

        const Vec3 to_player = player.position - sentinel.position;
        const float distance = core::length(to_player);
        if (distance > tuning.sentinel_range || distance < 1e-3f) continue;

        // Lead the shot, then spoil it. Perfect prediction is not difficulty,
        // it is a guarantee, and a guaranteed hit removes any reason to
        // manoeuvre.
        const float flight_time = distance / core::maxf(tuning.sentinel_projectile_speed, 1.0f);
        Vec3 aim = player.position + player.velocity * flight_time;
        aim += Vec3{random_unit(), random_unit(), random_unit()} * tuning.sentinel_spread;

        const Vec3 direction = core::normalize(aim - sentinel.position);
        fire_projectile(sentinel.position + direction * (tuning.sentinel_radius + 1.0f),
                        direction * tuning.sentinel_projectile_speed, tuning.sentinel_damage,
                        2.5f, 0.0f, Team::Hostile);
    }
}

void Combat::apply_breath(float dt, const FlightState& player, CombatEvents& events) {
    breath_origin_ = muzzle(player);
    // The cone follows the assist, so the flame the player sees is the flame
    // that does the damage -- there is no hidden bend and no invisible widening.
    breath_direction_ = breath_direction_for(player);
    if (!breathing_) return;

    // Species scales multiply the master dials, so raising breath_range in the
    // panel still moves every species together.
    const float half_angle = core::radians(tuning.breath_half_angle_deg * player_breath.angle);
    const float reach = tuning.breath_range * player_breath.range;
    for (Sentinel& sentinel : sentinels_) {
        if (!sentinel.alive) continue;
        // Tested against the target's centre pushed back toward the tip by its
        // radius, so a target the flame visibly engulfs counts as inside even
        // when its centre is a whisker outside the cone.
        const Vec3 toward = core::normalize(breath_origin_ - sentinel.position);
        const Vec3 near_point = sentinel.position + toward * tuning.sentinel_radius;
        if (!point_in_cone(sentinel.position, breath_origin_, breath_direction_, half_angle,
                           reach) &&
            !point_in_cone(near_point, breath_origin_, breath_direction_, half_angle,
                           reach)) {
            continue;
        }
        damage_sentinel(sentinel, tuning.breath_damage_per_second * player_breath.damage * dt,
                        events);
    }
}

CombatEvents Combat::update(float dt, const FlightState& player, const CombatInput& input) {
    CombatEvents events;
    if (dt <= 0.0f) return events;
    impacts_.clear();

    const bool player_alive = health_ > 0.0f;

    // Before anything reads the aim: firing, the breath cone and the HUD all
    // depend on this frame's lock.
    if (input.cycle_target) want_cycle_ = true;
    update_lock(player);

    // ---- cooldowns ----
    fire_timer_ = core::maxf(fire_timer_ - dt, 0.0f);
    boost_cooldown_timer_ = core::maxf(boost_cooldown_timer_ - dt, 0.0f);
    boost_timer_ = core::maxf(boost_timer_ - dt, 0.0f);
    time_since_damage_ += dt;

    // ---- breath meter ----
    // Latched: once the meter empties the button must be released and the meter
    // partly refilled before it restarts, so holding it down at zero does not
    // produce a stutter of single-frame damage.
    const bool wants_breath = input.breath && player_alive;
    if (breathing_) {
        breathing_ = wants_breath && breath_ > 0.0f;
    } else {
        breathing_ = wants_breath && breath_ >= tuning.breath_restart_threshold;
    }

    if (breathing_) {
        breath_ = core::maxf(breath_ - tuning.breath_drain * player_breath.drain * dt, 0.0f);
        time_since_breath_ = 0.0f;
        if (breath_ <= 0.0f) breathing_ = false;
    } else if (input.breath) {
        // Held but not breathing: the meter is empty and stays empty. Letting it
        // refill under a held button turns "out of breath" into a stutter of
        // single-frame bursts as it crosses the restart threshold and back.
        time_since_breath_ = 0.0f;
    } else {
        time_since_breath_ += dt;
        if (time_since_breath_ >= tuning.breath_regen_delay) {
            breath_ = core::minf(breath_ + tuning.breath_regen * dt, 1.0f);
        }
    }

    // ---- fireball ----
    if (input.fire && player_alive && fire_timer_ <= 0.0f) {
        fire_timer_ = tuning.fireball_cooldown;
        // Inherits the dragon's velocity, so a shot fired from a dive is
        // genuinely faster. Aiming then means pointing the nose, which is what
        // the player thinks they are doing.
        const Vec3 velocity =
            player.velocity + fireball_direction(player) * tuning.fireball_speed;
        fire_projectile(muzzle(player), velocity, tuning.fireball_damage,
                        tuning.fireball_radius, tuning.fireball_blast_radius, Team::Player);
        events.fired = true;
    }

    // ---- boost ----
    if (input.boost && player_alive && boost_cooldown_timer_ <= 0.0f) {
        boost_timer_ = tuning.boost_duration;
        boost_cooldown_timer_ = tuning.boost_cooldown;
    }

    apply_breath(dt, player, events);

    // Hostile flames, buffered by the bots since the last update. Cone-tested
    // against the player exactly like the player's breath tests targets.
    for (const BreathCone& flame : hostile_breaths_pending_) {
        if (health_ <= 0.0f) break;
        if (!point_in_cone(player.position, flame.origin, flame.direction,
                           core::radians(tuning.hostile_breath_half_angle_deg * flame.scales.angle),
                           tuning.hostile_breath_range * flame.scales.range)) {
            continue;
        }
        const float damage = tuning.hostile_breath_dps * flame.scales.damage * dt;
        health_ -= damage;
        events.damage_taken += damage;
        events.damage_from = flame.origin;
        events.took_damage = true;
        time_since_damage_ = 0.0f;
        if (health_ <= 0.0f) {
            health_ = 0.0f;
            events.player_died = true;
        }
    }
    hostile_breaths_drawn_ = std::move(hostile_breaths_pending_);
    hostile_breaths_pending_.clear();

    update_sentinels(dt, player, events);
    update_projectiles(dt, player, events);

    // ---- regeneration ----
    // Delayed rather than continuous: it rewards disengaging, which is the
    // manoeuvre this flight model is best at.
    if (health_ > 0.0f && time_since_damage_ >= tuning.regen_delay) {
        health_ = core::minf(health_ + tuning.health_regen * dt, tuning.max_health);
    }

    return events;
}

}  // namespace game
