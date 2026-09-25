#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "game/breath.h"
#include "game/flight.h"

namespace game {

class Terrain;

// Who a projectile is allowed to hurt. Two teams is all a duel needs, and it
// keeps the hit loop a comparison rather than a lookup.
enum class Team : uint8_t { Player, Hostile };

// Combat coefficients. Same rule as flight: everything here is meant to be
// dragged in ImGui while playing.
struct CombatTuning {
    // ---- melee ----
    //
    // The close-range answer. The breath cone and the fireball both need the
    // nose on the target, and in a turning fight the rival is very often
    // twenty metres away and off the nose, where the player could do nothing
    // but circle. A dragon that close bites. None of this needs aim: a BITE
    // lands on anything inside a wide cone ahead of the mouth within a few
    // body lengths; a claw or tail STRIKE lands on anything alongside or
    // behind, inside a sphere around the body. Distance and cone tests only.
    float bite_range = 30.0f;           // metres from the mouth, about four body lengths
    float bite_half_angle_deg = 50.0f;  // wide on purpose: this is the no-aim weapon
    float bite_damage = 28.0f;
    float strike_range = 16.0f;         // from the body centre, any direction
    float strike_damage = 18.0f;
    // Short: the first cut at 1.3 s made melee harder to land than the
    // breath, because a close pass lasts about a second and one swing in it
    // was one chance. A swing every half second turns a pass into a flurry.
    float melee_cooldown = 0.55f;
    // A lunge costs a little airspeed, so biting is a commitment rather than
    // a free action spammed on cooldown -- but only a little, or the flurry
    // above stalls the dragon.
    float melee_lunge_speed_cost = 2.0f;  // m/s
    // What a landed hit does beyond damage, in the Spyro tradition: the
    // target is STUNNED (a bot loses its controls and weapons, a drone stops
    // shooting and orbiting) and KNOCKED away from the biter. A hit that only
    // subtracts a number is indistinguishable from the flame; a hit that
    // sends the rival tumbling is a bite.
    float melee_stun = 1.6f;        // seconds; a strike stuns for 60% of it
    float melee_knockback = 14.0f;  // m/s added to the target's velocity, away from the biter
    // Hits inside this window of the last one chain: each step adds `bonus`
    // to the damage, up to three steps (x1.7). A miss breaks the chain.
    float melee_combo_window = 1.4f;
    float melee_combo_bonus = 0.35f;
    // Bots bite with the same reach; their damage is its own dial, like their
    // breath. A strike does 60% of it. Their bite knocks the player, never
    // stuns: losing the controls is the one thing a player must not suffer.
    float hostile_melee_damage = 12.0f;
    float hostile_melee_knockback = 10.0f;

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
    // Held outside the acquisition cone, so a turn does not drop the target --
    // but not so wide that the lock lives far off-screen.
    float lock_hold_cone_deg = 50.0f;
    float lock_range = 1600.0f;
    // Lock scoring is angle plus distance: alignment alone locks a 1500 m speck
    // dead ahead over a close target ten degrees off the nose, which is never
    // the one the player meant. Degrees of penalty per metre.
    float lock_distance_weight = 0.02f;
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

    // ---- hostile survivability (bots) ----
    // Bots heal like the player does: disengage-and-recover cuts both ways,
    // and it is a difficulty dial -- a rookie that never heals loses a war of
    // attrition that an ace refuses to grant.
    float hostile_regen = 4.0f;        // per second, after the delay
    float hostile_regen_delay = 6.0f;  // seconds without damage

    // ---- hostile breath (bots) ----
    float hostile_breath_dps = 38.0f;
    float hostile_breath_range = 190.0f;
    float hostile_breath_half_angle_deg = 12.0f;

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

    // Targets further than this get no bracket on the HUD (the locked one
    // always does). A run lays seven hostiles down a five-kilometre valley,
    // and seven range labels at the start read as a spreadsheet.
    float mark_range = 1400.0f;

