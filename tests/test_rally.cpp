// Course and run-timing invariants.
//
// The interesting failures here are all about *when* something is detected, not
// whether: a checkpoint missed because the dragon crossed it between two frames
// is invisible in play until someone's fastest run silently fails to count.
#include <cmath>
#include <cstdio>

#include "test_paths.h"
#include "game/autopilot.h"
#include "game/course.h"
#include "game/rally.h"
#include "game/terrain.h"

using namespace core;
using game::Ring;
using game::RunPhase;

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

// A ring at the origin facing down -Z, so flying along -Z passes through it.
Ring origin_ring(float radius = 30.0f) {
    Ring ring;
    ring.position = Vec3::zero();
    ring.orientation = look_rotation(Vec3::forward(), Vec3::up());
    ring.radius = radius;
    return ring;
}

void test_ring_crossing() {
    std::printf("ring crossing detection\n");
    const Ring ring = origin_ring();

    // Straight through the middle, travelling along -Z.
    CHECK(game::test_ring(ring, Vec3{0, 0, 10}, Vec3{0, 0, -10}).passed);

    // Through the plane but outside the radius: a miss, reported as such.
    game::RingCrossing wide = game::test_ring(ring, Vec3{50, 0, 10}, Vec3{50, 0, -10});
    CHECK(!wide.passed);
    CHECK(wide.crossed_plane);
    std::printf("  50 m off centre on a 30 m ring: missed by %.1f m\n", wide.miss_distance);
    CHECK(std::fabs(wide.miss_distance - 20.0f) < 0.5f);

    // Just inside and just outside the rim.
    CHECK(game::test_ring(ring, Vec3{29.0f, 0, 5}, Vec3{29.0f, 0, -5}).passed);
    CHECK(!game::test_ring(ring, Vec3{31.0f, 0, 5}, Vec3{31.0f, 0, -5}).passed);

    // Backwards through the ring must not count, or a course could be run in
    // reverse or a checkpoint re-triggered by drifting back through it.
    CHECK(!game::test_ring(ring, Vec3{0, 0, -10}, Vec3{0, 0, 10}).passed);

    // Never reaching the plane is not a crossing.
    CHECK(!game::test_ring(ring, Vec3{0, 0, 10}, Vec3{0, 0, 2}).passed);
    // Neither is flying past on the near side.
    CHECK(!game::test_ring(ring, Vec3{0, 0, 10}, Vec3{200, 0, 8}).passed);
}

void test_fast_crossing_is_not_missed() {
    std::printf("a fast pass is not missed between frames\n");
    // The whole reason ring tests are segment based. At 140 m/s a 60 Hz frame
    // covers 2.3 m; a point-in-volume test against a thin ring would simply not
    // see it. Here the dragon teleports 300 m in one step, straight through.
    const Ring ring = origin_ring();
    CHECK(game::test_ring(ring, Vec3{0, 0, 150}, Vec3{0, 0, -150}).passed);

    // And at a steep angle through the rim, still inside the radius.
    CHECK(game::test_ring(ring, Vec3{-120, 20, 120}, Vec3{20, -5, -20}).passed);
}

// Drives the dragon along a straight line at a fixed speed, feeding the rally.
void fly_to(game::Rally& rally, game::FlightState& state, Vec3 target, float speed, float dt) {
    while (distance(state.position, target) > speed * dt) {
        const Vec3 direction = normalize(target - state.position);
        state.position += direction * (speed * dt);
        state.orientation = look_rotation(direction, Vec3::up());
        rally.update(state, dt);
    }
}

