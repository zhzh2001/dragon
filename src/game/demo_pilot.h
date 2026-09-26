#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "game/autopilot.h"
#include "game/bot.h"
#include "game/flight.h"
#include "game/maneuver.h"

namespace game {

class Terrain;

// The hands-off player: plays the run and the arena through the same
// FlightInput and combat buttons a person uses, for a demo, a soak, and a
// hands-off mode (P). It is a decision layer, not a new AI: dogfights are
// flown by a BotPilot aimed at the chosen target, cruising and landing by the
// rally autopilot's steering, walking by the ground walk.
//
// The app describes the world each frame (DemoWorld) and applies the
// decision; this owns only its own rhythm, so it is testable without an app.

enum class DemoTargetKind : int { Tower, Rival, Hunter, Drone };

struct DemoTarget {
    core::Vec3 position = core::Vec3::zero();
    core::Vec3 velocity = core::Vec3::zero();
    DemoTargetKind kind = DemoTargetKind::Drone;
    int slot = -1;         // the combat slot, for matching the lock
    bool dormant = false;  // a run's rival still at its post
    float health = 1.0f;   // 0..1
};

struct DemoWorld {
    const Terrain* terrain = nullptr;
    std::vector<DemoTarget> targets;
    // Where to fly when nothing else wants doing: the corridor ahead in a run,
    // a patrol point in the arena.
    core::Vec3 waypoint = core::Vec3::zero();
    // The run's next cache, when there is one worth going for.
    bool has_cache = false;
    core::Vec3 cache = core::Vec3::zero();
    float cache_radius = 26.0f;
    // Index into `targets` of the tower guarding it, or -1.
    int cache_guard = -1;
    bool in_run = false;
    // Bank what was taken: head for the pass without taking side objectives.
    bool rush = false;
    // Where to fall back to and heal: up the corridor, high, away from towers.
    core::Vec3 safe_point = core::Vec3::zero();
    // The nearest meal (a grazer or a carcass), when there is one.
    bool has_prey = false;
    core::Vec3 prey = core::Vec3::zero();
    core::Vec3 prey_velocity = core::Vec3::zero();
    float health_fraction = 1.0f;
    int locked_slot = -1;
    // The player's actual lock acquisition limits, not a stronger pilot assist.
    float lock_cone_deg = 0.0f;
    float lock_range = 0.0f;
};

enum class DemoState : int {
    Cruise,
    Fight,    // a dragon or a drone, through the bot pilot
    Siege,    // a tower: dive, fire, pull up, extend, come round
    Land,     // descend onto a cache
    Walk,     // on the ground, into the ring
    Collect,  // standing in the ring
    TakeOff,
    Flee,
    Hunt,     // a low pass through a herd, snatching one
};
constexpr int DEMO_STATES = 9;
const char* demo_state_name(DemoState state);

struct DemoDecision {
    FlightInput flight;
    bool cycle_target = false;  // tap the same relock button as the player
    bool fire = false;
    bool breath = false;
    bool melee = false;
    bool boost = false;
    // A flip or roll the fight asked for; the caller's Maneuver flies it.
    ManeuverKind maneuver = ManeuverKind::None;
    float maneuver_direction = 1.0f;
};

struct DemoTuning {
    // Bank a representative cache each valley before further stops let the
    // hunters catch up. This is a pilot objective, not a collection rule.
    int caches_before_rush = 1;
    // Pace relock taps; keep a margin inside the player's acquisition cone.
    float relock_interval = 0.2f;
    float relock_cone_fraction = 0.8f;
    // Spend the player's boost once airborne to leave the slow climb-out.
    float takeoff_boost_height = 8.0f;
    float takeoff_boost_speed = 45.0f;
    // Keep boost for straight flight: boosting across a turn overshoots it.
    float cruise_boost_cone_deg = 30.0f;
    // Who is worth turning for. Objective first: a fight is taken when the
    // dragon is on you, not hunted down from a kilometre -- the pilot that
    // engaged at 500 and 1100 m spent seven minutes in dogfights with marks
    // that regenerated between passes, and never reached the pass.
    float fight_hunter_range = 350.0f;
    float fight_rival_range = 250.0f;
    float fight_dormant_range = 260.0f;
    float fight_drone_range = 900.0f;
    // Low on health: fall back to the safe point and circle there until the
    // regeneration has done its work -- pressing on at a sliver of health
    // into towers was how every early demo run ended.
    float flee_health = 0.35f;
    float recover_health = 0.75f;
    // Go for a cache within this; siege its guard first.
    float cache_range = 1700.0f;
    // Siege: fire inside this range and cone, pull up inside `siege_pull_up`
    // or below `siege_floor` of clearance, then extend and come round.
    float siege_fire_range = 480.0f;
    float siege_breath_range = 170.0f;
    float siege_cone_deg = 9.0f;
    float siege_pull_up = 70.0f;
    // Where a run in starts: this far out from the tower, this high over it.
    float siege_entry_range = 600.0f;
    float siege_entry_height = 170.0f;
    // A siege that has not killed its tower in this long gives the cache up
    // and flies on: in a canyon the entry could sit up a wall, and one tower
    // took two and a half minutes of a seven-minute run.
    float siege_budget = 60.0f;
    // Above the tower's 16 m, well under a dive's: at 60 it pulled up at
    // 224 m, before the flame was ever in reach.
    float siege_floor = 25.0f;
    // A weave across the run-in so a tower's lead is always wrong.
    float siege_jink = 45.0f;       // m
    float siege_jink_rate = 1.4f;   // rad/s
    // A grounded dragon walks this far to an unguarded cache (about 35 s);
    // further, it climbs out and comes back in on a proper approach. A hop
    // in between exited at 12 m, started the approach far too low, stalled
    // short and hopped again -- the second cycle after a first hoard.
    float walk_limit = 250.0f;
    // The climb-out after a take-off: it stays the job until the dragon is
    // this high and this fast. Handing over to the cruise straight after the
    // leap pitched a 10 m/s dragon up at the corridor 150 m overhead; it
    // stalled back onto the ground, leapt, stalled -- the cycle after the
    // first hoard -- or skated along the floor at 40 m/s for half a minute.
    //
    // Measured on the flight model (a scratch probe, full flap from a
    // standstill): below about 15 degrees a dragon never leaves ground
    // effect; at 20 it climbs about 2 m/s at a steady 19 m/s, drake or young.
    // So the climb-out is a HEIGHT, not a speed -- an exit at 28 m/s was
    // never reached and the take-off never ended.
    float climb_out_height = 35.0f;
    float climb_attitude = 0.36f;        // rad
    float climb_attitude_rising = 0.45f; // against rising ground

