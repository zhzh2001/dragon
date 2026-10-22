// Rising air (game/thermals.h, docs/GAPS.md G7): where the columns stand,
// what they lift, and that a glider in one climbs without flapping.
#include <cmath>
#include <cstdio>

#include "core/math.h"
#include "game/flight.h"
#include "game/terrain.h"
#include "game/thermals.h"

using namespace core;

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

const Vec3 SUN = normalize(Vec3{-0.56f, 0.33f, -0.76f});  // the late-afternoon default, roughly

void test_placement() {
    std::printf("thermals stand apart, over sunlit dry ground, the same every time\n");
    game::Terrain terrain;
    game::TerrainSettings settings;
    terrain.generate(settings);
    game::ThermalSettings ts;
    game::Thermals a, b;
    a.place(terrain, 7u, SUN, ts);
    b.place(terrain, 7u, SUN, ts);
    std::printf("  %zu columns\n", a.columns().size());
    CHECK(a.columns().size() >= 5);
    CHECK(a.columns().size() == b.columns().size());
    for (size_t i = 0; i < a.columns().size(); ++i) {
        const game::Thermal& t = a.columns()[i];
        CHECK(t.base.x == b.columns()[i].base.x && t.base.z == b.columns()[i].base.z);
        CHECK(t.base.y > settings.water_level);
        CHECK(dot(terrain.normal_at(t.base.x, t.base.z), SUN) >= ts.min_sun);
        for (size_t j = 0; j < i; ++j) {
            const Vec3 d = t.base - a.columns()[j].base;
            CHECK(std::sqrt(d.x * d.x + d.z * d.z) >= ts.min_spacing);
        }
    }
    game::Thermals other;
    other.place(terrain, 8u, SUN, ts);
    bool differs = other.columns().size() != a.columns().size();
    for (size_t i = 0; !differs && i < a.columns().size(); ++i) differs = other.columns()[i].base.x != a.columns()[i].base.x;
    CHECK(differs);
}

void test_lift_profile() {
    std::printf("lift peaks on the axis, dies off it, under the top and at the ground\n");
    game::Terrain terrain;
    game::TerrainSettings settings;
    terrain.generate(settings);
    game::Thermals th;
    th.place(terrain, 7u, SUN, game::ThermalSettings{});
    CHECK(!th.columns().empty());
    if (th.columns().empty()) return;
    const game::Thermal& c = th.columns()[0];
    const float ground = c.base.y;
    const float half = (c.top - ground) * 0.5f;  // mid-column, clear of both fades
    const float mid = th.lift_at(c.base + Vec3{0.0f, half, 0.0f}, ground, 0.0f);
    const float edge = th.lift_at(c.base + Vec3{c.radius * 2.5f, half, 0.0f}, ground, 0.0f);
    const float low = th.lift_at(c.base + Vec3{0.0f, 5.0f, 0.0f}, ground, 0.0f);
    const float above = th.lift_at(Vec3{c.base.x, c.top + 10.0f, c.base.z}, ground, 0.0f);
    std::printf("  axis %.1f m/s, 2.5 radii out %.2f, 5 m up %.2f, over the top %.2f\n", mid, edge, low, above);
    // The weakest a column can be: 0.75 of the strength, at the low of its
    // breathing (0.6).
    CHECK(mid > 0.75f * 0.6f * game::ThermalSettings{}.strength - 0.05f);
    CHECK(edge < 0.1f * mid);
    CHECK(low < 0.2f * mid);
    CHECK(above == 0.0f);
}

void test_glider_climbs() {
    std::printf("a glider in rising air climbs without a wingbeat; in still air it sinks\n");
    auto glide = [](float rising) {
        game::FlightModel f;
        f.reset(Vec3{0.0f, 1000.0f, 0.0f}, Quat::identity(), 30.0f);
        f.wind = Vec3{0.0f, rising, 0.0f};
        const float dt = 1.0f / 120.0f;
        for (int i = 0; i < int(5.0f / dt); ++i) f.update(game::FlightInput{}, nullptr, dt);  // settle
        const float y0 = f.state().position.y;
        for (int i = 0; i < int(10.0f / dt); ++i) f.update(game::FlightInput{}, nullptr, dt);
        return (f.state().position.y - y0) / 10.0f;
    };
    const float still = glide(0.0f);
    const float lifted = glide(6.0f);
    std::printf("  climb in still air %.2f m/s, in 6 m/s rising air %.2f m/s\n", still, lifted);
    CHECK(still < 0.0f);
    CHECK(lifted > 1.0f);
    CHECK(std::fabs((lifted - still) - 6.0f) < 1.5f);  // the air's rise, give or take the glide's settling
}

}  // namespace

int main() {
    test_placement();
    test_lift_profile();
    test_glider_climbs();
    std::printf("thermals: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