void test_run_timing() {
    std::printf("run timing and ordering\n");
    game::Course course;
    course.name = "test";
    for (int i = 0; i < 4; ++i) {
        Ring ring;
        ring.position = Vec3{0.0f, 100.0f, -float(i) * 200.0f};
        ring.orientation = look_rotation(Vec3::forward(), Vec3::up());
        ring.radius = 30.0f;
        course.rings.push_back(ring);
    }

    game::Rally rally;
    rally.set_course(course);
    CHECK(rally.phase() == RunPhase::Ready);
    CHECK(rally.next_ring_index() == 0);

    game::FlightState state;
    state.position = Vec3{0.0f, 100.0f, 200.0f};
    const float dt = 1.0f / 60.0f;
    const float speed = 50.0f;

    // Approaching but not yet through: the clock must not have started.
    fly_to(rally, state, Vec3{0.0f, 100.0f, 60.0f}, speed, dt);
    CHECK(rally.phase() == RunPhase::Ready);
    CHECK(rally.elapsed() == 0.0f);

    // Through the whole course.
    fly_to(rally, state, Vec3{0.0f, 100.0f, -700.0f}, speed, dt);
    std::printf("  phase %s, rings %d, time %s\n",
                rally.phase() == RunPhase::Finished ? "finished" : "running",
                rally.rings_passed(), game::format_time(rally.elapsed()).c_str());
    CHECK(rally.phase() == RunPhase::Finished);
    CHECK(rally.rings_passed() == 4);

    // 600 m between the first and last ring at 50 m/s.
    const float expected = 600.0f / speed;
    std::printf("  expected ~%.2f s, got %.2f s\n", expected, rally.last_run_time());
    CHECK(std::fabs(rally.last_run_time() - expected) < 0.2f);

    // Splits must be monotonic, one per ring, starting at zero.
    CHECK(rally.splits().size() == 4);
    CHECK(rally.splits()[0] == 0.0f);
    bool monotonic = true;
    for (size_t i = 1; i < rally.splits().size(); ++i) {
        if (rally.splits()[i] < rally.splits()[i - 1]) monotonic = false;
    }
    CHECK(monotonic);

    // First run on a course is always a record.
    CHECK(rally.last_run_was_record());
    CHECK(rally.has_ghost());
    std::printf("  ghost: %zu samples over %.2f s\n", rally.best_ghost().samples.size(),
                rally.best_ghost().duration);
    CHECK(rally.best_ghost().samples.size() > 10);
}

void test_rings_must_be_taken_in_order() {
    std::printf("rings must be taken in order\n");
    game::Course course;
    course.name = "ordered";
    for (int i = 0; i < 3; ++i) {
        Ring ring;
        // Spread sideways so a later ring can be reached without touching the
        // earlier ones.
        ring.position = Vec3{float(i) * 400.0f, 100.0f, -float(i) * 200.0f};
        ring.orientation = look_rotation(Vec3::forward(), Vec3::up());
        ring.radius = 30.0f;
        course.rings.push_back(ring);
    }

    game::Rally rally;
    rally.set_course(course);

    game::FlightState state;
    // Fly straight through where ring 2 is, skipping 0 and 1 entirely.
    state.position = Vec3{800.0f, 100.0f, -200.0f};
    fly_to(rally, state, Vec3{800.0f, 100.0f, -600.0f}, 50.0f, 1.0f / 60.0f);
    std::printf("  after passing ring 2 first: %d rings, phase %s\n", rally.rings_passed(),
                rally.phase() == RunPhase::Ready ? "ready" : "started");
    CHECK(rally.rings_passed() == 0);
    CHECK(rally.phase() == RunPhase::Ready);
}

void test_ghost_playback() {
    std::printf("ghost playback\n");
    game::GhostRun ghost;
    for (int i = 0; i <= 20; ++i) {
        game::GhostSample sample;
        sample.time = float(i) * 0.5f;
        sample.position = Vec3{float(i) * 10.0f, 100.0f, 0.0f};
        sample.orientation = Quat::from_axis_angle(Vec3::up(), radians(float(i) * 4.0f));
        sample.wing_angle = radians(float(i));
        ghost.samples.push_back(sample);
    }
    ghost.duration = 10.0f;

    // Exact sample hits.
    CHECK(std::fabs(ghost.pose_at(0.0f).position.x - 0.0f) < 1e-4f);
    CHECK(std::fabs(ghost.pose_at(10.0f).position.x - 200.0f) < 1e-4f);
    // Interpolated midpoint.
    std::printf("  pose at 2.25 s: x %.2f (expect 45)\n", ghost.pose_at(2.25f).position.x);
    CHECK(std::fabs(ghost.pose_at(2.25f).position.x - 45.0f) < 0.01f);
    // Clamped outside the recording.
    CHECK(std::fabs(ghost.pose_at(-5.0f).position.x - 0.0f) < 1e-4f);
    CHECK(std::fabs(ghost.pose_at(99.0f).position.x - 200.0f) < 1e-4f);
    // Orientation stays a unit quaternion through interpolation.
    const Quat q = ghost.pose_at(3.7f).orientation;
    CHECK(std::fabs(std::sqrt(dot(q, q)) - 1.0f) < 1e-3f);

    // Monotonic sampling across the whole run, densely: catches an index search
    // that walks the wrong way.
    float previous = -1.0f;
    bool monotonic = true;
    for (int i = 0; i <= 1000; ++i) {
        const float x = ghost.pose_at(float(i) * 0.01f).position.x;
        if (x < previous - 1e-3f) monotonic = false;
        previous = x;
    }
    CHECK(monotonic);
}