    // A take-off that has not got up in this long hands over to the cruise
    // and the stall guard rather than trying the same thing forever.
    float takeoff_budget = 25.0f;
    float siege_extend = 14.0f;  // s, at most, out to the far side
    // Landing: the glide path, the brake, and the touchdown.
    float land_slope = 0.22f;   // height above the cache per metre out
    float land_brake_range = 450.0f;
    float land_speed = 20.0f;   // m/s over the cache
    float land_abort = 160.0f;  // m past the cache while airborne: go round
    float go_round_time = 6.0f;
    // How long a decision holds before it is revisited, so it does not
    // flicker between two jobs every frame.
    float decide_interval = 0.4f;
    // A fight that is going nowhere: no damage on the mark for this long
    // (it regenerates between passes), and the pilot breaks off from that
    // one for `disengage_time` and flies on -- the first demo traded with a
    // rival for a minute and a half and never reached the pass.
    float stalemate_time = 20.0f;
    float disengage_time = 30.0f;
    float disengaged_close = 130.0f;  // unless it comes this close
    // A rush shoots on the way through; it never turns back to pursue.
    float rush_fight_range = 0.0f;
    // Energy: below `min_speed` in the air (and not on the last stretch of a
    // landing) the nose goes down and the wings beat until `recover_speed`.
    // The dogfight climbed after its mark to a stall at 6 m/s, and a stalled
    // dragon is the easiest target in the valley.
    // Under a fresh climb-out's 19 m/s, so the guard does not dive a dragon
    // that has just got up.
    float min_speed = 16.0f;
    float recover_speed = 28.0f;
    // Prey: a herd this near, and nearer than the next cache, is worth a
    // pass. The glide comes down to `hunt_height` over it (inside the swoop
    // reach) and a pass that misses rests the hunt for `hunt_rest` -- a
    // pilot that circled a herd for ever would never reach the pass.
    float hunt_range = 650.0f;
    float hunt_height = 6.5f;
    float hunt_budget = 25.0f;
    float hunt_rest = 20.0f;
    // The pass never goes below this clearance and holds this speed: the
    // first cut came down to 2 m, touched, and spent the run cycling from
    // hunt to take-off and back (241 s of take-off in one demo run). A
    // touchdown during a hunt rests it for `hunt_touchdown_rest`.
    float hunt_floor = 5.0f;
    float hunt_speed = 26.0f;
    float hunt_touchdown_rest = 45.0f;
};

class DemoPilot {
public:
    DemoTuning tuning;
    BotTuning fight_tuning;
    AutopilotTuning steering;

