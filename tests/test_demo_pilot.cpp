// The demo pilot's choices: on the ground it collects, walks or leaps; in the
// air a hunter outranks a rival outranks a cache; a guarded cache is sieged
// first; low health falls back; a stalemate is broken off; a siege commits
// only facing the tower; and the speed guard puts the nose down.
#include <cstdio>

#include "game/demo_pilot.h"

using core::Vec3;
using game::DemoPilot;
using game::DemoState;
using game::DemoTarget;
using game::DemoTargetKind;
using game::DemoWorld;
using game::FlightState;

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

FlightState flying(Vec3 position, Vec3 forward = Vec3::forward(), float speed = 45.0f) {
    FlightState s;
    s.position = position;
    s.orientation = core::look_rotation(forward, Vec3::up());
    s.velocity = forward * speed;
    s.airspeed = speed;
    s.ground_clearance = position.y;
    return s;
}

DemoTarget target(Vec3 position, DemoTargetKind kind, int slot) {
    DemoTarget t;
    t.position = position;
    t.kind = kind;
    t.slot = slot;
    return t;
}

DemoWorld run_world() {
    DemoWorld w;
    w.in_run = true;
    w.waypoint = Vec3{0.0f, 200.0f, -2000.0f};
    w.safe_point = Vec3{0.0f, 300.0f, 1000.0f};
    return w;
}

void test_ground() {
    std::printf("on the ground: collect inside the ring, walk a short way, leap otherwise\n");
    DemoPilot pilot;
    pilot.reset(1);
    DemoWorld w = run_world();
    w.has_cache = true;
    w.cache = Vec3{0.0f, 0.0f, 0.0f};
    FlightState s = flying(Vec3{3.0f, 2.2f, 0.0f}, Vec3::forward(), 0.0f);
    s.grounded = true;
    game::DemoDecision d = pilot.update(1.0f / 60.0f, s, w);
    CHECK(pilot.state() == DemoState::Collect);
    CHECK(d.flight.walk == 0.0f && d.flight.flap == 0.0f);

    // 40 m out, the cache behind the dragon: turn and walk.
    s.position = Vec3{0.0f, 2.2f, -40.0f};
    DemoPilot walker;
    walker.reset(1);
    d = walker.update(1.0f / 60.0f, s, w);
    CHECK(walker.state() == DemoState::Walk);
    CHECK(d.flight.walk_turn != 0.0f);  // it has to turn round first

    // Far from it: take off, the flap's rising edge after the first frame.
    s.position = Vec3{0.0f, 2.2f, -400.0f};
    DemoPilot leaper;
    leaper.reset(1);
    d = leaper.update(1.0f / 60.0f, s, w);
    CHECK(leaper.state() == DemoState::TakeOff);
    CHECK(d.flight.flap == 0.0f);
    // The flap beats on and off (a leap on every rising edge) within half a second.
    bool beat = false;
    for (int i = 0; i < 30; ++i) {
        d = leaper.update(1.0f / 60.0f, s, w);
        beat |= d.flight.flap == 1.0f;
    }
    CHECK(beat);

    // Airborne but low, the climb-out holds -- until its budget runs out.
    FlightState low = flying(Vec3{0.0f, 10.0f, -400.0f}, Vec3::forward(), 18.0f);
    low.ground_clearance = 8.0f;
    for (int i = 0; i < 120; ++i) leaper.update(1.0f / 60.0f, low, w);
    CHECK(leaper.state() == DemoState::TakeOff);
    for (int i = 0; i < int(leaper.tuning.takeoff_budget * 60.0f); ++i) leaper.update(1.0f / 60.0f, low, w);
    CHECK(leaper.state() != DemoState::TakeOff);
}

void test_priorities() {
    std::printf("in the air: hunter over rival over cache; guarded caches are sieged; hurt falls back\n");
    DemoWorld w = run_world();
    w.has_cache = true;
    w.cache = Vec3{0.0f, 0.0f, -800.0f};
    const FlightState s = flying(Vec3{0.0f, 200.0f, 0.0f});

    DemoPilot pilot;
    pilot.reset(2);
    pilot.update(1.0f / 60.0f, s, w);
    CHECK(pilot.state() == DemoState::Land);

    // A guard tower beside the cache: siege it first.
    w.targets.push_back(target(Vec3{60.0f, 8.0f, -800.0f}, DemoTargetKind::Tower, 4));
    w.cache_guard = 0;
    DemoPilot sieger;
    sieger.reset(2);
    sieger.update(1.0f / 60.0f, s, w);
    CHECK(sieger.state() == DemoState::Siege);
    CHECK(sieger.target() == 4);

    // A rival close by outranks the cache.
    w.targets.push_back(target(Vec3{0.0f, 200.0f, -200.0f}, DemoTargetKind::Rival, 7));
    DemoPilot fighter;
    fighter.reset(2);
    fighter.update(1.0f / 60.0f, s, w);
    CHECK(fighter.state() == DemoState::Fight && fighter.target() == 7);
    // A dormant rival a little further out does not.
    w.targets.back().dormant = true;
    w.targets.back().position = Vec3{0.0f, 200.0f, -280.0f};
    DemoPilot passer;
    passer.reset(2);
    passer.update(1.0f / 60.0f, s, w);
    CHECK(passer.state() == DemoState::Siege);
    w.targets.back().dormant = false;
    w.targets.back().position = Vec3{0.0f, 200.0f, -200.0f};

    // A hunter further out still outranks the rival.
    w.targets.push_back(target(Vec3{0.0f, 200.0f, 330.0f}, DemoTargetKind::Hunter, 9));
    DemoPilot hunted;
    hunted.reset(2);
    hunted.update(1.0f / 60.0f, s, w);
    CHECK(hunted.state() == DemoState::Fight && hunted.target() == 9);

    // Hurt: fall back, and stay back until well recovered.
    w.health_fraction = 0.2f;
    DemoPilot hurt;
    hurt.reset(2);
    hurt.update(1.0f / 60.0f, s, w);
    CHECK(hurt.state() == DemoState::Flee);
    w.health_fraction = 0.5f;
    for (int i = 0; i < 60; ++i) hurt.update(1.0f / 60.0f, s, w);
    CHECK(hurt.state() == DemoState::Flee);
    w.health_fraction = 0.9f;
    for (int i = 0; i < 60; ++i) hurt.update(1.0f / 60.0f, s, w);
    CHECK(hurt.state() != DemoState::Flee);

    // Every cache taken: the rush ignores what is not on top of it.
    DemoWorld rush = run_world();
    rush.rush = true;
    rush.targets.push_back(target(Vec3{0.0f, 200.0f, -450.0f}, DemoTargetKind::Rival, 3));
    DemoPilot runner;
    runner.reset(2);
    runner.update(1.0f / 60.0f, s, rush);
    CHECK(runner.state() == DemoState::Cruise);
}

