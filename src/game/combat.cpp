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
        // Prey: only within 700 m, and behind anything that fights back.
        if (sentinel.prey && range > 700.0f) return 1e9f;
        return angle + range * tuning.lock_distance_weight + (sentinel.prey ? 25.0f : 0.0f);
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

void Combat::reset(const Terrain* terrain, Vec3 arena_centre, uint32_t seed, int wave) {
    terrain_ = terrain;
    arena_centre_ = arena_centre;
    rng_ = seed ? seed : 1u;
    projectiles_.clear();
    sentinels_.clear();
    kills_ = 0;
    locked_ = -1;
    revive();
    if (wave > 0) spawn_wave(wave);
}

void Combat::heal(float amount) {
    if (health_ <= 0.0f || amount <= 0.0f) return;
    health_ = core::minf(health_ + amount, tuning.max_health);
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
    player_status_.clear();
    charge_ = 0.0f;
    charging_ = false;
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

int Combat::spawn_defence(Vec3 position, float radius, float muzzle_height, Element element) {
    Sentinel tower;
    tower.ground = true;
    tower.radius = radius;
    tower.muzzle_height = muzzle_height;
    tower.element = element;
    tower.centre = position;
    tower.orbit_radius = 0.0f;
    tower.orbit_speed = 0.0f;
    tower.bob = 0.0f;
    tower.health = tuning.defence_health;
    tower.max_health = tower.health;
    tower.alive = true;
    // Staggered by position so a line of towers does not volley as one.
    tower.fire_timer = tuning.defence_fire_interval * (0.4f + 0.6f * std::fabs(random_unit()));
    tower.position = orbit_position(tower);
    sentinels_.push_back(tower);
    return int(sentinels_.size()) - 1;
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

void Combat::fire_hostile(Vec3 position, Vec3 velocity, float damage, Element element) {
    fire_projectile(position, velocity, damage, 2.5f, 0.0f, Team::Hostile,
                    tuning.fireball_gravity, element);
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

void Combat::hostile_breath(Vec3 origin, Vec3 direction, int source, BreathScales scales,
                            Element element) {
    BreathCone cone;
    cone.origin = origin;
    cone.direction = core::normalize_or(direction, Vec3::forward());
    cone.source = source;
    cone.scales = scales;
    cone.element = element;
    hostile_breaths_pending_.push_back(cone);
}

void Combat::hostile_ram(Vec3 from, Vec3 direction, Element element, float scale) {
    hostile_rams_pending_.push_back({from, core::normalize_or(direction, Vec3::forward()), element, scale});
}

void Combat::hostile_melee(Vec3 mouth, Vec3 forward, Vec3 body, Element element, float scale) {
    MeleeSwing swing;
    swing.mouth = mouth;
    swing.forward = core::normalize_or(forward, Vec3::forward());
    swing.body = body;
    swing.element = element;
    swing.scale = scale;
    hostile_melee_pending_.push_back(swing);
}

MeleeKind melee_reach(Vec3 mouth, Vec3 forward, Vec3 body, Vec3 target, float target_radius,
                      const CombatTuning& tuning) {
    // Bite: the target's near surface inside the cone ahead of the mouth.
    const Vec3 toward_mouth = core::normalize_or(mouth - target, Vec3::zero());
    const Vec3 near_point = target + toward_mouth * target_radius;
    const float half_angle = core::radians(tuning.bite_half_angle_deg);
    if (point_in_cone(target, mouth, forward, half_angle, tuning.bite_range) ||
        point_in_cone(near_point, mouth, forward, half_angle, tuning.bite_range)) {
        return MeleeKind::Bite;
    }
    // Strike: anywhere around the body, claw or tail.
    if (core::distance(body, target) - target_radius <= tuning.strike_range) {
        return MeleeKind::Strike;
    }
    return MeleeKind::None;
}

MeleeGesture melee_gesture_for(Vec3 body, Vec3 forward, Vec3 right, Vec3 target, float& side) {
    const Vec3 to = core::normalize_or(target - body, forward);
    side = core::dot(to, right) >= 0.0f ? 1.0f : -1.0f;
    const float ahead = core::dot(to, forward);
    if (ahead >= std::cos(core::radians(60.0f))) return MeleeGesture::Bite;
    if (ahead <= std::cos(core::radians(125.0f))) return MeleeGesture::Tail;
    return MeleeGesture::Claw;
}

void Combat::apply_melee(const FlightState& player, CombatEvents& events) {
    const Vec3 mouth = muzzle(player);
    const Vec3 forward = player.forward();
    // The gesture follows the nearest thing worth swinging at, hit or miss:
    // a swing at empty air still throws the limb the mark would have needed.
    {
        float best = 2.0f * tuning.bite_range;
        const Sentinel* mark = nullptr;
        for (const Sentinel& sentinel : sentinels_) {
            if (!sentinel.alive) continue;
            const float d = core::distance(player.position, sentinel.position);
            if (d < best) {
                best = d;
                mark = &sentinel;
            }
        }
        if (mark) {
            events.melee_gesture = melee_gesture_for(player.position, forward, player.right(),
                                                     mark->position, events.melee_side);
        }
    }
    // The chain: a hit inside the window of the last one steps the multiplier.
    const int chain = combo_timer_ > 0.0f ? combo_ : 0;
    const float multiplier = 1.0f + tuning.melee_combo_bonus * float(std::min(chain, 2));
    bool landed = false;
    for (Sentinel& sentinel : sentinels_) {
        if (!sentinel.alive) continue;
        const float radius = sentinel.radius > 0.0f ? sentinel.radius : tuning.sentinel_radius;
        const MeleeKind kind =
            melee_reach(mouth, forward, player.position, sentinel.position, radius, tuning);
        if (kind == MeleeKind::None) continue;
        const bool bite = kind == MeleeKind::Bite;
        hit_sentinel(sentinel, (bite ? tuning.bite_damage : tuning.strike_damage) * multiplier,
                     player_element, tuning.elements.melee_weight, events);
        // Stun and knock: away from whichever part of the dragon connected,
        // with a little lift so the rival is thrown up out of the line.
        if (!sentinel.ground) {
            sentinel.stun = core::maxf(sentinel.stun, tuning.melee_stun * (bite ? 1.0f : 0.6f));
        }
        const Vec3 from = bite ? mouth : player.position;
        const Vec3 away = core::normalize_or(sentinel.position - from, forward);
        const Vec3 shove = core::normalize_or(away + Vec3{0.0f, 0.35f, 0.0f}, away) *
                           tuning.melee_knockback;
        if (sentinel.ground) {
            // A tower does not move.
        } else if (sentinel.external) {
            sentinel.knockback = sentinel.knockback + shove;
        } else {
            // A drone's position is rebuilt from its orbit each frame, so the
            // orbit itself is moved: a quarter second of the shove.
            sentinel.centre = sentinel.centre + shove * 0.25f;
        }
        // A bite is the better hit; report it over a strike on another target.
        if (bite || events.melee_hit == MeleeKind::None) {
            events.melee_hit = kind;
            events.melee_hit_position = sentinel.position;
        }
        landed = true;
    }
    if (landed) {
        combo_ = std::min(chain + 1, 3);
        combo_timer_ = tuning.melee_combo_window;
    } else {
        combo_ = 0;
        combo_timer_ = 0.0f;
    }
    events.melee_combo = landed ? combo_ : 0;
}

void Combat::spawn_training(Vec3 origin, Vec3 forward, Vec3 right) {
    sentinels_.clear();
    locked_ = -1;
    const Vec3 ahead = core::normalize_or(Vec3{forward.x, 0.0f, forward.z}, Vec3::forward());
    const Vec3 side = core::normalize_or(Vec3{right.x, 0.0f, right.z}, Vec3::right());
    // Six dummies out to 420 m. The first two sit inside the bite cone of a
    // straight flight, the next pair a strike's width to either side, the far
    // pair further out for the breath and the fireball. A pass through the
    // whole line takes about ten seconds at cruise; R puts you back on it.
    const float distances[6] = {60.0f, 110.0f, 170.0f, 240.0f, 320.0f, 420.0f};
    const float offsets[6] = {4.0f, -6.0f, 12.0f, -12.0f, 22.0f, -18.0f};
    const float heights[6] = {0.0f, 3.0f, -4.0f, 5.0f, -3.0f, 8.0f};
    for (int i = 0; i < 6; ++i) {
        Sentinel dummy;
        dummy.passive = true;
        dummy.centre = origin + ahead * distances[i] + side * offsets[i] +
                       Vec3{0.0f, heights[i], 0.0f};
        if (terrain_) {
            dummy.centre.y = core::maxf(dummy.centre.y,
                                        terrain_->height_at(dummy.centre.x, dummy.centre.z) + 40.0f);
        }
        dummy.orbit_radius = 0.0f;
        dummy.orbit_speed = 0.0f;
        dummy.bob = 0.0f;
        dummy.health = tuning.sentinel_health * 6.0f;
        dummy.max_health = dummy.health;
        dummy.alive = true;
        dummy.fire_timer = 1e9f;
        dummy.position = orbit_position(dummy);
        sentinels_.push_back(dummy);
    }
}

int Combat::sentinels_alive() const {
    int count = 0;
    for (const Sentinel& sentinel : sentinels_) {
        if (sentinel.alive) ++count;
    }
    return count;
}

float Combat::melee_cooldown() const {
    if (tuning.melee_cooldown <= 0.0f) return 0.0f;
    return core::saturate(melee_timer_ / tuning.melee_cooldown);
}

float Combat::fire_cooldown() const {
    if (tuning.fireball_cooldown <= 0.0f) return 0.0f;
    return core::clampf(fire_timer_ / tuning.fireball_cooldown, 0.0f, 1.0f);
}

float Combat::boost_cooldown() const {
    if (tuning.boost_cooldown <= 0.0f) return 0.0f;
    return core::clampf(boost_cooldown_timer_ / tuning.boost_cooldown, 0.0f, 1.0f);
}

Projectile& Combat::fire_projectile(Vec3 position, Vec3 velocity, float damage, float radius,
                                    float blast, Team team) {
    return fire_projectile(position, velocity, damage, radius, blast, team, tuning.fireball_gravity);
}

Projectile& Combat::fire_projectile(Vec3 position, Vec3 velocity, float damage, float radius,
                                    float blast, Team team, float gravity, Element element) {
    Projectile projectile;
    projectile.position = position;
    projectile.velocity = velocity;
    projectile.gravity = gravity;
    projectile.life = tuning.fireball_lifetime;
    projectile.damage = damage;
    projectile.radius = radius;
    projectile.blast_radius = blast;
    projectile.team = team;
    projectile.element = element;
    projectile.alive = true;

    // Reuse a dead slot before growing: projectiles are spawned constantly and
    // the vector is handed to the renderer every frame.
    for (Projectile& slot : projectiles_) {
        if (!slot.alive) {
            slot = projectile;
            return slot;
        }
    }
    projectiles_.push_back(projectile);
    return projectiles_.back();
}

void Combat::damage_sentinel(Sentinel& sentinel, float amount, CombatEvents& events, bool direct) {
    if (!sentinel.alive || amount <= 0.0f) return;
    sentinel.health -= amount;
    if (direct) {
        sentinel.hit_flash = 1.0f;
        sentinel.time_since_damage = 0.0f;
        ++events.hits_dealt;
        events.last_hit = sentinel.position;
        events.had_hit = true;
    }
    if (sentinel.health <= 0.0f) {
        sentinel.alive = false;
        sentinel.health = 0.0f;
        // A destroyed tower stays destroyed: the run's ground fire is a
        // resource the player can spend fire on clearing.
        sentinel.respawn_timer = sentinel.ground || sentinel.prey ? 1e9f
                                 : sentinel.passive       ? 2.5f
                                                          : tuning.sentinel_respawn;
        if (sentinel.prey) {
            ++events.prey_killed;
        } else {
            ++kills_;
            ++events.kills;
        }
    }
}

void Combat::hit_sentinel(Sentinel& sentinel, float amount, Element element, float weight,
                          CombatEvents& events, bool chain) {
    if (!sentinel.alive) return;
    const ElementTuning& et = tuning.elements;
    // A drenched player hits softer.
    const float dealt = amount * player_status_.weaken(et) *
                        element_damage_scale(element, sentinel.element, sentinel.status, et);
    const StatusReport report =
        apply_element(sentinel.status, element, sentinel.element, weight, false, et);
    // A freeze and a full stagger are stuns: a frozen dragon's wings lock, a
    // frozen tower stops firing, a staggered drone hangs.
    if (report.froze) sentinel.stun = core::maxf(sentinel.stun, sentinel.status.frozen);
    if (report.staggered) sentinel.stun = core::maxf(sentinel.stun, et.stagger_stun);
    if (report.froze || report.staggered || report.doused || report.burned || report.corroded ||
        report.drenched) {
        StatusBurst burst;
        burst.position = sentinel.position;
        burst.element = element;
        burst.report = report;
        bursts_.push_back(burst);
    }
    const Vec3 at = sentinel.position;
    const bool was_alive = sentinel.alive;
    damage_sentinel(sentinel, dealt, events);
    if (abilities.fury && !sentinel.prey) {
        fury_ = core::saturate(fury_ + dealt * tuning.fury_per_damage +
                               (was_alive && !sentinel.alive ? tuning.fury_per_kill : 0.0f));
    }
    // Storm arcs to the nearest other target, once, for a share: it does not
    // chain on from there, or one bolt would clear a valley.
    if (chain && report.shocked && element == Element::Storm) {
        Sentinel* next = nullptr;
        float best = et.shock_chain_range;
        for (Sentinel& other : sentinels_) {
            if (&other == &sentinel || !other.alive) continue;
            const float d = core::distance(other.position, at);
            if (d < best) {
                best = d;
                next = &other;
            }
        }
        if (next) {
            const Vec3 to = next->position;
            hit_sentinel(*next, dealt * et.shock_chain_share, element, weight, events, false);
            // A held breath arcs every frame; the drawn bolt is re-struck a
            // few times a second, which is what lightning looks like anyway.
            if (arc_timer_ <= 0.0f) {
                arcs_.push_back({at, to});
                arc_timer_ = 0.09f;
            }
        }
    }
}

CombatEvents Combat::apply_hit(int index, float amount, Element element, float weight) {
    CombatEvents events;
    if (index >= 0 && size_t(index) < sentinels_.size()) {
        hit_sentinel(sentinels_[size_t(index)], amount, element, weight, events);
    }
    return events;
}

void Combat::apply_ram(const FlightState& player, CombatEvents& events) {
    if (rammed_.size() < sentinels_.size()) rammed_.resize(sentinels_.size(), 0);
    const float reach = tuning.ram_radius * player_size;
    const Vec3 along = core::normalize_or(player.velocity, player.forward());
    for (size_t i = 0; i < sentinels_.size(); ++i) {
        Sentinel& target = sentinels_[i];
        if (!target.alive || rammed_[i]) continue;
        const float radius = target.radius > 0.0f ? target.radius : tuning.sentinel_radius;
        if (core::distance(player.position, target.position) > reach + radius) continue;
        rammed_[i] = 1;
        hit_sentinel(target, tuning.ram_damage, player_element, 1.0f, events);
        if (!target.ground) {
            target.stun = core::maxf(target.stun, tuning.ram_stun);
            const Vec3 shove = core::normalize_or(along + Vec3{0.0f, 0.3f, 0.0f}, along) * tuning.ram_knockback;
            if (target.external) {
                target.knockback = target.knockback + shove;
            } else {
                target.centre = target.centre + shove * 0.25f;
            }
        }
        ++events.rammed;
        events.ram_position = target.position;
    }
}

void Combat::release_fury(const FlightState& player, CombatEvents& events) {
    fury_ = 0.0f;
    events.fury_released = true;
    // Everything in the radius: full damage at the centre, half at the edge,
    // the element's status heavy, stunned and blown outward.
    for (Sentinel& target : sentinels_) {
        if (!target.alive) continue;
        const float d = core::distance(player.position, target.position);
        if (d > tuning.fury_radius) continue;
        const float falloff = 1.0f - 0.5f * d / core::maxf(tuning.fury_radius, 1.0f);
        const bool was = target.alive;
        hit_sentinel(target, tuning.fury_damage * falloff, player_element, tuning.fury_status, events, false);
        (void)was;
        if (!target.ground) {
            target.stun = core::maxf(target.stun, tuning.fury_stun);
            const Vec3 out = core::normalize_or(target.position - player.position, Vec3::up());
            if (target.external) {
                target.knockback = target.knockback + out * tuning.fury_knockback;
            } else {
                target.centre = target.centre + out * (tuning.fury_knockback * 0.25f);
            }
        }
    }
    fury_ = 0.0f;  // the hits above must not refill it
}

void Combat::hurt_player(float amount, Element element, float weight, Vec3 from,
                         CombatEvents& events) {
    if (health_ <= 0.0f) return;
    const ElementTuning& et = tuning.elements;
    const float dealt = amount * hostile_damage_scale *
                        element_damage_scale(element, player_element, player_status_, et);
    const StatusReport report =
        apply_element(player_status_, element, player_element, weight, true, et);
    if (report.froze) events.player_froze = true;
    if (report.staggered) {
        // Knocked off line rather than stunned: up and away from the blow.
        const Vec3 away = core::normalize_or(Vec3{random_unit(), 0.0f, random_unit()}, Vec3::right());
        events.knockback = events.knockback + core::normalize_or(away + Vec3{0.0f, 0.6f, 0.0f}, away) *
                                                  et.stagger_knock;
        events.player_staggered = true;
    }
    if (report.froze || report.staggered || report.doused || report.burned || report.corroded ||
        report.drenched) {
        StatusBurst burst;
        burst.element = element;
        burst.report = report;
        burst.on_player = true;
        bursts_.push_back(burst);
    }
    health_ -= dealt;
    if (abilities.fury) fury_ = core::saturate(fury_ + dealt * tuning.fury_per_damage_taken);
    events.damage_taken += dealt;
    events.damage_from = from;
    events.took_damage = true;
    time_since_damage_ = 0.0f;
    if (health_ <= 0.0f) {
        health_ = 0.0f;
        events.player_died = true;
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
        // A charged shot seeks its target: the velocity turned toward it at
        // the seek rate, speed kept, gravity off while it has one.
        if (projectile.seek >= 0 && size_t(projectile.seek) < sentinels_.size() &&
            sentinels_[size_t(projectile.seek)].alive) {
            const Vec3 to = sentinels_[size_t(projectile.seek)].position - projectile.position;
            const float speed = core::length(projectile.velocity);
            const Vec3 heading = core::normalize_or(projectile.velocity, Vec3::forward());
            const Vec3 want = core::normalize_or(to, heading);
            const float angle = std::acos(core::clampf(core::dot(heading, want), -1.0f, 1.0f));
            const float step = projectile.seek_rate * dt;
            const Vec3 turned = angle <= step || angle < 1e-4f
                                    ? want
                                    : core::normalize_or(core::lerp(heading, want, step / angle), want);
            projectile.velocity = turned * speed;
        } else {
            projectile.velocity.y -= projectile.gravity * dt;
        }
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
                hit_sentinel(sentinel, projectile.damage, projectile.element, projectile.status_weight,
                             events);
                // A tide round shoves what it hits; a tower does not move.
                if (projectile.element == Element::Tide && !sentinel.ground &&
                    sentinel.element != Element::Tide) {
                    const Vec3 shove = core::normalize_or(projectile.velocity, Vec3::forward()) *
                                       tuning.elements.drench_push;
                    if (sentinel.external) {
                        sentinel.knockback = sentinel.knockback + shove;
                    } else {
                        sentinel.centre = sentinel.centre + shove * 0.25f;
                    }
                }
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
                    hit_sentinel(sentinel, projectile.damage * falloff * 0.5f, projectile.element,
                                 0.5f * projectile.status_weight, events);
                    consumed = true;
                    break;
                }
            }
        } else if (health_ > 0.0f) {
            float distance = 0.0f;
            // The player's hit sphere is the dragon's body, not its wingspan --
            // being clipped through a wing membrane feels arbitrary.
            if (sweep_hit(player.position, tuning.player_radius * player_size, distance)) {
                // Where the round came from, not where it hit: the HUD has to
                // point the player at the shooter.
                hurt_player(projectile.damage, projectile.element, 1.0f,
                            previous - core::normalize_or(projectile.velocity, Vec3::zero()) * 400.0f,
                            events);
                consumed = true;
            }
        }

        if (consumed) {
            projectile.alive = false;
            impacts_.push_back({projectile.position, projectile.team, false, projectile.element});
            continue;
        }

        if (terrain_ && projectile.position.y <=
                            terrain_->height_at(projectile.position.x, projectile.position.z)) {
            projectile.alive = false;
            impacts_.push_back({projectile.position, projectile.team, true, projectile.element});
            // A hostile round with a blast splashes where it lands: a tower's
            // bolt into the ground beside a standing dragon is not a miss.
            if (projectile.team == Team::Hostile && projectile.blast_radius > 0.0f && health_ > 0.0f) {
                const float d = core::length(player.position - projectile.position);
                if (d < projectile.blast_radius) {
                    const float amount = projectile.damage * (1.0f - d / projectile.blast_radius) * 0.7f;
                    hurt_player(amount, projectile.element, 0.5f,
                                previous - core::normalize_or(projectile.velocity, Vec3::zero()) * 400.0f,
                                events);
                }
            }
        }
    }
}

