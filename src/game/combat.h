#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "game/flight.h"

namespace game {

class Terrain;

// Who a projectile is allowed to hurt. Two teams is all a duel needs, and it
// keeps the hit loop a comparison rather than a lookup.
enum class Team : uint8_t { Player, Hostile };

// Combat coefficients. Same rule as flight: everything here is meant to be
// dragged in ImGui while playing.
struct CombatTuning {
    // ---- fireball ----
    // Fast enough to lead a target rather than lob at it, slow enough that a
    // banking dragon can still slip the shot.
    float fireball_speed = 260.0f;
    float fireball_damage = 30.0f;
    // Deliberately generous. A 3D dogfight is hard enough to read without
    // pixel-accurate hitboxes, and a shot that "clearly hit" but did not is the
    // single most infuriating thing an air combat game can do.
    float fireball_radius = 3.0f;
    // A near miss still counts for reduced damage, falling off to nothing at
    // the edge. Same reasoning, applied to the shots that only just missed.
    float fireball_blast_radius = 12.0f;
    float fireball_cooldown = 0.85f;
    // Light gravity: enough that very long shots need a raised nose, not enough
    // to make aiming a ballistics exercise.
    float fireball_gravity = 4.0f;
    float fireball_lifetime = 5.0f;

    // ---- fire breath ----
    // Short ranged and continuous, so it trades reach for the ability to hold a
    // target under damage while manoeuvring. The fireball is the opposite.
    float breath_range = 155.0f;
    float breath_half_angle_deg = 12.0f;
    float breath_damage_per_second = 60.0f;
    // Meter is 0..1. Drain and regen are per second.
    float breath_drain = 0.30f;
    float breath_regen = 0.20f;
    // Stops the meter being tapped at zero for a stutter of damage.
    float breath_restart_threshold = 0.20f;
    float breath_regen_delay = 0.8f;

    // ---- boost ----
    float boost_cooldown = 5.0f;
    float boost_duration = 1.1f;

    // ---- survivability ----
    float max_health = 100.0f;
    float health_regen = 6.0f;     // per second, once regen starts
    float regen_delay = 6.0f;      // seconds without damage before it starts

    // ---- targeting ----
    //
    // A 3D dogfight against a small fast target is close to unaimable without
    // help: the nose has to lead a mark that is also manoeuvring, at a range
    // where a degree of error is tens of metres. So the dragon picks a target
    // and the shot bends toward it.
    float lock_cone_deg = 30.0f;   // half angle to acquire
    // Held well outside the acquisition cone, so a target does not drop the
    // instant it slides off the nose during a turn.
    float lock_hold_cone_deg = 65.0f;
    float lock_range = 1600.0f;
    // 0 aims purely down the nose, 1 aims perfectly at the intercept.
    //
    // High by default, and deliberately so: the residual error is what the
    // player sees. At 0.7 a shot 14 degrees off the nose at 500 m still misses
    // by 37 m, which reads as the assist doing nothing at all. At 0.9 the same
    // shot lands. Lower it for a harder aiming game, not to make it fairer.
    float aim_assist = 0.90f;
    // The breath cone is a visible object for as long as it is held, so its
    // assist is capped by angle as well as by strength. A fireball bending 30
    // degrees is invisible; a flame doing it looks like a garden hose.
    float breath_assist_max_deg = 16.0f;

    // ---- sentinels ----
    float sentinel_health = 60.0f;
    float sentinel_radius = 7.0f;
    float sentinel_fire_interval = 3.2f;
    float sentinel_projectile_speed = 150.0f;
    float sentinel_damage = 12.0f;
    // Aim error in metres at the target point, before lead. The difficulty dial:
    // bots that never miss are not hard, they are unfair.
    float sentinel_spread = 18.0f;
    float sentinel_range = 700.0f;
    float sentinel_respawn = 8.0f;
};

// What the player is asking combat to do this frame. Held vs edge is decided by
// the caller, so this struct means the same thing for a bot later.
struct CombatInput {
    bool breath = false;      // held
    bool fire = false;        // edge: one fireball per press
    bool boost = false;       // edge
};

struct Projectile {
    core::Vec3 position = core::Vec3::zero();
    core::Vec3 velocity = core::Vec3::zero();
    float life = 0.0f;
    float damage = 0.0f;
    float radius = 0.0f;
    float blast_radius = 0.0f;
    Team team = Team::Player;
    bool alive = false;
};

// A practice target: hovers a fixed orbit and shoots back on a timer.
//
// Explicitly not AI -- it does not steer, evade or think, and M14's bots replace
// it wholesale. It exists so that health, aim and the cooldown rhythm can be
// tuned against something that shoots back, which is the only way to know
// whether the combat core feels right.
struct Sentinel {
    core::Vec3 position = core::Vec3::zero();
    core::Vec3 velocity = core::Vec3::zero();
    float health = 0.0f;
    float max_health = 0.0f;
    bool alive = false;
    float respawn_timer = 0.0f;
    // Flash on hit, so a shot that connects is unmistakable at any range.
    float hit_flash = 0.0f;

