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

// Roll angle in degrees: 0 wings level, +-180 inverted. Sign follows the flight
// model's convention, where a right bank is negative.
//
// Deliberately NOT acos(dot(body up, world up)): that conflates roll with pitch,
// and reports a wings-level 40-degree dive as a 40-degree bank.
float roll_degrees(const FlightModel& model) {
    const Vec3 up = model.state().up();
    const Vec3 right = model.state().right();
    return degrees(std::atan2(dot(right, Vec3::up()), dot(up, Vec3::up())));
}
float abs_roll_degrees(const FlightModel& model) { return std::fabs(roll_degrees(model)); }

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
    // Start already banked 40 degrees right, so this measures the aerodynamics
    // rather than the roll controller. Forward is -Z, so a positive rotation
    // about it drops the right wing.
    FlightModel model;
    model.reset(Vec3{0.0f, 2000.0f, 0.0f},
                Quat::from_axis_angle(Vec3::forward(), radians(40.0f)), 50.0f);
    // Auto-level and the pitch assist exist to rescue the player, and here they
    // would erase the very thing under test. Switched off so this measures the
    // aerodynamics alone.
    model.tuning.auto_level = 0.0f;
    model.tuning.pitch_level = 0.0f;

    std::printf("  starting roll %+.0f deg\n", roll_degrees(model));
    CHECK(roll_degrees(model) < 0.0f);  // a right bank is negative

    const Vec3 start_heading =
        normalize(Vec3{model.state().velocity.x, 0.0f, model.state().velocity.z});

    // Pitch only: no roll command at all, so the turn can only come from the
    // banked lift vector.
    FlightInput bank;
    bank.pitch = 0.35f;
    run(model, bank, 5.0f);
    std::printf("  after 5 s: roll %+.0f deg\n", roll_degrees(model));

    const Vec3 end_heading =
        normalize(Vec3{model.state().velocity.x, 0.0f, model.state().velocity.z});
    const float turned = degrees(std::acos(clampf(dot(start_heading, end_heading), -1.0f, 1.0f)));
    // Rolling right in a right-handed Y-up world turns toward +X.
    std::printf("  heading changed %.0f deg, drifted %+.0f m in x\n", turned,
                model.state().position.x);
    CHECK(turned > 20.0f);
    CHECK(model.state().position.x > 0.0f);
    CHECK(state_is_sane(model));

    // Sideslip must stay small: a turn should be coordinated, not a skid. This
    // threshold is tight enough to catch a mis-tuned coordination term -- at
    // zero coordination the same manoeuvre slips 11 degrees, and over-coordinated
    // it slips the other way by more.
    std::printf("  sideslip %+.1f deg\n", degrees(model.state().sideslip));
    CHECK(std::fabs(degrees(model.state().sideslip)) < 6.0f);
}

void test_recovers_from_inverted() {
    std::printf("recovers from belly-up on its own\n");
    // Reported from playtest: the dragon rolls over easily and then cannot be
    // recovered. Hands off, auto-level must roll it upright before it hits the
    // ground -- otherwise an accidental roll is a death sentence.
    FlightModel model;
    // Rolled 175 degrees: inverted, but not exactly balanced on the singularity.
    const Quat inverted = Quat::from_axis_angle(Vec3::forward(), radians(175.0f));
    model.reset(Vec3{0.0f, 2000.0f, 0.0f}, inverted, 45.0f);

    const float start_bank = abs_roll_degrees(model);
    FlightInput released;
    float recovered_after = -1.0f;
    for (int i = 0; i < int(12.0f / DT); ++i) {
        model.update(released, nullptr, DT);
        if (recovered_after < 0.0f && abs_roll_degrees(model) < 25.0f) recovered_after = float(i) * DT;
    }
    std::printf("  bank %.0f -> %.0f deg, upright after %.1f s, lost %.0f m\n", start_bank,
                abs_roll_degrees(model), recovered_after, 2000.0f - model.state().position.y);
    CHECK(recovered_after > 0.0f);
    // Five seconds is already a long time to be falling inverted.
    CHECK(recovered_after < 5.0f);
    CHECK(state_is_sane(model));
}