void Combat::update_sentinels(float dt, const FlightState& player, CombatEvents& events) {
    for (Sentinel& sentinel : sentinels_) {
        sentinel.hit_flash = core::maxf(sentinel.hit_flash - dt * 4.0f, 0.0f);
        // Burn and corrosion keep paying after the hit; a kill by them is the
        // player's kill like any other. Kept from resetting the regen clock:
        // a burn is the hit's afterlife, not a fresh hit.
        if (sentinel.alive) {
            const float since = sentinel.time_since_damage;
            const float dot = tick_status(sentinel.status, dt, tuning.elements);
            if (dot > 0.0f) {
                damage_sentinel(sentinel, dot, events, false);
                sentinel.time_since_damage = since;
            }
        }
        sentinel.time_since_damage += dt;
        // Stunned: held bright for as long as it lasts, so the state reads at
        // range the way a hit does.
        sentinel.stun = core::maxf(sentinel.stun - dt, 0.0f);
        // A freeze is a stun that reads as ice, not as a hit (the renderer rimes it).
        if (sentinel.stun > 0.0f && sentinel.status.frozen <= 0.0f) {
            sentinel.hit_flash = core::maxf(sentinel.hit_flash, 0.55f);
        }

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
                sentinel.stun = 0.0f;
                sentinel.status.clear();
                sentinel.knockback = Vec3::zero();
                sentinel.fire_timer = sentinel.passive ? 1e9f : tuning.sentinel_fire_interval;
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
        // shot, not to be clever. A stunned drone hangs where the bite left it.
        const Vec3 previous = sentinel.position;
        // Chill slows the orbit like it slows a wing.
        const float slow = sentinel.status.slow(tuning.elements);
        if (sentinel.stun <= 0.0f) sentinel.phase += sentinel.orbit_speed * dt * (1.0f - slow);
        sentinel.position = orbit_position(sentinel);
        sentinel.velocity = dt > 0.0f ? (sentinel.position - previous) / dt : Vec3::zero();

        if (health_ <= 0.0f || sentinel.passive || sentinel.stun > 0.0f) continue;
        // Shocked or frozen: weapons cold. Chilled: the reload drags.
        if (sentinel.status.jammed()) continue;

        sentinel.fire_timer -= dt * (1.0f - 0.6f * slow) *
                               (sentinel.status.drench > 0.0f ? tuning.elements.drench_reload : 1.0f);
        if (sentinel.fire_timer > 0.0f) continue;
        const bool ground = sentinel.ground;
        const Vec3 to_player = player.position - sentinel.position;
        const float distance = core::length(to_player);
        const bool pressed = ground && (player.grounded || distance < tuning.defence_close_range);
        sentinel.fire_timer =
            ground ? tuning.defence_fire_interval /
                         (pressed ? core::maxf(tuning.defence_close_rate, 0.1f) : 1.0f)
                   : tuning.sentinel_fire_interval;
        const float range = ground ? tuning.defence_range : tuning.sentinel_range;
        if (distance > range || distance < 1e-3f) continue;

        // Lead the shot, then spoil it. Perfect prediction is not difficulty,
        // it is a guarantee, and a guaranteed hit removes any reason to
        // manoeuvre. A ground bolt also lifts its aim for the drop.
        const float speed = ground ? tuning.defence_projectile_speed : tuning.sentinel_projectile_speed;
        const float gravity = ground ? tuning.defence_gravity : 0.0f;
        const float flight_time = distance / core::maxf(speed, 1.0f);
        Vec3 aim = player.position + player.velocity * flight_time;
        aim.y += 0.5f * gravity * flight_time * flight_time;
        aim += Vec3{random_unit(), random_unit(), random_unit()} *
               (ground ? tuning.defence_spread * (pressed ? tuning.defence_close_spread : 1.0f)
                       : tuning.sentinel_spread);

        // A tower fires from its brazier, at the top, not from its middle.
        const Vec3 from = ground ? sentinel.position + Vec3{0.0f, sentinel.muzzle_height, 0.0f}
                                 : sentinel.position;
        const Vec3 shot = core::normalize(aim - from);
        fire_projectile(from + shot * (ground ? 2.0f : tuning.sentinel_radius + 1.0f),
                        shot * speed,
                        (ground ? tuning.defence_damage : tuning.sentinel_damage) *
                            sentinel.status.weaken(tuning.elements),
                        2.5f, ground ? tuning.defence_splash : 0.0f, Team::Hostile, gravity,
                        sentinel.element);
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
        const float resist = sentinel.ground ? core::saturate(tuning.defence_breath_resist) : 1.0f;
        hit_sentinel(sentinel, tuning.breath_damage_per_second * player_breath.damage * dt * resist,
                     player_element, tuning.elements.breath_weight * dt, events);
        // A stream of water shoves steadily: half a fireball's push a second.
        if (player_element == Element::Tide && !sentinel.ground && sentinel.element != Element::Tide) {
            const Vec3 shove = breath_direction_ * (tuning.elements.drench_push * 0.5f * dt);
            if (sentinel.external) {
                sentinel.knockback = sentinel.knockback + shove;
            } else {
                sentinel.centre = sentinel.centre + shove;
            }
        }
    }
}

