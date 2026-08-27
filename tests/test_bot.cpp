// Bot pilot invariants.
//
// The plan's bar for M14: bots fly the same model the player does, never cheat,
// and read as skill. What tests can pin: pursuit converges, terrain is avoided,
// fire discipline holds (cone, range, cooldown), damage triggers evasion, the
// rhythm cycles attack and extend, and hours of simulated flight stay finite.
#include <cmath>
#include <cstdio>

#include "game/bot.h"
#include "game/combat.h"
#include "game/terrain.h"

using namespace core;
using game::BotDecision;
using game::BotPilot;
using game::BotState;
using game::FlightModel;
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

FlightState target_at(Vec3 position, Vec3 velocity = Vec3::zero()) {
    FlightState state;
    state.position = position;
    state.velocity = velocity;
    if (length_sq(velocity) > 1e-3f) {
        state.orientation = look_rotation(velocity, Vec3::up());
    }
    return state;
}

void test_pursuit_converges() {
    std::printf("a bot closes on a straight-flying target\n");
    FlightModel flight;
    flight.reset(Vec3{0.0f, 900.0f, 0.0f}, Quat::identity(), 45.0f);
    BotPilot pilot;
    pilot.reset(7u);

    // The target cruises away at a catchable speed.
    Vec3 target_position{0.0f, 900.0f, -700.0f};
    const Vec3 target_velocity{0.0f, 0.0f, -20.0f};

    const float start = distance(flight.state().position, target_position);
    float best = start;
    for (int i = 0; i < 25 * 60; ++i) {
        const float dt = 1.0f / 60.0f;
        target_position += target_velocity * dt;
        const BotDecision decision = pilot.update(dt, flight.state(),
                                                  target_at(target_position, target_velocity),
                                                  true, -1e9f);
        flight.update(decision.flight, nullptr, dt);
        best = minf(best, distance(flight.state().position, target_position));
    }
    std::printf("  start %.0f m, closest approach %.0f m\n", start, best);
    CHECK(best < start * 0.35f);
    CHECK(best < pilot.tuning.fire_range);
}

void test_terrain_is_avoided() {
    std::printf("a bot chasing a low target does not fly into the ground\n");
    game::Terrain terrain;
    game::TerrainSettings settings;
    terrain.generate(settings);

    FlightModel flight;
    const Vec3 spawn{0.0f, terrain.height_at(0.0f, -400.0f) + 220.0f, 0.0f};
    flight.reset(spawn, Quat::identity(), 45.0f);
    BotPilot pilot;
    pilot.reset(11u);

    // A target hugging the terrain: the honest way in is a dive, and the
    // steering's avoidance floor has to win over the pursuit.
    float min_clearance = 1e9f;
    for (int i = 0; i < 30 * 60; ++i) {
        const float dt = 1.0f / 60.0f;
        const Vec3 self = flight.state().position;
        const Vec3 ahead = self + flight.state().velocity * 2.0f;
        const float ground =
            maxf(terrain.height_at(self.x, self.z), terrain.height_at(ahead.x, ahead.z));
        const Vec3 low_target{self.x, terrain.height_at(self.x, self.z - 500.0f) + 12.0f,
                              self.z - 500.0f};
        const BotDecision decision =
            pilot.update(dt, flight.state(), target_at(low_target), true, ground);
        flight.update(decision.flight, &terrain, dt);
        if (i > 120) {
            min_clearance = minf(min_clearance, flight.state().ground_clearance);
        }
    }
    std::printf("  minimum clearance %.0f m\n", min_clearance);
    CHECK(min_clearance > 15.0f);
    CHECK(!flight.state().grounded);
}

void test_fire_discipline() {
    std::printf("fires only in range, in the cone, on the cooldown\n");
    BotPilot pilot;
    pilot.reset(13u);

    FlightState self;
    self.position = Vec3::zero();
    self.velocity = Vec3{0.0f, 0.0f, -40.0f};

    // In range, dead ahead: fires -- then the cooldown holds.
    int shots = 0;
    for (int i = 0; i < 120; ++i) {
        const BotDecision d = pilot.update(1.0f / 60.0f, self,
                                           target_at(Vec3{0.0f, 0.0f, -400.0f}), true, -1e9f);
        if (d.fire) {
            ++shots;
            // The round leaves roughly toward the target, spread included.
            const Vec3 direction = normalize(d.fire_velocity - self.velocity);
            CHECK(dot(direction, Vec3::forward()) > 0.95f);
        }
    }
    CHECK(shots >= 1);
    CHECK(shots <= 2);  // two seconds, 1.7 s cooldown

    // Out of range: silent.
    BotPilot far_pilot;
    far_pilot.reset(17u);
    int far_shots = 0;
    for (int i = 0; i < 120; ++i) {
        far_shots += far_pilot.update(1.0f / 60.0f, self,
                                      target_at(Vec3{0.0f, 0.0f, -2000.0f}), true, -1e9f)
                         .fire
                         ? 1
                         : 0;
    }
    CHECK(far_shots == 0);

    // In range but 90 degrees off the nose: the nose is not on it, no shot.
    BotPilot off_pilot;
    off_pilot.reset(19u);
    int off_shots = 0;
    for (int i = 0; i < 30; ++i) {
        off_shots += off_pilot.update(1.0f / 60.0f, self,
                                      target_at(Vec3{400.0f, 0.0f, 0.0f}), true, -1e9f)
                         .fire
                         ? 1
                         : 0;
    }
    CHECK(off_shots == 0);
}