void test_best_times_round_trip() {
    std::printf("best times persist\n");
    game::BestTimes times;
    CHECK(times.submit("Valley Run", 62.5f));      // first is always a record
    CHECK(!times.submit("Valley Run", 70.0f));     // slower is not
    CHECK(times.submit("Valley Run", 58.25f));     // faster is
    // Course names with spaces must survive the round trip.
    CHECK(times.submit("Canyon Weave", 44.125f));
    CHECK(std::fabs(times.best("Valley Run") - 58.25f) < 1e-3f);
    CHECK(times.best("Nonexistent") == 0.0f);

    const std::string path_store = test_temp_path("best_times.txt");
    const char* path = path_store.c_str();
    CHECK(times.save(path));
    game::BestTimes loaded;
    CHECK(loaded.load(path));
    std::printf("  reloaded: Valley Run %.3f, Canyon Weave %.3f\n", loaded.best("Valley Run"),
                loaded.best("Canyon Weave"));
    CHECK(std::fabs(loaded.best("Valley Run") - 58.25f) < 1e-3f);
    CHECK(std::fabs(loaded.best("Canyon Weave") - 44.125f) < 1e-3f);
}

void test_generated_courses_are_flyable() {
    std::printf("generated courses are flyable\n");
    game::TerrainSettings settings;
    settings.half_extent = 2500.0f;
    settings.cell_size = 20.0f;  // coarse: only the shape matters here
    game::Terrain terrain;
    terrain.generate(settings);

    game::Course courses[3] = {game::make_valley_run(terrain, settings.half_extent),
                               game::make_canyon_weave(terrain, settings.half_extent),
                               game::make_summit_climb(terrain, settings.half_extent)};

    for (const game::Course& course : courses) {
        float min_clearance = 1e9f;
        float max_gap = 0.0f;
        for (size_t i = 0; i < course.rings.size(); ++i) {
            const Ring& ring = course.rings[i];
            const float clearance =
                ring.position.y - terrain.height_at(ring.position.x, ring.position.z);
            min_clearance = minf(min_clearance, clearance);
            if (i > 0) {
                max_gap = maxf(max_gap, distance(course.rings[i - 1].position, ring.position));
            }
            // Every ring must be squarely oriented, or you cannot fly through it.
            CHECK(std::fabs(length(ring.normal()) - 1.0f) < 1e-3f);
            CHECK(ring.radius > 10.0f);
        }
        // The player flies the chord between rings, not just the rings. Rings
        // being individually clear says nothing about the line between them, and
        // a path curving around a mountain produces chords straight through it.
        float min_chord_clearance = 1e9f;
        float worst_gradient = 0.0f;
        for (size_t i = 1; i < course.rings.size(); ++i) {
            const Vec3 a = course.rings[i - 1].position;
            const Vec3 b = course.rings[i].position;
            constexpr int SAMPLES = 32;
            for (int step = 0; step <= SAMPLES; ++step) {
                const Vec3 sample = lerp(a, b, float(step) / float(SAMPLES));
                min_chord_clearance = minf(min_chord_clearance,
                                           sample.y - terrain.height_at(sample.x, sample.z));
            }
            const float horizontal = length(Vec3{b.x - a.x, 0.0f, b.z - a.z});
            if (horizontal > 1.0f) {
                worst_gradient = maxf(worst_gradient, (b.y - a.y) / horizontal);
            }
        }

        std::printf("  %-14s %2zu rings, %.1f km, ring clr %.0f m, chord clr %.0f m, "
                    "worst climb %.3f\n",
                    course.name.c_str(), course.rings.size(), course.path_length() / 1000.0f,
                    min_clearance, min_chord_clearance, worst_gradient);
        CHECK(course.rings.size() >= 8);
        // No ring buried in a hillside.
        CHECK(min_clearance > 20.0f);
        // And no leg that flies through one.
        CHECK(min_chord_clearance > 10.0f);
        // Steeper than roughly 0.11 is not hard, it is impossible: the dragon
        // gains about 5 m/s of energy flapping at 45 m/s forward. Some headroom
        // is left because turning costs energy too.
        CHECK(worst_gradient < 0.14f);
        // No two consecutive rings so far apart that the next one is invisible.
        CHECK(max_gap < 1600.0f);
    }
}

