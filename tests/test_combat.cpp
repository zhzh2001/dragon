// Combat invariants.
//
// The two that actually matter in a 3D dogfight are hard to see by eye and easy
// to get wrong: a fast projectile must not pass through a target between frames,
// and a resource meter must not be tappable at zero. Both are pinned here.
#include <algorithm>
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

void test_melee_reach_and_cooldown() {
    std::printf("a bite lands ahead without aim, a strike lands alongside, and both cool down\n");
    Combat combat;
    combat.player_element = game::Element::None;  // the bare numbers, no burn after
    combat.reset(nullptr, Vec3::zero(), 7u);
    const FlightState player = player_at(Vec3::zero());
    const float health = combat.tuning.sentinel_health;

    // Twenty metres ahead but 25 degrees off the nose: twice the breath cone,
    // nothing the fireball would do without aim, and exactly what the bite is
    // for. (Measured from the mouth, which sits ahead of the body, the angle
    // is wider still.)
    const float off = radians(25.0f);
    isolate_sentinel(combat, Vec3{std::sin(off) * 20.0f, 0.0f, -std::cos(off) * 20.0f});
    CombatInput bite;
    bite.melee = true;
    game::CombatEvents events = combat.update(1.0f / 60.0f, player, bite);
    CHECK(events.melee_swung);
    CHECK(events.melee_hit == game::MeleeKind::Bite);
    CHECK(near(combat.sentinels()[0].health, health - combat.tuning.bite_damage, 1e-3f));
    CHECK(combat.melee_cooldown() > 0.9f);

    // Held down: nothing more happens until the cooldown has run.
    events = combat.update(1.0f / 60.0f, player, bite);
    CHECK(!events.melee_swung);
    CHECK(near(combat.sentinels()[0].health, health - combat.tuning.bite_damage, 1e-3f));
    for (int i = 0; i < 200; ++i) combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(near(combat.melee_cooldown(), 0.0f));

    // Directly alongside, out of the bite cone but inside the claw's reach.
    isolate_sentinel(combat, Vec3{12.0f, 0.0f, 0.0f});
    events = combat.update(1.0f / 60.0f, player, bite);
    CHECK(events.melee_hit == game::MeleeKind::Strike);
    CHECK(near(combat.sentinels()[0].health, health - combat.tuning.strike_damage, 1e-3f));
    for (int i = 0; i < 200; ++i) combat.update(1.0f / 60.0f, player, CombatInput{});

    // Ahead but past the bite's reach: a swing at empty air, still a swing.
    isolate_sentinel(combat, Vec3{0.0f, 0.0f, -80.0f});
    events = combat.update(1.0f / 60.0f, player, bite);
    CHECK(events.melee_swung);
    CHECK(events.melee_hit == game::MeleeKind::None);
    CHECK(near(combat.sentinels()[0].health, health, 1e-3f));
}

