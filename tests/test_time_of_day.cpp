// The time of day a run's valleys are dealt (game/time_of_day.h, docs/GAPS.md
// G8): morning to evening down the descent, the same on every machine, and a
// sun whose colour follows its height through the air.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "game/time_of_day.h"

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

void test_descent() {
    std::printf("a run goes from morning to evening, the sun ahead and low\n");
    for (uint32_t seed = 1; seed < 200; seed += 7) {
        const game::TimeOfDay first = game::time_of_day_for(seed, 0, 3);
        const game::TimeOfDay middle = game::time_of_day_for(seed, 1, 3);
        const game::TimeOfDay last = game::time_of_day_for(seed, 2, 3);
        CHECK(std::strcmp(first.name, "morning") == 0);
        CHECK(std::strcmp(middle.name, "afternoon") == 0);
        CHECK(std::strcmp(last.name, "evening") == 0);
        CHECK(first.mist > last.mist && last.mist > middle.mist && middle.mist == 0.0f);
        for (const game::TimeOfDay& t : {first, middle, last}) {
            // Never so low the walls shade the whole floor, never noon-flat.
            CHECK(t.sun_elevation_deg >= 12.0f && t.sun_elevation_deg <= 26.0f);
            // Ahead of a flight toward -Z: cos(azimuth) < 0.
            CHECK(std::cos(t.sun_azimuth_deg * 3.14159265f / 180.0f) < 0.0f);
        }
        // Morning from the right of the corridor, evening from the left.
        CHECK(std::sin(first.sun_azimuth_deg * 3.14159265f / 180.0f) > 0.0f);
        CHECK(std::sin(last.sun_azimuth_deg * 3.14159265f / 180.0f) < 0.0f);
        // The same seed, the same day.
        const game::TimeOfDay again = game::time_of_day_for(seed, 0, 3);
        CHECK(again.sun_elevation_deg == first.sun_elevation_deg && again.sun_azimuth_deg == first.sun_azimuth_deg);
    }
    // A one-valley run is a morning; a five-valley run's middle is the afternoon.
    CHECK(std::strcmp(game::time_of_day_for(5, 0, 1).name, "morning") == 0);
    CHECK(std::strcmp(game::time_of_day_for(5, 2, 5).name, "afternoon") == 0);
}

void test_sun_colour() {
    std::printf("the sun reddens and dims as it sinks, and is unchanged at the reference\n");
    const core::Vec3 same = game::sun_colour_ratio(19.0f, 19.0f, 1.0f, 1.2f);
    CHECK(std::fabs(same.x - 1.0f) < 1e-5f && std::fabs(same.y - 1.0f) < 1e-5f && std::fabs(same.z - 1.0f) < 1e-5f);
    const core::Vec3 low = game::sun_colour_ratio(10.0f, 19.0f, 1.0f, 1.2f);
    CHECK(low.x < 1.0f && low.z < low.y && low.y < low.x);
    // Not black: at 10 degrees the red channel keeps most of its light.
    CHECK(low.x > 0.8f && low.z > 0.4f);
    const core::Vec3 high = game::sun_colour_ratio(26.0f, 19.0f, 1.0f, 1.2f);
    CHECK(high.z > high.x && high.x > 1.0f);
    std::printf("  10 deg (%.3f, %.3f, %.3f), 26 deg (%.3f, %.3f, %.3f)\n", double(low.x), double(low.y),
                double(low.z), double(high.x), double(high.y), double(high.z));
}

}  // namespace

int main() {
    test_descent();
    test_sun_colour();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