void test_course_round_trip() {
    std::printf("courses persist\n");
    game::TerrainSettings settings;
    settings.half_extent = 1500.0f;
    settings.cell_size = 25.0f;
    game::Terrain terrain;
    terrain.generate(settings);

    const game::Course original = game::make_canyon_weave(terrain, settings.half_extent);
    const std::string path_store = test_temp_path("course.txt");
    const char* path = path_store.c_str();
    CHECK(game::save_course(original, path));

    game::Course loaded;
    CHECK(game::load_course(loaded, path));
    CHECK(loaded.name == original.name);
    CHECK(loaded.rings.size() == original.rings.size());

    float worst_position = 0.0f, worst_normal = 0.0f;
    for (size_t i = 0; i < loaded.rings.size(); ++i) {
        worst_position =
            maxf(worst_position, distance(loaded.rings[i].position, original.rings[i].position));
        worst_normal =
            maxf(worst_normal, length(loaded.rings[i].normal() - original.rings[i].normal()));
    }
    std::printf("  worst position error %.4f m, worst normal error %.5f\n", worst_position,
                worst_normal);
    CHECK(worst_position < 0.01f);
    CHECK(worst_normal < 0.001f);
}

// Flies a course with the autopilot and reports the outcome.
//
// This is the strongest statement available about a generated course: not that
// its rings are geometrically sane, but that a dragon obeying the real flight
// model can actually get through them all. It also exercises the autopilot,
// which is the seed of the bot AI.
struct AutoRun {
    bool finished = false;
    float time = 0.0f;
    int rings = 0;
    float min_clearance = 1e9f;
    int frames_grounded = 0;
};

// `dt_jitter` fakes the uneven frame times of a real windowed session. Worth
// testing separately: the flight model integrates explicitly, so behaviour that
// is safe at a rock-steady 60 Hz can clip terrain when a frame hitches.
// A competent pilot's bank ceiling. Discovered the hard way: with no ceiling the
// autopilot banks steeply enough that lift goes mostly horizontal, so pulling
// back turns instead of climbing and it sinks into terrain -- the classic
// descending spiral. So a bank limit is not training wheels, it is airmanship,
// and bots fly with one. The "expert" player preset removes it as a deliberate
// choice, and an expert player can indeed spiral into a hillside.
constexpr float PILOT_BANK_LIMIT = 75.0f;