void test_towers_answer_a_standing_dragon() {
    std::printf("towers: stone resists the breath, bites neither stun nor shove, and a\n"
                "  grounded or close dragon draws faster fire\n");
    // Breath: the same second of flame on a drone and on a tower.
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 5u);
    for (auto& s : combat.sentinels()) retire(s);
    combat.spawn_defence(Vec3{0.0f, 0.0f, -40.0f});
    game::Sentinel& tower = combat.sentinels().back();
    CHECK(tower.ground);
    const FlightState player = player_at(Vec3::zero());
    CombatInput hold;
    hold.breath = true;
    const float before = tower.health;
    for (int i = 0; i < 30; ++i) combat.update(1.0f / 60.0f, player, hold);
    const float tower_loss = before - combat.sentinels().back().health;
    Combat drone_fight;
    drone_fight.reset(nullptr, Vec3::zero(), 5u);
    drone_fight.tuning.sentinel_health = 500.0f;
    isolate_sentinel(drone_fight, Vec3{0.0f, 0.0f, -40.0f});
    const float drone_before = drone_fight.sentinels()[0].health;
    for (int i = 0; i < 30; ++i) drone_fight.update(1.0f / 60.0f, player, hold);
    const float drone_loss = drone_before - drone_fight.sentinels()[0].health;
    std::printf("  half a second of breath: tower -%.1f, drone -%.1f\n", tower_loss, drone_loss);
    CHECK(tower_loss > 0.0f);
    CHECK(near(tower_loss, drone_loss * combat.tuning.defence_breath_resist, 0.5f));

    // A bite lands but does not stun or move it.
    Combat bite_fight;
    bite_fight.reset(nullptr, Vec3::zero(), 7u);
    for (auto& s : bite_fight.sentinels()) retire(s);
    bite_fight.spawn_defence(Vec3{0.0f, 0.0f, -18.0f});
    CombatInput bite;
    bite.melee = true;
    const game::CombatEvents events = bite_fight.update(1.0f / 60.0f, player, bite);
    const game::Sentinel& bitten = bite_fight.sentinels().back();
    CHECK(events.melee_hit != game::MeleeKind::None);
    CHECK(bitten.health < bitten.max_health);
    CHECK(bitten.stun == 0.0f);
    CHECK(near(bitten.centre.z, -18.0f, 1e-3f));

    // Rate of fire: a dragon cruising 350 m out, then standing 150 m out.
    auto shots_in = [](bool grounded, float distance) {
        Combat c;
        c.tuning.max_health = 1e6f;
        c.reset(nullptr, Vec3::zero(), 9u);
        for (auto& s : c.sentinels()) retire(s);
        c.spawn_defence(Vec3::zero());
        FlightState target = player_at(Vec3{distance, 20.0f, 0.0f});
        target.grounded = grounded;
        int shots = 0;
        for (int i = 0; i < 600; ++i) {
            c.update(1.0f / 60.0f, target, CombatInput{});
            for (const game::Projectile& p : c.projectiles()) {
                if (p.alive && p.team == game::Team::Hostile &&
                    p.life > c.tuning.fireball_lifetime - 1.5f / 60.0f) {
                    ++shots;
                }
            }
        }
        return shots;
    };
    const int cruising = shots_in(false, 350.0f);
    const int standing = shots_in(true, 150.0f);
    std::printf("  shots in 10 s: cruising at 350 m %d, standing at 150 m %d\n", cruising, standing);
    CHECK(cruising >= 3);
    // The rate is neutral now (defence_close_rate 1): what a standing dragon
    // draws is tighter aim and the splash, not more bolts.
    CHECK(standing >= cruising);
}

void test_player_size_scales_the_body() {
    std::printf("a grown player is a bigger target: a bolt 10 m wide misses at 1x, hits at 1.3x\n");
    auto takes = [](float size) {
        Combat c;
        c.reset(nullptr, Vec3::zero(), 3u, 0);
        c.player_size = size;
        const FlightState player = player_at(Vec3::zero());
        // A hostile round passing 10 m to the side of the player's centre.
        c.fire_hostile(Vec3{10.0f, 0.0f, 200.0f}, Vec3{0.0f, 0.0f, -300.0f}, 10.0f);
        float taken = 0.0f;
        for (int i = 0; i < 90; ++i) taken += c.update(1.0f / 60.0f, player, CombatInput{}).damage_taken;
        return taken;
    };
    CHECK(takes(1.0f) == 0.0f);
    CHECK(takes(1.3f) > 0.0f);
    CHECK(takes(0.8f) == 0.0f);
}

