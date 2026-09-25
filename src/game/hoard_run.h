#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "game/course.h"
#include "game/flight.h"

namespace game {

class Terrain;
struct CombatEvents;
struct TerrainSettings;

// The run probe (DIRECTION.md row 3): one generated valley flown from its head
// to the pass at the far end, under pressure, for a hoard.
//
// Pure layout and scorekeeping. Given a terrain and a seed it places the
// encounters along the corridor the terrain already carves -- rival dragons,
// ground defences, hoard caches, the pass gate -- and given the player's
// flight state each frame it collects, banks and loses. It owns no bots, no
// combat and no rendering: the app spawns a rival where the layout says one
// stands, and a defence where it says one fires. That is what makes the whole
// loop testable without an app, exactly like the match.
//
// What the probe asks: is flying the corridor under pressure more fun than
// the free-form match, and do altitude and route matter? So the layout makes
// the routes differ. Caches lie low in the corridor, defences sit on the
// slopes beside it with a range that a ridge-hugging flight stays above, and
// rivals hold posts over the corridor until the player comes within their
// engage range.
enum class HoardPhase : int {
    Idle,    // no run; free play or the arena
    Flying,  // on the corridor
    Banked,  // crossed the pass with the hoard
    Lost,    // died; the hoard is gone
};

// What kind of valley a seed deals. The first playtest found that seeds did
// not change the run: the terrain never changed and the encounters were one
// template jittered. A kind reshapes the valley itself and the encounter mix.
enum class ValleyKind : int {
    Vale,      // wide floor, gentle bends, a balanced mix
    Canyon,    // narrow, tall walls, sharp bends; richer caches
    Gauntlet,  // many towers, one rival, four caches
    Nest,      // many rivals, no slope towers
    Count,
};
const char* valley_kind_name(ValleyKind kind);
ValleyKind valley_kind_for(uint32_t seed);

// Growth: the dragon you land at the pass is not the one you launched.
// Hoard taken and bounties earned feed a meter; each stage moves the tuning
// (the app applies it -- this only decides the stage).
enum class GrowthStage : int { Drake = 0, Young, Adult, Count };
const char* growth_stage_name(GrowthStage stage);

struct HoardRunSettings {
    uint32_t seed = 7;
    int rivals = 3;
    // Towers on the slopes, beyond the one that guards each cache.
    int defences = 2;
    int caches = 3;
    // Each cache has a tower within this distance of it, so taking the hoard
    // safely means clearing its guard first -- the towers were ignorable, and
    // ignoring everything was the best strategy.
    bool guards = true;
    float guard_min = 70.0f;
    float guard_max = 120.0f;
    // Bounties, in hoard, and health back on a kill. Fighting used to earn
    // nothing, so the rational run was avoidance.
    float bounty_tower = 40.0f;
    float bounty_rival = 60.0f;
    float bounty_hunter = 120.0f;
    float heal_on_kill = 20.0f;
    // Growth thresholds, in hoard gathered (banked or not).
    float grow_young = 140.0f;
    float grow_adult = 380.0f;
    // Hoard per cache, before the depth bonus (deeper caches pay up to 50%
    // more, so the far end of the corridor is worth the risk).
    float cache_value = 100.0f;
    // A cache is collected by being grounded inside this radius of it for
    // this long. Leaving lets the progress drain at twice the rate.
    float cache_radius = 26.0f;
    float collect_time = 2.5f;
    float pass_radius = 70.0f;
    // A rival wakes when the player comes within this of its post.
    float engage_range = 520.0f;
    // The dragonslayers. One at a time: the next is loosed `pressure_interval`
    // after the last one died, up to the cap. The first comes at
    // `pressure_scale` times the corridor's straight flight time at cruise --
    // two minutes was sooner than a straight flight took, so they arrived as
    // you landed for the first cache. `pressure_after` > 0 overrides it.
    float pressure_after = 0.0f;
    float pressure_scale = 1.6f;
    float cruise_speed = 45.0f;
    float pressure_interval = 50.0f;
    int max_hunters = 3;
    // How far out from the corridor's spine the defences stand, and how far
    // the caches lie off it.
    float defence_offset_min = 160.0f;
    float defence_offset_max = 320.0f;
    float cache_offset_max = 110.0f;
};

struct RunCache {
    core::Vec3 position = core::Vec3::zero();  // on the ground
    float value = 0.0f;
    bool collected = false;
    float progress = 0.0f;  // 0..1 of the collect time
};

struct RunRival {
    core::Vec3 position = core::Vec3::zero();  // the post it holds
    core::Vec3 facing = core::Vec3::forward();
    bool engaged = false;
};

struct RunDefence {
    core::Vec3 position = core::Vec3::zero();
    // The cache this tower guards, or -1 for a slope tower.
    int guards = -1;
};

struct RunLayout {
    uint32_t seed = 0;
    ValleyKind kind = ValleyKind::Vale;
    // The corridor, head to pass, at flying height above the valley floor.
    std::vector<core::Vec3> spine;
    std::vector<RunCache> caches;
    std::vector<RunRival> rivals;
    std::vector<RunDefence> defences;
    Ring gate;
    core::Vec3 start = core::Vec3::zero();
    core::Vec3 start_look = core::Vec3::forward();