    // Orbit it flies, in world space.
    core::Vec3 centre = core::Vec3::zero();
    float orbit_radius = 120.0f;
    float orbit_speed = 0.35f;  // rad/s
    float phase = 0.0f;
    float bob = 18.0f;
    float fire_timer = 0.0f;
};

// What happened this frame, for the HUD and, later, audio.
struct CombatEvents {
    int hits_dealt = 0;      // player projectiles or breath connecting
    int kills = 0;
    float damage_taken = 0.0f;
    bool player_died = false;
    // World position of the most recent hit the player scored, for a marker.
    core::Vec3 last_hit = core::Vec3::zero();
    bool had_hit = false;
    // Where the damage came from, so the HUD can point at the attacker. Being
    // hit by something you cannot locate is the least readable thing in an
    // aerial fight.
    core::Vec3 damage_from = core::Vec3::zero();
    bool took_damage = false;
};

// The combat core: player resources, projectiles, and the targets to use them
// on. Owns no rendering and no input, so a bot can drive it with the same
// CombatInput the player produces.
class Combat {
public:
    CombatTuning tuning;

    // `terrain` may be null, in which case projectiles ignore the ground.
    void reset(const Terrain* terrain, core::Vec3 arena_centre, uint32_t seed);

    // `player` is read, never written: boost is reported back through
    // boost_active() and applied by the flight model, so thrust stays in one
    // place.
    CombatEvents update(float dt, const FlightState& player, const CombatInput& input);

    // Called when the player respawns, to restore resources without rebuilding
    // the arena.
    void revive();

    // ---- player state ----
    float health() const { return health_; }
    float health_fraction() const { return tuning.max_health > 0.0f ? health_ / tuning.max_health : 0.0f; }
    float breath() const { return breath_; }
    bool breathing() const { return breathing_; }
    bool alive() const { return health_ > 0.0f; }
    // 0 when ready, 1 immediately after use.
    float fire_cooldown() const;
    float boost_cooldown() const;
    bool boost_active() const { return boost_timer_ > 0.0f; }

    // ---- world ----
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    const std::vector<Sentinel>& sentinels() const { return sentinels_; }
    std::vector<Sentinel>& sentinels() { return sentinels_; }
    int sentinels_alive() const;
    int kills() const { return kills_; }

    // ---- targeting ----
    // Index into sentinels(), or -1. Sticky: acquired inside a narrow cone and
    // held inside a much wider one.
    int locked_index() const { return locked_; }
    bool has_lock() const { return locked_ >= 0; }
    // Current position of the locked target, and where a fireball has to be
    // aimed to meet it. Exposed so the HUD draws exactly what the shot will do.
    core::Vec3 lock_position() const;
    core::Vec3 lock_intercept() const { return lock_intercept_; }
    // Direction the next fireball will travel, and the axis of the breath cone.
    // Both blend the nose toward the lock by `aim_assist`.
    core::Vec3 fireball_direction(const FlightState& player) const;
    core::Vec3 breath_direction_for(const FlightState& player) const;

    // Tip and axis of the breath cone this frame. Only meaningful while
    // breathing(); the renderer uses it directly so the flame drawn and the
    // volume that damages can never disagree.
    core::Vec3 breath_origin() const { return breath_origin_; }
    core::Vec3 breath_direction() const { return breath_direction_; }

    // Spawns a ring of sentinels around the arena centre. Called by reset, and
    // again from the UI to restock.
    void spawn_wave(int count);

private:
    void fire_projectile(core::Vec3 position, core::Vec3 velocity, float damage, float radius,
                         float blast, Team team);
    void update_projectiles(float dt, const FlightState& player, CombatEvents& events);
    void update_sentinels(float dt, const FlightState& player, CombatEvents& events);
    void apply_breath(float dt, const FlightState& player, CombatEvents& events);
    void damage_sentinel(Sentinel& sentinel, float amount, CombatEvents& events);
    float random_unit();
    void update_lock(const FlightState& player);
    core::Vec3 muzzle(const FlightState& player) const;
    core::Vec3 assisted_direction(const FlightState& player, core::Vec3 target,
                                  float max_turn_deg) const;

    const Terrain* terrain_ = nullptr;
    core::Vec3 arena_centre_ = core::Vec3::zero();

    std::vector<Projectile> projectiles_;
    std::vector<Sentinel> sentinels_;

    float health_ = 100.0f;
    float breath_ = 1.0f;
    bool breathing_ = false;
    float time_since_damage_ = 0.0f;
    float time_since_breath_ = 0.0f;
    float fire_timer_ = 0.0f;
    float boost_timer_ = 0.0f;
    float boost_cooldown_timer_ = 0.0f;
    int kills_ = 0;
    int locked_ = -1;
    core::Vec3 lock_intercept_ = core::Vec3::zero();

    core::Vec3 breath_origin_ = core::Vec3::zero();
    core::Vec3 breath_direction_ = core::Vec3::forward();

    uint32_t rng_ = 1u;
};

// Whether `point` lies inside the cone with the given tip, axis, half angle and
// length. Exposed because it is the one piece of combat geometry worth testing
// directly, and because targeting will want it later.
bool point_in_cone(core::Vec3 point, core::Vec3 tip, core::Vec3 axis, float half_angle_radians,
                   float length);

// Closest approach between a swept sphere and a static one, used so a fast
// projectile cannot pass through a target between two frames. Returns the
// fraction along the segment at which they are nearest, clamped to [0,1].
float closest_point_fraction(core::Vec3 from, core::Vec3 to, core::Vec3 point);

// Where to aim a projectile of `speed` so it meets a target moving at constant
// velocity.
//
// Solved by iteration rather than by the quadratic: the closed form needs
// special cases for a target faster than the projectile and for a zero
// discriminant, and three passes of "guess the flight time, move the target,
// re-measure" converges well inside a pixel at these speeds.
core::Vec3 intercept_point(core::Vec3 origin, core::Vec3 target, core::Vec3 target_velocity,
                           float speed, int iterations = 3);

}  // namespace game