void test_melee_stuns_knocks_and_chains() {
    std::printf("a bite stuns and knocks its target, and hits chain inside the window\n");
    Combat combat;
    combat.player_element = game::Element::None;  // the bare numbers, no burn after
    combat.reset(nullptr, Vec3::zero(), 7u);
    // Enough health to take the chain: at the default 60 the second, stepped
    // hit killed the target and the test measured the remainder.
    combat.tuning.sentinel_health = 300.0f;
    const FlightState player = player_at(Vec3::zero());
    const float health = combat.tuning.sentinel_health;
    const Vec3 spot{0.0f, 0.0f, -18.0f};
    isolate_sentinel(combat, spot);
    CombatInput bite;
    bite.melee = true;

    game::CombatEvents events = combat.update(1.0f / 60.0f, player, bite);
    CHECK(events.melee_hit == game::MeleeKind::Bite);
    CHECK(events.melee_combo == 1);
    const game::Sentinel& target = combat.sentinels()[0];
    CHECK(target.stun > 0.0f);
    // The orbit centre moved away from the mouth (further out along -Z).
    CHECK(target.centre.z < spot.z - 0.5f);
    const float first = health - target.health;
    CHECK(near(first, combat.tuning.bite_damage, 1e-3f));

    // Second hit inside the combo window: stepped damage.
    for (int i = 0; i < 40; ++i) combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(combat.melee_combo() == 1);
    combat.sentinels()[0].centre = spot;  // put it back in reach
    const float before = combat.sentinels()[0].health;
    events = combat.update(1.0f / 60.0f, player, bite);
    CHECK(events.melee_combo == 2);
    std::printf("  second hit: %.1f damage (kind %d), first was %.1f\n",
                before - combat.sentinels()[0].health, int(events.melee_hit), first);
    CHECK(near(before - combat.sentinels()[0].health,
               combat.tuning.bite_damage * (1.0f + combat.tuning.melee_combo_bonus), 1e-3f));

    // Let the window lapse: the chain is gone.
    for (int i = 0; i < 120; ++i) combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(combat.melee_combo() == 0);
}

