// The wilds (game/wilds.h): hunger, growth, the day, finds, the nest, a save.
#include <cmath>
#include <cstdio>

#include "game/wilds.h"

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

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

void test_hunger() {
    std::printf("hunger falls with time, faster with effort; fed mends, empty starves\n");
    game::Wilds glide, work;
    game::WildsState full;
    full.hunger = 1.0f;
    glide.reset(full);
    work.reset(full);
    for (int i = 0; i < 60 * 60; ++i) {
        glide.update(1.0f / 60.0f, 0.0f, false);
        work.update(1.0f / 60.0f, 1.0f, true);
    }
    std::printf("  after a minute: %.2f gliding, %.2f working\n", double(glide.state().hunger), double(work.state().hunger));
    CHECK(near(glide.state().hunger, 1.0f - 60.0f / glide.tuning.hunger_seconds, 0.01f));
    CHECK(work.state().hunger < glide.state().hunger - 0.2f);
    CHECK(glide.regen_share() == 1.0f && glide.starve_rate() == 0.0f);

    // To empty: the event fires once, and health then falls.
    int began = 0;
    for (int i = 0; i < 60 * 600; ++i) began += work.update(1.0f / 60.0f, 1.0f, false).starving_began ? 1 : 0;
    CHECK(work.starving());
    CHECK(began == 1);
    CHECK(work.starve_rate() > 0.0f && work.regen_share() == 0.0f);
    // A meal ends it.
    work.eat(1.0f);
    CHECK(!work.starving());
    CHECK(near(work.state().hunger, 0.4f, 0.001f));
    CHECK(work.starve_rate() == 0.0f);  // not starving, not yet fed enough to mend whole
    CHECK(work.regen_share() > 0.0f && work.regen_share() < 1.0f);
    work.eat(1.0f);
    CHECK(work.regen_share() == 1.0f);
    CHECK(work.state().meals == 2);
}

void test_growth() {
    std::printf("experience makes levels, each needing more; a level is health and breath\n");
    game::Wilds wilds;
    wilds.reset();
    CHECK(wilds.state().level == 1);
    CHECK(wilds.growth() == 0.0f);
    const float first = wilds.xp_needed();
    game::WildsEvents events = wilds.kill();
    CHECK(events.levels_gained == 0);
    events = wilds.kill();
    CHECK(events.levels_gained == 0);
    events = wilds.kill();  // 120 of 100
    CHECK(events.levels_gained == 1);
    CHECK(wilds.state().level == 2);
    CHECK(wilds.xp_needed() > first);
    CHECK(wilds.growth() > 0.0f);
    CHECK(wilds.xp_share() > 0.0f && wilds.xp_share() < 1.0f);
    // A great meal can be several levels at once, and the top is the top.
    events = wilds.eat(1000.0f);
    CHECK(events.levels_gained > 1);
    for (int i = 0; i < 2000; ++i) wilds.kill();
    CHECK(wilds.state().level == wilds.tuning.max_level);
    CHECK(wilds.xp_share() == 1.0f);
}

void test_day() {
    std::printf("the clock turns; a night lived through counts once; the sun follows\n");
    game::Wilds wilds;
    game::WildsState noon;
    noon.clock = 0.5f;
    noon.hunger = 1.0f;
    wilds.reset(noon);
    wilds.tuning.hunger_seconds = 1e9f;  // not this test's subject
    CHECK(!wilds.night());
    CHECK(wilds.sun_height() > 0.99f);
    int nights = 0, dawns = 0, dusks = 0;
    const float dt = 0.5f;
    for (float t = 0.0f; t < wilds.tuning.day_seconds * 2.0f; t += dt) {
        const game::WildsEvents events = wilds.update(dt, 0.0f, false);
        nights += events.night_survived ? 1 : 0;
        dawns += events.day_began ? 1 : 0;
        dusks += events.night_began ? 1 : 0;
        CHECK(wilds.sun_height() >= -1.0f && wilds.sun_height() <= 1.0f);
        CHECK(wilds.night() == (wilds.sun_height() <= 0.0f) || std::fabs(wilds.sun_height()) < 0.02f);
    }
    CHECK(nights == 2 && dawns == 2 && dusks == 2);
    CHECK(wilds.state().nights == 2);
    CHECK(wilds.state().day == 3);
    CHECK(wilds.state().xp > 0.0f || wilds.state().level > 1);
}