// `bank_limit` mirrors whatever ceiling the pilot is flying with.
AutoRun autopilot_run(const game::Terrain& terrain, const game::Course& course, float limit,
                      float dt_jitter = 0.0f, float bank_limit = 0.0f, float ring_scale = 1.0f) {
    game::Course scaled = course;
    for (Ring& ring : scaled.rings) ring.radius *= ring_scale;

    game::Rally rally;
    rally.set_course(scaled);

    const Ring& first = scaled.rings.front();
    Vec3 spawn = first.position - first.normal() * 420.0f;
    spawn.y = maxf(spawn.y, terrain.height_at(spawn.x, spawn.z) + 60.0f);

    game::FlightModel model;
    model.tuning.bank_limit_deg = bank_limit;
    model.reset(spawn, look_rotation(first.position - spawn, Vec3::up()), 42.0f);

    game::AutopilotTuning tuning;
    AutoRun result;
    const float base_dt = 1.0f / 60.0f;
    // Deterministic pseudo-jitter, so a failure is reproducible.
    uint32_t noise = 0x9E3779B9u;
    float clock = 0.0f;
    for (int i = 0; clock < limit; ++i) {
        float dt = base_dt;
        if (dt_jitter > 0.0f) {
            noise = noise * 1664525u + 1013904223u;
            const float unit = float((noise >> 8) & 0xFFFF) / 65535.0f;
            dt = base_dt * (1.0f + unit * dt_jitter);
        }
        clock += dt;
        const Ring* target = rally.next_ring();
        const Vec3 aim =
            target ? target->position : model.state().position + model.state().forward() * 500.0f;
        const Vec3 approach = target ? target->normal() : Vec3::zero();
        const float ground =
            terrain.height_at(model.state().position.x, model.state().position.z);

        model.update(game::steer_through(model.state(), aim, approach, tuning, ground), &terrain,
                     dt);
        rally.update(model.state(), dt);

        result.min_clearance = minf(result.min_clearance, model.state().position.y - ground);
        if (model.state().grounded) ++result.frames_grounded;

        if (rally.phase() == game::RunPhase::Finished) {
            result.finished = true;
            result.time = rally.last_run_time();
            break;
        }
    }
    result.rings = rally.rings_passed();
    return result;
}

void test_autopilot_survives_uneven_frames() {
    std::printf("the autopilot survives uneven frame times\n");
    // Reported from playtest: the autopilot crashed on Canyon Weave. It never
    // did at a fixed 60 Hz, and Canyon Weave was the course flying closest to
    // the ground -- so frame-time sensitivity was the suspect.
    game::TerrainSettings settings;
    settings.half_extent = 2500.0f;
    settings.cell_size = 6.0f;
    game::Terrain terrain;
    terrain.generate(settings);

    const game::Course courses[3] = {game::make_valley_run(terrain, settings.half_extent),
                                     game::make_canyon_weave(terrain, settings.half_extent),
                                     game::make_summit_climb(terrain, settings.half_extent)};

    // Up to 5x the nominal frame time, which is a bad hitch, not a bad machine.
    for (const game::Course& course : courses) {
        const AutoRun run = autopilot_run(terrain, course, 400.0f, 4.0f, PILOT_BANK_LIMIT);
        std::printf("  %-14s %s  %2d/%2zu rings, min clearance %.0f m%s\n", course.name.c_str(),
                    run.finished ? "finished" : "TIMED OUT", run.rings, course.rings.size(),
                    run.min_clearance, run.frames_grounded > 0 ? "  (HIT GROUND)" : "");
        CHECK(run.finished);
        CHECK(run.frames_grounded == 0);
        // Real margin, not a near miss. 22 m was the previous figure and it was
        // inside the autopilot's own avoidance threshold.
        CHECK(run.min_clearance > 25.0f);
    }
}

void test_relaxed_difficulty_is_completable() {
    std::printf("every course is completable on relaxed assists\n");
    // The point of a difficulty setting is that it makes the game *easier*, not
    // that it makes it impossible. A bank limit widens every turn -- radius goes
    // as v^2/(g tan bank) -- so a course that needs tight turns can become
    // unflyable with the assist on. Only a test catches that.
    game::TerrainSettings settings;
    settings.half_extent = 2500.0f;
    settings.cell_size = 6.0f;
    game::Terrain terrain;
    terrain.generate(settings);

    const game::Course courses[3] = {game::make_valley_run(terrain, settings.half_extent),
                                     game::make_canyon_weave(terrain, settings.half_extent),
                                     game::make_summit_climb(terrain, settings.half_extent)};

    for (const game::Course& course : courses) {
        // Relaxed: 70 degree bank ceiling, checkpoints 1.5x.
        const AutoRun run = autopilot_run(terrain, course, 450.0f, 0.0f, 70.0f, 1.5f);
        std::printf("  %-14s %s  %2d/%2zu rings, min clearance %.0f m%s\n", course.name.c_str(),
                    run.finished ? "finished" : "TIMED OUT", run.rings, course.rings.size(),
                    run.min_clearance, run.frames_grounded > 0 ? "  (HIT GROUND)" : "");
        CHECK(run.finished);
        CHECK(run.frames_grounded == 0);
    }
}