void test_training_room_is_passive() {
    std::printf("training dummies sit still ahead, never fire, and come back fast\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 7u);
    const FlightState player = player_at(Vec3::zero());
    combat.spawn_training(Vec3::zero(), Vec3::forward(), Vec3::right());
    CHECK(combat.sentinels().size() == 6);
    // The first dummy sits ahead, and a straight flight brings it into the
    // bite cone: from twenty metres short of it, dead ahead, it is a bite.
    const Vec3 first = combat.sentinels()[0].position;
    CHECK(first.z < 0.0f);
    const FlightState approaching = player_at(first + Vec3{0.0f, 0.0f, 20.0f});
    CHECK(game::melee_reach(combat.muzzle(approaching), Vec3::forward(), approaching.position,
                            first, combat.tuning.sentinel_radius,
                            combat.tuning) == game::MeleeKind::Bite);
    // Ten seconds in reach of six dummies: not a single hostile round, and
    // nothing moved.
    const float health = combat.health();
    for (int i = 0; i < 600; ++i) combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(near(combat.health(), health, 1e-3f));
    CHECK(combat.projectiles().empty() ||
          std::none_of(combat.projectiles().begin(), combat.projectiles().end(),
                       [](const game::Projectile& p) { return p.alive; }));
    CHECK(near(distance(combat.sentinels()[0].position, first), 0.0f, 1e-3f));
    // Killed, a dummy is back inside three seconds.
    combat.sentinels()[0].health = 1.0f;
    CombatInput bite;
    bite.melee = true;
    combat.update(1.0f / 60.0f, approaching, bite);
    CHECK(!combat.sentinels()[0].alive);
    for (int i = 0; i < 200; ++i) combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(combat.sentinels()[0].alive);
}

void test_melee_gesture_follows_the_mark() {
    std::printf("jaws for a mark ahead, a claw alongside, the tail behind\n");
    const Vec3 body = Vec3::zero();
    const Vec3 forward = Vec3::forward();  // -Z
    const Vec3 right = Vec3::right();      // +X
    float side = 0.0f;
    CHECK(game::melee_gesture_for(body, forward, right, Vec3{3.0f, 0.0f, -20.0f}, side) ==
          game::MeleeGesture::Bite);
    CHECK(game::melee_gesture_for(body, forward, right, Vec3{15.0f, 0.0f, -2.0f}, side) ==
          game::MeleeGesture::Claw);
    CHECK(side > 0.0f);
    CHECK(game::melee_gesture_for(body, forward, right, Vec3{-15.0f, 0.0f, 1.0f}, side) ==
          game::MeleeGesture::Claw);
    CHECK(side < 0.0f);
    CHECK(game::melee_gesture_for(body, forward, right, Vec3{2.0f, -3.0f, 18.0f}, side) ==
          game::MeleeGesture::Tail);
    CHECK(side > 0.0f);
}

void test_hostile_melee_reaches_the_player() {
    std::printf("a bot's bite hurts the player only within reach\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 7u);
    retire_all(combat);
    const FlightState player = player_at(Vec3::zero());
    const float health = combat.health();

    // A rival ten metres ahead, facing back at the player: bite.
    combat.hostile_melee(Vec3{0.0f, 0.0f, -10.0f}, Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, -15.0f});
    game::CombatEvents events = combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(events.bitten);
    CHECK(near(combat.health(), health - combat.tuning.hostile_melee_damage, 1e-3f));
    CHECK(events.took_damage);
    // Knocked away from the biter's mouth, which was ahead: the shove points +Z.
    CHECK(events.knockback.z > 1.0f);

    // The same rival facing away, but close alongside: a strike at 60%.
    const float after_bite = combat.health();
    combat.hostile_melee(Vec3{10.0f, 0.0f, -8.0f}, Vec3{0.0f, 0.0f, -1.0f}, Vec3{10.0f, 0.0f, 0.0f});
    events = combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(events.bitten);
    CHECK(near(combat.health(), after_bite - combat.tuning.hostile_melee_damage * 0.6f, 1e-3f));

    // A rival a hundred metres out swings at nothing.
    const float before = combat.health();
    combat.hostile_melee(Vec3{0.0f, 0.0f, -100.0f}, Vec3{0.0f, 0.0f, 1.0f}, Vec3{0.0f, 0.0f, -105.0f});
    events = combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(!events.bitten);
    CHECK(near(combat.health(), before, 1e-3f));
}

void test_fireball_hits_and_kills() {
    std::printf("fireballs damage and destroy a sentinel\n");
    Combat combat;
    combat.player_element = game::Element::None;  // the bare numbers, no burn after
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

void test_intercept_point() {
    std::printf("intercept prediction actually meets a moving target\n");
    const Vec3 origin = Vec3::zero();
    const float speed = 200.0f;

    // A stationary target needs no lead.
    const Vec3 still{0.0f, 0.0f, -500.0f};
    CHECK(near(length(game::intercept_point(origin, still, Vec3::zero(), speed) - still), 0.0f,
               0.01f));

    // A crossing target does. Verify by flying the shot: aim at the predicted
    // point, then check the target is there when the projectile arrives.
    const Vec3 target{0.0f, 0.0f, -500.0f};
    const Vec3 velocity{60.0f, 0.0f, 0.0f};
    const Vec3 aim = game::intercept_point(origin, target, velocity, speed);
    const float flight_time = length(aim - origin) / speed;
    const Vec3 where_it_will_be = target + velocity * flight_time;
    CHECK(near(length(aim - where_it_will_be), 0.0f, 0.5f));
    // And the lead is on the side the target is going.
    CHECK(aim.x > target.x);

    // Degenerate speed must not divide by zero.
    CHECK(near(length(game::intercept_point(origin, target, velocity, 0.0f) - target), 0.0f));
}

void test_lock_acquires_and_holds() {
    std::printf("lock acquires inside the cone and holds outside it\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 41u);
    retire_all(combat);
    combat.sentinels()[0].alive = true;
    combat.sentinels()[0].respawn_timer = 0.0f;
    combat.sentinels()[0].health = combat.tuning.sentinel_health;

    auto place = [&](Vec3 position) {
        game::Sentinel& sentinel = combat.sentinels()[0];
        sentinel.position = position;
        sentinel.centre = position;
        sentinel.orbit_radius = 0.0f;
        sentinel.orbit_speed = 0.0f;
        sentinel.bob = 0.0f;
        sentinel.fire_timer = 1e6f;
    };

    // Dead ahead: acquired.
    place(Vec3{0.0f, 0.0f, -400.0f});
    combat.update(1.0f / 60.0f, player_at(Vec3::zero()), CombatInput{});
    CHECK(combat.has_lock());
    CHECK(combat.locked_index() == 0);

    // Now well outside the acquisition cone but inside the hold cone: the lock
    // must survive, which is the whole point of the hysteresis.
    const float hold_angle = radians(combat.tuning.lock_cone_deg + 15.0f);
    place(Vec3{std::sin(hold_angle) * 400.0f, 0.0f, -std::cos(hold_angle) * 400.0f});
    combat.update(1.0f / 60.0f, player_at(Vec3::zero()), CombatInput{});
    CHECK(combat.has_lock());

    // Directly behind: dropped.
    place(Vec3{0.0f, 0.0f, 400.0f});
    combat.update(1.0f / 60.0f, player_at(Vec3::zero()), CombatInput{});
    CHECK(!combat.has_lock());

    // A target starting outside the acquisition cone is never picked up.
    const float wide = radians(combat.tuning.lock_cone_deg + 12.0f);
    place(Vec3{std::sin(wide) * 400.0f, 0.0f, -std::cos(wide) * 400.0f});
    combat.update(1.0f / 60.0f, player_at(Vec3::zero()), CombatInput{});
    CHECK(!combat.has_lock());

    // Beyond lock range, dead ahead: also not picked up.
    place(Vec3{0.0f, 0.0f, -(combat.tuning.lock_range + 500.0f)});
    combat.update(1.0f / 60.0f, player_at(Vec3::zero()), CombatInput{});
    CHECK(!combat.has_lock());
}

// The reason aim assist exists: a shot that the player pointed roughly at a
// target should connect. This is the test that would have caught "I only land
// very few attacks".
void test_aim_assist_lands_an_off_axis_shot() {
    std::printf("aim assist turns a near miss into a hit\n");
    auto shots_that_hit = [&](float assist, float off_axis_degrees, float range) {
        const float off_axis = radians(off_axis_degrees);
        const Vec3 target{std::sin(off_axis) * range, 0.0f, -std::cos(off_axis) * range};
        Combat combat;
        combat.reset(nullptr, Vec3::zero(), 47u);
        combat.tuning.aim_assist = assist;
        combat.tuning.fireball_blast_radius = 0.0f;
        combat.tuning.sentinel_health = 1e6f;  // survives, so every hit is counted
        retire_all(combat);
        game::Sentinel& sentinel = combat.sentinels()[0];
        sentinel.alive = true;
        sentinel.respawn_timer = 0.0f;
        sentinel.health = combat.tuning.sentinel_health;
        sentinel.max_health = combat.tuning.sentinel_health;
        sentinel.position = target;
        sentinel.centre = target;
        sentinel.orbit_radius = 0.0f;
        sentinel.orbit_speed = 0.0f;
        sentinel.bob = 0.0f;
        sentinel.fire_timer = 1e6f;

        const FlightState player = player_at(Vec3::zero());
        CombatInput fire;
        fire.fire = true;
        int hits = 0;
        for (int i = 0; i < 400; ++i) {
            hits += combat.update(1.0f / 60.0f, player, i == 0 ? fire : CombatInput{}).hits_dealt;
        }
        return hits;
    };

    // 14 degrees off the nose at 500 m is about 120 m of miss: hopeless unaided,
    // certain with full assist.
    CHECK(shots_that_hit(0.0f, 14.0f, 500.0f) == 0);
    CHECK(shots_that_hit(1.0f, 14.0f, 500.0f) == 1);

    // The case that matters: a shot the player would call "on target" -- inside
    // the lock cone at a normal engagement range -- has to land on the default
    // setting, or the assist is decoration. Partial assist leaves residual
    // error proportional to range, and that residual is the whole experience.
    Combat defaults;
    CHECK(shots_that_hit(defaults.tuning.aim_assist, 10.0f, 400.0f) == 1);
    CHECK(shots_that_hit(defaults.tuning.aim_assist, 6.0f, 700.0f) == 1);
}

// Breath follows the same assisted axis, so the flame drawn is the flame that
// damages. A target just outside the raw cone but inside the assisted one must
// burn.
void test_breath_follows_the_lock() {
    std::printf("breath sweeps onto the locked target\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 53u);
    retire_all(combat);
    game::Sentinel& sentinel = combat.sentinels()[0];
    sentinel.alive = true;
    sentinel.respawn_timer = 0.0f;
    sentinel.health = combat.tuning.sentinel_health;
    sentinel.max_health = combat.tuning.sentinel_health;

    // Outside the 12 degree breath cone, inside the 30 degree lock cone.
    const float angle = radians(20.0f);
    const float range = combat.tuning.breath_range * 0.6f;
    sentinel.position = Vec3{std::sin(angle) * range, 0.0f, -std::cos(angle) * range};
    sentinel.centre = sentinel.position;
    sentinel.orbit_radius = 0.0f;
    sentinel.orbit_speed = 0.0f;
    sentinel.bob = 0.0f;
    sentinel.fire_timer = 1e6f;

    const FlightState player = player_at(Vec3::zero());
    CombatInput hold;
    hold.breath = true;

    combat.tuning.aim_assist = 1.0f;
    combat.update(0.2f, player, hold);
    CHECK(combat.has_lock());
    CHECK(combat.sentinels()[0].health < combat.tuning.sentinel_health);
    // And the cone really is pointing at it, not merely reporting a hit.
    CHECK(game::point_in_cone(combat.sentinels()[0].position, combat.breath_origin(),
                              combat.breath_direction(),
                              radians(combat.tuning.breath_half_angle_deg),
                              combat.tuning.breath_range));
}

void test_lock_prefers_near_and_cycles() {
    std::printf("lock prefers the near target and relock walks candidates\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 61u);
    retire_all(combat);
    // Two targets in the cone: a distant one dead ahead, a near one 10 degrees
    // off. The near one is the one the player means.
    auto park = [&](game::Sentinel& sentinel, Vec3 position) {
        sentinel.alive = true;
        sentinel.respawn_timer = 0.0f;
        sentinel.health = combat.tuning.sentinel_health;
        sentinel.max_health = combat.tuning.sentinel_health;
        sentinel.position = position;
        sentinel.centre = position;
        sentinel.orbit_radius = 0.0f;
        sentinel.orbit_speed = 0.0f;
        sentinel.bob = 0.0f;
        sentinel.fire_timer = 1e6f;
    };
    park(combat.sentinels()[0], Vec3{0.0f, 0.0f, -1400.0f});  // far, dead ahead
    const float off = radians(10.0f);
    park(combat.sentinels()[1],
         Vec3{std::sin(off) * 250.0f, 0.0f, -std::cos(off) * 250.0f});  // near, off-axis

    const FlightState player = player_at(Vec3::zero());
    combat.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(combat.locked_index() == 1);  // near wins: 10 deg + 5 < 0 deg + 28

    // Relock walks to the other candidate, and again wraps back.
    CombatInput cycle;
    cycle.cycle_target = true;
    combat.update(1.0f / 60.0f, player, cycle);
    CHECK(combat.locked_index() == 0);
    combat.update(1.0f / 60.0f, player, cycle);
    CHECK(combat.locked_index() == 1);
}

void test_hostile_breath_and_mouth_muzzle() {
    std::printf("hostile flames burn the player in the cone; fire leaves the mouth\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 71u);
    retire_all(combat);
    const FlightState player = player_at(Vec3::zero());

    // A flame from ahead, pointing at the player: damage arrives, attributed to
    // the flame's origin.
    const float start = combat.health();
    combat.hostile_breath(Vec3{0.0f, 0.0f, -80.0f}, Vec3{0.0f, 0.0f, 1.0f});
    game::CombatEvents events = combat.update(0.5f, player, CombatInput{});
    CHECK(combat.health() < start);
    CHECK(events.took_damage);
    CHECK(near(events.damage_from.z, -80.0f, 1.0f));
    // The flame is drawable this frame.
    CHECK(combat.hostile_breaths().size() == 1);

    // Pointing away: no damage. And the buffer drains each update.
    const float after = combat.health();
    combat.hostile_breath(Vec3{0.0f, 0.0f, -80.0f}, Vec3{0.0f, 0.0f, -1.0f});
    combat.update(0.5f, player, CombatInput{});
    CHECK(near(combat.health(), after, 1e-3f));
    combat.update(0.5f, player, CombatInput{});
    CHECK(combat.hostile_breaths().empty());

    // The muzzle override moves where the player's own fire starts.
    combat.set_muzzle(Vec3{3.0f, 2.0f, -9.0f});
    CombatInput hold;
    hold.breath = true;
    combat.update(1.0f / 60.0f, player, hold);
    CHECK(near(combat.breath_origin().x, 3.0f, 1e-3f));
    CHECK(near(combat.breath_origin().z, -9.0f, 1e-3f));
}

void test_abilities() {
    std::printf("abilities: the charged shot, the ram, the fury\n");
    const FlightState player = player_at(Vec3::zero());
    const float dt = 1.0f / 60.0f;
    // A tap is a plain shot, on the release.
    {
        Combat c;
        c.reset(nullptr, Vec3::zero(), 21u, 0);
        c.abilities.charged_shot = true;
        CombatInput press;
        press.fire = press.fire_held = true;
        c.update(dt, player, press);
        const game::CombatEvents e = c.update(dt, player, CombatInput{});
        CHECK(e.fired && !e.charged_fired);
        bool big = false;
        for (const auto& p : c.projectiles()) big |= p.alive && p.charged;
        CHECK(!big);
    }
    // Held, it charges and leaves by itself at full: bigger, harder, heavier.
    {
        Combat c;
        c.reset(nullptr, Vec3::zero(), 21u, 0);
        c.abilities.charged_shot = true;
        CombatInput hold;
        hold.fire_held = true;
        bool charged = false;
        for (int i = 0; i < 90 && !charged; ++i) charged = c.update(dt, player, hold).charged_fired;
        CHECK(charged);
        float damage = 0.0f;
        for (const auto& p : c.projectiles()) {
            if (p.alive && p.charged) damage = p.damage;
        }
        CHECK(std::fabs(damage - c.tuning.fireball_damage * c.tuning.charged_damage) < 0.01f);
    }
    // A charged shot seeks: a target 25 degrees off the nose, beyond what
    // the aim assist bends, is still struck by a full charge.
    {
        Combat c;
        c.reset(nullptr, Vec3::zero(), 23u, 0);
        c.abilities.charged_shot = true;
        c.tuning.aim_assist = 0.0f;  // no help from the lock's bend: only the seek
        c.tuning.fireball_gravity = 0.0f;
        c.tuning.fireball_blast_radius = 0.0f;
        const int slot = c.spawn_external(1000.0f, 6.0f);
        const Vec3 off = Vec3{std::sin(core::radians(25.0f)), 0.0f, -std::cos(core::radians(25.0f))} * 300.0f;
        c.drive_external(slot, off, Vec3::zero());
        c.tuning.lock_cone_deg = 30.0f;
        CombatInput hold;
        hold.fire_held = true;
        bool fired = false;
        for (int i = 0; i < 300; ++i) {
            const game::CombatEvents e = c.update(dt, player, fired ? CombatInput{} : hold);
            fired = fired || e.charged_fired;
        }
        CHECK(fired);
        CHECK(c.sentinels()[size_t(slot)].health < 1000.0f - c.tuning.fireball_damage);
    }
    // The pounce: a boost with a target ahead picks it; one behind does not.
    {
        Combat c;
        c.reset(nullptr, Vec3::zero(), 29u, 0);
        c.abilities.ram = true;
        const int ahead = c.spawn_external(500.0f, 6.0f);
        c.drive_external(ahead, Vec3{15.0f, 0.0f, -140.0f}, Vec3::zero());
        CombatInput boost;
        boost.boost = true;
        c.update(dt, player, boost);
        CHECK(c.pounce_target() == ahead);
        Combat behind;
        behind.reset(nullptr, Vec3::zero(), 29u, 0);
        behind.abilities.ram = true;
        const int back = behind.spawn_external(500.0f, 6.0f);
        behind.drive_external(back, Vec3{0.0f, 0.0f, 200.0f}, Vec3::zero());
        behind.update(dt, player, boost);
        CHECK(behind.pounce_target() == -1);
    }
    // Prey: lockable, but behind a real enemy, and never a kill.
    {
        Combat c;
        c.reset(nullptr, Vec3::zero(), 31u, 0);
        const int grazer = c.spawn_external(20.0f, 3.0f);
        c.sentinels()[size_t(grazer)].prey = true;
        c.sentinels()[size_t(grazer)].passive = true;
        c.drive_external(grazer, Vec3{0.0f, 0.0f, -150.0f}, Vec3::zero());
        c.update(dt, player, CombatInput{});
        CHECK(c.locked_index() == grazer);  // alone, it is the lock: the hunt gets the assist
        const int rival = c.spawn_external(80.0f, 6.0f);
        c.drive_external(rival, Vec3{40.0f, 0.0f, -300.0f}, Vec3::zero());
        Combat fresh;
        fresh.reset(nullptr, Vec3::zero(), 31u, 0);
        const int g2 = fresh.spawn_external(20.0f, 3.0f);
        fresh.sentinels()[size_t(g2)].prey = true;
        fresh.drive_external(g2, Vec3{0.0f, 0.0f, -150.0f}, Vec3::zero());
        const int r2 = fresh.spawn_external(80.0f, 6.0f);
        fresh.drive_external(r2, Vec3{40.0f, 0.0f, -300.0f}, Vec3::zero());
        fresh.update(dt, player, CombatInput{});
        CHECK(fresh.locked_index() == r2);  // the one that fights back first
        const game::CombatEvents e = c.apply_hit(grazer, 50.0f, game::Element::None);
        CHECK(!c.sentinels()[size_t(grazer)].alive);
        CHECK(e.kills == 0 && e.prey_killed == 1 && c.kills() == 0);
    }
    // The ram: a boost through a drone damages and stuns it, once.
    {
        Combat c;
        c.reset(nullptr, Vec3::zero(), 21u, 0);
        c.abilities.ram = true;
        const int slot = c.spawn_external(500.0f, 6.0f);
        c.drive_external(slot, Vec3{0.0f, 0.0f, -8.0f}, Vec3::zero());
        CombatInput boost;
        boost.boost = true;
        int rams = 0;
        for (int i = 0; i < 30; ++i) rams += c.update(dt, player, i == 0 ? boost : CombatInput{}).rammed;
        CHECK(rams == 1);
        CHECK(c.sentinels()[size_t(slot)].health < 500.0f - c.tuning.ram_damage + 0.1f);
        CHECK(c.sentinels()[size_t(slot)].stun > 0.0f);
    }
    // The fury: fills from damage; released, it hits what is inside its
    // radius and not what is outside, and empties.
    {
        Combat c;
        c.reset(nullptr, Vec3::zero(), 21u, 0);
        c.abilities.fury = true;
        c.player_element = game::Element::None;
        const int near = c.spawn_external(1000.0f, 6.0f);
        const int far = c.spawn_external(1000.0f, 6.0f);
        c.drive_external(near, Vec3{0.0f, 0.0f, -60.0f}, Vec3::zero());
        c.drive_external(far, Vec3{0.0f, 0.0f, -600.0f}, Vec3::zero());
        CHECK(c.fury() == 0.0f);
        c.apply_hit(near, 0.5f / c.tuning.fury_per_damage, game::Element::None);
        CHECK(c.fury() > 0.45f && c.fury() < 0.55f);
        c.add_fury(1.0f);
        CombatInput go;
        go.fury = true;
        const float before_near = c.sentinels()[size_t(near)].health;
        const game::CombatEvents e = c.update(dt, player, go);
        CHECK(e.fury_released);
        CHECK(c.sentinels()[size_t(near)].health < before_near - 50.0f);
        CHECK(c.sentinels()[size_t(far)].health == 1000.0f);
        CHECK(c.fury() == 0.0f);
        // Not unlocked: nothing.
        Combat locked;
        locked.reset(nullptr, Vec3::zero(), 21u, 0);
        locked.add_fury(1.0f);
        CHECK(!locked.update(dt, player, go).fury_released);
    }
}

}  // namespace

int main() {
    test_cone();
    test_intercept_point();
    test_lock_acquires_and_holds();
    test_lock_prefers_near_and_cycles();
    test_hostile_breath_and_mouth_muzzle();
    test_aim_assist_lands_an_off_axis_shot();
    test_breath_follows_the_lock();
    test_closest_point_fraction();
    test_melee_reach_and_cooldown();
    test_melee_stuns_knocks_and_chains();
    test_player_size_scales_the_body();
    test_towers_answer_a_standing_dragon();
    test_training_room_is_passive();
    test_melee_gesture_follows_the_mark();
    test_hostile_melee_reaches_the_player();
    test_fireball_hits_and_kills();
    test_projectile_does_not_tunnel();
    test_breath_meter_latches();
    test_breath_damages_only_within_the_cone();
    test_fire_rate_is_limited();
    test_player_takes_damage_and_regenerates();
    test_boost_respects_its_cooldown();
    test_is_deterministic();
    test_no_nans_under_abuse();
    test_abilities();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