    void reset(uint32_t seed);
    DemoDecision update(float dt, const FlightState& self, const DemoWorld& world);

    DemoState state() const { return state_; }
    // The combat slot being fought or besieged, or -1.
    int target() const { return target_; }
    // For the telemetry: siege shots asked for, and the closest the tower
    // came to the nose on the last run in (degrees).
    int siege_shots = 0;
    // Seconds spent in each job, for the telemetry: where a run's time goes.
    float time_in[DEMO_STATES] = {};
    mutable float siege_best_off_axis_deg = 180.0f;
    float siege_last_range = 0.0f;

private:
    void decide(const FlightState& self, const DemoWorld& world);
    DemoDecision act(float dt, const FlightState& self, const DemoWorld& world);
    float ground_ahead(const FlightState& self, const DemoWorld& world) const;
    DemoDecision cruise(const FlightState& self, const DemoWorld& world, core::Vec3 aim,
                        bool boost);
    DemoDecision fight(float dt, const FlightState& self, const DemoWorld& world);
    DemoDecision siege(float dt, const FlightState& self, const DemoWorld& world);
    DemoDecision land(const FlightState& self, const DemoWorld& world);
    DemoDecision walk(const FlightState& self, const DemoWorld& world);
    DemoDecision hunt(const FlightState& self, const DemoWorld& world);
    float hunt_rest_timer_ = 0.0f;
    float relock_timer_ = 0.0f;

    DemoState state_ = DemoState::Cruise;
    int target_ = -1;
    float decide_timer_ = 0.0f;
    float extend_timer_ = 0.0f;
    core::Vec3 extend_point_ = core::Vec3::zero();
    float go_round_timer_ = 0.0f;
    float state_time_ = 0.0f;
    BotPilot fighter_;
    bool recovering_ = false;
    int fight_slot_ = -1;
    float fight_best_health_ = 1.0f;
    float fight_since_progress_ = 0.0f;
    int disengaged_slot_ = -1;
    float disengage_timer_ = 0.0f;
    bool run_in_ = false;  // a siege lined up at its entry point and committed
    bool entry_set_ = false;
    int siege_slot_ = -1;
    float siege_elapsed_ = 0.0f;
    core::Vec3 skipped_cache_ = core::Vec3{1e9f, 1e9f, 1e9f};
    core::Vec3 entry_point_ = core::Vec3::zero();
};

}  // namespace game
