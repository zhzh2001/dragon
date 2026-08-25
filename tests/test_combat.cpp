// Combat invariants.
//
// The two that actually matter in a 3D dogfight are hard to see by eye and easy
// to get wrong: a fast projectile must not pass through a target between frames,
// and a resource meter must not be tappable at zero. Both are pinned here.
#include <cmath>
#include <cstdio>

#include "game/combat.h"

using namespace core;
using game::Combat;
using game::CombatInput;
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

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// A player sitting at the origin facing -Z, which is this engine's forward.
FlightState player_at(Vec3 position, Vec3 facing = Vec3::forward()) {
    FlightState state;
    state.position = position;
    state.orientation = look_rotation(facing, Vec3::up());
    return state;
}

// Takes a sentinel out of the fight for good. Clearing `alive` alone is not
// enough: a dead sentinel respawns once its timer runs down, and the default
// timer is zero, so it would be back the very next frame.
void retire(game::Sentinel& sentinel) {
    sentinel.alive = false;
    sentinel.respawn_timer = 1e6f;
}

void retire_all(Combat& combat) {
    for (game::Sentinel& sentinel : combat.sentinels()) retire(sentinel);
}

// Puts one sentinel exactly where the test wants it and removes the rest, so a
// hit can be attributed.
void isolate_sentinel(Combat& combat, Vec3 position) {
    auto& sentinels = combat.sentinels();
    CHECK(!sentinels.empty());
    for (size_t i = 1; i < sentinels.size(); ++i) retire(sentinels[i]);
    sentinels[0].alive = true;
    sentinels[0].health = combat.tuning.sentinel_health;
    sentinels[0].max_health = combat.tuning.sentinel_health;
    sentinels[0].position = position;
    // Park the orbit on the spot so update() does not move it away.
    sentinels[0].centre = position;
    sentinels[0].orbit_radius = 0.0f;
    sentinels[0].orbit_speed = 0.0f;
    sentinels[0].bob = 0.0f;
    // Far enough in the future that return fire never confuses a test.
    sentinels[0].fire_timer = 1e6f;
}

void test_cone() {
    std::printf("the breath cone includes what it looks like it includes\n");
    const Vec3 tip = Vec3::zero();
    const Vec3 axis = Vec3::forward();  // -Z
    const float half = radians(15.0f);
    const float range = 100.0f;

    // Straight down the axis, inside range.
    CHECK(game::point_in_cone(Vec3{0.0f, 0.0f, -50.0f}, tip, axis, half, range));
    // Behind the tip is never in the cone, however well aligned.
    CHECK(!game::point_in_cone(Vec3{0.0f, 0.0f, 50.0f}, tip, axis, half, range));
    // Past the range.
    CHECK(!game::point_in_cone(Vec3{0.0f, 0.0f, -150.0f}, tip, axis, half, range));
    // The tip itself is excluded rather than being a degenerate hit on
    // everything at zero distance.
    CHECK(!game::point_in_cone(tip, tip, axis, half, range));

    // Just inside and just outside the wall, at 50 m out.
    const float wall = 50.0f * std::tan(half);
    CHECK(game::point_in_cone(Vec3{wall * 0.95f, 0.0f, -50.0f}, tip, axis, half, range));
    CHECK(!game::point_in_cone(Vec3{wall * 1.05f, 0.0f, -50.0f}, tip, axis, half, range));
    // The cone widens with distance: the same offset that missed at 50 m is
    // comfortably inside at 100 m.
    CHECK(game::point_in_cone(Vec3{wall * 1.05f, 0.0f, -99.0f}, tip, axis, half, range));
}

void test_closest_point_fraction() {
    std::printf("closest approach along a segment clamps to its ends\n");
    const Vec3 from{0.0f, 0.0f, 0.0f};
    const Vec3 to{10.0f, 0.0f, 0.0f};
    CHECK(near(game::closest_point_fraction(from, to, Vec3{5.0f, 3.0f, 0.0f}), 0.5f));
    // Before the start and past the end both clamp, which is what makes this a
    // segment test rather than a line test.
    CHECK(near(game::closest_point_fraction(from, to, Vec3{-20.0f, 0.0f, 0.0f}), 0.0f));
    CHECK(near(game::closest_point_fraction(from, to, Vec3{99.0f, 0.0f, 0.0f}), 1.0f));
    // A degenerate segment must not divide by zero.
    CHECK(near(game::closest_point_fraction(from, from, Vec3{1.0f, 1.0f, 1.0f}), 0.0f));
}

