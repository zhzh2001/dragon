// The elements: each hit leaves one status, a body shrugs off its own element,
// and there is no type chart. Pinned here because every one of these is a
// number that is easy to get wrong and hard to see in a fight: a burn that
// never stops, a freeze that re-freezes forever, an arc that chains a valley.
#include <cmath>
#include <cstdio>

#include "game/combat.h"
#include "game/element.h"

using namespace core;
using game::Combat;
using game::CombatInput;
using game::Element;
using game::ElementTuning;
using game::FlightState;
using game::Status;

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

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

FlightState player_at(Vec3 position) {
    FlightState state;
    state.position = position;
    state.orientation = look_rotation(Vec3::forward(), Vec3::up());
    return state;
}

void test_names() {
    std::printf("names round-trip, and an unknown name falls back\n");
    for (int i = 0; i < game::ELEMENT_COUNT; ++i) {
        CHECK(game::element_from_name(game::element_name(Element(i))) == Element(i));
    }
    CHECK(game::element_from_name("FROST") == Element::Frost);
    CHECK(game::element_from_name("plasma", Element::Stone) == Element::Stone);
    CHECK(game::element_breath(Element::Frost).element == Element::Frost);
    CHECK(game::element_breath(Element::Frost).buoyancy < 0.0f);  // frost pours
}

void test_resist_no_chart() {
    std::printf("own element resisted, every other element even: no type chart\n");
    const ElementTuning t;
    const Status clean;
    CHECK(near(game::element_damage_scale(Element::Fire, Element::Fire, clean, t), t.same_resist));
    for (int a = 0; a < game::ELEMENT_COUNT; ++a) {
        for (int d = 0; d < game::ELEMENT_COUNT; ++d) {
            if (a == d) continue;
            CHECK(near(game::element_damage_scale(Element(a), Element(d), clean, t), 1.0f));
        }
    }
    // A drone has no element: it resists nothing and its hits leave nothing.
    CHECK(near(game::element_damage_scale(Element::None, Element::None, clean, t), 1.0f));
    Status s;
    game::apply_element(s, Element::None, Element::Fire, 1.0f, false, t);
    CHECK(!s.any());
    // Corrosion makes everything land harder, own element included.
    Status corroded;
    corroded.corrode = 1.0f;
    CHECK(near(game::element_damage_scale(Element::Tide, Element::Fire, corroded, t),
               1.0f + t.corrode_vulnerability));
    // And a body takes none of its own element's status.
    Status frost;
    for (int i = 0; i < 10; ++i) game::apply_element(frost, Element::Frost, Element::Frost, 1.0f, false, t);
    CHECK(!frost.any());
}

void test_burn_and_douse() {
    std::printf("a burn pays for its time and stops; water puts it out and keeps it out\n");
    const ElementTuning t;
    Status s;
    const game::StatusReport r = game::apply_element(s, Element::Fire, Element::None, 1.0f, false, t);
    CHECK(r.burned);
    float total = 0.0f;
    for (int i = 0; i < 600; ++i) total += game::tick_status(s, 1.0f / 60.0f, t);
    CHECK(near(total, t.burn_dps * t.burn_time, 0.05f));
    CHECK(s.burn == 0.0f);
    // Burning, then drenched: doused.
    game::apply_element(s, Element::Fire, Element::None, 1.0f, false, t);
    const game::StatusReport d = game::apply_element(s, Element::Tide, Element::None, 1.0f, false, t);
    CHECK(d.doused && s.burn == 0.0f && s.drench > 0.0f);
    // Drenched: fire does not catch.
    game::apply_element(s, Element::Fire, Element::None, 1.0f, false, t);
    CHECK(s.burn == 0.0f);
}

void test_chill_freeze() {
    std::printf("chill builds and slows, full chill freezes once, and it thaws\n");
    const ElementTuning t;
    Status s;
    game::apply_element(s, Element::Frost, Element::None, 1.0f, false, t);
    CHECK(s.chill > 0.0f && s.frozen == 0.0f);
    CHECK(s.slow(t) > 0.0f && s.slow(t) < t.chill_slow);
    game::apply_element(s, Element::Frost, Element::None, 1.0f, false, t);
    const game::StatusReport r = game::apply_element(s, Element::Frost, Element::None, 1.0f, false, t);
    CHECK(r.froze);
    CHECK(near(s.frozen, t.freeze_time));
    CHECK(s.slow(t) == 1.0f && s.jammed());
    // More frost on a frozen body does not stack another freeze.
    game::apply_element(s, Element::Frost, Element::None, 5.0f, false, t);
    CHECK(s.chill == 0.0f);
    // The player's freeze is the shorter one.
    Status p;
    for (int i = 0; i < 3; ++i) game::apply_element(p, Element::Frost, Element::None, 1.0f, true, t);
    CHECK(near(p.frozen, t.player_freeze_time));
    // Left alone, chill thaws.
    Status c;
    game::apply_element(c, Element::Frost, Element::None, 1.0f, false, t);
    for (int i = 0; i < 300; ++i) game::tick_status(c, 1.0f / 60.0f, t);
    CHECK(c.chill == 0.0f);
}