void test_autopilot_completes_every_course() {
    std::printf("the autopilot can fly every generated course\n");
    game::TerrainSettings settings;
    settings.half_extent = 2500.0f;
    settings.cell_size = 12.0f;
    game::Terrain terrain;
    terrain.generate(settings);

    const game::Course courses[3] = {game::make_valley_run(terrain, settings.half_extent),
                                     game::make_canyon_weave(terrain, settings.half_extent),
                                     game::make_summit_climb(terrain, settings.half_extent)};

    for (const game::Course& course : courses) {
        const AutoRun run = autopilot_run(terrain, course, 400.0f, 0.0f, PILOT_BANK_LIMIT);
        std::printf("  %-14s %s  %s  %2d/%2zu rings, min clearance %.0f m%s\n",
                    course.name.c_str(), run.finished ? "finished" : "TIMED OUT",
                    game::format_time(run.time).c_str(), run.rings, course.rings.size(),
                    run.min_clearance,
                    run.frames_grounded > 0 ? "  (touched down)" : "");
        CHECK(run.finished);
        // A course nobody can finish is not a course.
        CHECK(run.rings == int(course.rings.size()));
        // And it must be flyable without scraping the ground.
        CHECK(run.min_clearance > 25.0f);
    }
}

void test_autopilot_control_is_smooth() {
    std::printf("autopilot commands do not oscillate\n");
    // The first version was pure proportional and hunted badly: roll slammed
    // between full deflection either way, once per second, and it never reached
    // the third checkpoint. Reversals per second is the metric that caught it.
    game::TerrainSettings settings;
    settings.half_extent = 2500.0f;
    settings.cell_size = 16.0f;
    game::Terrain terrain;
    terrain.generate(settings);
    const game::Course course = game::make_valley_run(terrain, settings.half_extent);

    game::Rally rally;
    rally.set_course(course);
    const Ring& first = course.rings.front();
    Vec3 spawn = first.position - first.normal() * 420.0f;
    spawn.y = maxf(spawn.y, terrain.height_at(spawn.x, spawn.z) + 60.0f);
    game::FlightModel model;
    model.tuning.bank_limit_deg = PILOT_BANK_LIMIT;
    model.reset(spawn, look_rotation(first.position - spawn, Vec3::up()), 42.0f);

    game::AutopilotTuning tuning;
    int saturated_reversals = 0;
    float previous_roll = 0.0f;
    const float dt = 1.0f / 60.0f;
    const float seconds = 60.0f;
    for (int i = 0; i < int(seconds / dt); ++i) {
        const Ring* target = rally.next_ring();
        const Vec3 aim =
            target ? target->position : model.state().position + model.state().forward() * 500.0f;
        const Vec3 approach = target ? target->normal() : Vec3::zero();
        const float ground =
            terrain.height_at(model.state().position.x, model.state().position.z);
        const game::FlightInput in =
            game::steer_through(model.state(), aim, approach, tuning, ground);
        model.update(in, &terrain, dt);
        rally.update(model.state(), dt);

        // A reversal between the extremes of the control range.
        if ((previous_roll > 0.9f && in.roll < -0.9f) || (previous_roll < -0.9f && in.roll > 0.9f)) {
            ++saturated_reversals;
        }
        previous_roll = in.roll;
    }
    std::printf("  %.0f s of flying: %d full-deflection roll reversals\n", seconds,
                saturated_reversals);
    CHECK(saturated_reversals < 5);
}

}  // namespace

int main() {
    test_ring_crossing();
    test_fast_crossing_is_not_missed();
    test_run_timing();
    test_rings_must_be_taken_in_order();
    test_ghost_playback();
    test_best_times_round_trip();
    test_generated_courses_are_flyable();
    test_course_round_trip();
    test_autopilot_completes_every_course();
    test_autopilot_survives_uneven_frames();
    test_relaxed_difficulty_is_completable();
    test_autopilot_control_is_smooth();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
