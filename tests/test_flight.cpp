// Flight model invariants.
//
// A flight model cannot be verified by looking at it -- a wrong one still
// produces something that moves. These tests pin the properties that must hold
// for the *energy* model to mean anything: that energy is only ever spent,
// that a stall is recoverable, that nothing produces NaN, and that the
// aerodynamics actually oppose the pilot the way air does.
#include <cmath>
#include <cstdio>

#include "game/flight.h"
#include "game/terrain.h"

using namespace core;
using game::FlightInput;
using game::FlightModel;
using game::FlightTuning;

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

constexpr float DT = 1.0f / 120.0f;

bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(Quat q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

bool state_is_sane(const FlightModel& model) {
    const game::FlightState& s = model.state();
    return finite(s.position) && finite(s.velocity) && finite(s.orientation) &&
           finite(s.angular_velocity) && std::isfinite(s.airspeed) &&
           std::isfinite(s.angle_of_attack) && std::isfinite(s.g_load) &&
           // The orientation must stay a unit quaternion or every derived axis
           // silently scales.
           std::fabs(std::sqrt(dot(s.orientation, s.orientation)) - 1.0f) < 1e-2f;
}

FlightModel make_level_flyer(float airspeed = 45.0f) {
    FlightModel model;
    model.reset(Vec3{0.0f, 1000.0f, 0.0f}, Quat::identity(), airspeed);
    return model;
}

void run(FlightModel& model, const FlightInput& input, float seconds,
         const game::Terrain* terrain = nullptr) {
    const int steps = int(seconds / DT);
    for (int i = 0; i < steps; ++i) model.update(input, terrain, DT);
}

// ---------------------------------------------------------------- tests

void test_glide_loses_energy_slowly() {
    std::printf("unpowered glide spends energy, never gains it\n");
    FlightModel model = make_level_flyer();
    FlightInput input;  // no flap, no controls

    const float start_energy = model.state().specific_energy;
    float previous = start_energy;
    bool monotonic = true;
    for (int i = 0; i < 1200; ++i) {
        model.update(input, nullptr, DT);
        const float now = model.state().specific_energy;
        // Drag can only remove energy. A tiny tolerance covers integration
        // noise; anything larger means the model is manufacturing energy.
        if (now > previous + 0.02f) monotonic = false;
        previous = now;
    }
    const float lost = start_energy - model.state().specific_energy;
    std::printf("  10 s glide: energy %.1f -> %.1f m (lost %.1f)\n", start_energy,
                model.state().specific_energy, lost);
    CHECK(monotonic);
    CHECK(lost > 0.0f);
    // A dragon-sized glider should not dump hundreds of metres in ten seconds.
    CHECK(lost < 120.0f);
    CHECK(state_is_sane(model));
}

void test_dive_trades_altitude_for_speed() {
    std::printf("diving buys speed, climbing spends it\n");
    // The defining property of an energy model. Without it, nothing downstream
    // -- racing lines, boom-and-zoom dogfighting -- has any depth.
    FlightModel model = make_level_flyer(40.0f);
    FlightInput dive;
    dive.pitch = -1.0f;  // nose down
    dive.tuck = 1.0f;    // fold wings

    const float start_speed = model.state().airspeed;
    const float start_altitude = model.state().position.y;
    run(model, dive, 6.0f);
    const float dive_speed = model.state().airspeed;
    const float dive_altitude = model.state().position.y;
    std::printf("  dive:  %.1f -> %.1f m/s,  alt %.0f -> %.0f m\n", start_speed, dive_speed,
                start_altitude, dive_altitude);
    CHECK(dive_speed > start_speed * 1.3f);
    CHECK(dive_altitude < start_altitude);

    // Now pull up and confirm the speed is handed back as height.
    FlightInput climb;
    climb.pitch = 0.55f;
    const float before_climb_energy = model.state().specific_energy;
    run(model, climb, 5.0f);
    std::printf("  climb: %.1f m/s, alt %.0f m\n", model.state().airspeed,
                model.state().position.y);
    CHECK(model.state().airspeed < dive_speed);
    // Energy still only decreases: the climb converts, it does not create.
    CHECK(model.state().specific_energy <= before_climb_energy + 0.5f);
    CHECK(state_is_sane(model));
}

void test_flapping_adds_energy() {
    std::printf("flapping is the only way energy enters the system\n");
    FlightModel model = make_level_flyer(30.0f);
    FlightInput flap;
    flap.flap = 1.0f;
    flap.pitch = 0.12f;

    const float start_energy = model.state().specific_energy;
    run(model, flap, 8.0f);
    std::printf("  8 s of flapping: energy %.1f -> %.1f m\n", start_energy,
                model.state().specific_energy);
    CHECK(model.state().specific_energy > start_energy);
    CHECK(state_is_sane(model));
}

void test_stall_and_recovery() {
    std::printf("stall is reachable and recoverable\n");
    FlightModel model = make_level_flyer(24.0f);

    // Hold the nose hard up: airspeed bleeds off and the wing lets go.
    FlightInput hard_up;
    hard_up.pitch = 1.0f;
    bool ever_stalled = false;
    for (int i = 0; i < 900; ++i) {
        model.update(hard_up, nullptr, DT);
        if (model.state().stalling) ever_stalled = true;
    }
    std::printf("  stalled during hard pull: %s (AoA %+.1f deg, %.1f m/s)\n",
                ever_stalled ? "yes" : "no", degrees(model.state().angle_of_attack),
                model.state().airspeed);
    CHECK(ever_stalled);
    CHECK(state_is_sane(model));

    // Release the stick: the stability and stall-recovery terms must bring the
    // angle of attack back inside the envelope on their own.
    FlightInput released;
    run(model, released, 8.0f);
    const float aoa = std::fabs(degrees(model.state().angle_of_attack));
    std::printf("  after release: AoA %.1f deg, %.1f m/s, stalling %s\n", aoa,
                model.state().airspeed, model.state().stalling ? "yes" : "no");
    CHECK(!model.state().stalling);
    CHECK(aoa < model.tuning.stall_angle_deg);
    CHECK(state_is_sane(model));
}

void test_level_trim_exists() {
    std::printf("a hands-off trim speed exists\n");
    // Released controls should settle toward a steady glide rather than
    // oscillating or diverging. If this fails, the aircraft is not stable and no
    // amount of camera work will make it feel good.
    FlightModel model = make_level_flyer(45.0f);
    FlightInput released;
    run(model, released, 12.0f);

    float min_climb = 1e9f, max_climb = -1e9f;
    for (int i = 0; i < 600; ++i) {
        model.update(released, nullptr, DT);
        min_climb = minf(min_climb, model.state().climb_rate);
        max_climb = maxf(max_climb, model.state().climb_rate);
    }
    std::printf("  settled climb rate range over 5 s: %.2f .. %.2f m/s\n", min_climb, max_climb);
    // Steady, not ringing.
    CHECK((max_climb - min_climb) < 6.0f);
    // And descending, since nothing is powering it.
    CHECK(max_climb < 1.0f);
    CHECK(state_is_sane(model));
}

void test_bank_turns_the_flight_path() {
    std::printf("banking curves the flight path\n");
    // Lift acts perpendicular to the relative wind, so a bank should turn the
    // velocity vector without any explicit "turn" code. This checks that the
    // lift direction is genuinely being computed from the airflow.
    FlightModel model = make_level_flyer(50.0f);
    const Vec3 start_heading = normalize(Vec3{model.state().velocity.x, 0.0f,
                                              model.state().velocity.z});

    FlightInput bank;
    bank.roll = 1.0f;   // roll right
    bank.pitch = 0.35f;  // hold the nose up to sustain the turn
    run(model, bank, 5.0f);

    const Vec3 end_heading =
        normalize(Vec3{model.state().velocity.x, 0.0f, model.state().velocity.z});
    const float turned = degrees(std::acos(clampf(dot(start_heading, end_heading), -1.0f, 1.0f)));
    // Rolling right in a right-handed Y-up world turns toward +X.
    std::printf("  heading changed %.0f deg, drifted %+.0f m in x\n", turned,
                model.state().position.x);
    CHECK(turned > 20.0f);
    CHECK(model.state().position.x > 0.0f);
    CHECK(state_is_sane(model));

    // Sideslip must stay small: a turn should be coordinated, not a skid.
    std::printf("  sideslip %+.1f deg\n", degrees(model.state().sideslip));
    CHECK(std::fabs(degrees(model.state().sideslip)) < 12.0f);
}

void test_extreme_inputs_stay_finite() {
    std::printf("extreme and contradictory inputs never produce NaN\n");
    // Every combination held at once, from a standing start, is the worst case
    // for divide-by-airspeed bugs.
    FlightModel model;
    model.reset(Vec3{0.0f, 500.0f, 0.0f}, Quat::identity(), 0.0f);

    FlightInput everything;
    everything.pitch = 1.0f;
    everything.yaw = -1.0f;
    everything.roll = 1.0f;
    everything.flap = 1.0f;
    everything.tuck = 1.0f;
    everything.brake = 1.0f;

    for (int i = 0; i < 3000; ++i) {
        model.update(everything, nullptr, DT);
        if (!state_is_sane(model)) break;
    }
    std::printf("  after 25 s: %.1f m/s at %.0f m, AoA %+.1f deg\n", model.state().airspeed,
                model.state().position.y, degrees(model.state().angle_of_attack));
    CHECK(state_is_sane(model));

    // Also check a genuinely zero-airspeed start with no input at all, and a
    // huge single timestep, which is what a debugger pause produces.
    FlightModel stationary;
    stationary.reset(Vec3{0.0f, 100.0f, 0.0f}, Quat::identity(), 0.0f);
    stationary.update(FlightInput(), nullptr, 0.5f);
    CHECK(state_is_sane(stationary));

    // Zero and negative dt must be no-ops rather than explosions.
    FlightModel guarded = make_level_flyer();
    const Vec3 before = guarded.state().position;
    guarded.update(FlightInput(), nullptr, 0.0f);
    guarded.update(FlightInput(), nullptr, -1.0f);
    CHECK(length(guarded.state().position - before) < 1e-6f);
    CHECK(state_is_sane(guarded));
}

void test_ground_stops_the_dragon() {
    std::printf("terrain stops the dragon instead of swallowing it\n");
    game::TerrainSettings settings;
    // Small and cheap: this test cares about collision, not scenery.
    settings.half_extent = 400.0f;
    settings.cell_size = 16.0f;
    game::Terrain terrain;
    terrain.generate(settings);

    FlightModel model;
    const float ground = terrain.height_at(0.0f, 0.0f);
    model.reset(Vec3{0.0f, ground + 200.0f, 0.0f}, Quat::identity(), 20.0f);

    // Point it straight down and dive into the surface.
    FlightInput dive;
    dive.pitch = -1.0f;
    dive.tuck = 1.0f;
    run(model, dive, 20.0f, &terrain);

    const float clearance = terrain.clearance_at(model.state().position);
    std::printf("  grounded %s, clearance %.2f m, speed %.1f m/s\n",
                model.state().grounded ? "yes" : "no", clearance, model.state().airspeed);
    CHECK(model.state().grounded);
    // Must rest on the surface, never below it.
    CHECK(clearance > model.tuning.ground_offset - 0.5f);
    CHECK(clearance < model.tuning.ground_offset + 2.0f);
    // Friction must actually bring it to rest.
    CHECK(model.state().airspeed < 15.0f);
    CHECK(state_is_sane(model));
}

void test_presets_all_fly() {
    std::printf("every preset flies without blowing up\n");
    struct Named {
        const char* name;
        FlightTuning tuning;
    };
    const Named presets[] = {
        {"default", FlightTuning()},
        {"glider", game::tuning_preset_glider()},
        {"agile", game::tuning_preset_agile()},
        {"heavy", game::tuning_preset_heavy()},
    };

    for (const Named& preset : presets) {
        FlightModel model = make_level_flyer(50.0f);
        model.tuning = preset.tuning;
        FlightInput input;
        input.flap = 0.5f;
        run(model, input, 15.0f);
        std::printf("  %-8s %.1f m/s, alt %.0f m, %s\n", preset.name, model.state().airspeed,
                    model.state().position.y, state_is_sane(model) ? "ok" : "BROKEN");
        CHECK(state_is_sane(model));
        // Each should still be airborne and moving, not tumbling or stopped.
        CHECK(model.state().airspeed > 5.0f);
    }
}

void test_tuning_round_trip() {
    std::printf("tuning saves and loads without drift\n");
    FlightTuning original = game::tuning_preset_agile();
    original.mass = 777.5f;
    original.roll_rate = 3.75f;

    const char* path = "/tmp/dragon_tuning_test.cfg";
    CHECK(game::save_tuning(original, path));

    FlightTuning loaded;  // starts at defaults
    CHECK(game::load_tuning(loaded, path));
    std::printf("  mass %.2f -> %.2f, roll %.3f -> %.3f\n", original.mass, loaded.mass,
                original.roll_rate, loaded.roll_rate);
    CHECK(std::fabs(loaded.mass - original.mass) < 0.01f);
    CHECK(std::fabs(loaded.roll_rate - original.roll_rate) < 0.001f);
    CHECK(std::fabs(loaded.wing_area - original.wing_area) < 0.01f);
    CHECK(std::fabs(loaded.control_lag - original.control_lag) < 0.0001f);
}

}  // namespace

int main() {
    test_glide_loses_energy_slowly();
    test_dive_trades_altitude_for_speed();
    test_flapping_adds_energy();
    test_stall_and_recovery();
    test_level_trim_exists();
    test_bank_turns_the_flight_path();
    test_extreme_inputs_stay_finite();
    test_ground_stops_the_dragon();
    test_presets_all_fly();
    test_tuning_round_trip();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