    // Straight-line length of the spine.
    float length() const;
    // Fraction along the spine of the point nearest `position`, 0..1.
    float fraction_at(core::Vec3 position) const;
};

RunLayout generate_run_layout(const Terrain& terrain, float half_extent,
                              const HoardRunSettings& settings);

// A seed's valley: its kind, the terrain shape (terrain seed, meander phase,
// width, walls) written into `terrain` over the arena's, and the encounter
// mix written into `run`. Both start from the caller's defaults, so the dials
// still move every kind together.
ValleyKind apply_valley_kind(uint32_t seed, TerrainSettings& terrain, HoardRunSettings& run);

struct RunResult {
    uint32_t seed = 0;
    ValleyKind kind = ValleyKind::Vale;
    GrowthStage stage = GrowthStage::Drake;
    bool banked = false;
    float hoard = 0.0f;    // what was banked; 0 on a loss
    float carried = 0.0f;  // what was held when the run ended
    int caches = 0;
    int kills = 0;
    int hunters = 0;
    float time = 0.0f;
    float distance = 0.0f;
};

// The best previous run's numbers, beside the results. Flat `key value` text
// like the flight tuning and the best times.
class RunRecords {
public:
    bool load(const char* path);
    bool save(const char* path) const;
    // Returns true if this run set a hoard record.
    bool submit(const RunResult& result);

    int runs = 0;
    int banked = 0;
    float best_hoard = 0.0f;
    float best_time = 0.0f;  // fastest banked run; 0 if none
    float last_hoard = 0.0f;
};

class HoardRun {
public:
    HoardRunSettings settings;

    // Generates the layout for `settings.seed` and starts flying it.
    void start(const Terrain& terrain, float half_extent);
    void abandon();

    // `player_alive` is combat's word on the matter; `events` carries the kills
    // and the death. Collection reads `player.grounded` and the position.
    void update(float dt, const FlightState& player, bool player_alive,
                const CombatEvents& events);

    HoardPhase phase() const { return phase_; }
    const char* phase_name() const;
    const RunLayout& layout() const { return layout_; }
    RunLayout& layout() { return layout_; }
    float hoard() const { return hoard_; }
    float elapsed() const { return elapsed_; }
    float distance() const { return distance_; }
    int caches_collected() const;
    int kills() const { return kills_; }
    // Bounty for a kill the app has identified; feeds the hoard and growth.
    void award(float amount);
    // Growth: everything gathered so far, and the stage it buys.
    float growth() const { return growth_; }
    GrowthStage stage() const;
    // 0..1 toward the next stage (1 at the top).
    float growth_progress() const;
    bool just_grew() const { return just_grew_; }
    // The app says whether a hunter is still flying; the next is scheduled
    // from when the last one fell.
    void set_hunter_alive(bool alive);
    // When the first hunter comes, in run seconds.
    float pressure_start() const { return pressure_start_; }
    int hunters_loosed() const { return hunters_; }
    // The cache the player is grounded at, or -1, and how far along.
    int collecting() const { return collecting_; }
    float collect_progress() const;
    // Nearest uncollected cache to `from`, or -1.
    int nearest_cache(core::Vec3 from) const;
    // A rival was hurt or otherwise provoked: it fights from now on.
    void engage_rival(int index);
    // The spine point ahead of `from`, or the gate: what an autopilot flies at.
    core::Vec3 next_waypoint(core::Vec3 from) const;

    // ---- one-frame flags ----
    bool just_collected() const { return just_collected_; }
    bool just_banked() const { return just_banked_; }
    bool just_lost() const { return just_lost_; }
    // A hunter should be spawned behind the player now. Consumes the flag.
    bool take_hunter_request();

    RunResult result() const;

private:
    void finish(bool banked);

    HoardPhase phase_ = HoardPhase::Idle;
    RunLayout layout_;
    float hoard_ = 0.0f;
    float elapsed_ = 0.0f;
    float distance_ = 0.0f;
    int kills_ = 0;
    int hunters_ = 0;
    int collecting_ = -1;
    bool hunter_pending_ = false;
    bool hunter_alive_ = false;
    float next_hunter_ = 0.0f;
    float pressure_start_ = 0.0f;
    float growth_ = 0.0f;
    bool just_grew_ = false;
    GrowthStage last_stage_ = GrowthStage::Drake;
    bool just_collected_ = false;
    bool just_banked_ = false;
    bool just_lost_ = false;
    bool have_previous_ = false;
    core::Vec3 previous_position_ = core::Vec3::zero();
};

}  // namespace game