void test_fireball_hits_and_kills() {
    std::printf("fireballs damage and destroy a sentinel\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 7u);
    // No blast assist: this test is about the direct hit.
    combat.tuning.fireball_blast_radius = 0.0f;

    const FlightState player = player_at(Vec3::zero());
    isolate_sentinel(combat, Vec3{0.0f, 0.0f, -300.0f});
    const float start_health = combat.sentinels()[0].health;

    CombatInput fire;
    fire.fire = true;
    combat.update(1.0f / 60.0f, player, fire);
    CHECK(!combat.projectiles().empty());

    // Fly it to the target.
    int hits = 0;
    for (int i = 0; i < 200 && combat.sentinels()[0].alive; ++i) {
        const game::CombatEvents events = combat.update(1.0f / 60.0f, player, CombatInput{});
        hits += events.hits_dealt;
    }
    CHECK(hits == 1);
    CHECK(combat.sentinels()[0].health < start_health);
    CHECK(near(start_health - combat.sentinels()[0].health, combat.tuning.fireball_damage, 0.01f));

    // Enough shots must finish it, and a kill must be reported exactly once.
    int kills = 0;
    for (int shot = 0; shot < 6 && combat.sentinels()[0].alive; ++shot) {
        CombatInput again;
        again.fire = true;
        // Wait out the cooldown, then fly the shot in.
        for (int i = 0; i < 200 && combat.sentinels()[0].alive; ++i) {
            const game::CombatEvents events =
                combat.update(1.0f / 60.0f, player, i == 0 ? again : CombatInput{});
            kills += events.kills;
            if (combat.fire_cooldown() <= 0.0f && i > 60) break;
        }
    }
    CHECK(!combat.sentinels()[0].alive);
    CHECK(kills == 1);
    CHECK(combat.kills() == 1);
}

void test_projectile_does_not_tunnel() {
    std::printf("a fast projectile cannot pass through a target in one step\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 11u);
    combat.tuning.fireball_blast_radius = 0.0f;
    combat.tuning.fireball_gravity = 0.0f;

    const FlightState player = player_at(Vec3::zero());
    // 210 m/s projectile, target at 100 m, and a frame long enough to step
    // clean over it: a point test would report nothing at all.
    isolate_sentinel(combat, Vec3{0.0f, 0.0f, -100.0f});
    const float step = 0.5f;
    CHECK(combat.tuning.fireball_speed * step > 100.0f);

    CombatInput fire;
    fire.fire = true;
    combat.update(1.0f / 60.0f, player, fire);

    int hits = 0;
    for (int i = 0; i < 4; ++i) hits += combat.update(step, player, CombatInput{}).hits_dealt;
    CHECK(hits == 1);
}

void test_breath_meter_latches() {
    std::printf("the breath meter cannot be tapped at empty\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 3u);
    const FlightState player = player_at(Vec3::zero());
    isolate_sentinel(combat, Vec3{0.0f, 0.0f, -40.0f});

    CombatInput hold;
    hold.breath = true;

    // Hold until the meter empties.
    float elapsed = 0.0f;
    while (combat.breath() > 0.0f && elapsed < 30.0f) {
        combat.update(1.0f / 60.0f, player, hold);
        elapsed += 1.0f / 60.0f;
    }
    CHECK(near(combat.breath(), 0.0f, 1e-3f));
    CHECK(!combat.breathing());

    // Holding it down at zero must produce no damage at all: without the latch
    // each frame regenerates a sliver and immediately spends it.
    const float health_before = combat.sentinels()[0].health;
    for (int i = 0; i < 120; ++i) combat.update(1.0f / 60.0f, player, hold);
    CHECK(near(combat.sentinels()[0].health, health_before, 1e-3f));
    CHECK(!combat.breathing());

    // Releasing lets it refill, and past the threshold it starts again.
    for (int i = 0; i < 600; ++i) combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(combat.breath() > combat.tuning.breath_restart_threshold);
    combat.update(1.0f / 60.0f, player, hold);
    CHECK(combat.breathing());
}

void test_breath_damages_only_within_the_cone() {
    std::printf("breath damages what is in front and nothing else\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 5u);
    const FlightState player = player_at(Vec3::zero());

    CombatInput hold;
    hold.breath = true;

    // Directly ahead, well inside range.
    isolate_sentinel(combat, Vec3{0.0f, 0.0f, -40.0f});
    combat.update(0.1f, player, hold);
    CHECK(combat.sentinels()[0].health < combat.tuning.sentinel_health);

    // Directly behind: never hit, however close.
    Combat behind;
    behind.reset(nullptr, Vec3::zero(), 5u);
    isolate_sentinel(behind, Vec3{0.0f, 0.0f, 40.0f});
    behind.update(0.1f, player, hold);
    CHECK(near(behind.sentinels()[0].health, behind.tuning.sentinel_health));

    // Beyond range, on the axis.
    Combat far_away;
    far_away.reset(nullptr, Vec3::zero(), 5u);
    isolate_sentinel(far_away, Vec3{0.0f, 0.0f, -far_away.tuning.breath_range - 60.0f});
    far_away.update(0.1f, player, hold);
    CHECK(near(far_away.sentinels()[0].health, far_away.tuning.sentinel_health));
}

void test_fire_rate_is_limited() {
    std::printf("the fireball cooldown limits the rate of fire\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 9u);
    retire_all(combat);

    const FlightState player = player_at(Vec3::zero());
    CombatInput fire;
    fire.fire = true;

    // Holding the button down for a second must not empty the magazine.
    int live = 0;
    for (int i = 0; i < 60; ++i) combat.update(1.0f / 60.0f, player, fire);
    for (const game::Projectile& projectile : combat.projectiles()) {
        if (projectile.alive) ++live;
    }
    const int expected = int(1.0f / combat.tuning.fireball_cooldown) + 1;
    CHECK(live <= expected);
    CHECK(live >= 1);
}

void test_player_takes_damage_and_regenerates() {
    std::printf("the player takes damage, dies, and heals after a lull\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 13u);
    // One sentinel, firing straight and often, from close range.
    combat.tuning.sentinel_spread = 0.0f;
    combat.tuning.sentinel_fire_interval = 0.2f;
    const FlightState player = player_at(Vec3::zero());
    isolate_sentinel(combat, Vec3{0.0f, 0.0f, -140.0f});
    combat.sentinels()[0].fire_timer = 0.0f;

    bool died = false;
    float taken = 0.0f;
    for (int i = 0; i < 3000 && !died; ++i) {
        const game::CombatEvents events = combat.update(1.0f / 60.0f, player, CombatInput{});
        taken += events.damage_taken;
        died = died || events.player_died;
    }
    CHECK(taken > 0.0f);
    CHECK(died);
    CHECK(near(combat.health(), 0.0f));
    CHECK(!combat.alive());

    // A dead player is not shot at again, so a respawn is not instantly undone.
    combat.revive();
    CHECK(combat.alive());
    CHECK(near(combat.health(), combat.tuning.max_health));

    // Regeneration waits out the delay rather than starting immediately.
    Combat quiet;
    quiet.reset(nullptr, Vec3::zero(), 17u);
    quiet.tuning.sentinel_spread = 0.0f;
    quiet.tuning.sentinel_fire_interval = 0.2f;
    quiet.tuning.regen_delay = 5.0f;
    quiet.tuning.health_regen = 10.0f;
    const FlightState still = player_at(Vec3::zero());
    isolate_sentinel(quiet, Vec3{0.0f, 0.0f, -140.0f});
    quiet.sentinels()[0].fire_timer = 0.0f;

    // Take a wound...
    while (quiet.health() >= quiet.tuning.max_health) {
        quiet.update(1.0f / 60.0f, still, CombatInput{});
    }
    CHECK(quiet.health() < quiet.tuning.max_health);

    // ...then break off. Two seconds lets every round already in the air land,
    // so what follows is measuring regeneration and not the tail of the fight.
    quiet.sentinels()[0].fire_timer = 1e6f;
    for (int i = 0; i < 120; ++i) quiet.update(1.0f / 60.0f, still, CombatInput{});
    const float settled = quiet.health();
    CHECK(settled < quiet.tuning.max_health);

    // Still inside the delay: nothing comes back.
    for (int i = 0; i < 60; ++i) quiet.update(1.0f / 60.0f, still, CombatInput{});
    CHECK(near(quiet.health(), settled, 0.01f));

    // Past it, health climbs and stops at full rather than running away.
    for (int i = 0; i < 600; ++i) quiet.update(1.0f / 60.0f, still, CombatInput{});
    CHECK(quiet.health() > settled);
    CHECK(quiet.health() <= quiet.tuning.max_health + 1e-3f);
    CHECK(near(quiet.health(), quiet.tuning.max_health, 0.01f));
}

void test_boost_respects_its_cooldown() {
    std::printf("boost lasts its duration and then waits out the cooldown\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 23u);
    retire_all(combat);
    combat.tuning.boost_duration = 1.0f;
    combat.tuning.boost_cooldown = 4.0f;

    const FlightState player = player_at(Vec3::zero());
    CombatInput boost;
    boost.boost = true;

    CHECK(!combat.boost_active());
    combat.update(1.0f / 60.0f, player, boost);
    CHECK(combat.boost_active());

    // Runs out after its duration.
    for (int i = 0; i < 70; ++i) combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(!combat.boost_active());

    // Still on cooldown, so pressing again does nothing.
    combat.update(1.0f / 60.0f, player, boost);
    CHECK(!combat.boost_active());
    CHECK(combat.boost_cooldown() > 0.0f);

    // Once the cooldown expires it arms again.
    for (int i = 0; i < 250; ++i) combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(near(combat.boost_cooldown(), 0.0f));
    combat.update(1.0f / 60.0f, player, boost);
    CHECK(combat.boost_active());
}

void test_is_deterministic() {
    std::printf("the same seed gives the same fight\n");
    auto run = [](uint32_t seed) {
        Combat combat;
        combat.reset(nullptr, Vec3::zero(), seed);
        const FlightState player = player_at(Vec3{0.0f, 200.0f, 0.0f});
        float total = 0.0f;
        for (int i = 0; i < 600; ++i) {
            CombatInput in;
            in.fire = (i % 40) == 0;
            in.breath = (i / 30) % 2 == 0;
            combat.update(1.0f / 60.0f, player, in);
        }
        for (const game::Projectile& projectile : combat.projectiles()) {
            total += projectile.position.x + projectile.position.y + projectile.position.z;
        }
        for (const game::Sentinel& sentinel : combat.sentinels()) total += sentinel.health;
        return total;
    };
    CHECK(near(run(1234u), run(1234u)));
    // A different seed must actually change the aim spread.
    CHECK(!near(run(1234u), run(9876u)));
}

void test_no_nans_under_abuse() {
    std::printf("nothing goes non-finite under extreme inputs\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 31u);
    FlightState player = player_at(Vec3::zero());
    player.velocity = Vec3{900.0f, -900.0f, 900.0f};

    for (int i = 0; i < 400; ++i) {
        CombatInput in;
        in.fire = true;
        in.breath = true;
        in.boost = true;
        // A zero step must be a no-op rather than a division by zero.
        combat.update(i % 7 == 0 ? 0.0f : 0.25f, player, in);
    }
    bool finite = std::isfinite(combat.health()) && std::isfinite(combat.breath());
    for (const game::Projectile& projectile : combat.projectiles()) {
        finite = finite && std::isfinite(projectile.position.x) &&
                 std::isfinite(projectile.position.y) && std::isfinite(projectile.position.z);
    }
    for (const game::Sentinel& sentinel : combat.sentinels()) {
        finite = finite && std::isfinite(sentinel.health) && std::isfinite(sentinel.position.x);
    }
    CHECK(finite);
}

}  // namespace

int main() {
    test_cone();
    test_closest_point_fraction();
    test_fireball_hits_and_kills();
    test_projectile_does_not_tunnel();
    test_breath_meter_latches();
    test_breath_damages_only_within_the_cone();
    test_fire_rate_is_limited();
    test_player_takes_damage_and_regenerates();
    test_boost_respects_its_cooldown();
    test_is_deterministic();
    test_no_nans_under_abuse();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