    // ---- ground defences (the run's watchtowers) ----
    // Pinned to a point on the terrain; a slower, heavier bolt with a visible
    // arc, so a flight straight down the corridor is in their reach and a
    // flight along the ridge above them is not. Destroyed ones stay down.
    float defence_health = 90.0f;
    float defence_fire_interval = 2.4f;
    float defence_projectile_speed = 170.0f;
    float defence_gravity = 14.0f;
    float defence_damage = 14.0f;
    float defence_spread = 14.0f;
    float defence_range = 420.0f;
    // The playtest landed beside a tower and breathed on it until it fell:
    // a tower was the one target that could not answer a dragon standing
    // still. Stone takes a share of the flame; bites and strikes land but
    // neither stun nor shove a building; inside the close range, or against
    // a grounded dragon, it fires faster and tighter; and a bolt that strikes
    // the ground splashes, so a near miss on a standing dragon still hurts.
    // After the second playtest ("towers overpowered, best to ignore them")
    // the resist and the rate multiplier went back to neutral: five answers
    // to one exploit was an overcorrection. The tighter aim at a grounded
    // dragon and the splash stay, which is what closed the exploit.
    float defence_breath_resist = 1.0f;   // share of breath damage that lands
    float defence_close_range = 200.0f;
    float defence_close_rate = 1.0f;      // fire-rate multiplier up close or grounded
    float defence_close_spread = 0.25f;   // spread multiplier up close or grounded
    float defence_splash = 10.0f;         // m
};

// What the player is asking combat to do this frame. Held vs edge is decided by
// the caller, so this struct means the same thing for a bot later.
struct CombatInput {
    bool breath = false;        // held
    bool fire = false;          // edge: one fireball per press
    bool boost = false;         // edge
    bool cycle_target = false;  // edge: relock onto the next candidate
    bool melee = false;         // edge: one bite or strike per press
};

// What a melee swing would connect with: the bite (ahead, from the mouth) or
// the claw/tail strike (alongside or behind, from the body). Bite wins when
// both apply.
enum class MeleeKind : uint8_t { None, Bite, Strike };

// Which limb a swing is thrown with, decided by where the mark is: the jaws
// for anything ahead, a claw for anything alongside, the tail for anything
// behind. It changes only how the swing LOOKS (the rig plays a different
// gesture); what it hits is melee_reach()'s business.
enum class MeleeGesture : uint8_t { Bite, Claw, Tail };

// The gesture for a swing at `target` from a dragon at `body` facing
// `forward` with `right` to its right. `side` comes back +1 for the right
// side, -1 for the left. With no target in mind, a bite.
MeleeGesture melee_gesture_for(core::Vec3 body, core::Vec3 forward, core::Vec3 right,
                               core::Vec3 target, float& side);

// A bot's swing, buffered like its flame and resolved against the player
// inside update().
struct MeleeSwing {
    core::Vec3 mouth = core::Vec3::zero();
    core::Vec3 forward = core::Vec3::forward();
    core::Vec3 body = core::Vec3::zero();
};

struct Projectile {
    core::Vec3 position = core::Vec3::zero();
    core::Vec3 velocity = core::Vec3::zero();
    // Downward acceleration, m/s^2. A fireball's is the tuning's; a ground
    // defence's bolt is heavier and arcs visibly.
    float gravity = 0.0f;
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
    // External hostiles (the M14 bots) reuse this slot for everything combat
    // knows how to do -- health, lock-on, projectile sweeps, hit flash, HUD
    // brackets, kill counting -- while their position and velocity are driven
    // by a real FlightModel outside, and they fire through fire_hostile()
    // instead of the orbit timer.
    bool external = false;
    // A training dummy: never fires, holds still, and comes back quickly.
    // The room to learn the reach of a bite in, without being shot at.
    bool passive = false;
    // A ground defence: pinned to the terrain, fires the heavy bolt, and
    // does not come back once destroyed.
    bool ground = false;
    // Seconds of stun left after a melee hit. A stunned drone neither orbits
    // nor fires; a stunned external is flown by nobody (its owner reads this
    // and drops the pilot's controls).
    float stun = 0.0f;
    // Velocity a melee hit has added, m/s, waiting for the owner of an
    // external slot to apply to its flight model and zero. Drones take theirs
    // directly on the orbit centre.
    core::Vec3 knockback = core::Vec3::zero();
    // Hit-sphere radius. Drones use the tuned default; an external dragon's
    // body is its own size.
    float radius = 0.0f;
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
    // For delayed regeneration, mirroring the player's.
    float time_since_damage = 1e9f;
};

// A projectile ending its life somewhere visible, for impact effects.
struct Impact {
    core::Vec3 position = core::Vec3::zero();
    Team team = Team::Player;
    bool on_terrain = false;
};

// A flame active this frame, for rendering. The cone drawn is the cone that
// damages -- same origin, same axis.
struct BreathCone {
    core::Vec3 origin = core::Vec3::zero();
    core::Vec3 direction = core::Vec3::forward();
    // Who breathed it, as an opaque tag the caller assigns -- the renderer
    // uses it to pick the species' colours and particle character. Combat
    // itself never reads it.
    int source = -1;
    // The breather's species scales. Carried on the cone rather than looked up,
    // because combat has no idea what a species is and should not acquire one.
    BreathScales scales;
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
    // A fireball left the player's mouth this frame -- the rig's spit.
    bool fired = false;
    // The player swung this frame -- the rig's lunge -- and what, if anything,
    // it connected with.
    bool melee_swung = false;
    MeleeKind melee_hit = MeleeKind::None;
    // Where the best melee hit landed, for the impact burst.
    core::Vec3 melee_hit_position = core::Vec3::zero();
    // The gesture the swing was thrown with (toward the nearest target in
    // mind, a bite when there is none) and which side, for the rig.
    MeleeGesture melee_gesture = MeleeGesture::Bite;
    float melee_side = 1.0f;
    // Length of the chain the hit extended (1 for a first hit), for the HUD.
    int melee_combo = 0;
    // Some of the damage taken this frame was a bite or a strike, and the
    // velocity it added to the player (the owner applies it).
    bool bitten = false;
    core::Vec3 knockback = core::Vec3::zero();
};

// The combat core: player resources, projectiles, and the targets to use them
// on. Owns no rendering and no input, so a bot can drive it with the same
// CombatInput the player produces.
class Combat {
public:
    CombatTuning tuning;