void test_breath_builds_status() {
    std::printf("a held frost breath freezes a target in about a second and a half\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 3u, 0);
    combat.player_element = Element::Frost;
    const int slot = combat.spawn_external(1e6f, 6.0f);
    combat.drive_external(slot, Vec3{0.0f, 0.0f, -60.0f}, Vec3::zero());
    CombatInput breathe;
    breathe.breath = true;
    const FlightState player = player_at(Vec3::zero());
    float frozen_at = -1.0f;
    for (int i = 0; i < 360 && frozen_at < 0.0f; ++i) {
        combat.update(1.0f / 60.0f, player, breathe);
        if (combat.sentinels()[size_t(slot)].status.frozen > 0.0f) frozen_at = float(i) / 60.0f;
    }
    std::printf("  frozen after %.2f s of breath\n", double(frozen_at));
    CHECK(frozen_at > 1.0f && frozen_at < 3.0f);
    // A frozen dragon is stunned: nobody is flying it.
    CHECK(combat.sentinels()[size_t(slot)].stun > 0.0f);
}

void test_storm_arcs_once() {
    std::printf("a storm fireball arcs to the next target for a share, and no further\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 5u, 0);
    combat.player_element = Element::Storm;
    combat.tuning.fireball_gravity = 0.0f;
    combat.tuning.fireball_blast_radius = 0.0f;
    const int a = combat.spawn_external(500.0f, 6.0f);
    const int b = combat.spawn_external(500.0f, 6.0f);
    const int c = combat.spawn_external(500.0f, 6.0f);
    combat.drive_external(a, Vec3{0.0f, 0.0f, -200.0f}, Vec3::zero());
    combat.drive_external(b, Vec3{50.0f, 0.0f, -200.0f}, Vec3::zero());   // in reach of a
    combat.drive_external(c, Vec3{100.0f, 0.0f, -200.0f}, Vec3::zero());  // in reach of b only
    CombatInput fire;
    fire.fire = true;
    const FlightState player = player_at(Vec3::zero());
    bool arced = false;
    for (int i = 0; i < 120; ++i) {
        combat.update(1.0f / 60.0f, player, i == 0 ? fire : CombatInput{});
        arced |= !combat.arcs().empty();
    }
    const auto& s = combat.sentinels();
    const float da = 500.0f - s[size_t(a)].health;
    const float db = 500.0f - s[size_t(b)].health;
    const float dc = 500.0f - s[size_t(c)].health;
    std::printf("  damage: struck %.1f, arced %.1f, beyond %.1f\n", double(da), double(db), double(dc));
    CHECK(arced);
    CHECK(near(da, combat.tuning.fireball_damage, 0.01f));
    CHECK(near(db, combat.tuning.fireball_damage * combat.tuning.elements.shock_chain_share, 0.01f));
    CHECK(dc == 0.0f);
}

void test_player_jammed_and_resists() {
    std::printf("a frozen player cannot fire; a frost player shrugs off frost\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 9u, 0);
    combat.player_element = Element::Fire;
    const FlightState player = player_at(Vec3::zero());
    // Three frost bolts point-blank.
    for (int i = 0; i < 3; ++i) {
        combat.fire_hostile(Vec3{0.0f, 0.0f, -20.0f}, Vec3{0.0f, 0.0f, 200.0f}, 1.0f, Element::Frost);
        for (int f = 0; f < 10; ++f) combat.update(1.0f / 60.0f, player, CombatInput{});
    }
    CHECK(combat.player_status().frozen > 0.0f);
    CHECK(combat.player_slow() == 1.0f);
    CombatInput fire;
    fire.fire = true;
    const game::CombatEvents e = combat.update(1.0f / 60.0f, player, fire);
    CHECK(!e.fired);

    Combat rime;
    rime.reset(nullptr, Vec3::zero(), 9u, 0);
    rime.player_element = Element::Frost;
    const float before = rime.health();
    for (int i = 0; i < 3; ++i) {
        rime.fire_hostile(Vec3{0.0f, 0.0f, -20.0f}, Vec3{0.0f, 0.0f, 200.0f}, 10.0f, Element::Frost);
        for (int f = 0; f < 10; ++f) rime.update(1.0f / 60.0f, player, CombatInput{});
    }
    CHECK(rime.player_status().frozen == 0.0f && rime.player_status().chill == 0.0f);
    CHECK(near(before - rime.health(), 3.0f * 10.0f * rime.tuning.elements.same_resist, 0.01f));
}

void test_frozen_tower_holds_fire() {
    std::printf("a frozen tower holds its fire; a burning one burns down\n");
    Combat combat;
    combat.reset(nullptr, Vec3::zero(), 11u, 0);
    combat.player_element = Element::Frost;
    const int t = combat.spawn_defence(Vec3{0.0f, 0.0f, -100.0f}, 8.0f, 18.0f, Element::Stone);
    auto& tower = combat.sentinels()[size_t(t)];
    for (int i = 0; i < 3; ++i) {
        combat.apply_hit(t, 1.0f, Element::Frost);
    }
    CHECK(tower.status.frozen > 0.0f && tower.stun > 0.0f);
    const FlightState player = player_at(Vec3::zero());
    tower.fire_timer = 0.0f;
    size_t shots = 0;
    for (int i = 0; i < 30; ++i) combat.update(1.0f / 60.0f, player, CombatInput{});
    for (const auto& p : combat.projectiles()) shots += p.alive ? 1u : 0u;
    CHECK(shots == 0);

    const float before = tower.health;
    combat.apply_hit(t, 0.0f, Element::Fire);
    for (int i = 0; i < 60; ++i) combat.update(1.0f / 60.0f, player_at(Vec3{0.0f, 5000.0f, 0.0f}), CombatInput{});
    CHECK(near(before - tower.health, combat.tuning.elements.burn_dps, 0.2f));
}

void test_rebalance() {
    std::printf("tide weakens the drenched; burns do not stop healing; the descent scales damage\n");
    const FlightState player = player_at(Vec3::zero());
    // A drenched player's fireball lands softer.
    Combat wet;
    wet.reset(nullptr, Vec3::zero(), 13u, 0);
    wet.player_element = Element::None;
    const int slot = wet.spawn_external(500.0f, 6.0f);
    wet.drive_external(slot, Vec3{0.0f, 0.0f, -60.0f}, Vec3::zero());
    wet.fire_hostile(Vec3{0.0f, 0.0f, -20.0f}, Vec3{0.0f, 0.0f, 200.0f}, 1.0f, Element::Tide);
    for (int f = 0; f < 10; ++f) wet.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(wet.player_status().drench > 0.0f);
    wet.apply_hit(slot, 10.0f, Element::None);
    CHECK(near(500.0f - wet.sentinels()[size_t(slot)].health, 10.0f * (1.0f - wet.tuning.elements.drench_weaken), 0.01f));

    // Burning: the health still comes back once the hits stop.
    Combat burnt;
    burnt.reset(nullptr, Vec3::zero(), 13u, 0);
    burnt.player_element = Element::Frost;
    burnt.fire_hostile(Vec3{0.0f, 0.0f, -20.0f}, Vec3{0.0f, 0.0f, 200.0f}, 20.0f, Element::Fire);
    for (int f = 0; f < 10; ++f) burnt.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(burnt.player_status().burn > 0.0f);
    const float low = burnt.health();
    for (int f = 0; f < 60 * 12; ++f) burnt.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(burnt.health() > low);  // the regen delay ran from the bolt, not the burn

    // The descent's damage scale reaches every hostile hit.
    Combat deep;
    deep.reset(nullptr, Vec3::zero(), 13u, 0);
    deep.player_element = Element::None;
    deep.hostile_damage_scale = 1.8f;
    const float before = deep.health();
    deep.fire_hostile(Vec3{0.0f, 0.0f, -20.0f}, Vec3{0.0f, 0.0f, 200.0f}, 10.0f, Element::None);
    for (int f = 0; f < 10; ++f) deep.update(1.0f / 60.0f, player, CombatInput{});
    CHECK(near(before - deep.health(), 18.0f, 0.01f));
}

}  // namespace

int main() {
    test_names();
    test_resist_no_chart();
    test_burn_and_douse();
    test_chill_freeze();
    test_breath_builds_status();
    test_storm_arcs_once();
    test_player_jammed_and_resists();
    test_frozen_tower_holds_fire();
    test_rebalance();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