void test_roll_is_controllable() {
    std::printf("a brief roll input does not flip the dragon\n");
    // Reported from playtest: rolling is so fast that a tap puts you belly-up.
    // A half-second of full roll should bank hard but stay the right way up.
    FlightModel model = make_level_flyer(45.0f);
    FlightInput roll;
    roll.roll = 1.0f;
    run(model, roll, 0.5f);
    const float banked = abs_roll_degrees(model);

    // Release and let it settle.
    FlightInput released;
    run(model, released, 6.0f);
    std::printf("  0.5 s of full roll -> %.0f deg bank, settles to %.0f deg\n", banked,
                abs_roll_degrees(model));
    CHECK(banked < 90.0f);
    CHECK(abs_roll_degrees(model) < 20.0f);
    CHECK(state_is_sane(model));
}

void test_bank_limit_prevents_inversion() {
    std::printf("holding a turn settles into a bank instead of rolling over\n");
    // Reported from playtest: turning either misses the target or flips the
    // dragon belly-up. Holding roll used to roll continuously, because
    // auto-level only engaged once the player let go.
    FlightModel model = make_level_flyer(45.0f);
    FlightInput hard_turn;
    hard_turn.roll = 1.0f;
    hard_turn.pitch = 0.4f;

    float peak_roll = 0.0f;
    for (int i = 0; i < int(20.0f / DT); ++i) {
        model.update(hard_turn, nullptr, DT);
        peak_roll = maxf(peak_roll, abs_roll_degrees(model));
    }
    std::printf("  20 s of full roll: peak %.0f deg, settled %.0f deg (limit %.0f)\n", peak_roll,
                abs_roll_degrees(model), model.tuning.bank_limit_deg);
    // Never past vertical, so lift never points at the ground.
    CHECK(peak_roll < 90.0f);
    // And it holds a useful bank rather than being flattened out.
    CHECK(abs_roll_degrees(model) > 40.0f);
    CHECK(state_is_sane(model));

    // The turn must still actually turn: a bank limit that stops the dragon
    // turning would be a worse cure than the disease.
    const Vec3 heading = normalize(Vec3{model.state().forward().x, 0.0f,
                                        model.state().forward().z});
    FlightModel fresh = make_level_flyer(45.0f);
    const Vec3 start = normalize(Vec3{fresh.state().forward().x, 0.0f,
                                      fresh.state().forward().z});
    run(fresh, hard_turn, 6.0f);
    const Vec3 after = normalize(Vec3{fresh.state().forward().x, 0.0f, fresh.state().forward().z});
    const float turned = degrees(std::acos(clampf(dot(start, after), -1.0f, 1.0f)));
    std::printf("  6 s of turning: %.0f deg of heading change\n", turned);
    CHECK(turned > 45.0f);
    (void)heading;

    // With the limit off, the same input should roll right over -- confirming
    // the assist is what prevents it, not some other term.
    FlightModel unlimited = make_level_flyer(45.0f);
    unlimited.tuning.bank_limit_deg = 0.0f;
    float unlimited_peak = 0.0f;
    for (int i = 0; i < int(20.0f / DT); ++i) {
        unlimited.update(hard_turn, nullptr, DT);
        unlimited_peak = maxf(unlimited_peak, abs_roll_degrees(unlimited));
    }
    std::printf("  same input with the limit off: peak %.0f deg\n", unlimited_peak);
    CHECK(unlimited_peak > 120.0f);
}

