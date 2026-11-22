// A breath of matter as pieces (game/breath_matter.h): what is thrown, where
// it lands, and what it leaves there.
#include <cmath>
#include <cstdio>

#include "core/math.h"
#include "game/breath_matter.h"

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

float flat_ground(float, float) { return 0.0f; }

int count(const game::BreathMatter& matter, game::MatterKind kind) {
    int n = 0;
    for (const game::MatterPiece& p : matter.pieces()) n += p.kind == kind ? 1 : 0;
    return n;
}

// Hold the breath for `held` seconds from 30 m up, aimed 20 degrees down, then
// let `after` seconds pass.
void breathe(game::BreathMatter& matter, game::Element element, float held, float after) {
    const Vec3 mouth{0.0f, 30.0f, 0.0f};
    const Vec3 aim = normalize(Vec3{0.0f, -0.36f, -1.0f});
    const float dt = 1.0f / 60.0f;
    for (float t = 0.0f; t < held + after; t += dt) {
        if (t < held) matter.emit(element, mouth, aim, Vec3{0.0f, 0.0f, -30.0f}, 150.0f, dt);
        matter.update(dt, flat_ground, -100.0f, {});
    }
}

void test_rates() {
    std::printf("a held breath throws its pieces at the tuned rate; fire and storm throw none\n");
    game::BreathMatter matter;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) matter.emit(game::Element::Frost, Vec3{0.0f, 500.0f, 0.0f}, Vec3::forward(), Vec3::zero(), 150.0f, dt);
    std::printf("  %d lances in a second\n", count(matter, game::MatterKind::IceLance));
    CHECK(std::abs(count(matter, game::MatterKind::IceLance) - int(matter.tuning.lance_rate)) <= 1);
    matter.clear();
    for (int i = 0; i < 60; ++i) matter.emit(game::Element::Fire, Vec3{0.0f, 500.0f, 0.0f}, Vec3::forward(), Vec3::zero(), 150.0f, dt);
    for (int i = 0; i < 60; ++i) matter.emit(game::Element::Storm, Vec3{0.0f, 500.0f, 0.0f}, Vec3::forward(), Vec3::zero(), 150.0f, dt);
    CHECK(matter.pieces().empty());
}

void test_frost_lands() {
    std::printf("lances reach the ground, leave rime and spikes, and the spikes stand\n");
    game::BreathMatter matter;
    breathe(matter, game::Element::Frost, 1.0f, 2.0f);
    std::printf("  %d spikes, %zu marks\n", count(matter, game::MatterKind::IceSpike), matter.marks().size());
    CHECK(count(matter, game::MatterKind::IceLance) == 0);
    CHECK(count(matter, game::MatterKind::IceSpike) >= 3);
    CHECK(!matter.marks().empty());
    for (const game::MatterMark& m : matter.marks()) {
        CHECK(m.element == game::Element::Frost);
        CHECK(std::fabs(m.position.y) < 0.01f);
        CHECK(m.position.z < -20.0f);  // down range, not under the mouth
    }
    for (const game::MatterPiece& p : matter.pieces()) {
        if (p.kind != game::MatterKind::IceSpike) continue;
        CHECK(p.resting);
        CHECK(p.position.y > 0.0f && p.position.y < 2.5f);
    }
    // And they go: nothing is left once every life has run out.
    const float dt = 1.0f / 30.0f;
    for (int i = 0; i < 30 * 40; ++i) matter.update(dt, flat_ground, -100.0f, {});
    CHECK(matter.pieces().empty());
    CHECK(matter.marks().empty());
}

void test_stone_rests() {
    std::printf("boulders break into rubble that comes to rest on the ground\n");
    game::BreathMatter matter;
    breathe(matter, game::Element::Stone, 1.0f, 4.0f);
    int resting = 0, moving = 0;
    for (const game::MatterPiece& p : matter.pieces()) {
        if (p.kind != game::MatterKind::Rubble) continue;
        (p.resting ? resting : moving) += 1;
        if (p.resting) CHECK(p.position.y >= 0.0f && p.position.y < 1.5f);
    }
    std::printf("  %d at rest, %d still moving\n", resting, moving);
    CHECK(resting >= 10);
    CHECK(moving == 0);
    CHECK(count(matter, game::MatterKind::Boulder) == 0);
}

