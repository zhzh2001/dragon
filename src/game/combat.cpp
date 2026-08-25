#include "game/combat.h"

#include <cmath>

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

float Combat::random_unit() {
    // xorshift32: the sentinels only need spread that does not repeat visibly,
    // and a deterministic sequence makes a headless run reproducible.
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return float(rng_ & 0xffffffu) / float(0xffffff) * 2.0f - 1.0f;
}

void Combat::reset(const Terrain* terrain, Vec3 arena_centre, uint32_t seed) {
    terrain_ = terrain;
    arena_centre_ = arena_centre;
    rng_ = seed ? seed : 1u;
    projectiles_.clear();
    sentinels_.clear();
    kills_ = 0;
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
                if (!sweep_hit(sentinel.position, tuning.sentinel_radius, distance)) continue;
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
            continue;
        }

        if (terrain_ && projectile.position.y <=
                            terrain_->height_at(projectile.position.x, projectile.position.z)) {
            projectile.alive = false;
        }
    }
}

void Combat::update_sentinels(float dt, const FlightState& player, CombatEvents& events) {
    (void)events;
    for (Sentinel& sentinel : sentinels_) {
        sentinel.hit_flash = core::maxf(sentinel.hit_flash - dt * 4.0f, 0.0f);

        if (!sentinel.alive) {
            sentinel.respawn_timer -= dt;
            if (sentinel.respawn_timer <= 0.0f) {
                sentinel.alive = true;
                sentinel.health = tuning.sentinel_health;
                sentinel.max_health = tuning.sentinel_health;
                sentinel.fire_timer = tuning.sentinel_fire_interval;
                // Back on its orbit, not at the origin: this branch returns
                // early, so nothing else would place it this frame.
                sentinel.position = orbit_position(sentinel);
                sentinel.velocity = Vec3::zero();
            }
            continue;
        }

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
    breath_origin_ = muzzle_of(player);
    breath_direction_ = player.forward();
    if (!breathing_) return;

    const float half_angle = core::radians(tuning.breath_half_angle_deg);
    for (Sentinel& sentinel : sentinels_) {
        if (!sentinel.alive) continue;
        // Tested against the target's centre pushed back toward the tip by its
        // radius, so a target the flame visibly engulfs counts as inside even
        // when its centre is a whisker outside the cone.
        const Vec3 toward = core::normalize(breath_origin_ - sentinel.position);
        const Vec3 near_point = sentinel.position + toward * tuning.sentinel_radius;
        if (!point_in_cone(sentinel.position, breath_origin_, breath_direction_, half_angle,
                           tuning.breath_range) &&
            !point_in_cone(near_point, breath_origin_, breath_direction_, half_angle,
                           tuning.breath_range)) {
            continue;
        }
        damage_sentinel(sentinel, tuning.breath_damage_per_second * dt, events);
    }
}

CombatEvents Combat::update(float dt, const FlightState& player, const CombatInput& input) {
    CombatEvents events;
    if (dt <= 0.0f) return events;

    const bool player_alive = health_ > 0.0f;

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
        breath_ = core::maxf(breath_ - tuning.breath_drain * dt, 0.0f);
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
        const Vec3 velocity = player.velocity + player.forward() * tuning.fireball_speed;
        fire_projectile(muzzle_of(player), velocity, tuning.fireball_damage,
                        tuning.fireball_radius, tuning.fireball_blast_radius, Team::Player);
    }

    // ---- boost ----
    if (input.boost && player_alive && boost_cooldown_timer_ <= 0.0f) {
        boost_timer_ = tuning.boost_duration;
        boost_cooldown_timer_ = tuning.boost_cooldown;
    }

    apply_breath(dt, player, events);
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