    // `terrain` may be null, in which case projectiles ignore the ground.
    // `wave` drones are spawned around the centre; a run passes 0 -- the
    // first run build left the default five in, and they were noise.
    void reset(const Terrain* terrain, core::Vec3 arena_centre, uint32_t seed, int wave = 5);

    // `player` is read, never written: boost is reported back through
    // boost_active() and applied by the flight model, so thrust stays in one
    // place.
    CombatEvents update(float dt, const FlightState& player, const CombatInput& input);

    // Called when the player respawns, to restore resources without rebuilding
    // the arena.
    void revive();
    // Health back, capped at the maximum (a kill's reward in a run, and the
    // top-up when growth raises the maximum).
    void heal(float amount);

    // ---- player state ----
    float health() const { return health_; }
    float health_fraction() const { return tuning.max_health > 0.0f ? health_ / tuning.max_health : 0.0f; }
    float breath() const { return breath_; }
    bool breathing() const { return breathing_; }
    bool alive() const { return health_ > 0.0f; }
    // 0 when ready, 1 immediately after use.
    float fire_cooldown() const;
    float boost_cooldown() const;
    float melee_cooldown() const;
    // Current melee chain length (0 when no chain is live), for the HUD.
    int melee_combo() const { return combo_timer_ > 0.0f ? combo_ : 0; }
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

    // Where fire leaves the player this frame -- the mouth when the app has set
    // it, a body offset otherwise. Public because the HUD draws the aim marker
    // from the same point the rounds actually use.
    core::Vec3 muzzle(const FlightState& player) const;

    // Tip and axis of the breath cone this frame. Only meaningful while
    // breathing(); the renderer uses it directly so the flame drawn and the
    // volume that damages can never disagree.
    core::Vec3 breath_origin() const { return breath_origin_; }
    core::Vec3 breath_direction() const { return breath_direction_; }

    // Spawns a ring of sentinels around the arena centre. Called by reset, and
    // again from the UI to restock.
    void spawn_wave(int count);
    // The training room: replaces the targets with a line of passive dummies
    // ahead of `origin` along `forward`, staggered to either side, so a
    // straight flight passes one after another inside bite or strike reach.
    // They never fire, hold still, take a beating and come back in seconds.
    void spawn_training(core::Vec3 origin, core::Vec3 forward, core::Vec3 right);
    // A ground defence at `position` (already on the terrain), added to
    // whatever targets exist.
    void spawn_defence(core::Vec3 position);

