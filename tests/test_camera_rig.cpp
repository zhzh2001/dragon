// Chase camera invariants.
//
// A camera cannot be judged by a screenshot -- the failures are all transient.
// These tests fly the dragon through the cases that break cameras (hugging
// terrain, hard turns, loops, inverted, stationary on the ground) and assert the
// properties a player would notice: never inside the ground, never losing the
// dragon off screen, never jittering.
#include <cmath>
#include <cstdio>

#include "game/chase_camera.h"
#include "game/flight.h"
#include "game/terrain.h"

using namespace core;
using game::ChaseCamera;
using game::FlightInput;
using game::FlightModel;

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

constexpr float DT = 1.0f / 60.0f;

game::Terrain make_terrain() {
    game::TerrainSettings settings;
    // Coarse and small: these tests care about slopes, not scenery.
    settings.half_extent = 1200.0f;
    settings.cell_size = 12.0f;
    game::Terrain terrain;
    terrain.generate(settings);
    return terrain;
}

// Is the dragon inside the camera's view cone?
bool dragon_on_screen(const ChaseCamera& camera, const FlightModel& model, float aspect) {
    const gfx::Camera& c = camera.camera();
    const Vec3 to_dragon = model.state().position - c.position;
    const float along = dot(to_dragon, c.forward());
    if (along <= 0.0f) return false;  // behind the camera
    const float half_v = std::tan(radians(c.fov_y_deg) * 0.5f) * along;
    const float half_h = half_v * aspect;
    return std::fabs(dot(to_dragon, c.up())) <= half_v &&
           std::fabs(dot(to_dragon, c.right())) <= half_h;
}

struct Report {
    float worst_clearance = 1e9f;  // camera height above ground
    int frames_underground = 0;
    int frames_offscreen = 0;
    float max_jump = 0.0f;  // largest single-frame camera move, metres
};

// Flies a scripted input and reports what the camera did.
Report fly(const game::Terrain& terrain, FlightInput input, float seconds, Vec3 spawn,
           game::ChaseCameraTuning tuning, bool first_person = false) {
    FlightModel model;
    model.reset(spawn, Quat::identity(), 45.0f);

    ChaseCamera camera;
    camera.tuning = tuning;
    camera.first_person = first_person;
    camera.snap_to(model.state());

    Report report;
    Vec3 previous = camera.camera().position;
    const int steps = int(seconds / DT);
    for (int i = 0; i < steps; ++i) {
        model.update(input, &terrain, DT);
        camera.update(model.state(), &terrain, Vec2{0.0f, 0.0f}, DT);

        const Vec3 eye = camera.camera().position;
        const float ground = terrain.height_at(eye.x, eye.z);
        const float clearance = eye.y - ground;
        report.worst_clearance = minf(report.worst_clearance, clearance);
        if (clearance < 0.0f) ++report.frames_underground;
        if (!dragon_on_screen(camera, model, 16.0f / 9.0f)) ++report.frames_offscreen;
        // Skip the first frames: settling from the snap is expected.
        if (i > 5) report.max_jump = maxf(report.max_jump, length(eye - previous));
        previous = eye;
    }
    return report;
}

// ---------------------------------------------------------------- tests

void test_never_enters_terrain() {
    std::printf("camera never enters the terrain\n");
    // The gap reported from the M4 playtest: the camera clipped into the ground
    // during hard manoeuvres near terrain. These are the manoeuvres that do it.
    game::Terrain terrain = make_terrain();
    const float ground = terrain.height_at(0.0f, 0.0f);

    struct Case {
        const char* name;
        FlightInput input;
        float altitude;
    };
    Case cases[6] = {};
    cases[0].name = "level, very low";
    cases[0].altitude = 8.0f;
    cases[1].name = "diving at the ground";
    cases[1].input.pitch = -0.7f;
    cases[1].input.tuck = 1.0f;
    cases[1].altitude = 300.0f;
    cases[2].name = "hard turn, low";
    cases[2].input.roll = 0.9f;
    cases[2].input.pitch = 0.5f;
    cases[2].altitude = 25.0f;
    cases[3].name = "climbing hard";
    cases[3].input.pitch = 0.8f;
    cases[3].input.flap = 1.0f;
    cases[3].altitude = 12.0f;
    cases[4].name = "looping";
    cases[4].input.pitch = 1.0f;
    cases[4].altitude = 120.0f;
    cases[5].name = "braking to a stop";
    cases[5].input.brake = 1.0f;
    cases[5].altitude = 15.0f;

    for (const Case& c : cases) {
        Report r = fly(terrain, c.input, 14.0f, Vec3{0.0f, ground + c.altitude, 0.0f},
                       game::camera_preset_chase());
        std::printf("  %-22s worst clearance %+7.2f m, %d frames underground\n", c.name,
                    r.worst_clearance, r.frames_underground);
        CHECK(r.frames_underground == 0);
        // Positive, and with some of the configured margin left over.
        CHECK(r.worst_clearance > 1.0f);
    }
}