void test_stream_falls() {
    std::printf("water arcs down, sheds drops and wets the ground; acid the same\n");
    game::BreathMatter water;
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 30; ++i) {
        water.emit(game::Element::Tide, Vec3{0.0f, 200.0f, 0.0f}, Vec3::forward(), Vec3::zero(), 150.0f, dt);
        water.update(dt, flat_ground, -100.0f, {});
    }
    CHECK(count(water, game::MatterKind::WaterSlug) >= 5);
    CHECK(count(water, game::MatterKind::WaterSlug) <= game::BreathMatter::kStreamNodes);
    CHECK(count(water, game::MatterKind::WaterDrop) >= 5);
    // Thrown above the aim, it is over the mouth's level at first; held
    // for two seconds, its far end has fallen below it: the stream arcs.
    float highest = 200.0f;
    for (const game::MatterPiece& p : water.pieces()) {
        if (p.kind == game::MatterKind::WaterSlug) highest = maxf(highest, p.position.y);
    }
    CHECK(highest > 200.3f);
    float lowest = 200.0f;
    for (int i = 0; i < 90; ++i) {
        water.emit(game::Element::Tide, Vec3{0.0f, 200.0f, 0.0f}, Vec3::forward(), Vec3::zero(), 150.0f, dt);
        water.update(dt, flat_ground, -100.0f, {});
        for (const game::MatterPiece& p : water.pieces()) {
            if (p.kind == game::MatterKind::WaterSlug) lowest = minf(lowest, p.position.y);
        }
    }
    CHECK(lowest < 199.0f);
    // While the breath is held the stream begins at the mouth.
    Vec3 mouth;
    CHECK(water.stream_held(mouth));
    CHECK(mouth.y == 200.0f);
    water.update(dt, flat_ground, -100.0f, {});
    CHECK(!water.stream_held(mouth));

    game::BreathMatter low;
    breathe(low, game::Element::Tide, 1.0f, 1.0f);
    CHECK(!low.marks().empty());
    if (!low.marks().empty()) CHECK(low.marks()[0].element == game::Element::Tide);
    game::BreathMatter acid;
    breathe(acid, game::Element::Blight, 1.0f, 1.0f);
    CHECK(!acid.marks().empty());
    if (!acid.marks().empty()) CHECK(acid.marks()[0].element == game::Element::Blight);
}

void test_marks_merge() {
    std::printf("marks near each other are one mark; there are never more than the shader holds\n");
    game::BreathMatter matter;
    for (int i = 0; i < 40; ++i) matter.mark(game::Element::Frost, Vec3{0.2f * float(i), 0.0f, 0.0f}, 2.0f, 20.0f);
    CHECK(matter.marks().size() <= 4);
    for (int i = 0; i < 100; ++i) matter.mark(game::Element::Stone, Vec3{30.0f * float(i), 0.0f, 500.0f}, 2.0f, 20.0f);
    CHECK(int(matter.marks().size()) == game::BreathMatter::kMaxMarks);
    // A mark fades in and out.
    game::MatterMark m;
    m.life = 10.0f;
    m.age = 0.0f;
    CHECK(m.strength() == 0.0f);
    m.age = 5.0f;
    CHECK(m.strength() == 1.0f);
    m.age = 9.5f;
    CHECK(m.strength() > 0.0f && m.strength() < 1.0f);
}

void test_targets_and_water() {
    std::printf("a lance bursts on a target in its way; a boulder into water leaves no rubble\n");
    game::BreathMatter matter;
    const float dt = 1.0f / 120.0f;
    std::vector<game::BreathMatter::Target> targets;
    game::BreathMatter::Target target;
    target.position = Vec3{0.0f, 300.0f, -40.0f};
    target.radius = 6.0f;
    targets.push_back(target);
    bool burst_on_target = false;
    for (int i = 0; i < 120; ++i) {
        if (i < 20) matter.emit(game::Element::Frost, Vec3{0.0f, 300.0f, 0.0f}, Vec3::forward(), Vec3::zero(), 150.0f, dt);
        matter.update(dt, flat_ground, -100.0f, targets);
        for (const game::MatterImpact& impact : matter.impacts()) burst_on_target |= impact.on_target;
    }
    CHECK(burst_on_target);
    CHECK(matter.marks().empty());

    game::BreathMatter lake;
    for (int i = 0; i < 600; ++i) {
        if (i < 60) lake.emit(game::Element::Stone, Vec3{0.0f, 20.0f, 0.0f}, Vec3::forward(), Vec3::zero(), 150.0f, 1.0f / 60.0f);
        // The bed is at -5 and the water at 0.
        lake.update(1.0f / 60.0f, [](float, float) { return -5.0f; }, 0.0f, {});
    }
    CHECK(count(lake, game::MatterKind::Rubble) == 0);
    CHECK(lake.marks().empty());
}

void test_budget() {
    std::printf("the pieces never pass the budget\n");
    game::BreathMatter matter;
    matter.tuning.max_pieces = 120;
    const float dt = 1.0f / 60.0f;
    size_t most = 0;
    for (int i = 0; i < 600; ++i) {
        matter.emit(game::Element::Frost, Vec3{0.0f, 12.0f, 0.0f}, normalize(Vec3{0.0f, -0.4f, -1.0f}), Vec3::zero(), 150.0f, dt);
        matter.emit(game::Element::Tide, Vec3{0.0f, 12.0f, 0.0f}, normalize(Vec3{0.0f, -0.4f, -1.0f}), Vec3::zero(), 150.0f, dt);
        matter.update(dt, flat_ground, -100.0f, {});
        most = most > matter.pieces().size() ? most : matter.pieces().size();
    }
    std::printf("  at most %zu pieces\n", most);
    CHECK(most <= 120);
}

}  // namespace

int main() {
    test_rates();
    test_frost_lands();
    test_stone_rests();
    test_stream_falls();
    test_marks_merge();
    test_targets_and_water();
    test_budget();
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