void test_stalemate_breaks_off() {
    std::printf("a fight with no damage for the stalemate time is broken off\n");
    DemoWorld w = run_world();
    w.targets.push_back(target(Vec3{0.0f, 200.0f, -200.0f}, DemoTargetKind::Rival, 5));
    FlightState s = flying(Vec3{0.0f, 200.0f, 0.0f});
    DemoPilot pilot;
    pilot.reset(3);
    pilot.tuning.stalemate_time = 2.0f;
    pilot.tuning.disengage_time = 5.0f;
    const float dt = 1.0f / 60.0f;
    pilot.update(dt, s, w);
    CHECK(pilot.state() == DemoState::Fight);
    for (int i = 0; i < 150; ++i) pilot.update(dt, s, w);  // 2.5 s, the mark untouched
    CHECK(pilot.state() != DemoState::Fight);
    // Damage it and the next fight is a fresh one after the break-off.
    for (int i = 0; i < 330; ++i) pilot.update(dt, s, w);  // past the 5 s break
    CHECK(pilot.state() == DemoState::Fight);
}

void test_speed_guard() {
    std::printf("slow in the air: nose down and beat until the speed is back\n");
    DemoWorld w = run_world();
    FlightState s = flying(Vec3{0.0f, 250.0f, 0.0f}, Vec3::forward(), 15.0f);
    DemoPilot pilot;
    pilot.reset(4);
    game::DemoDecision d = pilot.update(1.0f / 60.0f, s, w);
    CHECK(d.flight.pitch < -0.5f && d.flight.flap == 1.0f);
    s.airspeed = 40.0f;
    s.velocity = Vec3::forward() * 40.0f;
    d = pilot.update(1.0f / 60.0f, s, w);
    CHECK(d.flight.pitch > -0.5f);
}

void test_siege_commits_facing() {
    std::printf("a siege runs in only facing the tower, and fires on the run in\n");
    DemoWorld w = run_world();
    w.has_cache = true;
    w.cache = Vec3{0.0f, 0.0f, -700.0f};
    w.targets.push_back(target(Vec3{0.0f, 8.0f, -700.0f}, DemoTargetKind::Tower, 2));
    w.cache_guard = 0;
    // Facing away, 650 m out: no shots, it turns first.
    DemoPilot away;
    away.reset(5);
    game::DemoDecision d = away.update(1.0f / 60.0f, flying(Vec3{0.0f, 180.0f, -50.0f}, Vec3{0, 0, 1}), w);
    CHECK(away.state() == DemoState::Siege);
    CHECK(!d.fire);
    // Facing it, in range: shots.
    DemoPilot facing;
    facing.reset(5);
    const Vec3 at{0.0f, 110.0f, -300.0f};
    const Vec3 dir = core::normalize(w.targets[0].position - at);
    facing.update(1.0f / 60.0f, flying(Vec3{0.0f, 180.0f, 100.0f}), w);  // enters the siege far out
    d = facing.update(1.0f / 60.0f, flying(at, dir), w);
    CHECK(d.fire);
}

void test_hunt() {
    std::printf("a herd near and nearer than the cache is hunted; a nearer cache comes first\n");
    DemoWorld w = run_world();
    w.has_prey = true;
    w.prey = Vec3{0.0f, 0.0f, -300.0f};
    const FlightState s = flying(Vec3{0.0f, 120.0f, 0.0f});
    DemoPilot hunter;
    hunter.reset(6);
    game::DemoDecision d = hunter.update(1.0f / 60.0f, s, w);
    CHECK(hunter.state() == DemoState::Hunt);
    CHECK(d.flight.pitch < 0.0f);  // down toward it
    w.has_cache = true;
    w.cache = Vec3{0.0f, 0.0f, -200.0f};
    DemoPilot lander;
    lander.reset(6);
    lander.update(1.0f / 60.0f, s, w);
    CHECK(lander.state() == DemoState::Land);
    // A hunt that runs out of time rests.
    w.has_cache = false;
    DemoPilot tired;
    tired.reset(6);
    tired.tuning.hunt_budget = 1.0f;
    for (int i = 0; i < 120; ++i) tired.update(1.0f / 60.0f, s, w);
    CHECK(tired.state() != DemoState::Hunt);
}

}  // namespace

int main() {
    test_ground();
    test_priorities();
    test_stalemate_breaks_off();
    test_speed_guard();
    test_siege_commits_facing();
    test_hunt();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