void test_all_presets_stay_clear() {
    std::printf("every preset stays clear of terrain\n");
    game::Terrain terrain = make_terrain();
    const float ground = terrain.height_at(0.0f, 0.0f);

    FlightInput low_turn;
    low_turn.roll = 0.8f;
    low_turn.pitch = 0.45f;

    struct Named {
        const char* name;
        game::ChaseCameraTuning tuning;
    };
    const Named presets[3] = {{"chase", game::camera_preset_chase()},
                              {"action", game::camera_preset_action()},
                              {"cinematic", game::camera_preset_cinematic()}};
    for (const Named& p : presets) {
        Report r = fly(terrain, low_turn, 14.0f, Vec3{0.0f, ground + 20.0f, 0.0f}, p.tuning);
        std::printf("  %-10s worst clearance %+7.2f m, %d underground, max jump %.2f m\n", p.name,
                    r.worst_clearance, r.frames_underground, r.max_jump);
        CHECK(r.frames_underground == 0);
    }
}

void test_dragon_stays_in_frame() {
    std::printf("the dragon stays on screen\n");
    // A camera that loses its subject is worse than one that clips. Hard turns
    // and loops are where a lagging camera falls behind.
    game::Terrain terrain = make_terrain();
    const float ground = terrain.height_at(0.0f, 0.0f);

    struct Case {
        const char* name;
        FlightInput input;
    };
    Case cases[4] = {};
    cases[0].name = "hard roll";
    cases[0].input.roll = 1.0f;
    cases[1].name = "loop";
    cases[1].input.pitch = 1.0f;
    cases[2].name = "turn and dive";
    cases[2].input.roll = 0.8f;
    cases[2].input.pitch = -0.5f;
    cases[3].name = "everything at once";
    cases[3].input.roll = 1.0f;
    cases[3].input.pitch = 0.8f;
    cases[3].input.yaw = 1.0f;
    cases[3].input.flap = 1.0f;

    for (const Case& c : cases) {
        Report r = fly(terrain, c.input, 14.0f, Vec3{0.0f, ground + 500.0f, 0.0f},
                       game::camera_preset_chase());
        const int total = int(14.0f / DT);
        std::printf("  %-20s %d/%d frames off screen, max jump %.2f m\n", c.name,
                    r.frames_offscreen, total, r.max_jump);
        // A few frames at the very edge during a violent manoeuvre is tolerable;
        // sustained loss is not.
        CHECK(r.frames_offscreen < total / 20);
    }
}

void test_no_jitter() {
    std::printf("no per-frame jitter\n");
    // Frame-to-frame jumps are the most nauseating camera failure and the
    // hardest to spot in a screenshot. At 60 Hz and 100 m/s the camera should
    // never move more than a couple of metres in one frame.
    game::Terrain terrain = make_terrain();
    const float ground = terrain.height_at(0.0f, 0.0f);

    FlightInput dive;
    dive.pitch = -0.4f;
    dive.tuck = 1.0f;
    Report r = fly(terrain, dive, 16.0f, Vec3{0.0f, ground + 600.0f, 0.0f},
                   game::camera_preset_chase());
    std::printf("  fast dive: max single-frame move %.2f m\n", r.max_jump);
    CHECK(r.max_jump < 6.0f);

    // Skimming a slope makes the arm shorten and extend repeatedly, which is
    // where a badly tuned collision response oscillates.
    FlightInput skim;
    skim.pitch = 0.05f;
    Report skim_report = fly(terrain, skim, 20.0f, Vec3{0.0f, ground + 10.0f, 0.0f},
                             game::camera_preset_chase());
    std::printf("  terrain skim: max single-frame move %.2f m\n", skim_report.max_jump);
    CHECK(skim_report.max_jump < 6.0f);
}

