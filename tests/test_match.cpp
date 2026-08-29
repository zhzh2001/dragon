// Match loop invariants: phases advance in order, weapons are cold outside the
// fight, scores end the match exactly at the target, and the clock resolves a
// standoff honestly.
#include <cstdio>

#include "game/combat.h"
#include "game/match.h"

using game::CombatEvents;
using game::Match;
using game::MatchPhase;

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

CombatEvents kill_event() {
    CombatEvents events;
    events.kills = 1;
    return events;
}

CombatEvents death_event() {
    CombatEvents events;
    events.player_died = true;
    return events;
}

void run_seconds(Match& match, float seconds, const CombatEvents& events = {}) {
    for (int i = 0; i < int(seconds * 60.0f); ++i) {
        match.update(1.0f / 60.0f, i == 0 ? events : CombatEvents{});
    }
}

void test_phases_and_win() {
    std::printf("countdown -> fight -> victory at the kill target\n");
    Match match;
    match.settings.target_kills = 3;
    match.settings.countdown = 3.0f;
    match.settings.time_limit = 0.0f;

    CHECK(match.phase() == MatchPhase::Idle);
    CHECK(match.weapons_live());  // free play is armed

    match.start();
    CHECK(match.phase() == MatchPhase::Countdown);
    CHECK(!match.weapons_live());
    // Kills during the countdown must not count (weapons are cold anyway, but
    // the scorer itself refuses them).
    match.update(1.0f / 60.0f, kill_event());
    CHECK(match.player_kills() == 0);

    run_seconds(match, 3.1f);
    CHECK(match.phase() == MatchPhase::Fighting);
    CHECK(match.weapons_live());

    match.update(1.0f / 60.0f, kill_event());
    match.update(1.0f / 60.0f, kill_event());
    CHECK(match.phase() == MatchPhase::Fighting);
    CHECK(match.player_kills() == 2);
    match.update(1.0f / 60.0f, kill_event());
    CHECK(match.phase() == MatchPhase::Results);
    CHECK(match.player_won());
    CHECK(!match.draw());
    CHECK(!match.weapons_live());

    // Scores freeze on the results screen.
    match.update(1.0f / 60.0f, kill_event());
    CHECK(match.player_kills() == 3);

    // Rematch resets everything.
    match.start();
    CHECK(match.player_kills() == 0);
    CHECK(match.phase() == MatchPhase::Countdown);
}

void test_defeat_and_clock() {
    std::printf("defeat at the death target; the clock favours the leader\n");
    Match match;
    match.settings.target_kills = 2;
    match.settings.countdown = 0.0f;
    match.settings.time_limit = 0.0f;
    match.start();
    CHECK(match.phase() == MatchPhase::Fighting);  // zero countdown skips ahead
    match.update(1.0f / 60.0f, death_event());
    match.update(1.0f / 60.0f, death_event());
    CHECK(match.phase() == MatchPhase::Results);
    CHECK(!match.player_won());

    // Time expiry: ahead wins, tied draws.
    Match timed;
    timed.settings.target_kills = 50;
    timed.settings.countdown = 0.0f;
    timed.settings.time_limit = 5.0f;
    timed.start();
    timed.update(1.0f / 60.0f, kill_event());
    run_seconds(timed, 5.2f);
    CHECK(timed.phase() == MatchPhase::Results);
    CHECK(timed.player_won());

    Match tied;
    tied.settings.target_kills = 50;
    tied.settings.countdown = 0.0f;
    tied.settings.time_limit = 5.0f;
    tied.start();
    run_seconds(tied, 5.2f);
    CHECK(tied.phase() == MatchPhase::Results);
    CHECK(tied.draw());

    // Abandon returns to armed free play.
    tied.abandon();
    CHECK(tied.phase() == MatchPhase::Idle);
    CHECK(tied.weapons_live());
}

void test_bot_regeneration() {
    std::printf("external hostiles heal after a lull, at the tuned rate\n");
    game::Combat combat;
    combat.reset(nullptr, core::Vec3::zero(), 83u);
    combat.tuning.hostile_regen = 10.0f;
    combat.tuning.hostile_regen_delay = 1.0f;
    for (auto& sentinel : combat.sentinels()) {
        sentinel.alive = false;
        sentinel.respawn_timer = 1e6f;
    }
    const int slot = combat.spawn_external(100.0f, 6.5f);
    combat.drive_external(slot, core::Vec3{0.0f, 0.0f, -400.0f}, core::Vec3::zero());
    combat.damage_external(slot, 40.0f);

    game::FlightState player;
    auto health = [&]() { return combat.sentinels()[size_t(slot)].health; };
    CHECK(health() == 60.0f);

    // Inside the delay: nothing.
    for (int i = 0; i < 30; ++i) combat.update(1.0f / 60.0f, player, game::CombatInput{});
    CHECK(health() < 60.5f);

    // Past it: ~10/s, capped at max.
    for (int i = 0; i < 120; ++i) combat.update(1.0f / 60.0f, player, game::CombatInput{});
    CHECK(health() > 68.0f);
    for (int i = 0; i < 600; ++i) combat.update(1.0f / 60.0f, player, game::CombatInput{});
    CHECK(health() <= 100.0f + 1e-3f);
    CHECK(health() > 99.0f);

    // Non-external drones do not heal (they are practice targets).
    game::Combat drones;
    drones.reset(nullptr, core::Vec3::zero(), 89u);
    drones.tuning.hostile_regen = 10.0f;
    drones.sentinels()[0].fire_timer = 1e6f;
    drones.damage_external(0, 0.0f);  // no-op; damage via the real path
    auto& drone = drones.sentinels()[0];
    drone.health = 20.0f;
    drone.time_since_damage = 0.0f;
    for (int i = 0; i < 600; ++i) drones.update(1.0f / 60.0f, player, game::CombatInput{});
    CHECK(drone.health == 20.0f);
}

}  // namespace

int main() {
    test_phases_and_win();
    test_defeat_and_clock();
    test_bot_regeneration();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