CombatEvents Combat::update(float dt, const FlightState& player, const CombatInput& input) {
    CombatEvents events;
    if (dt <= 0.0f) return events;
    impacts_.clear();
    bursts_.clear();
    arcs_.clear();
    arc_timer_ = core::maxf(arc_timer_ - dt, 0.0f);

    const bool player_alive = health_ > 0.0f;

    // Before anything reads the aim: firing, the breath cone and the HUD all
    // depend on this frame's lock.
    if (input.cycle_target) want_cycle_ = true;
    update_lock(player);

    // ---- cooldowns ----
    // Drenched, the fireball reloads slower.
    fire_timer_ = core::maxf(fire_timer_ - dt * (player_status_.drench > 0.0f
                                                     ? tuning.elements.drench_reload
                                                     : 1.0f),
                             0.0f);
    melee_timer_ = core::maxf(melee_timer_ - dt, 0.0f);
    combo_timer_ = core::maxf(combo_timer_ - dt, 0.0f);
    boost_cooldown_timer_ = core::maxf(boost_cooldown_timer_ - dt, 0.0f);
    boost_timer_ = core::maxf(boost_timer_ - dt, 0.0f);
    time_since_damage_ += dt;

    // ---- breath meter ----
    // Latched: once the meter empties the button must be released and the meter
    // partly refilled before it restarts, so holding it down at zero does not
    // produce a stutter of single-frame damage.
    // Shocked or frozen: the breath will not light and the fireball will not
    // leave, however hard the button is pressed.
    const bool jammed = player_status_.jammed();
    const bool wants_breath = input.breath && player_alive && !jammed;
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
            // Drenched: the fire in the belly relights at half the rate.
            const float drenched = player_status_.drench > 0.0f ? 0.5f : 1.0f;
            breath_ = core::minf(breath_ + tuning.breath_regen * drenched * dt, 1.0f);
        }
    }

    // ---- fireball ----
    // Inherits the dragon's velocity, so a shot fired from a dive is
    // genuinely faster. Aiming then means pointing the nose, which is what
    // the player thinks they are doing. `power` 0 is a plain shot, 1 a full
    // charge.
    auto shoot = [&](float power) {
        fire_timer_ = tuning.fireball_cooldown;
        const float speed = tuning.fireball_speed * (1.0f + 0.15f * power);
        const Vec3 velocity = player.velocity + fireball_direction(player) * speed;
        Projectile& p = fire_projectile(
            muzzle(player), velocity, tuning.fireball_damage * core::lerpf(1.0f, tuning.charged_damage, power),
            tuning.fireball_radius * core::lerpf(1.0f, tuning.charged_radius, power),
            tuning.fireball_blast_radius * core::lerpf(1.0f, tuning.charged_blast, power), Team::Player,
            tuning.fireball_gravity, player_element);
        p.charged = power > 0.0f;
        p.status_weight = core::lerpf(1.0f, tuning.charged_status, power);
        p.seek = power > 0.0f ? locked_ : -1;
        p.seek_rate = tuning.charged_seek * power;
        events.fired = true;
        events.charged_fired = power > 0.0f;
    };
    if (abilities.charged_shot) {
        // Held, it gathers (once the reload is done); let go -- or reach full
        // -- and it leaves. A tap is a plain shot on the release.
        const bool can = player_alive && !jammed;
        if ((input.fire_held || input.fire) && can) {
            charging_ = true;
            if (fire_timer_ <= 0.0f) charge_ = core::minf(charge_ + dt / core::maxf(tuning.charge_time, 0.05f), 1.0f);
        }
        const bool release = charging_ && (!(input.fire_held || input.fire) || charge_ >= 1.0f);
        if (!can) {
            charging_ = false;
            charge_ = 0.0f;
        } else if (release && fire_timer_ <= 0.0f) {
            const float power = charge_ < tuning.charge_min
                                    ? 0.0f
                                    : (charge_ - tuning.charge_min) / core::maxf(1.0f - tuning.charge_min, 1e-3f);
            shoot(power);
            charging_ = false;
            charge_ = 0.0f;
        }
    } else if (input.fire && player_alive && !jammed && fire_timer_ <= 0.0f) {
        shoot(0.0f);
    }

    // ---- melee ----
    // The swing happens whether or not anything is in reach: the lunge and
    // its airspeed cost are the player's to spend, and a bite at empty air is
    // what teaches the reach.
    if (input.melee && player_alive && melee_timer_ <= 0.0f) {
        melee_timer_ = tuning.melee_cooldown;
        events.melee_swung = true;
        apply_melee(player, events);
    }

    // ---- boost ----
    if (input.boost && player_alive && boost_cooldown_timer_ <= 0.0f) {
        boost_timer_ = tuning.boost_duration;
        boost_cooldown_timer_ = tuning.boost_cooldown;
    }
    // The ram: a fresh boost forgets who it has hit, and picks what to
    // pounce on -- the lock if it is in reach, else the nearest in the cone.
    if (boost_active() && !was_boosting_) {
        rammed_.assign(sentinels_.size(), 0);
        pounce_ = -1;
        if (abilities.ram) {
            const float cone = std::cos(core::radians(tuning.pounce_cone_deg));
            auto in_reach = [&](int i) {
                const Sentinel& s = sentinels_[size_t(i)];
                const Vec3 to = s.position - player.position;
                const float d = core::length(to);
                return s.alive && d < tuning.pounce_range && d > 1e-3f &&
                       core::dot(to / d, player.forward()) > cone;
            };
            if (locked_ >= 0 && in_reach(locked_)) pounce_ = locked_;
        }
    }
    // A pounce holds the boost until contact or its time; it ends on contact.
    if (pounce_ >= 0 && boost_active() && !was_boosting_pounce_) {
        boost_timer_ = core::maxf(boost_timer_, tuning.pounce_time);
    }
    was_boosting_pounce_ = pounce_ >= 0 && boost_active();
    if (pounce_ >= 0 && (size_t(pounce_) >= rammed_.size() || rammed_[size_t(pounce_)] ||
                         !sentinels_[size_t(pounce_)].alive)) {
        pounce_ = -1;
        boost_timer_ = core::minf(boost_timer_, 0.15f);  // landed: the burst is spent
    }
    was_boosting_ = boost_active();
    if (abilities.ram && boost_active() && player_alive) apply_ram(player, events);
    // ---- fury ----
    if (abilities.fury && input.fury && player_alive && fury_ >= 1.0f) release_fury(player, events);

    apply_breath(dt, player, events);

    // Hostile flames, buffered by the bots since the last update. Cone-tested
    // against the player exactly like the player's breath tests targets.
    for (const BreathCone& flame : hostile_breaths_pending_) {
        if (health_ <= 0.0f) break;
        // A bigger body reaches into the flame: its near side is tested too.
        const float half = core::radians(tuning.hostile_breath_half_angle_deg * flame.scales.angle);
        const float range = tuning.hostile_breath_range * flame.scales.range;
        const Vec3 near_side = player.position +
                               core::normalize_or(flame.origin - player.position, Vec3::zero()) *
                                   (tuning.player_radius * core::maxf(player_size - 1.0f, 0.0f));
        if (!point_in_cone(player.position, flame.origin, flame.direction, half, range) &&
            !point_in_cone(near_side, flame.origin, flame.direction, half, range)) {
            continue;
        }
        const float damage = tuning.hostile_breath_dps * flame.scales.damage * dt;
        hurt_player(damage, flame.element, tuning.elements.breath_weight * dt, flame.origin, events);
    }
    hostile_breaths_drawn_ = std::move(hostile_breaths_pending_);
    hostile_breaths_pending_.clear();

    // Hostile bites and strikes, with the geometry the player's swing uses.
    // The player's body is about the size of a sentinel for this purpose.
    for (const MeleeSwing& swing : hostile_melee_pending_) {
        if (health_ <= 0.0f) break;
        const MeleeKind kind = melee_reach(swing.mouth, swing.forward, swing.body, player.position,
                                           tuning.sentinel_radius * player_size, tuning);
        if (kind == MeleeKind::None) continue;
        const float damage =
            tuning.hostile_melee_damage * (kind == MeleeKind::Bite ? 1.0f : 0.6f) * swing.scale;
        hurt_player(damage, swing.element, tuning.elements.melee_weight, swing.mouth, events);
        events.bitten = true;
        const Vec3 away = core::normalize_or(player.position - swing.mouth, swing.forward);
        events.knockback = events.knockback + away * tuning.hostile_melee_knockback;
    }
    hostile_melee_pending_.clear();
    for (const RamHit& ram : hostile_rams_pending_) {
        if (health_ <= 0.0f) break;
        hurt_player(tuning.hostile_ram_damage * ram.scale, ram.element, 1.0f, ram.from, events);
        events.bitten = true;
        events.knockback = events.knockback + core::normalize_or(ram.direction + Vec3{0.0f, 0.25f, 0.0f}, ram.direction) *
                                                  tuning.hostile_ram_knockback;
    }
    hostile_rams_pending_.clear();

    update_sentinels(dt, player, events);
    update_projectiles(dt, player, events);

    // ---- the player's status ----
    // Burn and corrosion tick down the health, but do NOT hold the
    // regeneration off: they did, and a blight tower's bolt every couple of
    // seconds meant the player never healed at all -- the afterlife of a hit
    // is not a fresh hit.
    if (health_ > 0.0f) {
        const float dot = tick_status(player_status_, dt, tuning.elements) * hostile_damage_scale;
        if (dot > 0.0f) {
            health_ -= dot;
            events.damage_taken += dot;
            if (health_ <= 0.0f) {
                health_ = 0.0f;
                events.player_died = true;
            }
        }
    }

    // ---- regeneration ----
    // Delayed rather than continuous: it rewards disengaging, which is the
    // manoeuvre this flight model is best at.
    if (health_ > 0.0f && time_since_damage_ >= tuning.regen_delay) {
        health_ = core::minf(health_ + tuning.health_regen * dt, tuning.max_health);
    }

    return events;
}

}  // namespace game
