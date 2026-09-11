// Breath profiles: the per-species weapon data and its text format.
//
// The format matters more than it looks. A species profile sits beside its
// glTF and is hand-edited, so an unknown key must be ignored rather than
// fatal, and a missing file must leave the defaults standing -- otherwise
// adding a field to the struct breaks every model that shipped without it.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "game/breath.h"

static int failures = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);            \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static bool near(float a, float b, float tolerance = 1e-4f) {
    const float d = a - b;
    return (d < 0 ? -d : d) <= tolerance;
}

static std::string temp_path(const char* name) {
    return std::string("/tmp/dragon_test_") + name;
}

int main() {
    // Defaults are fire: a species that ships no profile breathes exactly what
    // the game breathed before species existed.
    {
        game::BreathProfile fire;
        CHECK(near(fire.scales.range, 1.0f));
        CHECK(near(fire.scales.angle, 1.0f));
        CHECK(near(fire.scales.damage, 1.0f));
        CHECK(near(fire.scales.drain, 1.0f));
        CHECK(fire.buoyancy > 0.0f);   // flame rises
        CHECK(fire.hot.x > fire.hot.z);  // and is warm
    }

    // A missing file is the normal case, not an error: it must report failure
    // without touching the profile it was handed.
    {
        game::BreathProfile profile;
        profile.scales.range = 2.5f;
        const bool loaded = game::load_breath_profile(profile, "/tmp/dragon_test_absent.cfg");
        CHECK(!loaded);
        CHECK(near(profile.scales.range, 2.5f));
    }

    // Round trip: every field survives a save and load.
    {
        game::BreathProfile written;
        written.scales.range = 0.55f;
        written.scales.angle = 2.4f;
        written.scales.damage = 1.35f;
        written.scales.drain = 0.8f;
        written.hot = core::Vec3{0.3f, 1.9f, 2.6f};
        written.cool = core::Vec3{0.05f, 0.4f, 1.1f};
        written.buoyancy = -14.0f;  // a heavy breath that pours downward
        written.spread = 0.42f;
        written.size_start = 3.5f;
        written.size_end = 11.0f;
        written.life = 0.85f;
        written.rate = 420.0f;
        written.drag = 2.2f;
        written.brightness = 1.4f;

        const std::string path = temp_path("breath_roundtrip.cfg");
        CHECK(game::save_breath_profile(written, path.c_str()));

        game::BreathProfile read;
        CHECK(game::load_breath_profile(read, path.c_str()));
        CHECK(near(read.scales.range, written.scales.range));
        CHECK(near(read.scales.angle, written.scales.angle));
        CHECK(near(read.scales.damage, written.scales.damage));
        CHECK(near(read.scales.drain, written.scales.drain));
        CHECK(near(read.hot.x, written.hot.x));
        CHECK(near(read.hot.y, written.hot.y));
        CHECK(near(read.hot.z, written.hot.z));
        CHECK(near(read.cool.x, written.cool.x));
        CHECK(near(read.cool.y, written.cool.y));
        CHECK(near(read.cool.z, written.cool.z));
        // Negative buoyancy is the whole point of a frost or gas breath, so it
        // specifically must survive the text round trip.
        CHECK(near(read.buoyancy, written.buoyancy));
        CHECK(near(read.spread, written.spread));
        CHECK(near(read.size_start, written.size_start));
        CHECK(near(read.size_end, written.size_end));
        CHECK(near(read.life, written.life));
        CHECK(near(read.rate, written.rate));
        CHECK(near(read.drag, written.drag));
        CHECK(near(read.brightness, written.brightness));
        std::remove(path.c_str());
    }

    // A partial file overrides only what it names. This is what lets a species
    // say "the same fire, but wider" in three lines.
    {
        const std::string path = temp_path("breath_partial.cfg");
        FILE* file = std::fopen(path.c_str(), "w");
        CHECK(file != nullptr);
        if (file) {
            std::fputs("# a wider, weaker fire\n"
                       "angle_scale 2.0\n"
                       "damage_scale 0.5\n"
                       "not_a_field 99\n"
                       "malformed-line-without-space\n",
                       file);
            std::fclose(file);
        }
        game::BreathProfile profile;
        const float default_life = profile.life;
        CHECK(game::load_breath_profile(profile, path.c_str()));
        CHECK(near(profile.scales.angle, 2.0f));
        CHECK(near(profile.scales.damage, 0.5f));
        // Untouched by the file, so still the default.
        CHECK(near(profile.scales.range, 1.0f));
        CHECK(near(profile.life, default_life));
        std::remove(path.c_str());
    }

    if (failures == 0) std::printf("breath: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
