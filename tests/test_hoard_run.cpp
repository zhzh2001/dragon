// The run probe's invariants: a seeded layout is deterministic and lands its
// encounters where the design says (caches low and dry, defences beside the
// corridor, rivals over it, the gate at the pass); collecting needs the ground
// and the time; the pass banks; death loses; the hunters come on the clock;
// and the records keep the best run.
#include <cstdio>
#include <cstdlib>

#include "game/combat.h"
#include "game/hoard_run.h"
#include "game/terrain.h"

using core::Vec3;
using game::CombatEvents;
using game::HoardPhase;
using game::HoardRun;
using game::RunLayout;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* what, int line) {
    ++g_checks;
    if (!condition) {
        std::printf("  FAIL (line %d): %s\n", line, what);
        ++g_failures;
    }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

game::TerrainSettings small_terrain() {
    game::TerrainSettings s;
    s.half_extent = 1500.0f;
    s.cell_size = 12.0f;
    return s;
}

game::FlightState flying_at(Vec3 position, Vec3 forward = Vec3::forward()) {
    game::FlightState state;
    state.position = position;
    state.orientation = core::look_rotation(forward, Vec3::up());
    state.velocity = forward * 40.0f;
    state.airspeed = 40.0f;
    return state;
}

void test_layout() {
    std::printf("layout: deterministic, and placed where the design says\n");
    game::Terrain terrain;
    terrain.generate(small_terrain());
    const float extent = terrain.settings().half_extent;
    const float water = terrain.settings().water_level;

    game::HoardRunSettings settings;
    settings.seed = 42;
    const RunLayout a = game::generate_run_layout(terrain, extent, settings);
    const RunLayout b = game::generate_run_layout(terrain, extent, settings);
    settings.seed = 43;
    const RunLayout c = game::generate_run_layout(terrain, extent, settings);

    CHECK(a.spine.size() >= 2);
    CHECK(a.caches.size() == 3);
    CHECK(a.defences.size() == 4);
    CHECK(a.rivals.size() == 3);
    CHECK(a.length() > extent);
    // Same seed, same valley.
    CHECK(core::distance(a.caches[0].position, b.caches[0].position) < 1e-3f);
    CHECK(core::distance(a.rivals[1].position, b.rivals[1].position) < 1e-3f);
    // Another seed, another layout.
    CHECK(core::distance(a.caches[0].position, c.caches[0].position) > 1.0f);

    // The spine runs head to pass along the valley, monotonic in z.
    for (size_t i = 1; i < a.spine.size(); ++i) CHECK(a.spine[i].z > a.spine[i - 1].z);
    // The gate is at the far end, and the start at the head.
    CHECK(core::distance(Vec3{a.gate.position.x, 0.0f, a.gate.position.z},
                         Vec3{a.spine.back().x, 0.0f, a.spine.back().z}) < 1.0f);
    CHECK(a.start.z < a.spine[1].z);
    CHECK(a.gate.radius == settings.pass_radius);

    // Caches: on dry ground, near the corridor, ordered down it and worth
    // more the deeper they lie.
    float previous_z = -1e9f;
    float previous_value = 0.0f;
    for (const game::RunCache& cache : a.caches) {
        CHECK(cache.position.y >= water + 3.0f);
        CHECK(std::abs(cache.position.y - terrain.height_at(cache.position.x, cache.position.z)) < 0.5f);
        CHECK(cache.position.z > previous_z);
        CHECK(cache.value > previous_value);
        previous_z = cache.position.z;
        previous_value = cache.value;
        // Off the spine by at most the offset dial (plus the spine's own bend).
        float nearest = 1e9f;
        for (const Vec3& p : a.spine) {
            nearest = core::minf(nearest, core::length(Vec3{p.x - cache.position.x, 0.0f,
                                                            p.z - cache.position.z}));
        }
        CHECK(nearest < settings.cache_offset_max + 120.0f);
    }
    // Defences: on the ground beside the corridor, alternating sides.
    for (size_t i = 0; i < a.defences.size(); ++i) {
        const Vec3 p = a.defences[i].position;
        CHECK(p.y >= water + 3.0f);
        CHECK(std::abs(p.y - 8.0f - terrain.height_at(p.x, p.z)) < 0.5f);
    }
    // Rivals hold posts over the corridor, facing back toward the head.
    for (const game::RunRival& rival : a.rivals) {
        CHECK(rival.position.y > terrain.height_at(rival.position.x, rival.position.z) + 100.0f);
        CHECK(rival.facing.z < 0.0f);
        CHECK(!rival.engaged);
    }
    // fraction_at walks the spine.
    CHECK(a.fraction_at(a.spine.front()) < 0.02f);
    CHECK(a.fraction_at(a.spine.back()) > 0.98f);
    CHECK(std::abs(a.fraction_at(a.spine[a.spine.size() / 2]) - 0.5f) < 0.1f);
}

void test_collect_bank_and_lose() {
    std::printf("collecting needs the ground and the time; the pass banks; death loses\n");
    game::Terrain terrain;
    terrain.generate(small_terrain());
    const float extent = terrain.settings().half_extent;

    HoardRun run;
    run.settings.seed = 42;
    run.settings.collect_time = 2.0f;
    CHECK(run.phase() == HoardPhase::Idle);
    run.start(terrain, extent);
    CHECK(run.phase() == HoardPhase::Flying);
    CHECK(run.hoard() == 0.0f);

    const game::RunCache& cache = run.layout().caches[0];
    const Vec3 at = cache.position + Vec3{0.0f, 2.0f, 0.0f};
    const float dt = 1.0f / 60.0f;

    // Hovering over it does nothing.
    game::FlightState hover = flying_at(at);
    hover.grounded = false;
    for (int i = 0; i < 180; ++i) run.update(dt, hover, true, CombatEvents{});
    CHECK(run.caches_collected() == 0);
    CHECK(run.collecting() < 0);

    // Landed on it: progress fills over the collect time.
    game::FlightState landed = flying_at(at);
    landed.grounded = true;
    landed.velocity = Vec3::zero();
    for (int i = 0; i < 60; ++i) run.update(dt, landed, true, CombatEvents{});
    CHECK(run.collecting() == 0);
    CHECK(run.collect_progress() > 0.4f && run.collect_progress() < 0.6f);
    // Taking off drains it, faster than it filled.
    for (int i = 0; i < 20; ++i) run.update(dt, hover, true, CombatEvents{});
    CHECK(run.layout().caches[0].progress < 0.3f);
    // Land again and stay.
    bool collected_flag = false;
    for (int i = 0; i < 150; ++i) {
        run.update(dt, landed, true, CombatEvents{});
        collected_flag |= run.just_collected();
    }
    CHECK(collected_flag);
    CHECK(run.caches_collected() == 1);
    CHECK(run.hoard() == run.layout().caches[0].value);
    CHECK(run.nearest_cache(at) == 1);

    // Kills count.
    CombatEvents kill;
    kill.kills = 2;
    run.update(dt, hover, true, kill);
    CHECK(run.kills() == 2);

    // Crossing the gate banks the hoard. Approach from the head side along the
    // gate's normal and step through it.
    const game::Ring& gate = run.layout().gate;
    const Vec3 approach = gate.normal();
    game::FlightState before = flying_at(gate.position - approach * 20.0f, approach);
    run.update(dt, before, true, CombatEvents{});
    game::FlightState after = flying_at(gate.position + approach * 20.0f, approach);
    run.update(dt, after, true, CombatEvents{});
    CHECK(run.phase() == HoardPhase::Banked);
    CHECK(run.just_banked());
    const game::RunResult banked = run.result();
    CHECK(banked.banked);
    CHECK(banked.hoard == run.hoard());
    CHECK(banked.kills == 2);
    CHECK(banked.caches == 1);
    // Nothing more happens after the end.
    run.update(dt, after, true, kill);
    CHECK(run.kills() == 2);

    // A fresh run that dies loses the hoard.
    HoardRun doomed;
    doomed.settings.seed = 42;
    doomed.start(terrain, extent);
    for (int i = 0; i < 200; ++i) doomed.update(dt, landed, true, CombatEvents{});
    CHECK(doomed.hoard() > 0.0f);
    CombatEvents death;
    death.player_died = true;
    doomed.update(dt, landed, false, death);
    CHECK(doomed.phase() == HoardPhase::Lost);
    CHECK(doomed.just_lost());
    const game::RunResult lost = doomed.result();
    CHECK(!lost.banked);
    CHECK(lost.hoard == 0.0f);
    CHECK(lost.carried > 0.0f);
}

void test_engagement_and_hunters() {
    std::printf("rivals wake within engage range; hunters come on the clock\n");
    game::Terrain terrain;
    terrain.generate(small_terrain());
    const float extent = terrain.settings().half_extent;

    HoardRun run;
    run.settings.seed = 7;
    run.settings.pressure_after = 10.0f;
    run.settings.pressure_interval = 5.0f;
    run.settings.max_hunters = 2;
    run.start(terrain, extent);
    const float dt = 1.0f / 60.0f;

    // Far from every rival: none engaged, no hunters yet.
    game::FlightState far = flying_at(run.layout().start);
    for (int i = 0; i < 60; ++i) run.update(dt, far, true, CombatEvents{});
    int engaged = 0;
    for (const game::RunRival& r : run.layout().rivals) engaged += r.engaged ? 1 : 0;
    CHECK(engaged == 0);
    CHECK(run.hunters_loosed() == 0);
    CHECK(!run.take_hunter_request());

    // Fly up to the first rival's post.
    game::FlightState near = flying_at(run.layout().rivals[0].position + Vec3{0.0f, 0.0f, -300.0f});
    run.update(dt, near, true, CombatEvents{});
    CHECK(run.layout().rivals[0].engaged);
    // Provoking one works too.
    run.engage_rival(2);
    CHECK(run.layout().rivals[2].engaged);

    // The clock: past pressure_after a hunter is requested, then another an
    // interval later, then no more past the cap.
    while (run.elapsed() < 10.5f) run.update(dt, near, true, CombatEvents{});
    CHECK(run.hunters_loosed() == 1);
    CHECK(run.take_hunter_request());
    CHECK(!run.take_hunter_request());  // consumed
    while (run.elapsed() < 15.5f) run.update(dt, near, true, CombatEvents{});
    CHECK(run.hunters_loosed() == 2);
    CHECK(run.take_hunter_request());
    while (run.elapsed() < 30.0f) run.update(dt, near, true, CombatEvents{});
    CHECK(run.hunters_loosed() == 2);
    CHECK(!run.take_hunter_request());

    // The waypoint walks the spine toward the gate.
    const Vec3 w0 = run.next_waypoint(run.layout().start);
    CHECK(w0.z > run.layout().start.z);
    const Vec3 w_end = run.next_waypoint(run.layout().spine.back());
    CHECK(core::distance(w_end, run.layout().gate.position) < 1e-3f);
}

void test_records() {
    std::printf("records keep the best banked hoard and the fastest banked time\n");
    game::RunRecords records;
    game::RunResult lost;
    lost.banked = false;
    lost.carried = 250.0f;
    lost.time = 100.0f;
    CHECK(!records.submit(lost));
    CHECK(records.runs == 1 && records.banked == 0 && records.best_hoard == 0.0f);

    game::RunResult first;
    first.banked = true;
    first.hoard = 300.0f;
    first.time = 200.0f;
    CHECK(records.submit(first));
    CHECK(records.best_hoard == 300.0f && records.best_time == 200.0f);

    game::RunResult faster_poorer;
    faster_poorer.banked = true;
    faster_poorer.hoard = 150.0f;
    faster_poorer.time = 150.0f;
    CHECK(!records.submit(faster_poorer));
    CHECK(records.best_hoard == 300.0f && records.best_time == 150.0f);
    CHECK(records.runs == 3 && records.banked == 2);

    const char* path = "/tmp/dragon_test_runs.txt";
    CHECK(records.save(path));
    game::RunRecords loaded;
    CHECK(loaded.load(path));
    CHECK(loaded.runs == 3 && loaded.banked == 2 && loaded.best_hoard == 300.0f &&
          loaded.best_time == 150.0f && loaded.last_hoard == 150.0f);
    std::remove(path);
}

}  // namespace

int main() {
    test_layout();
    test_collect_bank_and_lose();
    test_engagement_and_hunters();
    test_records();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