void test_damage_triggers_evasion_and_rhythm_cycles() {
    std::printf("a hit forces a jink, and the fight cycles attack and extend\n");
    BotPilot pilot;
    pilot.reset(23u);
    FlightState self;
    self.position = Vec3::zero();
    self.velocity = Vec3{0.0f, 0.0f, -40.0f};
    const FlightState player = target_at(Vec3{0.0f, 0.0f, -500.0f});

    CHECK(pilot.state() == BotState::Attack);
    pilot.notify_hit();
    CHECK(pilot.state() == BotState::Evade);

    // The jink runs its course, then the bot repositions rather than resuming
    // the same line.
    for (int i = 0; i < int(pilot.tuning.evade_duration * 60.0f) + 10; ++i) {
        pilot.update(1.0f / 60.0f, self, player, true, -1e9f);
    }
    CHECK(pilot.state() == BotState::Extend);

    // A full fight cycles back to attack: run long enough and both states show.
    bool saw_attack = false;
    for (int i = 0; i < 30 * 60; ++i) {
        pilot.update(1.0f / 60.0f, self, player, true, -1e9f);
        if (pilot.state() == BotState::Attack) saw_attack = true;
    }
    CHECK(saw_attack);
}

void test_reaction_window_is_honest() {
    std::printf("aim uses stale data: a fresh break is not tracked instantly\n");
    BotPilot pilot;
    pilot.tuning.aim_spread_deg = 0.0f;
    pilot.reset(29u);
    FlightState self;
    self.position = Vec3::zero();
    self.velocity = Vec3{0.0f, 0.0f, -40.0f};

    // Let the pilot settle on a target moving right...
    FlightState player = target_at(Vec3{0.0f, 0.0f, -400.0f}, Vec3{40.0f, 0.0f, 0.0f});
    for (int i = 0; i < 60; ++i) pilot.update(1.0f / 60.0f, self, player, true, -1e9f);

    // ...then break hard left. The very next frame the pilot must still be
    // aiming off the OLD velocity: leading right, not left.
    player.velocity = Vec3{-40.0f, 0.0f, 0.0f};
    bool fired = false;
    Vec3 lead = Vec3::zero();
    for (int i = 0; i < 6 && !fired; ++i) {
        const BotDecision d = pilot.update(1.0f / 60.0f, self, player, true, -1e9f);
        if (d.fire) {
            fired = true;
            lead = d.fire_velocity - self.velocity;
        }
    }
    if (fired) CHECK(lead.x > 0.0f);  // still leading the stale rightward break
}

void test_long_sim_stays_finite() {
    std::printf("ten minutes of dogfight stays finite\n");
    game::Terrain terrain;
    game::TerrainSettings settings;
    terrain.generate(settings);

    FlightModel flight;
    flight.reset(Vec3{0.0f, 600.0f, 0.0f}, Quat::identity(), 45.0f);
    BotPilot pilot;
    pilot.reset(31u);

    // The "player" flies an erratic loop the bot has to keep re-solving.
    int grounded_frames = 0;
    for (int i = 0; i < 600 * 60; ++i) {
        const float dt = 1.0f / 60.0f;
        const float t = float(i) * dt;
        const Vec3 target{600.0f * std::sin(0.11f * t), 500.0f + 150.0f * std::sin(0.23f * t),
                          -600.0f * std::cos(0.07f * t)};
        const Vec3 self = flight.state().position;
        const Vec3 ahead = self + flight.state().velocity * 2.0f;
        const BotDecision d = pilot.update(
            dt, flight.state(), target_at(target), true,
            maxf(terrain.height_at(self.x, self.z), terrain.height_at(ahead.x, ahead.z)));
        flight.update(d.flight, &terrain, dt);
        if (flight.state().grounded) ++grounded_frames;
        if (i % 600 == 0) pilot.notify_hit();  // keep every state exercised
    }
    const Vec3 p = flight.state().position;
    CHECK(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
    CHECK(std::isfinite(flight.state().airspeed));
    std::printf("  grounded frames: %d\n", grounded_frames);
    CHECK(grounded_frames == 0);
}

}  // namespace

int main() {
    test_pursuit_converges();
    test_terrain_is_avoided();
    test_fire_discipline();
    test_damage_triggers_evasion_and_rhythm_cycles();
    test_reaction_window_is_honest();
    test_long_sim_stays_finite();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