    // ---- external hostiles (bots) ----
    // Claims a slot; returns its index into sentinels().
    int spawn_external(float health, float radius);
    // Per-frame: where the bot's flight model actually put it.
    void drive_external(int index, core::Vec3 position, core::Vec3 velocity);
    // A hostile round fired by an external pilot.
    void fire_hostile(core::Vec3 position, core::Vec3 velocity, float damage);
    // The bot flew into a mountain; combat records the kill the usual way.
    void kill_external(int index);
    // Lesser terrain scrapes cost health through the same accounting.
    void damage_external(int index, float amount);
    void clear_hostiles();
    // An external pilot breathing fire this frame. Buffered and resolved
    // against the player inside update(), so damage attribution and events go
    // through the one path that owns them.
    void hostile_breath(core::Vec3 origin, core::Vec3 direction, int source = -1,
                        BreathScales scales = {});
    // An external pilot biting or striking this frame: its mouth, the way it
    // faces, and its body centre. Resolved against the player in update().
    void hostile_melee(core::Vec3 mouth, core::Vec3 forward, core::Vec3 body);

    // The player's species scales, set by the app whenever the model changes.
    // Public like `tuning` is: it is data the owner sets, not state combat
    // evolves.
    BreathScales player_breath;
    // Last frame's hostile flames, for drawing.
    const std::vector<BreathCone>& hostile_breaths() const { return hostile_breaths_drawn_; }
    // Projectiles that ended this frame -- hits and terrain strikes -- for the
    // renderer to detonate.
    const std::vector<Impact>& impacts() const { return impacts_; }

    // The fire actually leaves the dragon's MOUTH, which the rig animates; the
    // app tells combat where that is each frame. Without an override the
    // muzzle falls back to a fixed body offset.
    void set_muzzle(core::Vec3 world_position) {
        muzzle_override_ = world_position;
        has_muzzle_override_ = true;
    }

private:
    void fire_projectile(core::Vec3 position, core::Vec3 velocity, float damage, float radius,
                         float blast, Team team, float gravity);
    void fire_projectile(core::Vec3 position, core::Vec3 velocity, float damage, float radius,
                         float blast, Team team);
    void update_projectiles(float dt, const FlightState& player, CombatEvents& events);
    void update_sentinels(float dt, const FlightState& player, CombatEvents& events);
    void apply_breath(float dt, const FlightState& player, CombatEvents& events);
    void apply_melee(const FlightState& player, CombatEvents& events);
    void damage_sentinel(Sentinel& sentinel, float amount, CombatEvents& events);
    float random_unit();
    void update_lock(const FlightState& player);
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
    float melee_timer_ = 0.0f;
    int combo_ = 0;
    float combo_timer_ = 0.0f;
    float boost_timer_ = 0.0f;
    float boost_cooldown_timer_ = 0.0f;
    int kills_ = 0;
    int locked_ = -1;
    bool want_cycle_ = false;
    core::Vec3 lock_intercept_ = core::Vec3::zero();

    std::vector<Impact> impacts_;
    std::vector<BreathCone> hostile_breaths_pending_;
    std::vector<BreathCone> hostile_breaths_drawn_;
    std::vector<MeleeSwing> hostile_melee_pending_;
    core::Vec3 muzzle_override_ = core::Vec3::zero();
    bool has_muzzle_override_ = false;

    core::Vec3 breath_origin_ = core::Vec3::zero();
    core::Vec3 breath_direction_ = core::Vec3::forward();

    uint32_t rng_ = 1u;
};

// Whether `point` lies inside the cone with the given tip, axis, half angle and
// length. Exposed because it is the one piece of combat geometry worth testing
// directly, and because targeting will want it later.
bool point_in_cone(core::Vec3 point, core::Vec3 tip, core::Vec3 axis, float half_angle_radians,
                   float length);

// Which melee weapon reaches a target of `target_radius` at `target` from a
// dragon whose mouth is at `mouth`, facing `forward`, with its body centred at
// `body`. The bite cone is tested against the target's near surface, like the
// breath, so a target the jaws visibly close on counts. Exposed for tests and
// for the bots, which decide with the same geometry the player's swing uses.
MeleeKind melee_reach(core::Vec3 mouth, core::Vec3 forward, core::Vec3 body, core::Vec3 target,
                      float target_radius, const CombatTuning& tuning);

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
