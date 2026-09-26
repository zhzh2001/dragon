// The prey herd: grazers stay on their ground while nothing threatens them,
// bolt from a low dragon and ignore a high one, and are eaten three ways --
// swooped, bitten, or burnt and then picked up. Pinned because a herd that
// wanders off the map or runs faster than a dragon is only found after
// minutes of play.
#include <cstdio>

#include "game/hoard_run.h"
#include "game/prey.h"
#include "game/terrain.h"

using core::Vec3;
using game::PreyHerds;
using game::PreyState;

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

game::FlightState dragon_at(Vec3 position) {
    game::FlightState state;
    state.position = position;
    state.orientation = core::look_rotation(Vec3::forward(), Vec3::up());
    state.velocity = Vec3::forward() * 30.0f;
    state.airspeed = 30.0f;
    return state;
}

// A herd on the flattest dry ground of the first run layout.
std::vector<game::RunHerd> one_herd(const game::Terrain& terrain) {
    game::HoardRunSettings settings;
    settings.herds = 1;
    const game::RunLayout layout =
        game::generate_run_layout(terrain, terrain.settings().half_extent, settings);
    return layout.herds;
}

float farthest_from(const PreyHerds& herds, Vec3 point) {
    float d = 0.0f;
    for (const game::Prey& p : herds.animals()) {
        if (p.state == PreyState::Eaten) continue;
        const Vec3 off{p.position.x - point.x, 0.0f, p.position.z - point.z};
        d = core::maxf(d, core::length(off));
    }
    return d;
}

void test_graze_and_flee(const game::Terrain& terrain) {
    std::printf("calm, they stay on their ground; a low dragon scatters them, a high one does not\n");
    const auto herd = one_herd(terrain);
    CHECK(herd.size() == 1);
    const Vec3 home = herd[0].position;
    PreyHerds herds;
    herds.reset(herd, terrain, 3u);
    CHECK(herds.alive() == herd[0].count);
    const float dt = 1.0f / 30.0f;
    // A minute with the dragon far away.
    for (int i = 0; i < 1800; ++i) herds.update(dt, dragon_at(home + Vec3{2000.0f, 300.0f, 0.0f}), 1.0f, terrain);
    const float spread = farthest_from(herds, home);
    std::printf("  calm minute: farthest %.0f m from home\n", double(spread));
    CHECK(spread < herds.tuning.herd_radius + 15.0f);
    for (const game::Prey& p : herds.animals()) {
        CHECK(std::fabs(p.position.y - terrain.height_at(p.position.x, p.position.z)) < 0.01f);
    }

    // High overhead: a speck, ignored.
    PreyHerds high = herds;
    for (int i = 0; i < 60; ++i) high.update(dt, dragon_at(home + Vec3{0.0f, 300.0f, 0.0f}), 1.0f, terrain);
    int fleeing = 0;
    for (const game::Prey& p : high.animals()) fleeing += p.state == PreyState::Flee ? 1 : 0;
    CHECK(fleeing == 0);

    // Low and close: they run, faster than a walk and slower than a dragon.
    const Vec3 threat = home + Vec3{90.0f, 20.0f, 0.0f};
    float top = 0.0f;
    for (int i = 0; i < 90; ++i) {
        herds.update(dt, dragon_at(threat), 1.0f, terrain);
        for (const game::Prey& p : herds.animals()) top = core::maxf(top, p.speed);
    }
    fleeing = 0;
    for (const game::Prey& p : herds.animals()) fleeing += p.state == PreyState::Flee ? 1 : 0;
    std::printf("  scattered: %d fleeing, top speed %.1f m/s\n", fleeing, double(top));
    CHECK(fleeing >= herd[0].count / 2);
    CHECK(top > 10.0f && top <= herds.tuning.run_speed + 0.01f);
}

void test_three_ways_to_eat(const game::Terrain& terrain) {
    std::printf("swooped, bitten, or burnt and picked up\n");
    const auto herd = one_herd(terrain);
    PreyHerds herds;
    herds.reset(herd, terrain, 5u);
    const int count = herds.alive();

    // Swoop: the body low over one.
    const game::Prey target = herds.animals()[0];
    game::PreyEvents e = herds.update(1.0f / 60.0f, dragon_at(target.position + Vec3{0.0f, 5.0f, 0.0f}), 1.0f, terrain);
    CHECK(e.eaten >= 1 && e.by_swoop);
    CHECK(herds.animals()[0].state == PreyState::Eaten);
    // Too high to snatch.
    PreyHerds skim;
    skim.reset(herd, terrain, 5u);
    e = skim.update(1.0f / 60.0f, dragon_at(skim.animals()[0].position + Vec3{0.0f, 14.0f, 0.0f}), 1.0f, terrain);
    CHECK(e.eaten == 0);
    // ...unless the dragon has grown into the reach.
    e = skim.update(1.0f / 60.0f, dragon_at(skim.animals()[0].position + Vec3{0.0f, 14.0f, 0.0f}), 1.7f, terrain);
    CHECK(e.eaten >= 1 && skim.animals()[0].state == PreyState::Eaten);

    // Bite: one, the closest in the cone, not the herd.
    PreyHerds bitten;
    bitten.reset(herd, terrain, 5u);
    const Vec3 at = bitten.animals()[1].position + Vec3{0.0f, 1.5f, 0.0f};
    game::PreyEvents b;
    bitten.bite(at + Vec3{0.0f, 0.0f, 12.0f}, Vec3::forward(), 30.0f, core::radians(50.0f), b);
    CHECK(b.eaten == 1);
    CHECK(bitten.alive() == count - 1);

    // Fire: a carcass, then eaten when swooped.
    PreyHerds burnt;
    burnt.reset(herd, terrain, 5u);
    const Vec3 body = burnt.animals()[2].position + Vec3{0.0f, 1.5f, 0.0f};
    game::PreyEvents f;
    for (int i = 0; i < 120; ++i) {
        burnt.breathe(body + Vec3{0.0f, 40.0f, 40.0f}, core::normalize(Vec3{0.0f, -40.0f, -40.0f}),
                      core::radians(8.0f), 150.0f, 30.0f, 1.0f / 60.0f, f);
    }
    CHECK(f.killed >= 1);
    CHECK(burnt.animals()[2].state == PreyState::Carcass);
    const Vec3 carcass = burnt.animals()[2].position;
    e = burnt.update(1.0f / 60.0f, dragon_at(carcass + Vec3{0.0f, 3.0f, 0.0f}), 1.0f, terrain);
    CHECK(e.eaten >= 1);
    CHECK(burnt.animals()[2].state == PreyState::Eaten);
}

}  // namespace

int main() {
    game::Terrain terrain;
    terrain.generate(small_terrain());
    test_graze_and_flee(terrain);
    test_three_ways_to_eat(terrain);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