void test_finds_and_sleep() {
    std::printf("a place counts once; sleep is refused at midday and takes the clock to dawn\n");
    game::Wilds wilds;
    game::WildsState noon;
    noon.clock = 0.5f;
    wilds.reset(noon);
    game::WildsEvents events;
    CHECK(wilds.find(3, events));
    CHECK(!wilds.find(3, events));
    CHECK(wilds.found(3) && !wilds.found(4));
    CHECK(wilds.state().finds.size() == 1);

    CHECK(!wilds.sleep(events));  // midday
    game::WildsState evening;
    evening.clock = 0.8f;
    evening.hunger = 1.0f;
    wilds.reset(evening);
    events = game::WildsEvents{};
    CHECK(wilds.sleep(events));
    CHECK(near(wilds.state().clock, wilds.tuning.dawn, 1e-5f));
    CHECK(wilds.state().day == 2);
    CHECK(events.night_survived && events.day_began);
    CHECK(wilds.state().hunger < 1.0f && wilds.state().hunger > 0.3f);  // sleep is cheap, not free
    CHECK(!wilds.night());

    // A fall: the level is kept, the progress toward the next is not, and
    // the dragon wakes at the next dawn.
    wilds.kill();
    CHECK(wilds.state().xp > 0.0f);
    const int level = wilds.state().level;
    const int day = wilds.state().day;
    for (int i = 0; i < 100; ++i) wilds.update(1.0f, 0.0f, false);
    wilds.fall();
    CHECK(wilds.state().xp == 0.0f && wilds.state().level == level);
    CHECK(wilds.state().day == day + 1 && near(wilds.state().clock, wilds.tuning.dawn, 1e-5f));
    CHECK(wilds.state().falls == 1);
    // Growth runs 0 to 4 over the levels.
    CHECK(wilds.growth() >= 0.0f && wilds.growth() < 0.5f);
    for (int i = 0; i < 3000; ++i) wilds.kill();
    CHECK(near(wilds.growth(), 4.0f, 1e-4f));
    CHECK(wilds.arc() >= 0.0f && wilds.arc() <= 1.0f);
}

void test_save() {
    std::printf("a save comes back as it went; an unknown key is skipped\n");
    game::Wilds wilds;
    wilds.reset();
    for (int i = 0; i < 7; ++i) wilds.kill();
    wilds.eat(0.5f);
    game::WildsEvents events;
    wilds.find(2, events);
    wilds.find(9, events);
    for (int i = 0; i < 600; ++i) wilds.update(0.5f, 0.3f, false);
    wilds.mutable_state().seed = 4026531841u;  // past what a float holds
    wilds.mutable_state().valley = 3;
    const std::string text = wilds.save();
    const game::WildsState back = game::Wilds::load("future_key 7\n" + text);
    const game::WildsState& was = wilds.state();
    CHECK(back.level == was.level && back.day == was.day);
    CHECK(near(back.xp, was.xp, 0.01f) && near(back.hunger, was.hunger, 0.001f) && near(back.clock, was.clock, 0.001f));
    CHECK(back.kills == 7 && back.meals == 1);
    CHECK(back.seed == 4026531841u && back.valley == 3);
    CHECK(back.finds.size() == 2 && back.finds[0] == 2 && back.finds[1] == 9);
    // Nonsense loads as a fresh start.
    const game::WildsState fresh = game::Wilds::load("level -4\nhunger 9\nclock 3\n");
    CHECK(fresh.level == 1 && fresh.hunger == 1.0f && fresh.clock == 0.0f);
}

}  // namespace

int main() {
    test_hunger();
    test_growth();
    test_day();
    test_finds_and_sleep();
    test_save();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