void test_first_person_is_inside_the_head() {
    std::printf("first person tracks the head\n");
    game::Terrain terrain = make_terrain();
    const float ground = terrain.height_at(0.0f, 0.0f);

    FlightModel model;
    model.reset(Vec3{0.0f, ground + 300.0f, 0.0f}, Quat::identity(), 45.0f);
    ChaseCamera camera;
    camera.first_person = true;
    camera.snap_to(model.state());

    FlightInput turn;
    turn.roll = 0.6f;
    turn.pitch = 0.3f;
    float max_offset_error = 0.0f;
    for (int i = 0; i < int(10.0f / DT); ++i) {
        model.update(turn, &terrain, DT);
        camera.update(model.state(), &terrain, Vec2{0.0f, 0.0f}, DT);
        // The eye must stay rigidly attached, with no lag at all -- the point of
        // this view is that nothing is smoothed away.
        const Vec3 expected =
            model.state().position + rotate(model.state().orientation, camera.head_offset);
        max_offset_error = maxf(max_offset_error, length(camera.camera().position - expected));
    }
    std::printf("  max deviation from the head: %.4f m\n", max_offset_error);
    CHECK(max_offset_error < 1e-3f);
}

void test_free_look_orbits_without_steering() {
    std::printf("free look orbits the camera, not the dragon\n");
    game::Terrain terrain = make_terrain();
    const float ground = terrain.height_at(0.0f, 0.0f);

    FlightModel model;
    model.reset(Vec3{0.0f, ground + 400.0f, 0.0f}, Quat::identity(), 45.0f);
    ChaseCamera camera;
    camera.tuning = game::camera_preset_chase();
    camera.tuning.free_look_return = 0.0f;  // hold, so the test is deterministic
    camera.snap_to(model.state());

    const Vec3 heading_before = model.state().forward();
    // Look hard to one side.
    for (int i = 0; i < 30; ++i) {
        model.update(FlightInput(), &terrain, DT);
        camera.update(model.state(), &terrain, Vec2{-3.0f, 0.0f}, DT);
    }
    std::printf("  free look %+.0f deg after 30 frames\n", camera.free_look_angles().x);
    CHECK(camera.free_look_angles().x > 30.0f);

    // The dragon must not have been steered by looking.
    const float heading_change =
        degrees(std::acos(clampf(dot(heading_before, model.state().forward()), -1.0f, 1.0f)));
    std::printf("  dragon heading changed %.2f deg (should be ~0)\n", heading_change);
    CHECK(heading_change < 3.0f);

    // The camera must still be able to see the dragon while looking aside.
    CHECK(dragon_on_screen(camera, model, 16.0f / 9.0f));

    // Yaw must clamp rather than wrapping around forever.
    for (int i = 0; i < 600; ++i) {
        model.update(FlightInput(), &terrain, DT);
        camera.update(model.state(), &terrain, Vec2{-10.0f, -10.0f}, DT);
    }
    std::printf("  clamped to %+.0f  %+.0f deg\n", camera.free_look_angles().x,
                camera.free_look_angles().y);
    CHECK(camera.free_look_angles().x <= camera.tuning.free_look_yaw_limit + 0.01f);
    CHECK(camera.free_look_angles().y <= camera.tuning.free_look_pitch_limit + 0.01f);
}

void test_grounded_is_stable() {
    std::printf("camera is stable with the dragon at rest\n");
    // Landed on a slope with no input is a still frame: any movement at all is
    // drift, and drift is very visible when nothing else is moving.
    game::Terrain terrain = make_terrain();
    FlightModel model;
    const float ground = terrain.height_at(120.0f, -80.0f);
    model.reset(Vec3{120.0f, ground + 1.0f, -80.0f}, Quat::identity(), 0.0f);

    ChaseCamera camera;
    camera.tuning = game::camera_preset_chase();
    // Settle first.
    for (int i = 0; i < int(6.0f / DT); ++i) {
        model.update(FlightInput(), &terrain, DT);
        camera.update(model.state(), &terrain, Vec2{0.0f, 0.0f}, DT);
    }
    const Vec3 settled = camera.camera().position;
    float drift = 0.0f;
    for (int i = 0; i < int(4.0f / DT); ++i) {
        model.update(FlightInput(), &terrain, DT);
        camera.update(model.state(), &terrain, Vec2{0.0f, 0.0f}, DT);
        drift = maxf(drift, length(camera.camera().position - settled));
    }
    std::printf("  grounded, clearance %.2f m, drift over 4 s: %.3f m\n",
                camera.camera().position.y - terrain.height_at(camera.camera().position.x,
                                                               camera.camera().position.z),
                drift);
    CHECK(model.state().grounded);
    CHECK(drift < 0.2f);
    CHECK(camera.camera().position.y >
          terrain.height_at(camera.camera().position.x, camera.camera().position.z));
}

}  // namespace

int main() {
    test_never_enters_terrain();
    test_all_presets_stay_clear();
    test_dragon_stays_in_frame();
    test_no_jitter();
    test_first_person_is_inside_the_head();
    test_free_look_orbits_without_steering();
    test_grounded_is_stable();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