void test_rudder_turns_the_nose() {
    std::printf("rudder produces a real heading change\n");
    // Reported from playtest: Q/E appears to do nothing. Weathercock stability
    // cancels commanded yaw, so rudder needs to out-authority it.
    FlightModel model = make_level_flyer(45.0f);
    const Vec3 start = normalize(Vec3{model.state().forward().x, 0.0f, model.state().forward().z});

    FlightInput rudder;
    rudder.yaw = 1.0f;  // nose right
    run(model, rudder, 3.0f);

    const Vec3 end = normalize(Vec3{model.state().forward().x, 0.0f, model.state().forward().z});
    const float turned = degrees(std::acos(clampf(dot(start, end), -1.0f, 1.0f)));
    std::printf("  3 s of right rudder: %.0f deg of heading, slip %+.1f deg\n", turned,
                degrees(model.state().sideslip));
    // Enough to be unmistakably useful for fine aiming.
    CHECK(turned > 15.0f);
    // And it should turn the correct way: right rudder yaws toward +X.
    CHECK(end.x > start.x);
    CHECK(state_is_sane(model));
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

// Landing and taking off are states, not accidents. A dragon set down on the
// ground stays down and settles level; the first flap from the ground is a leap
// that leaves it; heft makes the same commands act on a heavier body.
void test_landing_and_takeoff() {
    std::printf("landing settles, the first flap from the ground is a leap\n");
    game::TerrainSettings settings;
    settings.half_extent = 600.0f;
    game::Terrain terrain;
    terrain.generate(settings);

    // Set down on the floor, slow, nose slightly up and banked: it should
    // come to rest level within a couple of seconds and stay grounded.
    game::FlightModel model;
    const float x = terrain.valley_center_x(0.0f) + 150.0f, z = 0.0f;
    const float ground = terrain.surface_at(x, z);
    model.reset(Vec3{x, ground + model.tuning.ground_offset, z},
                normalize(Quat::from_axis_angle(Vec3::unit_z(), radians(25.0f)) *
                          Quat::from_axis_angle(Vec3::unit_x(), radians(10.0f))),
                3.0f);
    game::FlightInput idle;
    int grounded_frames = 0;
    for (int i = 0; i < 180; ++i) {
        model.update(idle, &terrain, 1.0f / 60.0f);
        if (model.state().grounded) ++grounded_frames;
    }
    CHECK(model.state().grounded);
    CHECK(grounded_frames > 150);
    CHECK(length(model.state().velocity) < 0.5f);
    // Level: the body up is within a few degrees of the surface normal.
    const Vec3 normal = terrain.normal_at(x, z);
    CHECK(dot(model.state().up(), normal) > 0.99f);
    CHECK(state_is_sane(model));

    // A flap from the ground: airborne within a second, and climbing.
    game::FlightInput flap;
    flap.flap = 1.0f;
    for (int i = 0; i < 6; ++i) model.update(flap, &terrain, 1.0f / 60.0f);
    CHECK(!model.state().grounded);
    CHECK(model.state().velocity.y > 2.0f);
    for (int i = 0; i < 54; ++i) model.update(flap, &terrain, 1.0f / 60.0f);
    CHECK(model.state().ground_clearance > 2.0f);
    CHECK(state_is_sane(model));

    // Heft: the same roll command turns a heavier dragon more slowly.
    game::FlightModel light, heavy;
    heavy.tuning.heft = 2.0f;
    light.reset(Vec3{0.0f, 500.0f, 0.0f}, Quat::identity(), 40.0f);
    heavy.reset(Vec3{0.0f, 500.0f, 0.0f}, Quat::identity(), 40.0f);
    game::FlightInput roll;
    roll.roll = 1.0f;
    for (int i = 0; i < 30; ++i) {
        light.update(roll, nullptr, 1.0f / 60.0f);
        heavy.update(roll, nullptr, 1.0f / 60.0f);
    }
    const float light_bank = std::fabs(std::atan2(dot(light.state().right(), Vec3::up()),
                                                  dot(light.state().up(), Vec3::up())));
    const float heavy_bank = std::fabs(std::atan2(dot(heavy.state().right(), Vec3::up()),
                                                  dot(heavy.state().up(), Vec3::up())));
    CHECK(heavy_bank < light_bank * 0.85f);
    CHECK(heavy_bank > 0.0f);
}

int main() {
    test_landing_and_takeoff();
    test_glide_loses_energy_slowly();
    test_dive_trades_altitude_for_speed();
    test_flapping_adds_energy();
    test_stall_and_recovery();
    test_level_trim_exists();
    test_bank_turns_the_flight_path();
    test_recovers_from_inverted();
    test_bank_limit_prevents_inversion();
    test_roll_is_controllable();
    test_rudder_turns_the_nose();
    test_extreme_inputs_stay_finite();
    test_ground_stops_the_dragon();
    test_presets_all_fly();
    test_tuning_round_trip();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
