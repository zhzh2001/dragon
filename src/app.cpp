#include "app.h"

#include <SDL3/SDL.h>

#include <cstdio>

#include "core/log.h"
#include "core/math.h"
#include "gfx/primitives.h"
#include "imgui.h"

using core::Quat;
using core::Vec3;

namespace app {

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            options.frames = SDL_atoi(argv[++i]);
        } else if (arg == "--screenshot" && i + 1 < argc) {
            options.screenshot = argv[++i];
        } else if (arg == "--cam-mode" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode == "chase") options.camera_mode = 0;
            else if (mode == "action") options.camera_mode = 1;
            else if (mode == "cinematic") options.camera_mode = 2;
            else if (mode == "fp") options.first_person = true;
            else LOG_WARN("--cam-mode expects chase|action|cinematic|fp");
        } else if (arg == "--course" && i + 1 < argc) {
            options.course_index = SDL_atoi(argv[++i]);
        } else if (arg == "--model" && i + 1 < argc) {
            options.model = argv[++i];
        } else if (arg == "--cycle-models" && i + 1 < argc) {
            options.cycle_models = SDL_atoi(argv[++i]);
        } else if (arg == "--bot-range" && i + 1 < argc) {
            options.bot_range = float(SDL_atof(argv[++i]));
        } else if (arg == "--models" && i + 1 < argc) {
            // Comma-separated roster: the player flies the first, bots are
            // dealt the rest. Splitting here keeps the app free of parsing.
            const std::string list = argv[++i];
            size_t start = 0;
            while (start <= list.size()) {
                const size_t comma = list.find(',', start);
                const size_t end = comma == std::string::npos ? list.size() : comma;
                if (end > start) options.models.push_back(list.substr(start, end - start));
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else if (arg == "--hue" && i + 1 < argc) {
            core::Vec4& h = options.hue;
            if (SDL_sscanf(argv[++i], "%f,%f,%f,%f", &h.x, &h.y, &h.z, &h.w) == 4) {
                options.has_hue = true;
            } else {
                LOG_WARN("--hue expects r,g,b,strength");
            }
        } else if (arg == "--bind-pose") {
            options.bind_pose = true;
        } else if (arg == "--inspect") {
            options.inspect = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                options.inspect_angle_deg = float(SDL_atof(argv[++i]));
            }
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                options.inspect_distance = float(SDL_atof(argv[++i]));
            }
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                options.inspect_elevation_deg = float(SDL_atof(argv[++i]));
            }
        } else if (arg == "--inspect-head") {
            options.inspect_head = true;
        } else if (arg == "--skeleton") {
            options.skeleton = true;
        } else if (arg == "--studio" && i + 1 < argc) {
            options.studio_scenario = SDL_atoi(argv[++i]);
        } else if (arg == "--combat") {
            options.combat = true;
        } else if (arg == "--attack") {
            options.combat = true;
            options.attack = true;
        } else if (arg == "--bots" && i + 1 < argc) {
            options.combat = true;
            options.bots = SDL_atoi(argv[++i]);
        } else if (arg == "--match") {
            options.combat = true;
            options.match = true;
        } else if (arg == "--autopilot") {
            options.autopilot = true;
        } else if (arg == "--hide-ui") {
            options.hide_ui = true;
        } else if (arg == "--telemetry") {
            options.telemetry_interval = 60;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                options.telemetry_interval = SDL_atoi(argv[++i]);
            }
        } else if (arg == "--input" && i + 1 < argc) {
            float* v = options.input_override;
            if (SDL_sscanf(argv[++i], "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4],
                           &v[5]) == 6) {
                options.has_input_override = true;
            } else {
                LOG_WARN("--input expects pitch,roll,yaw,flap,tuck,brake");
            }
        } else if (arg == "--cam" && i + 1 < argc) {
            float v[6] = {};
            if (SDL_sscanf(argv[++i], "%f,%f,%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3], &v[4],
                           &v[5]) == 6) {
                options.camera_position = Vec3{v[0], v[1], v[2]};
                options.camera_target = Vec3{v[3], v[4], v[5]};
                options.has_camera = true;
            } else {
                LOG_WARN("--cam expects x,y,z,tx,ty,tz");
            }
        } else {
            LOG_WARN("unknown option '%s'", arg.c_str());
        }
    }
    return options;
}

bool App::init(const Options& options) {
    options_ = options;

    gfx::Device::Config config;
    config.title = "Dragon Engine -- M6";
    config.width = 1280;
    config.height = 720;
    config.headless = options.headless;
    if (!device_.init(config)) return false;

    if (!ui_.init(device_)) return false;

    pipelines_.init(&device_, SHADER_ROOT);
    if (!debug_.init(&device_, &pipelines_)) return false;
    if (!world_.init(&device_, &pipelines_)) return false;
    if (!shadow_.init(&device_, &pipelines_)) return false;
    world_.set_shadow_map(&shadow_);
    if (!foliage_.init(&device_, &pipelines_, &shadow_)) return false;

    regenerate_terrain();

    // The model roster. --models gives the whole list, --model sets just the
    // player's, and neither leaves the single built-in dragon. Everything past
    // the first entry exists so one match can field more than one species
    // rather than four hue-pushed copies of the same mesh.
    {
        model_sampler_ = gfx::create_model_sampler(device_.gpu());
        std::vector<std::string> roster = options_.models;
        if (roster.empty()) {
            roster.push_back(options_.model.empty() ? std::string(ASSET_ROOT "/dragon.glb")
                                                    : options_.model);
        }
        for (const std::string& path : roster) {
            auto model = std::make_unique<LoadedModel>();
            load_model(path, *model);
            models_.push_back(std::move(model));
        }
        player_model_ = 0;
        LOG_INFO("model roster: %zu entry(s)", models_.size());
        for (size_t i = 0; i < models_.size(); ++i) {
            LOG_INFO("  [%zu] %s (%d joints, scale %.3f)", i, models_[i]->path.c_str(),
                     models_[i]->skeleton.count(), double(models_[i]->asset.scale));
        }
    }

    dragon_rig_.init(player_model().skeleton, player_model().joints);
    ghost_rig_.init(player_model().skeleton, player_model().joints);
    // The chain dynamics need to know how big the dragon is in metres.
    dragon_rig_.set_model_scale(player_model().asset.scale);
    ghost_rig_.set_model_scale(player_model().asset.scale);
    apply_idle_clip(player_model(), dragon_rig_);
    apply_idle_clip(player_model(), ghost_rig_);
    // Rig tuning is separate from flight handling: the former changes only the
    // visible pose, the latter changes forces and control response. Both were
    // resolved per model by load_model, so a bot flying another species gets
    // that species' pose without the player's being disturbed.
    dragon_rig_.tuning = player_model().rig_tuning;
    ghost_rig_.tuning = dragon_rig_.tuning;
    model_rig_tuning_path_ = player_model().rig_tuning_path;
    // Combat has no notion of species; it is told the player's scales here and
    // on every model switch, so a headless run gets them without a panel.
    combat_.player_breath = player_model().breath.scales;

    // A tuning file next to the assets overrides the built-in defaults, so a
    // good session's numbers survive a rebuild.
    game::load_tuning(flight_.tuning, ASSET_ROOT "/flight_tuning.cfg");
    // A model may carry its own handling on top: <model>.flight.cfg next to
    // the glTF. The heavy-looking dragon and the quick-looking wyvern fly the
    // same numbers otherwise, and the eye disagrees.
    model_tuning_path_ = player_model().flight_tuning_path;
    if (game::load_tuning(flight_.tuning, model_tuning_path_.c_str())) {
        LOG_INFO("loaded model handling from %s", model_tuning_path_.c_str());
    }

    // A unit-radius torus scaled per ring: one mesh, any checkpoint size.
    ring_mesh_.upload(device_.gpu(),
                      gfx::make_torus(RING_MESH_RADIUS, 0.05f, core::Vec3::one(), 40, 10),
                      "checkpoint_ring");
    // A unit sphere scaled per use: projectiles, sentinels, blast markers.
    sphere_mesh_.upload(device_.gpu(), gfx::make_sphere(1.0f, core::Vec3::one(), 18, 12),
                        "unit_sphere");
    if (!particles_.init(&device_, &pipelines_)) return false;
    // Headless runs have no ears; a machine without an output device plays on
    // silently rather than failing.
    if (!options.headless) audio_.init();
    rebuild_courses();
    best_times_.load(ASSET_ROOT "/best_times.txt");
    current_course_ = options.course_index;
    apply_assist_preset(0);

    autopilot_ = options.autopilot;
    // Skeleton overlay is opt-in even when inspecting: for an imported rig it
    // obscures the very mesh being checked.
    apply_camera_preset(options.camera_mode);
    chase_.first_person = options.first_person;
    respawn_dragon();

    if (options.skeleton) show_skeleton_ = true;
    if (options.has_hue) {
        player_hue_ = core::Vec3{options.hue.x, options.hue.y, options.hue.z};
        player_recolour_ = options.hue.w;
    }
    if (options.studio_scenario >= 0) {
        studio_active_ = true;
        studio_scenario_ = options.studio_scenario % int(game::StudioScenario::Count);
        studio_centre_ = flight_.state().position + core::Vec3{0.0f, 45.0f, 0.0f};
    }

    // After the spawn: the arena is built around where the dragon actually
    // starts, so a headless combat run opens with targets in front of it.
    if (options.bot_range > 0.0f) {
        bot_spawn_range_ = options.bot_range;
        // Keep the jitter proportional, or a short range spawns everyone on
        // top of each other.
        bot_spawn_jitter_ = options.bot_range * 0.25f;
    }
    if (options.combat) {
        combat_enabled_ = true;
        combat_.reset(&terrain_, flight_.state().position, 20260824u);
        if (options.bots > 0) spawn_bots(options.bots);
        if (options.match) {
            bot_count_ = options.bots > 0 ? options.bots : bot_count_;
            start_match();
        }
    }

    if (options.has_camera) {
        camera_.set_position(options.camera_position, options.camera_target);
        free_camera_ = true;
    } else {
        frame_camera_on_valley();
    }
    // Fast enough to fly a valley at scale; the debug camera is for surveying.
    camera_.speed = 90.0f;

    running_ = true;
    return true;
}

void App::rebuild_courses() {
    const float extent = terrain_settings_.half_extent;
    courses_.clear();
    courses_.push_back(game::make_valley_run(terrain_, extent));
    courses_.push_back(game::make_canyon_weave(terrain_, extent));
    courses_.push_back(game::make_summit_climb(terrain_, extent));

    // A hand-authored course on disk takes precedence over the generated set.
    game::Course custom;
    if (game::load_course(custom, ASSET_ROOT "/course.txt")) courses_.push_back(custom);
}

void App::apply_assist_preset(int index) {
    assist_preset_ = index;
    game::FlightTuning& t = flight_.tuning;
    switch (index) {
        case 1:  // standard
            t.bank_limit_deg = 78.0f;
            assists_.auto_flap = true;
            assists_.ring_radius_scale = 1.1f;
            break;
        case 2:  // expert -- no bank barrier, no auto-flap, tighter checkpoints
            t.bank_limit_deg = 0.0f;
            assists_.auto_flap = false;
            assists_.ring_radius_scale = 0.85f;
            break;
        default:  // relaxed
            t.bank_limit_deg = 70.0f;
            assists_.auto_flap = true;
            assists_.ring_radius_scale = 1.5f;
            break;
    }
    // Checkpoint size is part of the difficulty, so reselecting applies it.
    select_course(current_course_);
}

void App::select_course(int index) {
    if (courses_.empty()) return;
    current_course_ = int(core::clampf(float(index), 0.0f, float(courses_.size() - 1)));

    // Scale checkpoint radii for difficulty. Missing a ring by a metre is a
    // frustration; the challenge should be the line between them.
    game::Course scaled = courses_[size_t(current_course_)];
    for (game::Ring& ring : scaled.rings) ring.radius *= assists_.ring_radius_scale;
    rally_.set_course(scaled);
    // Records outlive a course switch, so restore the one for this course.
    rally_.set_best_time(best_times_.best(rally_.course().name));
}

void App::respawn_dragon() {
    // Airborne in the valley corridor, already at a comfortable cruise, facing
    // along the valley. Starting from a stall on the ground would make every
    // test begin with a recovery.
    const float z = -terrain_settings_.half_extent * 0.55f;
    const float x = terrain_.valley_center_x(z);
    const float ground = terrain_.height_at(x, z);

    const float ahead_z = z + 700.0f;
    core::Vec3 spawn{x, ground + 170.0f, z};
    core::Vec3 look{terrain_.valley_center_x(ahead_z), ground + 150.0f, ahead_z};

    // With a course loaded, start on a run-up to the first ring rather than in
    // the middle of the valley. A flying start is part of the time trial, so the
    // approach has to be somewhere you can actually build speed.
    if (!rally_.course().rings.empty()) {
        const game::Ring& first = rally_.course().rings.front();
        const core::Vec3 approach = first.normal();  // the way you fly through it
        spawn = first.position - approach * 420.0f;
        const float ground_here = terrain_.height_at(spawn.x, spawn.z);
        spawn.y = core::maxf(spawn.y, ground_here + 60.0f);
        look = first.position;
    }

    flight_.reset(spawn, core::look_rotation(look - spawn, core::Vec3::up()), 42.0f);
    chase_.snap_to(flight_.state());
    // Health and breath come back, but the sentinels do not: dying should cost
    // the progress made against a wave, not reset the fight.
    combat_.revive();
    rally_.restart();
    split_flash_ = 0.0f;
    miss_flash_ = 0.0f;
    trail_count_ = 0;
    trail_cursor_ = 0;
}

gfx::ModelUniforms App::ghost_model_uniforms(const game::GhostSample& sample) const {
    gfx::ModelUniforms model;
    model.model =
        core::Mat4::trs(sample.position, sample.orientation, core::Vec3::one()) * player_model().asset.matrix();
    // Cool and slightly emissive, so the ghost reads as a recording rather than
    // as a second dragon in the world.
    model.tint = core::Vec4{0.35f, 0.62f, 0.95f, 0.28f};
    return model;
}

const gfx::Camera& App::active_camera() const {
    return free_camera_ ? camera_.camera() : chase_.camera();
}

void App::set_mouse_captured(bool captured) {
    if (captured == mouse_captured_) return;
    mouse_captured_ = captured;
    if (!options_.headless) SDL_SetWindowRelativeMouseMode(device_.window(), captured);
}

// Drives the stick toward a target assembled from whichever device is active.
//
// Nothing here accumulates except the optional mouse deflection, which decays.
// Releasing every control always returns the stick to exactly centre, which is
// what makes a bad attitude recoverable.
void App::update_stick(float dt) {
    core::Vec2 target{0.0f, 0.0f};

    if (!free_camera_) {
        // Gamepad left stick is absolute, which is why it is the best of the
        // three: the physical stick position *is* the command.
        if (input_.has_gamepad()) {
            const float raw_x =
                input_.gamepad_axis(SDL_GAMEPAD_AXIS_LEFTX, controls_.gamepad_deadzone);
            const float raw_y =
                input_.gamepad_axis(SDL_GAMEPAD_AXIS_LEFTY, controls_.gamepad_deadzone);
            // Expo curve preserves sign while softening the centre.
            target.x = core::signf(raw_x) * std::pow(std::fabs(raw_x), controls_.gamepad_expo);
            target.y = core::signf(raw_y) * std::pow(std::fabs(raw_y), controls_.gamepad_expo);
        }

        // Keyboard is a digital stick: held means full deflection. The smoothing
        // below turns that into a ramp rather than a step.
        if (!ui_.wants_keyboard()) {
            target.x += input_.axis(SDL_SCANCODE_A, SDL_SCANCODE_D);
            target.y += input_.axis(SDL_SCANCODE_S, SDL_SCANCODE_W);
        }

        if (controls_.mouse_stick && mouse_captured_ && !ui_.wants_mouse()) {
            const core::Vec2 delta = input_.mouse_delta();
            mouse_deflection_.x = core::clampf(
                mouse_deflection_.x + delta.x * controls_.mouse_sensitivity, -1.0f, 1.0f);
            mouse_deflection_.y = core::clampf(
                mouse_deflection_.y + delta.y * controls_.mouse_sensitivity, -1.0f, 1.0f);
            target.x += mouse_deflection_.x;
            target.y += mouse_deflection_.y;
        }
    }

    // The mouse spring always relaxes, even while the mouse is driving, so
    // deflection reflects recent motion rather than the whole session.
    mouse_deflection_.x = core::damp(mouse_deflection_.x, 0.0f, controls_.mouse_return, dt);
    mouse_deflection_.y = core::damp(mouse_deflection_.y, 0.0f, controls_.mouse_return, dt);

    // Clamp the combined target to the unit disc, so diagonal input is not
    // stronger than cardinal input.
    const float magnitude = core::length(core::Vec3{target.x, target.y, 0.0f});
    if (magnitude > 1.0f) target *= 1.0f / magnitude;

    stick_.x = core::damp(stick_.x, target.x, controls_.stick_smoothing, dt);
    stick_.y = core::damp(stick_.y, target.y, controls_.stick_smoothing, dt);
}

// Free look, in degrees for this frame. Right-drag with the mouse, or the
// gamepad's right stick.
core::Vec2 App::read_free_look(float dt) const {
    if (free_camera_) return core::Vec2{0.0f, 0.0f};

    core::Vec2 look{0.0f, 0.0f};

    // Right-drag is free for looking because flight steering does not use the
    // mouse by default.
    if (!controls_.mouse_stick && input_.mouse_down(SDL_BUTTON_RIGHT) && !ui_.wants_mouse()) {
        const core::Vec2 delta = input_.mouse_delta();
        look.x += delta.x * controls_.free_look_mouse;
        look.y += delta.y * controls_.free_look_mouse;
    }

    if (input_.has_gamepad()) {
        const float invert = controls_.invert_free_look_y ? -1.0f : 1.0f;
        look.x += input_.gamepad_axis(SDL_GAMEPAD_AXIS_RIGHTX, controls_.gamepad_deadzone) *
                  controls_.free_look_gamepad * dt;
        look.y += input_.gamepad_axis(SDL_GAMEPAD_AXIS_RIGHTY, controls_.gamepad_deadzone) *
                  controls_.free_look_gamepad * dt * invert;
    }
    return look;
}

void App::apply_camera_preset(int index) {
    camera_preset_ = index;
    switch (index) {
        case 1: chase_.tuning = game::camera_preset_action(); break;
        case 2: chase_.tuning = game::camera_preset_cinematic(); break;
        default: chase_.tuning = game::camera_preset_chase(); break;
    }
    // The boost FOV surge rides on top of whatever the preset chose, so the
    // preset's own base is recorded here and re-applied each frame.
    base_fov_ = chase_.tuning.fov_base_deg;
}

game::FlightInput App::read_flight_input() const {
    game::FlightInput in;

    if (autopilot_) {
        // Aim at the next checkpoint, or hold the last heading once the run is
        // over. The autopilot flies through the same FlightInput a player uses,
        // so it cannot cheat the flight model.
        const game::Ring* target = rally_.next_ring();
        const core::Vec3 aim =
            target ? target->position
                   : flight_.state().position + flight_.state().forward() * 500.0f;
        // Passing the ring's normal makes it line up on the approach axis rather
        // than cutting across the plane and clipping the rim.
        const core::Vec3 approach = target ? target->normal() : core::Vec3::zero();
        const core::Vec3 position = flight_.state().position;
        return game::steer_through(flight_.state(), aim, approach, autopilot_tuning_,
                                   terrain_.height_at(position.x, position.z));
    }

    // --input holds a control position at the stick, and the stick is upstream
    // of the assists -- so the scripted input falls through into them rather
    // than returning here. It used to return immediately, which meant no
    // headless run could ever exercise auto-flap or the tuck nose-over: the
    // landing the player complained about was unreproducible precisely because
    // the flag skipped the code that was interfering with it.
    const bool scripted = options_.has_input_override;
    if (scripted) {
        const float* v = options_.input_override;
        in.pitch = v[0];
        in.roll = v[1];
        in.yaw = v[2];
        in.flap = v[3];
        in.tuck = v[4];
        in.brake = v[5];
    } else {
        if (free_camera_) return in;

        // W pitches the nose up. invert_pitch gives the flight-sim
        // pull-back-to-climb feel instead.
        in.pitch = stick_.y * (controls_.invert_pitch ? -1.0f : 1.0f);
        in.roll = stick_.x;

        const bool keyboard_free = !ui_.wants_keyboard();
        if (keyboard_free) {
            in.yaw = input_.axis(SDL_SCANCODE_Q, SDL_SCANCODE_E);
            in.flap = input_.down(SDL_SCANCODE_SPACE) ? 1.0f : 0.0f;
            in.tuck = (input_.down(SDL_SCANCODE_LSHIFT) || input_.down(SDL_SCANCODE_RSHIFT))
                          ? 1.0f
                          : 0.0f;
            in.brake = (input_.down(SDL_SCANCODE_LCTRL) || input_.down(SDL_SCANCODE_RCTRL))
                           ? 1.0f
                           : 0.0f;
        }
    }

    // Combat boost is an ability with its own cooldown, so the flight model only
    // ever sees whether it is currently firing.
    in.boost = combat_.boost_active() ? 1.0f : 0.0f;

    if (input_.has_gamepad()) {
        in.tuck = core::maxf(in.tuck, input_.gamepad_trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    }
    // Tuck commits the nose. Folding the wings only sheds lift: at level
    // attitude that is a slow flat mush, and "the dive button dives slower than
    // pushing the stick" is exactly how it played. The stick still overrides --
    // the assist fades with any deliberate pitch input.
    in.pitch += assists_.tuck_nose_over * in.tuck * -(1.0f - core::minf(std::fabs(in.pitch), 1.0f));
    in.pitch = core::clampf(in.pitch, -1.0f, 1.0f);

    // Auto-flap. Holding a key to stay airborne is busywork rather than skill,
    // and forgetting it is the most common way a new pilot ends up in the ground.
    // Proportional rather than on/off, so it only supplies the energy actually
    // missing and leaves the dive-and-climb trade intact.
    if (assists_.auto_flap) {
        const game::FlightState& s = flight_.state();
        // The brake is a command to SPEND energy and this assist exists to
        // supply it, so held together they are a tug of war the assist always
        // won. A braked approach sat at full flap from 99 m of clearance all
        // the way down to 40 m -- the speed-deficit branch reads the
        // deliberate deceleration as an energy shortfall and answers it -- and
        // the pilot could not descend on purpose. Only under 40 m did the old
        // gate release, by which point the dragon was slow, stalled and
        // arrived rather than landed. The brake now scales the whole assist
        // down in proportion: half brake, half assist, full brake none,
        // wherever the dragon is. The earlier fix capped the gate at 40 m
        // while the assist itself reaches `auto_flap_clearance` (75 m), which
        // left a 35 m band where landing intent was simply not recognised.
        const float brake_intent = core::saturate((in.brake - 0.15f) / 0.45f);
        // Landing intent: braking anywhere the assist would otherwise reach,
        // or already down.
        const bool landing =
            s.grounded ||
            (brake_intent > 0.6f && s.ground_clearance < assists_.auto_flap_clearance);
        // Diving is a decision: no assist flaps against a nose pointed down.
        const bool diving = s.forward().y < -0.25f || in.tuck > 0.1f;
        float assist = 0.0f;
        if (!landing) {
            const float deficit = (assists_.auto_flap_speed - s.airspeed) / 14.0f;
            assist = diving ? 0.0f : core::saturate(deficit);
            // Sink protection: a fight at healthy airspeed still glides steadily
            // downhill, and a pilot busy aiming does not notice until the ground
            // does. Flap against an unintended descent -- unintended meaning no
            // tuck, no brake and no dive.
            if (!diving && in.brake < 0.1f) {
                assist = core::maxf(assist, core::saturate((-s.climb_rate - 4.0f) / 8.0f));
            }
            // Close to the ground and sinking: proportional, not a hard 1.
            if (s.ground_clearance < assists_.auto_flap_clearance && s.climb_rate < 1.0f &&
                !diving) {
                assist = core::maxf(assist, core::saturate((assists_.auto_flap_clearance -
                                                            s.ground_clearance) / 30.0f));
            }
            assist *= 1.0f - brake_intent;
        }
        in.flap = core::maxf(in.flap, assist);
    }

    if (input_.has_gamepad()) {
        // D-pad rudder, triggers are the two energy verbs, A flaps. The
        // shoulders belong to combat: with rudder there too, firing a fireball
        // also yawed the dragon right, and holding breath dragged it left --
        // which bled energy and read as the assists being broken.
        const float pad_yaw = (input_.gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1.0f : 0.0f) -
                              (input_.gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? 1.0f : 0.0f);
        in.yaw = core::clampf(in.yaw + pad_yaw, -1.0f, 1.0f);
        in.flap = core::maxf(in.flap, input_.gamepad_button(SDL_GAMEPAD_BUTTON_SOUTH) ? 1.0f : 0.0f);
        in.brake = core::maxf(in.brake, input_.gamepad_trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    }
    return in;
}

gfx::ModelUniforms App::dragon_model_uniforms() const {
    const game::FlightState& s = dragon_state();
    gfx::ModelUniforms model;
    // The asset correction is applied inside the dragon's own frame, so it
    // aligns the model to the engine without disturbing the flight transform.
    model.model = core::Mat4::trs(s.position, s.orientation, core::Vec3::one()) * player_model().asset.matrix();
    model.recolour = core::Vec4{player_hue_.x, player_hue_.y, player_hue_.z, player_recolour_};
    return model;
}

void App::replant() {
    vegetation_.plant(terrain_, vegetation_settings_);
    for (int k = 0; k < gfx::TREE_KINDS; ++k) {
        foliage_.set_trees(device_, gfx::TreeKind(k), vegetation_.trees(gfx::TreeKind(k)));
    }
    foliage_.wind = vegetation_settings_.wind;
}

void App::regenerate_terrain() {
    terrain_.generate(terrain_settings_);
    terrain_mesh_.release(device_.gpu());
    terrain_mesh_.upload(device_.gpu(), terrain_.mesh_data(), "terrain");
    terrain_skirt_mesh_.release(device_.gpu());
    if (!terrain_.skirt_mesh_data().indices.empty()) {
        terrain_skirt_mesh_.upload(device_.gpu(), terrain_.skirt_mesh_data(), "terrain_skirt");
    }
    // The water surface: one quad at the water line over the whole world. The
    // terrain hides it everywhere the ground is above the line, which is
    // everywhere but the river.
    {
        gfx::MeshData quad;
        const float half = terrain_settings_.half_extent * terrain_settings_.skirt_extent_factor;
        const float y = terrain_settings_.water_level;
        for (int i = 0; i < 4; ++i) {
            gfx::MeshVertex v;
            v.position = core::Vec3{(i & 1) ? half : -half, y, (i & 2) ? half : -half};
            v.normal = core::Vec3::up();
            v.color = core::Vec3::one();
            quad.vertices.push_back(v);
        }
        quad.indices = {0, 2, 1, 1, 2, 3};
        water_mesh_.release(device_.gpu());
        water_mesh_.upload(device_.gpu(), quad, "water");
    }
    replant();
    // Snow should sit sensibly relative to whatever the peaks came out at.
    material_.water_level = terrain_settings_.water_level;
    material_.rock_slope = 0.62f;
    material_.half_extent = terrain_settings_.half_extent;
    // High enough that snow reads as mountain caps rather than covering the
    // whole upper valley.
    material_.snow_line = core::lerpf(terrain_.min_height(), terrain_.max_height(), 0.74f);
}

void App::frame_camera_on_valley() {
    // Stand in the valley corridor, a little above the floor, looking along it.
    const float z = -terrain_settings_.half_extent * 0.55f;
    const float x = terrain_.valley_center_x(z);
    const float ground = terrain_.height_at(x, z);
    Vec3 eye{x, ground + 220.0f, z};
    const float look_z = z + 900.0f;
    Vec3 target{terrain_.valley_center_x(look_z), ground + 40.0f, look_z};
    camera_.set_position(eye, target);
}

void App::shutdown() {
    if (device_.gpu()) SDL_WaitForGPUIdle(device_.gpu());
    terrain_mesh_.release(device_.gpu());
    // Every roster entry owns GPU handles, not just the one being flown.
    for (auto& model : models_) {
        model->mesh.release(device_.gpu());
        for (SDL_GPUTexture* texture : model->textures) {
            if (texture) SDL_ReleaseGPUTexture(device_.gpu(), texture);
        }
        model->textures.clear();
    }
    ring_mesh_.release(device_.gpu());
    if (model_sampler_) SDL_ReleaseGPUSampler(device_.gpu(), model_sampler_);
    foliage_.shutdown(device_);
    world_.shutdown(device_);
    shadow_.shutdown(device_);
    audio_.shutdown();
    particles_.shutdown();
    debug_.shutdown();
    pipelines_.shutdown();
    ui_.shutdown();
    device_.shutdown();
}

float App::average_frame_ms() const {
    if (frame_filled_ == 0) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < frame_filled_; ++i) sum += frame_history_[i];
    return (sum / float(frame_filled_)) * 1000.0f;
}

void App::pump_events() {
    input_.begin_frame();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        bool consumed = ui_.process_event(event);
        input_.handle_event(event, consumed);
        if (event.type == SDL_EVENT_QUIT) running_ = false;
    }

    // Escape releases the mouse first and only quits once the cursor is already
    // free, so it never feels like the game trapped the pointer.
    if (input_.pressed(SDL_SCANCODE_ESCAPE)) {
        if (mouse_captured_) {
            set_mouse_captured(false);
        } else {
            running_ = false;
        }
    }

    if (input_.pressed(SDL_SCANCODE_TAB)) {
        free_camera_ = !free_camera_;
        if (free_camera_) {
            set_mouse_captured(false);
            // Detach where the chase camera already is, so the view is
            // continuous instead of teleporting to wherever the free camera was
            // last parked.
            const gfx::Camera& from = chase_.camera();
            camera_.set_position(from.position, from.position + from.forward() * 60.0f);
        }
    }
    if (input_.pressed(SDL_SCANCODE_R)) respawn_dragon();
    if (input_.pressed(SDL_SCANCODE_V)) chase_.first_person = !chase_.first_person;
    // M cycles the roster. The point is comparing species with the scenario
    // held still, so this deliberately does not touch the studio state: the
    // manoeuvre keeps playing and only the creature under it changes.
    if (options_.cycle_models > 0 && models_.size() > 1 &&
        frame_index_ > 0 && frame_index_ % options_.cycle_models == 0) {
        set_player_model((player_model_ + 1) % int(models_.size()));
    }
    if (input_.pressed(SDL_SCANCODE_M) && models_.size() > 1) {
        set_player_model((player_model_ + 1) % int(models_.size()));
    }
    if (input_.pressed(SDL_SCANCODE_F1)) show_panels_ = !show_panels_;
    if (input_.pressed(SDL_SCANCODE_1)) apply_camera_preset(0);
    if (input_.pressed(SDL_SCANCODE_2)) apply_camera_preset(1);
    if (input_.pressed(SDL_SCANCODE_3)) apply_camera_preset(2);

    // Only mouse steering needs the pointer; keyboard and gamepad leave it free
    // so the tuning panel stays usable while flying.
    if (!free_camera_ && controls_.mouse_stick && !ui_.wants_mouse() &&
        input_.mouse_pressed(SDL_BUTTON_LEFT)) {
        set_mouse_captured(true);
    }
    if (!controls_.mouse_stick && mouse_captured_) set_mouse_captured(false);

    // Right mouse means "look around" in both modes: it turns the survey camera
    // in free-camera mode and orbits the chase camera otherwise. Relative mode
    // hides the cursor and delivers unbounded deltas, so the view can keep
    // turning past the window edge.
    const bool want_look = input_.mouse_down(SDL_BUTTON_RIGHT) && !ui_.wants_mouse() &&
                           !controls_.mouse_stick;
    if (want_look != mouse_look_) {
        mouse_look_ = want_look;
        if (!options_.headless) SDL_SetWindowRelativeMouseMode(device_.window(), mouse_look_);
    }
}

void App::update(float dt) {
    time_seconds_ += dt;

    reload_timer_ += dt;
    if (reload_timer_ >= 0.25f) {
        reload_timer_ = 0.0f;
        shader_reloads_ += pipelines_.poll_hot_reload();
    }

    update_stick(dt);

    // The dragon always flies, even while the free camera is being used to look
    // at it -- otherwise you cannot inspect a manoeuvre from outside.
    flight_.update(read_flight_input(), &terrain_, dt);

    rig_action_ = anim::RigAction{};
    if (studio_active_) {
        studio_time_previous_ = studio_time_;
        studio_time_ += dt * studio_time_scale_;
        const auto scenario = game::StudioScenario(studio_scenario_);
        const float ground = terrain_.height_at(studio_centre_.x, studio_centre_.z);
        studio_state_ = game::studio_state(scenario, studio_time_, studio_centre_, ground,
                                          flight_.tuning.ground_offset);
        if (scenario == game::StudioScenario::Attack) {
            dragon_rig_.set_aim_target(game::studio_attack_target(studio_time_, studio_centre_));
        }
        rig_action_ = game::studio_action(scenario, studio_time_previous_, studio_time_);
    }

    chase_.update(dragon_state(), &terrain_, read_free_look(dt), dt);
    if (free_camera_) camera_.update(input_, dt, mouse_look_);

    // Inspection view: a tight orbit locked to the dragon, for looking at the
    // rig rather than at the world.
    if (options_.inspect) {
        // dragon_state(), not flight_.state(): in the studio the dragon is
        // rendered at the pinned studio pose, and a camera orbiting the live
        // flight model would be looking at empty sky.
        const game::FlightState& s = dragon_state();
        const float angle = core::radians(options_.inspect_angle_deg);
        const float elevation = core::radians(core::clampf(options_.inspect_elevation_deg, -89.0f, 89.0f));
        const float d = options_.inspect_distance;
        const core::Vec3 offset = core::rotate(s.orientation,
                                               core::Vec3{std::sin(angle) * std::cos(elevation) * d,
                                                          std::sin(elevation) * d,
                                                          std::cos(angle) * std::cos(elevation) * d});
        core::Vec3 focus = s.position;
        if (options_.inspect_head) {
            // Last frame's head, in world space -- the same transform the
            // muzzle uses.
            const core::Vec3 head_model = dragon_rig_.head_position();
            if (core::length_sq(head_model) > 1e-6f) {
                focus = core::transform_point(
                    core::Mat4::trs(s.position, s.orientation, core::Vec3::one()) * player_model().asset.matrix(),
                    head_model);
            }
        }
        camera_.set_position(focus + offset, focus);
        free_camera_ = true;
    }

    if (combat_enabled_ && !studio_active_) {
        // Fire leaves the MOUTH the rig animates, not a fixed body offset. The
        // rig runs later this frame, so this is last frame's head -- one frame
        // of lag on a body-attached point is invisible.
        {
            const game::FlightState& s = flight_.state();
            const core::Mat4 to_world =
                core::Mat4::trs(s.position, s.orientation, core::Vec3::one()) * player_model().asset.matrix();
            const core::Vec3 head_model = dragon_rig_.head_position();
            if (core::length_sq(head_model) > 1e-6f) {
                combat_.set_muzzle(core::transform_point(to_world, head_model));
            }
        }

        // Relock is edge-triggered; Input tracks key edges but only held state
        // for gamepad buttons, so the edge is derived here.
        const bool cycle_down = input_.has_gamepad() &&
                                input_.gamepad_button(SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        cycle_requested_ = input_.pressed(SDL_SCANCODE_T) ||
                           (cycle_down && !cycle_button_was_down_);
        cycle_button_was_down_ = cycle_down;

        update_bots(dt);
        const game::CombatEvents events = combat_.update(dt, flight_.state(), read_combat_input());
        match_.update(dt, events);
        for (const game::Impact& impact : combat_.impacts()) {
            emit_impact(impact.position, impact.team == game::Team::Hostile, impact.on_terrain);
            // Loudness by proximity to the ear, not to the dragon: the chase
            // camera is where the player sits.
            const float d = core::distance(active_camera().position, impact.position);
            audio_.play(audio::Clip::Explosion, 1.2f / (1.0f + d * d / (170.0f * 170.0f)));
        }
        if (combat_.breathing()) {
            emit_flame(combat_.breath_origin(), combat_.breath_direction(),
                       combat_.tuning.breath_range * combat_.player_breath.range, false, dt,
                       &player_model().breath);
        }
        for (const game::BreathCone& flame : combat_.hostile_breaths()) {
            // A bot tagged its cone with its model index; an untagged cone
            // (a sentinel) keeps the shared hostile blue.
            const game::BreathProfile* profile =
                flame.source >= 0 && size_t(flame.source) < models_.size()
                    ? &models_[size_t(flame.source)]->breath
                    : nullptr;
            emit_flame(flame.origin, flame.direction,
                       combat_.tuning.hostile_breath_range * flame.scales.range, true, dt,
                       profile);
        }
        // A thin ember trail off every live round, so its path lingers a beat.
        for (const game::Projectile& projectile : combat_.projectiles()) {
            if (!projectile.alive) continue;
            gfx::Particle p;
            p.position = projectile.position;
            p.velocity = projectile.velocity * 0.05f;
            p.drag = 2.5f;
            p.life = 0.30f;
            p.size_start = 1.4f;
            p.size_end = 0.4f;
            const bool mine = projectile.team == game::Team::Player;
            p.color_start = mine ? core::Vec3{1.8f, 1.0f, 0.35f} : core::Vec3{0.9f, 1.2f, 1.9f};
            p.color_end = mine ? core::Vec3{0.8f, 0.2f, 0.05f} : core::Vec3{0.15f, 0.3f, 0.8f};
            p.brightness = 0.9f;
            particles_.spawn(p);
        }
        // Enter starts the rematch from the results screen; R already means
        // respawn and stays out of it.
        if (match_.phase() == game::MatchPhase::Results &&
            input_.pressed(SDL_SCANCODE_RETURN)) {
            start_match();
        }

        // The head turns toward whatever is locked, so the dragon visibly looks
        // at what it is about to burn. Read after the update so the head and the
        // aim agree on the same frame, and consumed by the rig further down.
        if (combat_.has_lock()) {
            dragon_rig_.set_aim_target(combat_.lock_position());
        } else {
            dragon_rig_.clear_aim_target();
        }
        // And the mouth: open on the flame, a spit on the fireball.
        rig_action_.breath = combat_.breathing() ? 1.0f : 0.0f;
        rig_action_.fire = events.fired;
        rig_action_.boost = combat_.boost_active() ? 1.0f : 0.0f;

        if (events.had_hit) {
            hit_marker_ = 0.35f;
            hit_marker_position_ = events.last_hit;
            // A burning target sheds embers continuously: breath damage has no
            // projectile impact to detonate, and a white glow alone was not
            // reading as "your flame is landing".
            if (combat_.breathing()) {
                for (int i = 0; i < 2; ++i) {
                    gfx::Particle p;
                    p.position = events.last_hit +
                                 core::Vec3{particle_unit(), particle_unit(), particle_unit()} *
                                     4.0f;
                    p.velocity = core::Vec3{6.0f * particle_unit(),
                                            8.0f + 5.0f * particle_unit(),
                                            6.0f * particle_unit()};
                    p.drag = 1.0f;
                    p.life = 0.5f;
                    p.size_start = 1.3f;
                    p.size_end = 0.3f;
                    p.color_start = core::Vec3{2.2f, 1.3f, 0.4f};
                    p.color_end = core::Vec3{0.9f, 0.2f, 0.05f};
                    p.brightness = 1.1f;
                    particles_.spawn(p);
                }
            }
        }
        // Being INSIDE a flame is its own visual: embers swarming the body.
        // Damage numbers arrive silently, but fire crawling over your own
        // dragon in the chase view is unmistakable.
        for (const game::BreathCone& cone : combat_.hostile_breaths()) {
            if (!game::point_in_cone(flight_.state().position, cone.origin, cone.direction,
                                     core::radians(combat_.tuning.hostile_breath_half_angle_deg),
                                     combat_.tuning.hostile_breath_range)) {
                continue;
            }
            const game::FlightState& s = flight_.state();
            for (int i = 0; i < 3; ++i) {
                gfx::Particle p;
                p.position = s.position + core::Vec3{particle_unit(), particle_unit(),
                                                     particle_unit()} *
                                              5.0f;
                p.velocity = s.velocity * 0.6f +
                             core::Vec3{particle_unit(), 6.0f + 4.0f * particle_unit(),
                                        particle_unit() * 4.0f};
                p.drag = 1.2f;
                p.life = 0.45f;
                p.size_start = 1.2f;
                p.size_end = 0.3f;
                p.color_start = core::Vec3{2.0f, 1.2f, 0.4f};
                p.color_end = core::Vec3{0.9f, 0.2f, 0.05f};
                p.brightness = 1.1f;
                particles_.spawn(p);
            }
        }

        if (events.damage_taken > 0.0f) {
            // Rate-limited: a flame deals damage every frame, and forty
            // overlapping cries per second was the "strange loud flame" of the
            // playtest. One screech, then a beat before the next.
            if (hit_sound_cooldown_ <= 0.0f) {
                audio_.play(audio::Clip::Screech, 0.85f);
                hit_sound_cooldown_ = 0.45f;
            }
            damage_flash_ = 1.0f;
            // Held well past the flash: the point is to let the player turn and
            // find the shooter, which takes longer than the hit registers.
            damage_direction_ = events.damage_from;
            damage_marker_ = 3.0f;
        }
        if (events.player_died) {
            audio_.play(audio::Clip::KnockOut, 1.0f);
            respawn_dragon();
        }
    }
    hit_marker_ = core::maxf(hit_marker_ - dt, 0.0f);
    damage_flash_ = core::maxf(damage_flash_ - dt * 1.6f, 0.0f);
    damage_marker_ = core::maxf(damage_marker_ - dt, 0.0f);
    hit_sound_cooldown_ = core::maxf(hit_sound_cooldown_ - dt, 0.0f);

    // Boost: a rising rush on activation, and a wake of hot streaks while the
    // thrust lasts -- with the sound and particles both keyed to the same flag
    // the flight model reads, they can never disagree with the physics.
    const bool boosting = combat_.boost_active();
    if (boosting && !was_boosting_) audio_.play(audio::Clip::Boost, 0.9f);
    if (boosting) {
        // Air, not fire: pale slipstream threads peeling off BOTH wingtips, and
        // faint streaks rushing PAST the body -- the world moving, not the
        // dragon burning. Flame-coloured boost read as being on fire. The
        // threads leave the animated tips (the wing flaps through the burn),
        // one thread per side per step so the two wings always match: an
        // earlier version drew the side from a random bit it never advanced,
        // and whole frames of threads landed on one wing.
        const game::FlightState& s = flight_.state();
        const core::Mat4 to_world =
            core::Mat4::trs(s.position, s.orientation, core::Vec3::one()) * player_model().asset.matrix();
        const core::Vec3 pale{0.55f, 0.65f, 0.8f};
        static float boost_carry = 0.0f;
        boost_carry += dt * 90.0f;
        while (boost_carry >= 1.0f) {
            boost_carry -= 1.0f;
            for (int side = 0; side < 2; ++side) {
                gfx::Particle p;
                if (player_model().wingtip_joint[side] >= 0) {
                    p.position = core::transform_point(
                        to_world,
                        dragon_rig_.world_matrices()[size_t(player_model().wingtip_joint[side])].col[3].xyz());
                } else {
                    p.position = s.position + s.right() * (side == 0 ? 9.0f : -9.0f) + s.up();
                }
                // Left behind, curling slightly: the vortex, not a jet.
                p.velocity = s.velocity * 0.12f + s.up() * (1.5f * particle_unit()) +
                             s.right() * (1.0f * particle_unit());
                p.size_start = 0.6f;
                p.size_end = 2.6f;
                p.drag = 1.0f;
                p.life = 0.7f;
                p.color_start = pale;
                p.color_end = pale * 0.25f;
                p.brightness = 0.55f;
                particles_.spawn(p);
            }
            // Slipstream streak: born ahead and beside, swept backward fast so
            // it rushes past the camera.
            gfx::Particle streak;
            streak.position = s.position + s.forward() * (30.0f + 20.0f * particle_unit()) +
                              s.right() * (12.0f * particle_unit()) +
                              s.up() * (8.0f * particle_unit());
            streak.velocity = s.velocity * 0.1f - s.forward() * 60.0f;
            streak.size_start = 0.5f;
            streak.size_end = 1.6f;
            streak.drag = 1.0f;
            streak.life = 0.5f;
            streak.color_start = pale;
            streak.color_end = pale * 0.3f;
            streak.brightness = 0.5f;
            particles_.spawn(streak);
        }
    }
    if (boosting && !was_boosting_) {
        // The kick: a ring of air blown outward around the body at ignition, so
        // the start of a boost is an event and not just a brighter wake.
        const game::FlightState& s = flight_.state();
        for (int i = 0; i < 48; ++i) {
            const float angle = core::TWO_PI * float(i) / 48.0f;
            const core::Vec3 radial = s.right() * std::cos(angle) + s.up() * std::sin(angle);
            gfx::Particle p;
            p.position = s.position + radial * 3.0f - s.forward() * 2.0f;
            p.velocity = radial * 22.0f + s.velocity * 0.6f;
            p.drag = 2.5f;
            p.life = 0.45f;
            p.size_start = 1.2f;
            p.size_end = 3.0f;
            p.color_start = core::Vec3{0.8f, 0.9f, 1.0f};
            p.color_end = core::Vec3{0.2f, 0.25f, 0.35f};
            p.brightness = 0.7f;
            particles_.spawn(p);
        }
    }
    // The warp is mostly the lens: the field of view surges wide for the burn
    // and eases back, smoothed by the camera's own fov lag.
    const float fov_surge = boosting ? 14.0f : 0.0f;
    boost_fov_ = core::damp(boost_fov_, fov_surge, 0.18f, dt);
    chase_.tuning.fov_base_deg = base_fov_ + boost_fov_;
    was_boosting_ = boosting;

    particles_.update(dt);

    // ---- ears ----
    if (audio_.ready()) {
        const game::FlightState& s = dragon_state();
        audio_.set_wind(core::saturate(s.airspeed / 90.0f));

        // The flame you hear is any flame near you: yours at full strength, a
        // bot's by how close its cone is.
        float flame = combat_.breathing() ? 1.0f : 0.0f;
        for (const game::BreathCone& cone : combat_.hostile_breaths()) {
            const float d = core::distance(cone.origin, s.position);
            flame = core::maxf(flame, 1.0f - core::saturate(d / 220.0f));
        }
        audio_.set_flame(flame * (combat_enabled_ ? 1.0f : 0.0f));

        // The wingbeat: one whoosh at the start of each powered downstroke,
        // volume following how hard the wings are actually working.
        if (s.flap_amplitude > 0.3f && s.flap_phase < previous_flap_phase_) {
            audio_.play(audio::Clip::Flap, 0.5f * s.flap_amplitude);
        }
        previous_flap_phase_ = s.flap_phase;

        // Firing: the cooldown jumping up is the shot leaving.
        if (combat_.fire_cooldown() > previous_fire_cooldown_ + 0.5f) {
            audio_.play(audio::Clip::Shot, 0.8f);
        }
        previous_fire_cooldown_ = combat_.fire_cooldown();
    }

    if (!studio_active_) rally_.update(flight_.state(), dt);
    if (rally_.just_passed_ring()) split_flash_ = 1.6f;
    if (rally_.just_missed_ring()) miss_flash_ = 1.2f;
    split_flash_ = core::maxf(split_flash_ - dt, 0.0f);
    miss_flash_ = core::maxf(miss_flash_ - dt, 0.0f);

    // Record the result BEFORE any restart. restart() clears the one-frame
    // just_finished flag, so an auto-restart placed above this silently ate
    // every record the autopilot set.
    if (rally_.just_finished() && rally_.last_run_was_record()) {
        best_times_.submit(rally_.course().name, rally_.last_run_time());
        best_times_.save(ASSET_ROOT "/best_times.txt");
    }

    // The autopilot laps the course, which is what lets a ghost exist in a
    // headless capture and doubles as a soak test.
    if (autopilot_ && rally_.phase() == game::RunPhase::Finished) respawn_dragon();

    dragon_rig_.set_action(rig_action_);
    if (!options_.bind_pose) dragon_rig_.update(dragon_state(), dt);

    // The ghost's rig is driven from its recording, reconstructed as a flight
    // state. Only the fields the rig reads need to be real.
    game::GhostSample ghost_sample;
    if (show_ghost_ && rally_.ghost_pose(ghost_sample)) {
        game::FlightState ghost_state;
        ghost_state.position = ghost_sample.position;
        ghost_state.orientation = ghost_sample.orientation;
        ghost_state.wing_angle = ghost_sample.wing_angle;
        ghost_state.wing_tuck = ghost_sample.wing_tuck;
        ghost_state.angular_velocity = ghost_sample.angular_velocity;
        ghost_state.ground_clearance = 1000.0f;  // never extends its legs
        ghost_rig_.update(ghost_state, dt);
    }

    push_telemetry(flight_.state());

    // Breadcrumb trail, at a fixed spatial-ish rate rather than per frame.
    trail_timer_ += dt;
    if (trail_timer_ >= 0.06f) {
        trail_timer_ = 0.0f;
        trail_[trail_cursor_] = flight_.state().position;
        trail_cursor_ = (trail_cursor_ + 1) % TRAIL_POINTS;
        if (trail_count_ < TRAIL_POINTS) ++trail_count_;
    }

    if (show_grid_) debug_.grid(400.0f, 25.0f, Vec3{0.11f, 0.13f, 0.16f});

    if (show_probes_) {
        const float extent = terrain_settings_.half_extent;
        for (float z = -extent; z <= extent; z += 60.0f) {
            const float x = terrain_.valley_center_x(z);
            debug_.cross(Vec3{x, terrain_.height_at(x, z) + 3.0f, z}, 8.0f,
                         Vec3{0.9f, 0.7f, 0.25f});
        }
    }

    draw_flight_debug();
    draw_skeleton_debug();
}

void App::push_telemetry(const game::FlightState& state) {
    const int i = telemetry_.cursor;
    telemetry_.airspeed[i] = state.airspeed;
    telemetry_.altitude[i] = state.position.y;
    telemetry_.aoa_deg[i] = core::degrees(state.angle_of_attack);
    telemetry_.g_load[i] = state.g_load;
    telemetry_.climb[i] = state.climb_rate;
    telemetry_.energy[i] = state.specific_energy;
    telemetry_.cursor = (i + 1) % TELEMETRY_SAMPLES;
}

// Draws the rig as bones in world space. The fastest way to tell a skinning
// problem from an animation problem: if the bones look right and the mesh does
// not, the weights are wrong.
void App::draw_skeleton_debug() {
    if (!show_skeleton_) return;
    const game::FlightState& s = dragon_state();
    const core::Mat4 to_world =
        core::Mat4::trs(s.position, s.orientation, core::Vec3::one()) * player_model().asset.matrix();
    const std::vector<core::Mat4>& skinning = dragon_rig_.skinning_matrices();
    if (skinning.empty()) return;

    // Joint positions come from the rig's world matrices directly. Deriving them
    // by pushing a bind position through the skinning matrix only works when the
    // inverse binds were recomputed from the hierarchy -- an imported rig uses
    // the file's, and that assumption collapsed every bone onto one point.
    const std::vector<core::Mat4>& world = dragon_rig_.world_matrices();
    if (world.empty()) return;
    for (int i = 0; i < player_model().skeleton.count(); ++i) {
        const core::Vec3 posed =
            core::transform_point(to_world, world[size_t(i)].translation_part());
        const int parent = player_model().skeleton.joint(i).parent;
        if (parent != anim::NO_PARENT) {
            const core::Vec3 parent_posed =
                core::transform_point(to_world, world[size_t(parent)].translation_part());
            debug_.line(parent_posed, posed, Vec3{0.95f, 0.85f, 0.35f}, true);
        }
        debug_.cross(posed, 0.22f, Vec3{0.4f, 0.9f, 0.95f}, true);
    }
    (void)skinning;
}

void App::draw_flight_debug() {
    const game::FlightState& s = flight_.state();

    if (show_ground_probe_) {
        // Ground clearance under the dragon, plus a shadow-ring on the terrain.
        // At speed this ring is a better altitude read than the number is.
        const float ground = terrain_.height_at(s.position.x, s.position.z);
        const Vec3 below{s.position.x, ground, s.position.z};
        const bool low = s.ground_clearance < 40.0f;
        const Vec3 color = low ? Vec3{0.95f, 0.45f, 0.3f} : Vec3{0.35f, 0.75f, 0.45f};
        debug_.line(s.position, below, color, true);
        debug_.circle(below, terrain_.normal_at(s.position.x, s.position.z), 9.0f, color, 24, true);
    }

    if (show_flight_path_ && trail_count_ > 1) {
        // Where the dragon has actually been, which is not the same as where it
        // was pointing -- seeing both is how you spot a model that skids.
        for (int i = 1; i < trail_count_; ++i) {
            const int a = (trail_cursor_ - trail_count_ + i - 1 + TRAIL_POINTS * 2) % TRAIL_POINTS;
            const int b = (trail_cursor_ - trail_count_ + i + TRAIL_POINTS * 2) % TRAIL_POINTS;
            const float age = float(i) / float(trail_count_);
            debug_.line(trail_[a], trail_[b], Vec3{0.35f, 0.55f, 0.9f} * age);
        }
    }

    if (show_forces_) {
        // Force vectors, scaled so weight is a fixed length -- everything is
        // then readable relative to "one gravity".
        const float scale = 14.0f / core::maxf(flight_.tuning.mass * flight_.tuning.gravity, 1.0f);
        const Vec3 origin = s.position;
        debug_.arrow(origin, origin + flight_.debug_lift * scale, Vec3{0.4f, 0.9f, 0.5f}, true);
        debug_.arrow(origin, origin + flight_.debug_drag * scale, Vec3{0.95f, 0.4f, 0.35f}, true);
        debug_.arrow(origin, origin + flight_.debug_thrust * scale, Vec3{0.95f, 0.8f, 0.3f}, true);
        debug_.arrow(origin, origin + flight_.debug_gravity * scale, Vec3{0.5f, 0.55f, 0.7f}, true);

        // Body axes and the velocity vector. The gap between forward and
        // velocity *is* the angle of attack, made visible.
        debug_.axes(s.position, s.orientation, 9.0f, true);
        debug_.arrow(s.position, s.position + core::normalize_or(s.velocity, Vec3::forward()) * 22.0f,
                     Vec3{0.3f, 0.85f, 0.95f}, true);
    }

    // In free-camera mode the dragon needs a marker, or it is easy to lose. Not
    // while inspecting it, where the marker is the only thing in the way.
    if (free_camera_ && !options_.inspect) {
        debug_.sphere(s.position, 6.0f, Vec3{0.9f, 0.75f, 0.35f}, 20, true);
    }

    if (show_ring_path_) {
        const game::Course& course = rally_.course();
        for (size_t i = 1; i < course.rings.size(); ++i) {
            const bool ahead = int(i) > rally_.next_ring_index();
            debug_.line(course.rings[i - 1].position, course.rings[i].position,
                        ahead ? Vec3{0.28f, 0.40f, 0.55f} : Vec3{0.14f, 0.18f, 0.20f});
        }
        // A dropped line from the live ring to the ground: altitude is the
        // hardest part of a checkpoint to judge from a distance.
        if (const game::Ring* next = rally_.next_ring()) {
            const float ground = terrain_.height_at(next->position.x, next->position.z);
            debug_.line(next->position, Vec3{next->position.x, ground, next->position.z},
                        Vec3{0.85f, 0.62f, 0.2f});
        }
    }

    if (show_camera_rig_) {
        // The spring arm made visible: pivot, arm, and aim point. Seeing the arm
        // shorten against a ridge is the only way to tell a collision response
        // from a tuning problem.
        const Vec3 pivot = chase_.pivot();
        const Vec3 eye = chase_.camera().position;
        const bool blocked = chase_.arm_is_blocked();
        debug_.sphere(pivot, 1.2f, Vec3{0.4f, 0.8f, 0.9f}, 12, true);
        debug_.line(pivot, eye, blocked ? Vec3{0.95f, 0.45f, 0.3f} : Vec3{0.4f, 0.8f, 0.9f}, true);
        debug_.sphere(eye, 1.6f, Vec3{0.9f, 0.9f, 0.5f}, 12, true);
        debug_.cross(chase_.camera().position + chase_.camera().forward() * 30.0f, 2.5f,
                     Vec3{0.9f, 0.6f, 0.9f}, true);
    }
}

void App::build_ui(float dt) {
    (void)dt;
    if (options_.hide_ui) return;

    // The tuning panels cover most of the screen, which is right while tuning
    // and useless while playing. One key clears the lot; the HUD stays.
    if (!show_panels_) {
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        draw->AddText(ImVec2(12.0f, float(device_.height()) - 22.0f),
                      IM_COL32(200, 210, 225, 130), "F1  panels");
        return;
    }

    ImGui::GetForegroundDrawList()->AddText(ImVec2(12.0f, float(device_.height()) - 22.0f),
                                            IM_COL32(200, 210, 225, 110), "F1  hide panels");

    build_flight_ui();
    build_rally_ui();
    build_dragon_ui();
    build_combat_ui();
    build_studio_ui();

    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    // Collapsed by default: it is the tallest panel and holds the settings
    // touched least often, so it is most of the clutter and none of the play.
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine");

    const float ms = average_frame_ms();
    ImGui::Text("%.2f ms  (%.0f fps)   %ux%u", ms, ms > 0.0f ? 1000.0f / ms : 0.0f,
                device_.width(), device_.height());
    if (audio_.ready()) {
        float volume = audio_.master();
        if (ImGui::SliderFloat("volume", &volume, 0.0f, 1.0f)) audio_.set_master(volume);
    }

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        game::ChaseCameraTuning& c = chase_.tuning;

        if (ImGui::RadioButton("chase (1)", camera_preset_ == 0)) apply_camera_preset(0);
        ImGui::SameLine();
        if (ImGui::RadioButton("action (2)", camera_preset_ == 1)) apply_camera_preset(1);
        ImGui::SameLine();
        if (ImGui::RadioButton("cinematic (3)", camera_preset_ == 2)) apply_camera_preset(2);

        ImGui::Checkbox("first person (V)", &chase_.first_person);
        ImGui::SameLine();
        ImGui::Checkbox("free camera (tab)", &free_camera_);

        // Watching the arm shorten is how a collision response is told apart
        // from a tuning problem.
        ImGui::Text("arm %.1f / %.1f m%s", chase_.arm_length(), chase_.requested_arm_length(),
                    chase_.arm_is_blocked() ? "  BLOCKED" : "");
        const core::Vec2 look = chase_.free_look_angles();
        ImGui::Text("free look %+.0f  %+.0f deg", look.x, look.y);
        ImGui::Checkbox("show camera rig", &show_camera_rig_);

        if (ImGui::TreeNode("Arm")) {
            ImGui::SliderFloat("distance", &c.distance, 5.0f, 60.0f, "%.1f m");
            ImGui::SliderFloat("distance/speed", &c.distance_speed_gain, 0.0f, 0.4f);
            ImGui::SliderFloat("height", &c.height, 0.0f, 20.0f, "%.1f m");
            ImGui::SliderFloat("pivot forward", &c.pivot_forward, -5.0f, 10.0f, "%.1f m");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Aim & lag")) {
            ImGui::SliderFloat("look ahead", &c.look_ahead, 0.0f, 40.0f, "%.1f m");
            ImGui::SliderFloat("turn lead", &c.look_ahead_turn, 0.0f, 30.0f, "%.1f m/(rad/s)");
            ImGui::SliderFloat("aim height bias", &c.look_down_bias, -20.0f, 20.0f, "%.1f m");
            ImGui::SliderFloat("position lag", &c.position_lag, 0.0f, 0.6f, "%.3f s");
            ImGui::SliderFloat("aim lag", &c.aim_lag, 0.0f, 0.4f, "%.3f s");
            ImGui::SliderFloat("roll inherit", &c.roll_inheritance, 0.0f, 1.0f);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Field of view")) {
            ImGui::SliderFloat("fov base", &c.fov_base_deg, 40.0f, 100.0f, "%.0f deg");
            ImGui::SliderFloat("fov/speed", &c.fov_speed_gain, 0.0f, 0.6f);
            ImGui::SliderFloat("fov max", &c.fov_max_deg, 60.0f, 120.0f, "%.0f deg");
            ImGui::SliderFloat("fov lag", &c.fov_lag, 0.0f, 1.0f, "%.2f s");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Collision")) {
            ImGui::SliderFloat("ground margin", &c.collision_margin, 0.5f, 20.0f, "%.1f m");
            ImGui::SliderFloat("shorten lag", &c.collision_shorten_lag, 0.0f, 0.2f, "%.3f s");
            ImGui::SliderFloat("extend lag", &c.collision_extend_lag, 0.0f, 1.5f, "%.2f s");
            ImGui::SliderFloat("min distance", &c.min_distance, 1.0f, 20.0f, "%.1f m");
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Free look & shake")) {
            ImGui::SliderFloat("mouse deg/px", &controls_.free_look_mouse, 0.02f, 0.6f, "%.3f");
            ImGui::SliderFloat("pad deg/s", &controls_.free_look_gamepad, 20.0f, 300.0f, "%.0f");
            ImGui::SliderFloat("recentre", &c.free_look_return, 0.0f, 4.0f, "%.2f s");
            ImGui::SliderFloat("shake/speed", &c.shake_speed, 0.0f, 0.3f);
            ImGui::SliderFloat("shake/g", &c.shake_g, 0.0f, 0.3f);
            ImGui::SliderFloat("shake max", &c.shake_max, 0.0f, 5.0f, "%.1f deg");
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::SliderFloat("free cam speed", &camera_.speed, 1.0f, 400.0f, "%.0f m/s");
        if (ImGui::Button("frame valley")) frame_camera_on_valley();
    }

    if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("%.0f m across, height %.0f..%.0f m", terrain_settings_.half_extent * 2.0f,
                    terrain_.min_height(), terrain_.max_height());
        ImGui::Text("%u triangles", terrain_mesh_.index_count() / 3);

        // Regeneration rebuilds and re-uploads the whole mesh, so it is applied
        // on release rather than while the slider is being dragged.
        bool dirty = false;
        int seed = int(terrain_settings_.seed);
        if (ImGui::InputInt("seed", &seed)) {
            terrain_settings_.seed = uint32_t(seed < 0 ? 0 : seed);
            dirty = true;
        }
        dirty |= ImGui::SliderFloat("mountain height", &terrain_settings_.mountain_height, 50.0f,
                                    900.0f, "%.0f m");
        dirty |= ImGui::SliderFloat("mountain scale", &terrain_settings_.mountain_scale, 300.0f,
                                    3000.0f, "%.0f m");
        dirty |= ImGui::SliderFloat("hill height", &terrain_settings_.hill_height, 0.0f, 90.0f,
                                    "%.0f m");
        dirty |= ImGui::SliderFloat("valley width", &terrain_settings_.valley_width, 60.0f, 1400.0f,
                                    "%.0f m");
        dirty |= ImGui::SliderFloat("valley falloff", &terrain_settings_.valley_falloff, 100.0f,
                                    2000.0f, "%.0f m");
        dirty |= ImGui::SliderFloat("valley meander", &terrain_settings_.valley_meander, 0.0f,
                                    1600.0f, "%.0f m");
        if (ImGui::TreeNode("Vegetation")) {
            bool replant_now = false;
            replant_now |= ImGui::Checkbox("trees", &vegetation_settings_.trees);
            ImGui::SameLine();
            ImGui::Checkbox("grass", &vegetation_settings_.grass);
            replant_now |= ImGui::SliderFloat("tree spacing", &vegetation_settings_.tree_spacing,
                                              6.0f, 40.0f, "%.0f m");
            replant_now |= ImGui::SliderFloat("forest cover", &vegetation_settings_.forest_cover,
                                              0.0f, 1.0f);
            replant_now |= ImGui::SliderFloat("treeline above floor",
                                              &vegetation_settings_.treeline_above_floor, 50.0f,
                                              800.0f, "%.0f m");
            replant_now |= ImGui::SliderFloat("tree max slope", &vegetation_settings_.tree_max_slope,
                                              0.3f, 1.0f);
            replant_now |= ImGui::SliderFloat("slope sink", &vegetation_settings_.slope_sink, 0.0f,
                                              12.0f, "%.1f m");
            ImGui::SliderFloat("grass radius", &vegetation_settings_.grass_radius, 30.0f, 250.0f,
                               "%.0f m");
            ImGui::SliderFloat("grass spacing", &vegetation_settings_.grass_spacing, 1.0f, 8.0f,
                               "%.1f m");
            ImGui::SliderFloat("wind", &vegetation_settings_.wind, 0.0f, 3.0f);
            ImGui::TextDisabled("%u trees, %u grass tufts near the camera", foliage_.tree_count(),
                                foliage_.grass_count());
            if (replant_now && !ImGui::IsAnyItemActive()) replant();
            ImGui::TreePop();
        }
        dirty |= ImGui::SliderFloat("half extent", &terrain_settings_.half_extent, 500.0f, 4000.0f,
                                    "%.0f m");
        dirty |= ImGui::SliderFloat("cell size", &terrain_settings_.cell_size, 3.0f, 20.0f,
                                    "%.0f m");
        if (dirty && !ImGui::IsAnyItemActive()) {
            regenerate_terrain();
            // Generated courses follow the terrain, so they are stale now.
            rebuild_courses();
            select_course(current_course_);
        }

        ImGui::SliderFloat("snow line", &material_.snow_line, 0.0f, 900.0f, "%.0f m");
        ImGui::SliderFloat("rock slope", &material_.rock_slope, 0.3f, 0.95f);
        ImGui::SliderFloat("water level", &material_.water_level, 0.0f, 200.0f, "%.0f m");
        ImGui::Checkbox("wireframe", &world_.wireframe);
    }

    if (ImGui::CollapsingHeader("Sky & light", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("sun azimuth", &lighting_.sun_azimuth_deg, 0.0f, 360.0f, "%.0f deg");
        ImGui::SliderFloat("sun elevation", &lighting_.sun_elevation_deg, -5.0f, 89.0f, "%.0f deg");
        ImGui::SliderFloat("sun intensity", &lighting_.sun_intensity, 0.0f, 8.0f);
        ImGui::ColorEdit3("sun colour", lighting_.sun_color);
        ImGui::ColorEdit3("zenith", lighting_.sky_zenith);
        ImGui::ColorEdit3("horizon", lighting_.sky_horizon);
        ImGui::ColorEdit3("fog", lighting_.fog_color);
        ImGui::SliderFloat("fog density", &lighting_.fog_density, 0.0f, 0.003f, "%.5f");
        ImGui::SliderFloat("ambient", &lighting_.ambient, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("enabled", &shadow_.enabled);
        ImGui::Text("%ux%u, %.2f m/texel", shadow_.resolution(), shadow_.resolution(),
                    shadow_.extent * 2.0f / float(shadow_.resolution()));
        ImGui::SliderFloat("extent", &shadow_.extent, 200.0f, 3000.0f, "%.0f m");
        ImGui::SliderFloat("bias", &shadow_.depth_bias, 0.0f, 0.01f, "%.5f");
        ImGui::SliderFloat("strength", &shadow_.strength, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Debug draw")) {
        ImGui::Checkbox("grid", &show_grid_);
        ImGui::Checkbox("valley markers", &show_probes_);
        ImGui::Checkbox("ground probe", &show_ground_probe_);
        ImGui::Text("lines: %d", debug_.line_count());
    }

    if (ImGui::CollapsingHeader("Shaders")) {
        ImGui::Text("reloads: %d", shader_reloads_);
        if (pipelines_.broken_count() > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%d pipeline(s) broken",
                               pipelines_.broken_count());
        }
        ImGui::TextDisabled("edit shaders/*.msl and save");
    }

    ImGui::Separator();
    if (free_camera_) {
        ImGui::TextDisabled("free cam: WASD move, QE up/down, shift boost");
        ImGui::TextDisabled("hold right-mouse to look");
    }
    ImGui::TextDisabled("tab toggles free camera, esc quits");
    ImGui::End();
}

namespace {

// Projects a world point to screen pixels. Returns false when the point is
// behind the camera, where a projection would fold it back onto the screen at a
// mirrored position.
bool project_to_screen(const core::Mat4& view_proj, core::Vec3 world, float width, float height,
                       ImVec2& out) {
    const core::Vec4 clip = view_proj * core::Vec4{world, 1.0f};
    if (clip.w <= 1e-4f) return false;
    const float ndc_x = clip.x / clip.w;
    const float ndc_y = clip.y / clip.w;
    out = ImVec2((ndc_x * 0.5f + 0.5f) * width, (1.0f - (ndc_y * 0.5f + 0.5f)) * height);
    return true;
}

}  // namespace

// The playing HUD, drawn with ImGui's foreground draw list rather than as
// windows. It needs shapes and free positioning, not widgets, and this avoids
// building a 2D renderer for it.
void App::draw_hud() {
    if (!show_hud_ || options_.hide_ui || studio_active_) return;

    if (combat_enabled_) draw_combat_hud();

    // During a match the rally HUD stands down: you are fighting, not racing,
    // and the checkpoint marker collides with the scoreline.
    if (match_.phase() != game::MatchPhase::Idle) return;

    const game::Course& course = rally_.course();
    if (course.rings.empty()) return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float width = float(device_.width());
    const float height = float(device_.height());
    const core::Mat4 view_proj = active_camera().view_projection(device_.aspect());

    const ImU32 WHITE = IM_COL32(255, 255, 255, 230);
    const ImU32 DIM = IM_COL32(210, 220, 235, 150);
    const ImU32 GOLD = IM_COL32(255, 190, 70, 235);
    const ImU32 AHEAD = IM_COL32(120, 235, 140, 240);
    const ImU32 BEHIND = IM_COL32(255, 120, 100, 240);

    // ---- next checkpoint marker ----
    if (const game::Ring* next = rally_.next_ring()) {
        ImVec2 screen;
        const bool on_screen = project_to_screen(view_proj, next->position, width, height, screen) &&
                               screen.x > 0.0f && screen.x < width && screen.y > 0.0f &&
                               screen.y < height;
        const float range = core::distance(flight_.state().position, next->position);

        if (on_screen) {
            // A reticle scaled to the ring's apparent size, so it frames the
            // checkpoint instead of hiding it.
            const float apparent = core::clampf(next->radius / core::maxf(range, 1.0f) * height *
                                                    0.5f,
                                                14.0f, 260.0f);
            draw->AddCircle(screen, apparent, GOLD, 40, 2.0f);
            // Corner ticks read as a target even when the circle is large.
            for (int i = 0; i < 4; ++i) {
                const float angle = core::PI * 0.25f + core::PI * 0.5f * float(i);
                const ImVec2 inner(screen.x + std::cos(angle) * apparent * 0.72f,
                                   screen.y + std::sin(angle) * apparent * 0.72f);
                const ImVec2 outer(screen.x + std::cos(angle) * apparent * 1.05f,
                                   screen.y + std::sin(angle) * apparent * 1.05f);
                draw->AddLine(inner, outer, GOLD, 2.0f);
            }
            char label[32];
            std::snprintf(label, sizeof(label), "%.0f m", range);
            draw->AddText(ImVec2(screen.x + apparent + 8.0f, screen.y - 8.0f), GOLD, label);
        } else {
            // Off screen: an arrow pinned near the edge, pointing the shortest
            // way to turn. Without this, losing a checkpoint means flying in
            // circles hunting for it.
            const gfx::Camera& camera = active_camera();
            const core::Vec3 to_ring = next->position - camera.position;
            const float right = core::dot(to_ring, camera.right());
            const float up = core::dot(to_ring, camera.up());
            const float ahead = core::dot(to_ring, camera.forward());

            core::Vec2 direction{right, -up};
            // Behind the camera, the shortest turn is sideways, so bias the
            // arrow outward rather than letting it collapse to the centre.
            if (ahead < 0.0f && core::length(core::Vec3{direction.x, direction.y, 0.0f}) < 1e-3f) {
                direction = core::Vec2{1.0f, 0.0f};
            }
            const float length = core::length(core::Vec3{direction.x, direction.y, 0.0f});
            if (length > 1e-4f) direction *= 1.0f / length;

            const ImVec2 centre(width * 0.5f, height * 0.5f);
            const float radius = core::minf(width, height) * 0.36f;
            const ImVec2 tip(centre.x + direction.x * radius, centre.y + direction.y * radius);
            const float angle = std::atan2(direction.y, direction.x);
            const ImVec2 left(tip.x + std::cos(angle + 2.5f) * 22.0f,
                              tip.y + std::sin(angle + 2.5f) * 22.0f);
            const ImVec2 back(tip.x + std::cos(angle - 2.5f) * 22.0f,
                              tip.y + std::sin(angle - 2.5f) * 22.0f);
            draw->AddTriangleFilled(tip, left, back, GOLD);

            char label[32];
            std::snprintf(label, sizeof(label), "%.0f m", range);
            draw->AddText(ImVec2(tip.x - 18.0f, tip.y + 22.0f), GOLD, label);
        }
    }

    // ---- timer block, top centre ----
    const float timer_x = width * 0.5f;
    char line[64];

    // Scaled text rather than the default UI size: a HUD timer is read at a
    // glance while flying, not studied.
    ImFont* font = ImGui::GetFont();
    constexpr float TIMER_SIZE = 38.0f;
    constexpr float LABEL_SIZE = 16.0f;
    auto centred = [&](const char* text, float size, float y, ImU32 colour) {
        const float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
        draw->AddText(font, size, ImVec2(timer_x - w * 0.5f, y), colour, text);
    };

    const std::string elapsed = game::format_time(rally_.elapsed());

    // Panel behind the readout, so it stays legible over snow and sky alike.
    draw->AddRectFilled(ImVec2(timer_x - 150.0f, 8.0f), ImVec2(timer_x + 150.0f, 118.0f),
                        IM_COL32(10, 14, 20, 155), 8.0f);

    centred(elapsed.c_str(), TIMER_SIZE, 12.0f,
            rally_.phase() == game::RunPhase::Running ? WHITE : DIM);

    std::snprintf(line, sizeof(line), "checkpoint %d / %zu", rally_.rings_passed(),
                  course.rings.size());
    centred(line, LABEL_SIZE, 56.0f, DIM);

    if (rally_.best_time() > 0.0f) {
        std::snprintf(line, sizeof(line), "best %s", game::format_time(rally_.best_time()).c_str());
    } else {
        std::snprintf(line, sizeof(line), "no record yet");
    }
    centred(line, LABEL_SIZE, 74.0f, DIM);

    switch (rally_.phase()) {
        case game::RunPhase::Ready:
            centred("fly through the first ring to start", LABEL_SIZE, 94.0f, GOLD);
            break;
        case game::RunPhase::Finished:
            centred(rally_.last_run_was_record() ? "NEW RECORD  --  R to run again"
                                                 : "finished  --  R to run again",
                    LABEL_SIZE, 94.0f, rally_.last_run_was_record() ? AHEAD : DIM);
            break;
        case game::RunPhase::Running:
            break;
    }

    // ---- split delta flash ----
    // Only meaningful once there is a ghost to be measured against.
    if (split_flash_ > 0.0f && rally_.has_ghost() && rally_.last_split_delta() != 0.0f) {
        const float delta = rally_.last_split_delta();
        std::snprintf(line, sizeof(line), "%+.2f s", delta);
        const ImU32 colour = delta < 0.0f ? AHEAD : BEHIND;
        // Fade out over the flash, so it draws the eye and then gets out of it.
        const float alpha = core::saturate(split_flash_ / 1.6f);
        const ImU32 faded = (colour & 0x00FFFFFF) | (ImU32(alpha * 240.0f) << 24);
        centred(line, 26.0f, 124.0f, faded);
    }

    if (miss_flash_ > 0.0f) {
        std::snprintf(line, sizeof(line), "missed by %.0f m", rally_.last_miss_distance());
        const float alpha = core::saturate(miss_flash_ / 1.2f);
        const ImU32 faded = (BEHIND & 0x00FFFFFF) | (ImU32(alpha * 240.0f) << 24);
        centred(line, 20.0f, 156.0f, faded);
    }

    // ---- airspeed, bottom centre ----
    std::snprintf(line, sizeof(line), "%.0f m/s", flight_.state().airspeed);
    centred(line, 24.0f, height - 46.0f, WHITE);
}

void App::build_rally_ui() {
    ImGui::SetNextWindowPos(ImVec2(392.0f, float(device_.height()) - 236.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    // Collapsed by default: Combat is the panel a fight actually needs;
    // the rest stay one click away.
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    ImGui::Begin("Rally");

    for (size_t i = 0; i < courses_.size(); ++i) {
        const bool selected = int(i) == current_course_;
        const float best = best_times_.best(courses_[i].name);
        char label[96];
        std::snprintf(label, sizeof(label), "%s  (%.1f km)  %s", courses_[i].name.c_str(),
                      courses_[i].path_length() / 1000.0f,
                      best > 0.0f ? game::format_time(best).c_str() : "--");
        if (ImGui::RadioButton(label, selected) && !selected) {
            select_course(int(i));
            respawn_dragon();
        }
    }

    ImGui::Separator();
    ImGui::Text("%s", rally_.phase() == game::RunPhase::Ready
                          ? "ready"
                          : (rally_.phase() == game::RunPhase::Running ? "running" : "finished"));
    ImGui::Text("%s   checkpoint %d / %zu", game::format_time(rally_.elapsed()).c_str(),
                rally_.rings_passed(), rally_.course().rings.size());
    if (ImGui::Button("restart run (R)")) respawn_dragon();

    ImGui::Checkbox("autopilot", &autopilot_);
    ImGui::SameLine();
    ImGui::TextDisabled("(flies the course itself)");

    ImGui::Checkbox("HUD", &show_hud_);
    ImGui::SameLine();
    ImGui::Checkbox("ghost", &show_ghost_);
    ImGui::SameLine();
    ImGui::Checkbox("route", &show_ring_path_);

    if (rally_.has_ghost()) {
        ImGui::Text("ghost: %s over %zu samples",
                    game::format_time(rally_.best_ghost().duration).c_str(),
                    rally_.best_ghost().samples.size());
    } else {
        ImGui::TextDisabled("no ghost yet -- finish a run");
    }

    if (ImGui::CollapsingHeader("Splits")) {
        const std::vector<float>& splits = rally_.splits();
        for (size_t i = 0; i < splits.size(); ++i) {
            float delta = 0.0f;
            if (rally_.has_ghost() && i < rally_.best_ghost().ring_times.size()) {
                delta = splits[i] - rally_.best_ghost().ring_times[i];
            }
            if (delta != 0.0f) {
                ImGui::TextColored(delta < 0.0f ? ImVec4(0.45f, 0.9f, 0.5f, 1.0f)
                                                : ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
                                   "%2zu  %s  %+.2f", i + 1, game::format_time(splits[i]).c_str(),
                                   delta);
            } else {
                ImGui::Text("%2zu  %s", i + 1, game::format_time(splits[i]).c_str());
            }
        }
    }

    if (ImGui::CollapsingHeader("Course tools")) {
        // Enough authoring to capture a line you found by flying it, which is
        // how a good course actually gets designed.
        if (ImGui::Button("append ring here")) {
            game::Course edited = rally_.course();
            game::Ring ring;
            ring.position = flight_.state().position;
            ring.orientation = flight_.state().orientation;
            ring.radius = 32.0f;
            edited.rings.push_back(ring);
            courses_[size_t(current_course_)] = edited;
            rally_.set_course(edited);
        }
        ImGui::SameLine();
        if (ImGui::Button("drop last")) {
            game::Course edited = rally_.course();
            if (!edited.rings.empty()) edited.rings.pop_back();
            courses_[size_t(current_course_)] = edited;
            rally_.set_course(edited);
        }
        if (ImGui::Button("save as assets/course.txt")) {
            game::save_course(rally_.course(), ASSET_ROOT "/course.txt");
        }
        ImGui::TextDisabled("saved courses load on next start");
    }

    ImGui::End();
}

// Animation and material tuning for the dragon itself. Kept out of the Flight
// window because these are looked at while parked and staring at the model,
// not while flying it.
// The studio panel: pick a manoeuvre, read what to look for, drag the rig
// sliders in the Dragon panel while it loops.
void App::build_studio_ui() {
    ImGui::SetNextWindowPos(ImVec2(float(device_.width()) * 0.5f - 190.0f, 12.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
    // Collapsed by default: Combat is the panel a fight actually needs;
    // the rest stay one click away.
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    ImGui::Begin("Studio");

    if (ImGui::Checkbox("animation studio", &studio_active_) && studio_active_) {
        studio_centre_ = flight_.state().position + core::Vec3{0.0f, 45.0f, 0.0f};
        studio_time_ = 0.0f;
    }

    // Which creature is on the stand. At the top because comparing species is
    // what the studio is FOR: the scenario stays put and the model changes
    // under it, which is the only way to tell a rig fault from a species'
    // character. M cycles it without leaving the keyboard.
    if (models_.size() > 1) {
        ImGui::TextDisabled("model  (M cycles)");
        for (size_t i = 0; i < models_.size(); ++i) {
            if (i % 2) ImGui::SameLine();
            // Just the stem: the full asset path does not fit and every entry
            // shares the prefix anyway.
            const std::string& path = models_[i]->path;
            const size_t slash = path.find_last_of('/');
            const std::string stem = slash == std::string::npos ? path : path.substr(slash + 1);
            if (ImGui::RadioButton(stem.c_str(), player_model_ == int(i))) {
                set_player_model(int(i));
            }
        }
        ImGui::Separator();
    }
    if (studio_active_) {
        const int count = int(game::StudioScenario::Count);
        if (ImGui::BeginCombo("scenario",
                              game::studio_scenario_name(game::StudioScenario(studio_scenario_)))) {
            for (int i = 0; i < count; ++i) {
                if (ImGui::Selectable(game::studio_scenario_name(game::StudioScenario(i)),
                                      i == studio_scenario_)) {
                    studio_scenario_ = i;
                    studio_time_ = 0.0f;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("look for: %s",
                           game::studio_scenario_notes(game::StudioScenario(studio_scenario_)));
        ImGui::SliderFloat("time scale", &studio_time_scale_, 0.05f, 2.0f);
        if (ImGui::Button("restart")) studio_time_ = 0.0f;
        ImGui::SameLine();
        ImGui::TextDisabled("t = %.1f s   tab for free camera", studio_time_);

        // The dials that decide the pose being judged, right under the
        // scenario that judges it. They are the same fields as in the Dragon
        // panel's Wings header; that header is collapsed inside a collapsed
        // panel, and a dial that cannot be found does not exist.
        anim::RigTuning& rig = dragon_rig_.tuning;
        const game::StudioScenario scenario = game::StudioScenario(studio_scenario_);
        if (scenario == game::StudioScenario::Grounded) {
            ImGui::SeparatorText("standing wing");
            ImGui::SliderFloat("keeps tuck", &rig.ground_stow_tuck_share, 0.0f, 1.0f);
            ImGui::SliderFloat("sweep aft", &rig.ground_stow_sweep_deg, -90.0f, 160.0f, "%.0f deg");
            ImGui::SliderFloat("fold", &rig.ground_stow_fold_deg, -30.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("wrist up", &rig.ground_stow_wrist_deg, -170.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("fingers up", &rig.ground_stow_finger_deg, -180.0f, 180.0f,
                               "%.0f deg");
            ImGui::SliderFloat("wrist close", &rig.ground_stow_close_deg, 0.0f, 180.0f, "%.0f deg");
            ImGui::SliderFloat("elbow share", &rig.ground_stow_elbow_scale, 0.0f, 1.5f);
            ImGui::SliderFloat("fan close", &rig.ground_stow_converge_deg, 0.0f, 120.0f,
                               "%.0f deg");
            // The aimed fold: where each wing segment points, in the body's
            // frame. This is what closes the wing on a sculpt whose membrane
            // plane defeats the angle stow above (docs/ANIMATION.md, "A
            // folded wing is where its bones point").
            ImGui::SeparatorText("standing wing, aimed");
            ImGui::SliderFloat("aim the fold", &rig.ground_wing_aim, 0.0f, 1.0f);
            ImGui::SliderFloat("upper arm sweep aft", &rig.ground_wing_arm_sweep_deg, -90.0f,
                               180.0f, "%.0f deg");
            ImGui::SliderFloat("upper arm elevation", &rig.ground_wing_arm_elev_deg, -60.0f,
                               90.0f, "%.0f deg");
            ImGui::SliderFloat("forearm sweep aft", &rig.ground_wing_forearm_sweep_deg,
                               -180.0f, 180.0f, "%.0f deg");
            ImGui::SliderFloat("forearm elevation", &rig.ground_wing_forearm_elev_deg, -60.0f,
                               90.0f, "%.0f deg");
            ImGui::SliderFloat("hand sweep aft", &rig.ground_wing_hand_sweep_deg, -90.0f,
                               180.0f, "%.0f deg");
            ImGui::SliderFloat("hand elevation", &rig.ground_wing_hand_elev_deg, -90.0f, 60.0f,
                               "%.0f deg");
            ImGui::SliderFloat("fan pleat", &rig.ground_wing_fan_deg, 0.0f, 30.0f, "%.0f deg");
            // The stance: for a sculpt whose bind pose is not a standing one.
            // Positive swings a segment's far end forward; the readout below
            // says which foot is off the ground while the angles are dragged.
            ImGui::SeparatorText("standing body and legs");
            ImGui::SliderFloat("stance", &rig.ground_stance, 0.0f, 1.0f);
            ImGui::SliderFloat("body pitch (nose up)", &rig.ground_body_pitch_deg, -60.0f, 60.0f,
                               "%.0f deg");
            ImGui::SliderFloat("hip forward", &rig.ground_hip_deg, -90.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("knee forward", &rig.ground_knee_deg, -90.0f, 120.0f, "%.0f deg");
            ImGui::SliderFloat("ankle forward", &rig.ground_ankle_deg, -90.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("shoulder forward", &rig.ground_shoulder_deg, -90.0f, 90.0f,
                               "%.0f deg");
            ImGui::SliderFloat("elbow forward", &rig.ground_elbow_deg, -120.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("wrist forward", &rig.ground_wrist_deg, -90.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("leg splay", &rig.ground_leg_splay_deg, -45.0f, 45.0f, "%.0f deg");
            ImGui::SliderFloat("arm splay", &rig.ground_arm_splay_deg, -45.0f, 45.0f, "%.0f deg");
            ImGui::SliderFloat("feet on the floor", &rig.ground_feet_level, 0.0f, 1.0f);
            ImGui::SliderFloat("wrists are feet (wyvern)", &rig.ground_wing_plant, 0.0f, 1.0f);
            ImGui::SliderFloat("neck up", &rig.ground_neck_pitch_deg, -60.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("tail up", &rig.ground_tail_pitch_deg, -90.0f, 60.0f, "%.0f deg");
            ImGui::SliderFloat("foot hang", &rig.foot_hang_deg, -45.0f, 60.0f, "%.0f deg");
            {
                std::string heights;
                for (const auto& [name, height] : dragon_rig_.foot_heights()) {
                    char line[96];
                    std::snprintf(line, sizeof line, "%s %+.2f m  ", name.c_str(),
                                  double(height));
                    heights += line;
                }
                if (!heights.empty()) {
                    ImGui::TextDisabled("above the lowest foot: %s", heights.c_str());
                }
            }
        } else if (scenario == game::StudioScenario::Dive ||
                   scenario == game::StudioScenario::PullOut) {
            ImGui::SeparatorText("tucked wing");
            ImGui::SliderFloat("tuck sweep", &rig.tuck_sweep_deg, 0.0f, 120.0f, "%.0f deg");
            ImGui::SliderFloat("tuck fold", &rig.tuck_fold_deg, 0.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("tuck droop", &rig.tuck_droop_deg, 0.0f, 60.0f, "%.0f deg");
            ImGui::SliderFloat("elbow fold scale", &rig.wing_elbow_fold_scale, 0.0f, 2.0f);
            ImGui::SliderFloat("finger fold scale", &rig.wing_finger_fold_scale, 0.0f, 2.0f);
            ImGui::SeparatorText("legs");
            ImGui::SliderFloat("leg tuck", &rig.leg_tuck_deg, 0.0f, 110.0f, "%.0f deg");
            ImGui::SliderFloat("leg trail", &rig.leg_trail_deg, -30.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("front leg trail", &rig.front_leg_trail_deg, -30.0f, 90.0f,
                               "%.0f deg");
        } else if (scenario == game::StudioScenario::Flap) {
            ImGui::SeparatorText("wingbeat");
            ImGui::SliderFloat("recovery wrist", &rig.recovery_wrist_deg, -90.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("recovery fingers", &rig.recovery_finger_deg, -90.0f, 90.0f,
                               "%.0f deg");
            ImGui::SliderFloat("recovery hand droop", &rig.recovery_droop_deg, -45.0f, 60.0f,
                               "%.0f deg");
            ImGui::SliderFloat("stroke plane tilt", &rig.stroke_plane_tilt_deg, -45.0f, 45.0f,
                               "%.0f deg");
            ImGui::SeparatorText("legs");
            ImGui::SliderFloat("leg tuck", &rig.leg_tuck_deg, 0.0f, 110.0f, "%.0f deg");
            ImGui::SliderFloat("leg trail", &rig.leg_trail_deg, -30.0f, 90.0f, "%.0f deg");
            ImGui::SliderFloat("front leg trail", &rig.front_leg_trail_deg, -30.0f, 90.0f,
                               "%.0f deg");
        }
        if (ImGui::Button("save rig profile for this model")) {
            anim::save_rig_tuning(rig, player_model().rig_tuning_path.c_str());
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", player_model().rig_tuning_path.c_str());
    } else {
        ImGui::TextDisabled("scripted manoeuvres for inspecting the rig");
    }
    ImGui::End();
}

void App::build_dragon_ui() {
    anim::RigTuning& rig = dragon_rig_.tuning;

    // Right of the Engine window and above Rally. Placement matters: the first
    // version of this panel opened underneath Engine and was invisible, which
    // is indistinguishable from not having built it at all.
    ImGui::SetNextWindowPos(ImVec2(408.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(384, 0), ImGuiCond_FirstUseEver);
    // Collapsed by default: Combat is the panel a fight actually needs;
    // the rest stay one click away.
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    ImGui::Begin("Dragon");

    ImGui::TextDisabled("%s, %d joints, %zu submesh(es)", player_model().path.c_str(),
                        player_model().skeleton.count(), player_model().mesh.submeshes().size());

    // Which creature the player wears. At the top because swapping species is
    // the first thing anyone does with a roster, and because it re-initialises
    // both rigs -- the sliders below belong to whichever one is selected.
    if (models_.size() > 1) {
        for (size_t i = 0; i < models_.size(); ++i) {
            if (i) ImGui::SameLine();
            if (ImGui::RadioButton(models_[i]->path.c_str(), player_model_ == int(i))) {
                set_player_model(int(i));
            }
        }
    }

    if (ImGui::CollapsingHeader("Material maps", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Toggling one at a time is the only honest way to see what it does.
        ImGui::Checkbox("base colour", &material_toggles_.base_colour);
        ImGui::Checkbox("normal map", &material_toggles_.normal_map);
        ImGui::Checkbox("roughness / metallic / occlusion", &material_toggles_.orm_map);
        if (!material_toggles_.base_colour || !material_toggles_.normal_map ||
            !material_toggles_.orm_map) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "a map is switched off");
            ImGui::SameLine();
            if (ImGui::SmallButton("all on")) material_toggles_ = gfx::MaterialToggles{};
        }
        ImGui::TextDisabled("%zu texture(s) loaded", player_model().textures.size());
        // Hide colour: the same recolour the bots use, for the player.
        ImGui::ColorEdit3("hide hue", &player_hue_.x, ImGuiColorEditFlags_Float |
                                                          ImGuiColorEditFlags_HDR);
        ImGui::SliderFloat("hide recolour", &player_recolour_, 0.0f, 1.0f);
    }

    // The breath belongs to the species, not to combat: it is the clearest
    // place a creature reads as elemental. Open by default because it is the
    // dial that decides whether a new species feels like a new species.
    // Named to distinguish it from the Combat panel's "Breath", which holds
    // the master dials these multiply.
    if (ImGui::CollapsingHeader("Breath (this species)", ImGuiTreeNodeFlags_DefaultOpen)) {
        game::BreathProfile& b = player_model().breath;
        // HDR because the particles are additive and the hot core sits above 1.
        // Keep these mid-value: the tonemap whitens anything bright, so a pale
        // frost breath clips to a white smear (EFFECTS.md).
        ImGui::ColorEdit3("hot core", &b.hot.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::ColorEdit3("cool tip", &b.cool.x,
                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        ImGui::TextDisabled("multipliers on the Combat panel's master dials");
        ImGui::SliderFloat("range x", &b.scales.range, 0.25f, 3.0f);
        ImGui::SliderFloat("cone x", &b.scales.angle, 0.25f, 3.0f);
        ImGui::SliderFloat("damage x", &b.scales.damage, 0.25f, 3.0f);
        ImGui::SliderFloat("drain x", &b.scales.drain, 0.25f, 3.0f);
        // Buoyancy is the single strongest character dial: positive billows
        // like flame, negative pours downhill like frost or a heavy gas.
        ImGui::SliderFloat("buoyancy", &b.buoyancy, -25.0f, 25.0f, "%.1f m/s2");
        ImGui::SliderFloat("spread", &b.spread, 0.0f, 0.6f);
        ImGui::SliderFloat("size start", &b.size_start, 0.2f, 12.0f);
        ImGui::SliderFloat("size end", &b.size_end, 0.2f, 24.0f);
        ImGui::SliderFloat("puff life", &b.life, 0.2f, 3.0f, "%.2f s");
        ImGui::SliderFloat("rate", &b.rate, 40.0f, 800.0f, "%.0f /s");
        ImGui::SliderFloat("drag", &b.drag, 0.2f, 5.0f);
        ImGui::SliderFloat("brightness", &b.brightness, 0.05f, 3.0f);
        if (ImGui::Button("save breath for this model")) {
            game::save_breath_profile(b, player_model().breath_path.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("reset breath")) b = game::BreathProfile{};
        // Combat reads the player's scales from here, so a slider drag takes
        // effect on the next frame's cone rather than on the next spawn.
        combat_.player_breath = b.scales;
    }

    if (ImGui::CollapsingHeader("Wings")) {
        // The beat first: these are what separate a wingbeat from a wave, and
        // a dial that decides whether the flap reads belongs where it is seen.
        ImGui::SeparatorText("wingbeat");
        ImGui::SliderFloat("stroke plane tilt", &rig.stroke_plane_tilt_deg, -45.0f, 45.0f,
                           "%.0f deg");
        ImGui::SliderFloat("recovery elbow", &rig.recovery_elbow_deg, -60.0f, 60.0f, "%.0f deg");
        ImGui::SliderFloat("recovery wrist", &rig.recovery_wrist_deg, -90.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("recovery fingers", &rig.recovery_finger_deg, -90.0f, 90.0f,
                           "%.0f deg");
        ImGui::SliderFloat("recovery hand droop", &rig.recovery_droop_deg, -45.0f, 60.0f,
                           "%.0f deg");
        ImGui::SliderFloat("reopen phase", &rig.wing_recovery_extend_phase, 0.0f, 0.95f);
        ImGui::SliderFloat("hand twist", &rig.stroke_twist_deg, -45.0f, 45.0f, "%.0f deg");
        ImGui::SliderFloat("outer beat delay", &rig.wing_phase_delay, 0.0f, 0.10f, "%.3f cycles");
        ImGui::SliderFloat("body heave", &rig.beat_heave_m, 0.0f, 1.5f, "%.2f m");
        ImGui::SliderFloat("heave lag", &rig.beat_heave_lag, 0.0f, 0.5f, "%.2f cycles");
        ImGui::SliderFloat("body pitch", &rig.beat_pitch_deg, 0.0f, 8.0f, "%.1f deg");
        ImGui::SeparatorText("stroke");
        ImGui::SliderFloat("flap amplitude", &rig.flap_shoulder_deg, 0.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("outboard decay", &rig.outboard_decay, 0.0f, 2.0f);
        ImGui::SliderFloat("phase lag", &rig.wing_phase_lag, 0.0f, 1.5f);
        ImGui::SliderFloat("tuck sweep", &rig.tuck_sweep_deg, 0.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("tuck fold", &rig.tuck_fold_deg, 0.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("tuck droop", &rig.tuck_droop_deg, 0.0f, 45.0f, "%.0f deg");
        ImGui::SliderFloat("upstroke fold", &rig.upstroke_fold_deg, 0.0f, 45.0f, "%.0f deg");
        ImGui::SliderFloat("brake flare", &rig.brake_flare_deg, 0.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("elbow fold scale", &rig.wing_elbow_fold_scale, 0.0f, 2.0f);
        ImGui::SliderFloat("wrist fold scale", &rig.wing_wrist_fold_scale, 0.0f, 2.0f);
        ImGui::SliderFloat("finger fold scale", &rig.wing_finger_fold_scale, 0.0f, 2.0f);
        ImGui::SliderFloat("raised flap fold", &rig.wing_flap_fold_deg, 0.0f, 45.0f,
                           "%.0f deg");
        ImGui::SliderFloat("flap angle limit", &rig.wing_flap_limit_deg, 0.0f, 90.0f,
                           "%.0f deg");
        ImGui::SliderFloat("recovery fold", &rig.wing_recovery_fold_deg, 0.0f, 40.0f, "%.0f deg");
        ImGui::SeparatorText("standing stow");
        ImGui::SliderFloat("stow sweep", &rig.ground_stow_sweep_deg, 0.0f, 60.0f, "%.0f deg");
        ImGui::SliderFloat("stow fold", &rig.ground_stow_fold_deg, 0.0f, 60.0f, "%.0f deg");
        ImGui::SliderFloat("stow wrist up", &rig.ground_stow_wrist_deg, -160.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("stow finger down", &rig.ground_stow_finger_deg, -180.0f, 180.0f,
                           "%.0f deg");
        ImGui::SliderFloat("stow zigzag", &rig.ground_stow_close_deg, 0.0f, 180.0f, "%.0f deg");
        ImGui::SliderFloat("stow elbow share", &rig.ground_stow_elbow_scale, 0.0f, 1.5f);
        ImGui::SliderFloat("stow keeps tuck", &rig.ground_stow_tuck_share, 0.0f, 1.0f);
        ImGui::SliderFloat("stow fan close", &rig.ground_stow_converge_deg, 0.0f, 120.0f,
                           "%.0f deg");
    }

    if (ImGui::CollapsingHeader("Neck & tail dynamics", ImGuiTreeNodeFlags_DefaultOpen)) {
        // These are the two that decide the whole feel: stiffness sets how far
        // the chain swings out, damping how long it rings afterwards.
        ImGui::SliderFloat("stiffness", &rig.chain_stiffness, 1.0f, 60.0f, "%.1f");
        ImGui::SliderFloat("damping", &rig.chain_damping, 0.2f, 20.0f, "%.2f");
        ImGui::SliderFloat("inertia", &rig.chain_inertia, 0.0f, 3.0f);
        ImGui::SliderFloat("gravity", &rig.chain_gravity, 0.0f, 2.0f);
        ImGui::SliderFloat("drag", &rig.chain_drag, 0.0f, 0.5f);
        ImGui::SliderFloat("max bend", &rig.chain_max_bend_deg, 0.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("muscle tone", &rig.chain_tone, 0.0f, 6.0f);
        ImGui::SliderFloat("load tone accel", &rig.chain_load_tone_accel, 1.0f, 40.0f, "%.0f m/s^2");
        ImGui::SliderFloat("neck stiffness x", &rig.neck_stiffness_scale, 0.5f, 8.0f);
        ImGui::SliderFloat("neck gravity x", &rig.neck_gravity_scale, 0.0f, 1.5f);
        ImGui::SliderFloat("neck brace", &rig.neck_inertia_scale, 0.0f, 1.0f);
        ImGui::SliderFloat("neck damping x", &rig.neck_damping_scale, 0.5f, 5.0f);
        ImGui::SliderFloat("tail damping x", &rig.tail_damping_scale, 0.5f, 5.0f);
        ImGui::SliderFloat("tail tip stiffness", &rig.tail_tip_stiffness, 0.05f, 1.0f);
        ImGui::SliderFloat("neck range", &rig.neck_range_deg, 5.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("tail range", &rig.tail_range_deg, 20.0f, 170.0f, "%.0f deg");
        ImGui::SliderInt("iterations", &rig.chain_iterations, 1, 12);
    }

    if (ImGui::CollapsingHeader("Flight response", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("active posture, on top of the passive dynamics");
        ImGui::SliderFloat("tail rudder", &rig.tail_rudder_deg, 0.0f, 60.0f, "%.0f deg");
        ImGui::SliderFloat("tail elevator", &rig.tail_elevator_deg, 0.0f, 45.0f, "%.0f deg");
        ImGui::SliderFloat("neck lead", &rig.neck_lead_deg, 0.0f, 30.0f, "%.0f deg");
        ImGui::SliderFloat("neck streamline", &rig.neck_streamline_deg, 0.0f, 25.0f, "%.0f deg");
        ImGui::SliderFloat("wing load flex", &rig.wing_load_flex_deg, 0.0f, 20.0f, "%.0f deg/g");
        ImGui::SliderFloat("wing roll lean", &rig.wing_roll_lean_deg, 0.0f, 25.0f, "%.0f deg");
        ImGui::SliderFloat("idle fade in flight", &rig.clip_flight_fade, 0.0f, 1.0f);
        ImGui::SliderFloat("leg trail", &rig.leg_trail_deg, -60.0f, 60.0f, "%.0f deg");
        ImGui::SliderFloat("front leg trail", &rig.front_leg_trail_deg, -60.0f, 60.0f, "%.0f deg");
        ImGui::SliderFloat("brake leg extend", &rig.leg_brake_extend, 0.0f, 1.0f);
        ImGui::SliderFloat("brake leg forward", &rig.leg_brake_forward_deg, 0.0f, 60.0f,
                           "%.0f deg");
        ImGui::SliderFloat("foot hang", &rig.foot_hang_deg, -60.0f, 60.0f, "%.0f deg");
        ImGui::SliderFloat("toe curl", &rig.toe_curl_deg, -45.0f, 45.0f, "%.0f deg");
    }

    if (ImGui::CollapsingHeader("Legs & authored motion")) {
        ImGui::SliderFloat("leg tuck", &rig.leg_tuck_deg, 0.0f, 120.0f, "%.0f deg");
        if (dragon_rig_.has_base_clip()) {
            ImGui::TextDisabled("clip '%s'%s", player_model().animations[size_t(player_model().idle_clip)].name.c_str(),
                                player_model().idle_clip_hold >= 0.0f ? " (held at its last frame)" : "");
            // Weight 0 is the honest A/B: it restores exactly the un-layered rig.
            ImGui::SliderFloat("clip weight", &rig.base_clip_weight, 0.0f, 1.0f);
            ImGui::SliderFloat("clip rate", &rig.base_clip_rate, 0.0f, 3.0f);
            // How much ground idle survives in the air at all.
            ImGui::SliderFloat("airborne weight", &rig.clip_air_weight, 0.0f, 1.0f);
        } else {
            ImGui::TextDisabled("no authored clip in this model");
        }
    }

    if (ImGui::CollapsingHeader("Attack posture")) {
        ImGui::SliderFloat("jaw rest offset", &rig.jaw_rest_deg, -40.0f, 20.0f, "%.0f deg");
        ImGui::SliderFloat("jaw open", &rig.jaw_open_deg, 0.0f, 50.0f, "%.0f deg");
        ImGui::SliderFloat("spit recoil", &rig.spit_recoil_deg, 0.0f, 25.0f, "%.0f deg");
        ImGui::SliderFloat("spit duration", &rig.spit_duration, 0.15f, 1.0f, "%.2f s");
        ImGui::SliderFloat("breath thrust", &rig.breath_neck_thrust_deg, 0.0f, 20.0f, "%.0f deg");
        ImGui::SliderFloat("breath neck tone", &rig.breath_neck_tone, 0.0f, 4.0f);
        ImGui::SliderFloat("breath tremor", &rig.breath_tremor_deg, 0.0f, 3.0f, "%.1f deg");
        ImGui::SliderFloat("talon spread", &rig.attack_toe_spread_deg, 0.0f, 30.0f, "%.0f deg");
        ImGui::SliderFloat("neck aim share", &rig.neck_aim_share, 0.0f, 1.0f);
        ImGui::SliderFloat("neck aim max", &rig.neck_aim_max_deg, 0.0f, 60.0f, "%.0f deg");
        ImGui::TextDisabled("jaw: %s   open %.2f", player_model().joints.jaw == anim::NO_PARENT ? "none" : "found",
                            dragon_rig_.jaw_open());
    }

    if (ImGui::CollapsingHeader("Speed and load posture")) {
        ImGui::SliderFloat("speed sweep", &rig.speed_sweep_deg, 0.0f, 60.0f, "%.0f deg");
        ImGui::SliderFloat("speed fold", &rig.speed_fold_deg, 0.0f, 40.0f, "%.0f deg");
        ImGui::SliderFloat("sweep from", &rig.sweep_speed_start, 20.0f, 80.0f, "%.0f m/s");
        ImGui::SliderFloat("sweep full at", &rig.sweep_speed_full, 50.0f, 140.0f, "%.0f m/s");
        ImGui::SliderFloat("tip flutter", &rig.flutter_deg, 0.0f, 6.0f, "%.1f deg");
        ImGui::SliderFloat("flutter from", &rig.flutter_speed_start, 30.0f, 100.0f, "%.0f m/s");
        ImGui::SliderFloat("brake buffet", &rig.brake_buffet_deg, 0.0f, 8.0f, "%.1f deg");
        ImGui::SliderFloat("load twist", &rig.load_twist_deg, 0.0f, 12.0f, "%.0f deg/g");
        ImGui::SliderFloat("load forward sweep", &rig.load_forward_sweep_deg, 0.0f, 15.0f,
                           "%.0f deg/g");
    }

    if (ImGui::CollapsingHeader("Rig profile")) {
        if (ImGui::Button("save for this model")) {
            anim::save_rig_tuning(rig, model_rig_tuning_path_.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("reload profile")) {
            anim::load_rig_tuning(rig, model_rig_tuning_path_.c_str());
        }
        ImGui::TextDisabled("%s", model_rig_tuning_path_.c_str());
    }

    ImGui::Checkbox("show skeleton", &show_skeleton_);

    // A replay must animate exactly like the live dragon, or the ghost stops
    // being a fair comparison.
    ghost_rig_.tuning = rig;
    ImGui::End();
}

// ---------------------------------------------------------------- bots (M14)

// Puts a bot on the edge of the fight: a random bearing from the player, well
// out, above the terrain, pointed inward.
void App::place_bot(BotShip& bot, uint32_t seed) {
    uint32_t rng = seed ? seed : 1u;
    auto unit = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return float(rng & 0xffffffu) / float(0xffffff) * 2.0f - 1.0f;
    };
    const game::FlightState& player = flight_.state();
    const float bearing = unit() * core::PI;
    core::Vec3 spawn = player.position +
                       core::Vec3{std::sin(bearing), 0.0f, std::cos(bearing)} *
                           (bot_spawn_range_ + bot_spawn_jitter_ * unit());
    const float ground = terrain_.height_at(spawn.x, spawn.z);
    spawn.y = core::maxf(player.position.y + 60.0f * unit(), ground + 150.0f);
    bot.flight.reset(spawn, core::look_rotation(player.position - spawn, core::Vec3::up()),
                     45.0f);
    bot.pilot.reset(seed * 2654435761u + 1u);
    bot.was_alive = true;
}

// Difficulty is honest imperfection, so skill is exactly three dials: how
// stale the bot's picture of you is, how scattered its solution, how often it
// shoots. An ace is not stronger -- it is current, precise and busy.
void App::apply_bot_skill(int level) {
    bot_skill_ = level;
    game::BotTuning t;
    switch (level) {
        case 0:  // rookie: fragile, never heals, loses wars of attrition.
            t.reaction_interval = 0.55f;
            t.aim_spread_deg = 5.0f;
            t.fire_cooldown = 2.3f;
            t.lead_curvature = 0.3f;
            t.damage = 9.0f;
            bot_health_ = 60.0f;
            combat_.tuning.hostile_regen = 0.0f;
            break;
        default:  // veteran
            t.reaction_interval = 0.30f;
            t.aim_spread_deg = 2.5f;
            t.fire_cooldown = 1.6f;
            bot_health_ = 80.0f;
            combat_.tuning.hostile_regen = 4.0f;
            break;
        case 2:  // ace: tough, and refuses to stay wounded.
            t.reaction_interval = 0.15f;
            t.aim_spread_deg = 1.2f;
            t.fire_cooldown = 1.1f;
            t.damage = 14.0f;
            t.fire_range = 650.0f;
            bot_health_ = 100.0f;
            combat_.tuning.hostile_regen = 8.0f;
            break;
    }
    bot_tuning_ = t;
    for (auto& bot : bots_) bot->pilot.tuning = bot_tuning_;
}

// A match starts clean: fresh player, fresh bots, weapons cold until the
// countdown ends.
void App::start_match() {
    respawn_dragon();
    combat_.revive();
    spawn_bots(bot_count_);
    match_.start();
}

// The outermost wing joint per side: the one furthest from the centreline in
// the bind pose, over the shared arm and every finger. Not the last joint of
// the last finger -- on this asset that is a helper bound at the origin.
// The ground idle is whichever clip says so by name. An asset with only a
// landing has a standing pose at the end of it, so that frame is held; an asset
// with a single unnamed clip (the first dragon's 'Scene') gets that clip.
void App::choose_idle_clip(LoadedModel& model) const {
    model.idle_clip = -1;
    model.idle_clip_hold = -1.0f;
    auto lowered = [](std::string text) {
        for (char& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    for (size_t i = 0; i < model.animations.size(); ++i) {
        const std::string name = lowered(model.animations[i].name);
        for (const char* key : {"idle", "stand", "rest", "breath", "hover"}) {
            if (name.find(key) != std::string::npos) model.idle_clip = int(i);
        }
        if (model.idle_clip >= 0) break;
    }
    if (model.idle_clip < 0) {
        for (size_t i = 0; i < model.animations.size(); ++i) {
            if (lowered(model.animations[i].name).find("land") != std::string::npos) {
                model.idle_clip = int(i);
                model.idle_clip_hold =
                    core::maxf(model.animations[i].duration - 1.0f / 30.0f, 0.0f);
                break;
            }
        }
    }
    if (model.idle_clip < 0 && model.animations.size() == 1) model.idle_clip = 0;
    if (model.idle_clip >= 0) {
        LOG_INFO("ground idle: clip '%s'%s",
                 model.animations[size_t(model.idle_clip)].name.c_str(),
                 model.idle_clip_hold >= 0.0f ? " held at its last frame" : "");
    } else if (!model.animations.empty()) {
        LOG_INFO("ground idle: none of %zu clips looks like an idle; bind pose it is",
                 model.animations.size());
    }
}

void App::apply_idle_clip(const LoadedModel& model, anim::DragonRig& rig) const {
    if (model.idle_clip < 0 || size_t(model.idle_clip) >= model.animations.size()) return;
    rig.set_base_clip(&model.animations[size_t(model.idle_clip)], model.idle_clip_hold);
}

void App::find_wingtips(LoadedModel& model) {
    for (int side = 0; side < 2; ++side) {
        model.wingtip_joint[side] = -1;
        float best = 0.0f;
        auto consider = [&](int joint) {
            const float reach = std::fabs(model.skeleton.world_bind(joint).translation_part().x);
            if (reach > best) {
                best = reach;
                model.wingtip_joint[side] = joint;
            }
        };
        for (const int joint : model.joints.wing_root[side]) consider(joint);
        for (const auto& finger : model.joints.wing_fingers[side]) {
            for (const int joint : finger) consider(joint);
        }
    }
}

// Loads one creature: mesh, textures, skeleton, the joint map the procedural
// rig drives, the alignment that puts an arbitrary author's units into ours,
// and the two tuning files that may sit beside the glTF. An empty path, or one
// that fails to load, falls back to the generated rig rather than failing --
// the app must always have a dragon.
bool App::load_model(const std::string& path, LoadedModel& out) {
    anim::SkinnedMeshData mesh_data;
    const std::string model_path =
        path.empty() ? std::string(ASSET_ROOT "/dragon.glb") : path;

    // Prefer an imported model, fall back to the generated one. The rig is
    // driven the same way either way -- it only needs to know which joints
    // form the neck, tail and wings, and map_dragon_joints works that out
    // from an arbitrary skeleton.
    const anim::GltfLoadResult loaded =
        anim::load_skinned_gltf(model_path.c_str(), out.skeleton, mesh_data);
    if (loaded.ok) {
        out.joints = anim::map_dragon_joints(out.skeleton);
        out.imported = out.joints.valid();
        if (!out.imported) {
            LOG_WARN("%s: imported skeleton has no recognisable wings; falling back",
                     model_path.c_str());
        }
    } else {
        LOG_INFO("no imported dragon (%s); using the generated rig", loaded.error.c_str());
    }

    if (!out.imported) {
        anim::build_dragon(out.shape, out.skeleton, out.joints, mesh_data);
        out.path = "generated";
    } else {
        out.path = model_path;
        // Scale so the wingspan matches what the flight model assumes, and
        // recentre, because an asset's origin is wherever its author left it
        // -- this one sits over a hundred units from its own geometry. Both
        // are starting points, refined by eye with the sliders.
        // Match the wingspan the flight model assumes: 40 m^2 of wing over
        // roughly a 19 m span. Scaling by the wingspan rather than the
        // overall length keeps the aerodynamics and the visuals agreeing.
        const core::Vec3 extent = loaded.bounds_max - loaded.bounds_min;
        out.asset.scale = extent.x > 0.1f ? 19.0f / extent.x : 1.0f;

        // This asset faces +Z; the engine's forward is -Z. Determined from
        // the rig rather than by eye: its head bone sits at positive Z and
        // its tail tip at negative Z.
        const int head = out.joints.head;
        const int tail = out.joints.tail.empty() ? anim::NO_PARENT : out.joints.tail.back();
        if (head != anim::NO_PARENT && tail != anim::NO_PARENT) {
            const float head_z = out.skeleton.world_bind(head).translation_part().z;
            const float tail_z = out.skeleton.world_bind(tail).translation_part().z;
            if (head_z > tail_z) {
                out.asset.yaw_deg = 180.0f;
                LOG_INFO("%s: asset faces +Z (head %.2f, tail %.2f); yawing 180",
                         out.path.c_str(), head_z, tail_z);
            }
        }

        const core::Vec3 centre = (loaded.bounds_min + loaded.bounds_max) * 0.5f;
        out.asset.offset =
            core::rotate(core::from_euler(0.0f, core::radians(out.asset.yaw_deg), 0.0f),
                         centre * -out.asset.scale);
        LOG_INFO("%s: alignment scale %.4f, offset (%.2f %.2f %.2f)", out.path.c_str(),
                 out.asset.scale, out.asset.offset.x, out.asset.offset.y, out.asset.offset.z);
    }
    find_wingtips(out);

    // Upload whatever textures came with the model. The loader recorded the
    // colour space of each, which is not something the pixels reveal. The
    // debug name carries the roster slot so two species are told apart in a
    // GPU capture.
    const std::string tag = "model" + std::to_string(models_.size());
    for (size_t i = 0; i < loaded.textures.size(); ++i) {
        const bool srgb = i < loaded.texture_srgb.size() && loaded.texture_srgb[i] != 0;
        const std::string name =
            tag + "_" + std::string(srgb ? "colour_" : "data_") + std::to_string(i);
        out.textures.push_back(gfx::create_texture_from_image(device_.gpu(), loaded.textures[i],
                                                              name.c_str(), srgb));
    }
    out.mesh.upload(device_.gpu(), mesh_data, tag.c_str());

    if (!loaded.animations.empty()) {
        out.animations = loaded.animations;
        choose_idle_clip(out);
    }

    // A model may carry a rig profile and a handling profile beside its glTF.
    // Missing profiles leave the built-in defaults untouched, so a model
    // without either behaves exactly as one did before the roster existed.
    // The generated fallback borrows the default dragon's profiles, which is
    // where they lived when there was only ever one model.
    const std::string cfg_stem = out.imported ? out.path : std::string(ASSET_ROOT "/dragon.glb");
    out.rig_tuning_path = cfg_stem + ".rig.cfg";
    out.flight_tuning_path = cfg_stem + ".flight.cfg";
    out.breath_path = cfg_stem + ".breath.cfg";
    if (anim::load_rig_tuning(out.rig_tuning, out.rig_tuning_path.c_str())) {
        LOG_INFO("loaded model rig from %s", out.rig_tuning_path.c_str());
    }
    game::load_tuning(out.flight_tuning, out.flight_tuning_path.c_str());
    game::load_breath_profile(out.breath, out.breath_path.c_str());
    return true;
}

// Re-points the player at another roster entry. Both rigs hold spring state
// indexed by joint, so they cannot simply be told about a different skeleton:
// they are re-initialised, which also resets the chains to their rest pose.
void App::set_player_model(int index) {
    if (index < 0 || size_t(index) >= models_.size() || index == player_model_) return;
    player_model_ = index;
    LoadedModel& model = player_model();
    dragon_rig_.init(model.skeleton, model.joints);
    ghost_rig_.init(model.skeleton, model.joints);
    dragon_rig_.set_model_scale(model.asset.scale);
    ghost_rig_.set_model_scale(model.asset.scale);
    dragon_rig_.tuning = model.rig_tuning;
    ghost_rig_.tuning = model.rig_tuning;
    apply_idle_clip(model, dragon_rig_);
    apply_idle_clip(model, ghost_rig_);
    model_rig_tuning_path_ = model.rig_tuning_path;
    model_tuning_path_ = model.flight_tuning_path;
    combat_.player_breath = model.breath.scales;
    LOG_INFO("player model: [%d] %s", index, model.path.c_str());
}

void App::spawn_bots(int count) {
    combat_.clear_hostiles();
    bots_.clear();
    for (int i = 0; i < count; ++i) {
        auto bot = std::make_unique<BotShip>();
        bot->slot = combat_.spawn_external(bot_health_, 6.5f);
        // Deal the roster round-robin, skipping the player's own entry when
        // there is anything else to fly. With a one-model roster this is the
        // player's model for everyone, exactly as it was before.
        bot->model = models_.size() > 1
                         ? int((size_t(player_model_) + 1 + size_t(i)) % models_.size())
                         : player_model_;
        LoadedModel& worn = model_at(bot->model);
        bot->rig.init(worn.skeleton, worn.joints);
        bot->rig.set_model_scale(worn.asset.scale);
        apply_idle_clip(worn, bot->rig);
        // Pose and handling both follow the species, not the player.
        bot->rig.tuning = worn.rig_tuning;
        bot->flight.tuning = worn.flight_tuning;
        bot->pilot.tuning = bot_tuning_;
        // Four hides, cycling: rust, bone, moss, violet. Recoloured at the
        // texture's own luminance (a multiplicative tint on this dark hide
        // produced four indistinguishable greys), so "the green one" is a
        // thing a player can say across a fight -- the cheapest variety there
        // is. Values above 1 are deliberate: the hide is dark and the hue has
        // to carry it back into the visible range.
        static const core::Vec3 palette[] = {
            {2.0f, 0.55f, 0.35f},  // rust
            {1.9f, 1.6f, 0.95f},   // bone
            {0.75f, 1.7f, 0.6f},   // moss
            {1.4f, 0.7f, 2.0f},    // violet
        };
        bot->hue = palette[size_t(i) % 4];
        LOG_INFO("bot %d: %s", i, worn.path.c_str());
        place_bot(*bot, uint32_t(20260826 + i * 977));
        bot->last_health = bot_health_;
        bots_.push_back(std::move(bot));
    }
}

void App::update_bots(float dt) {
    for (auto& bot : bots_) {
        if (bot->slot < 0 || size_t(bot->slot) >= combat_.sentinels().size()) continue;
        game::Sentinel& slot = combat_.sentinels()[size_t(bot->slot)];
        // A slot that is not external belongs to a drone: the linkage is stale
        // (combat was reset under this bot) and driving it would puppet a
        // sphere around the sky.
        if (!slot.external) {
            bot->slot = -1;
            continue;
        }

        // Death and respawn ride Combat's timer; the app owns where the body
        // comes back and in what state.
        if (!slot.alive) {
            if (bot->was_alive) {
                // The moment of death, wherever it came from -- shot, flame,
                // scrape or mountain.
                const float d = core::distance(active_camera().position, slot.position);
                audio_.play(audio::Clip::KnockOut, 1.1f / (1.0f + d * d / (260.0f * 260.0f)),
                            0.82f + 0.08f * particle_unit());
            }
            bot->was_alive = false;
            continue;
        }
        if (!bot->was_alive) {
            place_bot(*bot, uint32_t(SDL_GetTicksNS() & 0xffffffu) | 1u);
            bot->last_health = slot.health;
        }

        // Damage since last frame is the pilot's cue to jink -- and to cry out.
        // Bots run slightly deeper and varied, so a flight of them never
        // chorusing in unison and the player's own cry stays distinct.
        if (slot.health < bot->last_health - 0.01f) {
            bot->pilot.notify_hit();
            if (bot->hit_cry_cooldown <= 0.0f) {
                const float d = core::distance(active_camera().position, slot.position);
                audio_.play(audio::Clip::Screech, 0.9f / (1.0f + d * d / (240.0f * 240.0f)),
                            0.8f + 0.1f * particle_unit());
                bot->hit_cry_cooldown = 0.5f;
            }
        }
        bot->hit_cry_cooldown = core::maxf(bot->hit_cry_cooldown - dt, 0.0f);
        bot->last_health = slot.health;

        const game::FlightState& self = bot->flight.state();
        // Terrain as seen along the flight path, not just below: avoidance that
        // only looks down flies into rising slopes at speed. Three samples out
        // to three seconds, because one sample at two seconds saw only the base
        // of a valley wall.
        float ground = terrain_.height_at(self.position.x, self.position.z);
        for (float look = 1.0f; look <= 3.0f; look += 1.0f) {
            const core::Vec3 ahead = self.position + self.velocity * look;
            ground = core::maxf(ground, terrain_.height_at(ahead.x, ahead.z));
        }
        game::BotDecision decision =
            bot->pilot.update(dt, self, flight_.state(), combat_.alive(), ground);
        const float sink_before = bot->flight.state().climb_rate;
        bot->flight.update(decision.flight, &terrain_, dt);

        // Terrain contact scales with violence. A plummet is death; a scrape
        // costs health and the recovery reflex takes the bot back off the deck;
        // a gentle touch is a touch. Instantly deleting a dragon that grazed a
        // slope read as a bug, because it was one.
        if (bot->flight.state().grounded && !bot->grounded_last_frame) {
            if (sink_before < -25.0f) {
                LOG_INFO("bot crashed: state=%s sink=%.0f m/s speed=%.0f m/s",
                         bot->pilot.state_name(), -sink_before,
                         bot->flight.state().airspeed);
                combat_.kill_external(bot->slot);
                bot->was_alive = false;
                continue;
            }
            if (sink_before < -8.0f) {
                LOG_INFO("bot scraped terrain: state=%s sink=%.0f m/s",
                         bot->pilot.state_name(), -sink_before);
                combat_.damage_external(bot->slot, 22.0f);
                bot->pilot.notify_hit();
                // The scrape may have been fatal for a wounded bot.
                if (!combat_.sentinels()[size_t(bot->slot)].alive) {
                    bot->was_alive = false;
                    continue;
                }
            }
        }
        bot->grounded_last_frame = bot->flight.state().grounded;

        // A bot that stays on the ground is stuck -- wedged on a slope the
        // flight model cannot take off from. Give the recovery reflex a fair
        // window, then write it off as a crash so the respawn puts it back in
        // the fight.
        if (bot->flight.state().grounded) {
            bot->grounded_time += dt;
            if (bot->grounded_time > 3.0f) {
                LOG_INFO("bot stuck on terrain for 3 s; respawning");
                combat_.kill_external(bot->slot);
                bot->was_alive = false;
                bot->grounded_time = 0.0f;
                continue;
            }
        } else {
            bot->grounded_time = 0.0f;
        }

        combat_.drive_external(bot->slot, bot->flight.state().position,
                               bot->flight.state().velocity);
        // Bots honour the same weapons-cold phases the player does. This strip
        // must come BEFORE anything consumes the decision: it once sat between
        // the fireball and the flame, gating one and not the other.
        if (!match_.weapons_live()) {
            decision.fire = false;
            decision.breathe = false;
        }

        // Fire leaves the bot's mouth too: last frame's animated head, like
        // the player's, with the fixed offset as the fallback for a headless
        // rig.
        core::Vec3 muzzle = bot->flight.state().position + bot->flight.state().forward() * 7.5f;
        {
            const core::Vec3 head_model = bot->rig.head_position();
            if (core::length_sq(head_model) > 1e-6f) {
                const core::Mat4 to_world =
                    core::Mat4::trs(self.position, self.orientation, core::Vec3::one()) *
                    player_model().asset.matrix();
                muzzle = core::transform_point(to_world, head_model);
            }
        }
        if (decision.fire) {
            combat_.fire_hostile(muzzle, decision.fire_velocity, bot->pilot.tuning.damage);
        }
        bot->breathing = decision.breathe;
        if (decision.breathe) {
            combat_.hostile_breath(muzzle, bot->flight.state().forward(), bot->model,
                                   model_at(bot->model).breath.scales);
        }
        anim::RigAction action;
        action.breath = decision.breathe ? 1.0f : 0.0f;
        action.fire = decision.fire;
        bot->rig.set_action(action);

        // The head tracks the player when close and hunting -- the tell that a
        // flame is coming, and where the flame visually comes from.
        if (bot->pilot.state() == game::BotState::Attack &&
            core::distance(self.position, flight_.state().position) < 350.0f) {
            bot->rig.set_aim_target(flight_.state().position);
        }

        bot->rig.update(bot->flight.state(), dt);
        bot->was_alive = true;
    }
}

float App::particle_unit() {
    particle_rng_ ^= particle_rng_ << 13;
    particle_rng_ ^= particle_rng_ >> 17;
    particle_rng_ ^= particle_rng_ << 5;
    return float(particle_rng_ & 0xffffffu) / float(0xffffff) * 2.0f - 1.0f;
}

// Fire as particles: hot fast puffs launched down the cone, spreading and
// slowing, buoyant at the end of their life the way combustion products are.
// The damage cone is untouched -- this is what the cone LOOKS like.
void App::emit_flame(core::Vec3 origin, core::Vec3 direction, float range, bool hostile,
                     float dt, const game::BreathProfile* profile) {
    const core::Vec3 side =
        core::normalize_or(core::cross(direction, core::Vec3::up()), core::Vec3::right());
    const core::Vec3 lift =
        core::normalize_or(core::cross(side, direction), core::Vec3::up());

    // Spawn rate in particles per second, integrated so frame rate does not
    // change the flame's density.
    static float carry = 0.0f;
    // The breathing species decides both the colour and how the puffs move, so
    // two species in one fight do not share a flame. A cone with no species
    // (a sentinel drone) keeps the cold hostile blue, which is what makes
    // incoming fire readable as incoming at a glance.
    game::BreathProfile fallback;
    if (hostile && !profile) {
        fallback.hot = core::Vec3{1.3f, 1.7f, 2.2f};
        fallback.cool = core::Vec3{0.2f, 0.4f, 1.0f};
    }
    const game::BreathProfile& b = profile ? *profile : fallback;
    const core::Vec3 hot = b.hot;
    const core::Vec3 cool = b.cool;
    // Launch speed solved against drag so a puff's travel equals the damage
    // range: with velocity decaying as e^(-kt), distance = v(1-e^(-kT))/k.
    // The flame's visible length IS its reach, which is how the player judges
    // whether a target is in it.
    carry += dt * b.rate;
    const float speed_for_range = range * b.drag / (1.0f - std::exp(-b.drag * b.life));
    while (carry >= 1.0f) {
        carry -= 1.0f;
        const float speed = speed_for_range * (1.0f + 0.08f * particle_unit());
        gfx::Particle p;
        p.position = origin + direction * (2.0f + particle_unit());
        p.velocity = direction * speed + (side * particle_unit() + lift * particle_unit()) *
                                              (speed * b.spread);
        // Positive billows like flame, negative pours like frost or heavy gas.
        p.acceleration = core::Vec3{0.0f, b.buoyancy, 0.0f};
        p.drag = b.drag;
        p.life = b.life * (1.0f + 0.08f * particle_unit());
        p.size_start = b.size_start;
        p.size_end = b.size_end;
        p.color_start = hot;
        p.color_end = cool;
        p.brightness = b.brightness;
        particles_.spawn(p);
    }
}

// A hit: a radial burst of embers plus a short-lived hot flash.
void App::emit_impact(core::Vec3 position, bool hostile, bool on_terrain) {
    const core::Vec3 hot = hostile ? core::Vec3{1.2f, 1.6f, 2.4f} : core::Vec3{2.4f, 1.4f, 0.5f};
    const core::Vec3 cool = hostile ? core::Vec3{0.15f, 0.3f, 0.9f} : core::Vec3{0.9f, 0.2f, 0.04f};
    const int embers = on_terrain ? 26 : 18;
    for (int i = 0; i < embers; ++i) {
        gfx::Particle p;
        p.position = position;
        core::Vec3 direction{particle_unit(), particle_unit(), particle_unit()};
        if (on_terrain && direction.y < 0.0f) direction.y = -direction.y;  // splash upward
        p.velocity = core::normalize_or(direction, core::Vec3::up()) *
                     (18.0f + 14.0f * std::fabs(particle_unit()));
        p.acceleration = core::Vec3{0.0f, -22.0f, 0.0f};  // embers fall
        p.drag = 1.8f;
        p.life = 0.5f + 0.3f * std::fabs(particle_unit());
        p.size_start = 0.9f;
        p.size_end = 0.25f;
        p.color_start = hot;
        p.color_end = cool;
        p.brightness = 1.2f;
        particles_.spawn(p);
    }
    // The flash: one big soft particle that dies fast.
    gfx::Particle flash;
    flash.position = position;
    flash.life = 0.22f;
    flash.size_start = 3.5f;
    flash.size_end = on_terrain ? 11.0f : 8.0f;
    flash.color_start = hot;
    flash.color_end = cool;
    flash.brightness = 1.6f;
    particles_.spawn(flash);
}

// Combat is deliberately on separate bindings from flight, and on buttons that
// do not already mean something: the flight controls were fought over once
// already and are not worth disturbing.
game::CombatInput App::read_combat_input() const {
    game::CombatInput in;
    if (!combat_enabled_) return in;
    // Weapons are cold during the countdown and on the results screen.
    if (!match_.weapons_live()) return in;

    if (options_.attack) {
        in.breath = true;
        in.fire = true;   // the cooldown decides the actual rate
        in.boost = true;  // likewise: a burn at t=0 and every cooldown after
        return in;
    }

    if (free_camera_ || autopilot_) return in;

    const bool ui_has_mouse = ui_.wants_mouse();
    // Left mouse only doubles as breath while mouse steering is off, which it is
    // by default; with it on the button is already the steering capture.
    in.breath = input_.down(SDL_SCANCODE_F) ||
                (!controls_.mouse_stick && !ui_has_mouse && input_.mouse_down(SDL_BUTTON_LEFT)) ||
                input_.gamepad_button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    in.fire = input_.pressed(SDL_SCANCODE_G) ||
              input_.gamepad_button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    in.boost = input_.pressed(SDL_SCANCODE_LSHIFT) && false;  // shift is tuck-dive
    in.boost = input_.pressed(SDL_SCANCODE_X) ||
               input_.gamepad_button(SDL_GAMEPAD_BUTTON_WEST);
    in.cycle_target = cycle_requested_;
    return in;
}

// Sentinels, projectiles and the flame, drawn with the one unit sphere. Colour
// carries all the meaning here: hostile fire has to be distinguishable from
// your own at a glance and at speed.
void App::draw_combat(SDL_GPURenderPass* pass) {
    if (!combat_enabled_ || !sphere_mesh_.valid()) return;

    // `unlit` drops the sun and ambient terms: fire is a light source, not a
    // surface, and lighting it as one washes the colour out to white.
    auto draw_ball = [&](core::Vec3 position, float radius, core::Vec3 colour, float emissive,
                         bool unlit = false) {
        gfx::ModelUniforms model;
        model.model =
            core::Mat4::trs(position, core::Quat::identity(), core::Vec3{radius, radius, radius});
        model.tint = core::Vec4{colour.x, colour.y, colour.z, emissive};
        model.material.w = unlit ? 1.0f : 0.0f;
        world_.draw_mesh(device_, pass, sphere_mesh_, model);
    };

    for (const game::Sentinel& sentinel : combat_.sentinels()) {
        if (!sentinel.alive) continue;
        // External hostiles are drawn as full dragons elsewhere; the drone ball
        // on top of a dragon read as a growth.
        if (sentinel.external) continue;
        // Flashes white when hit. At 500 m a health bar is unreadable but a
        // flash is not, and knowing a shot landed is what lets you commit.
        const float flash = sentinel.hit_flash;
        const float health = sentinel.max_health > 0.0f ? sentinel.health / sentinel.max_health : 0.0f;
        // A hit flashes HOT ORANGE, because the other bright thing a sentinel
        // does -- firing -- puts a blue-white bolt on top of it, and two white
        // flashes are indistinguishable at range.
        const core::Vec3 base{0.72f, 0.24f, 0.18f};
        const core::Vec3 colour = core::lerp(base, core::Vec3{1.0f, 0.55f, 0.10f}, flash);
        draw_ball(sentinel.position, combat_.tuning.sentinel_radius * (1.0f + 0.18f * flash),
                  colour, 0.18f + flash * 1.6f);
        // A smaller inner sphere shrinks as it takes damage: a health readout
        // that needs no UI and works at any distance or angle.
        draw_ball(sentinel.position, combat_.tuning.sentinel_radius * 0.55f * health,
                  core::Vec3{1.0f, 0.70f, 0.16f}, 1.0f, true);
    }

    for (const game::Projectile& projectile : combat_.projectiles()) {
        if (!projectile.alive) continue;
        const bool mine = projectile.team == game::Team::Player;
        const core::Vec3 colour = mine ? core::Vec3{1.0f, 0.45f, 0.10f}
                                       : core::Vec3{0.45f, 0.80f, 1.0f};
        // Incoming fire is drawn much larger than it is. Its hitbox is 2.5 m,
        // which at 400 m is a couple of pixels -- invisible, and being hit by
        // something invisible is the least readable thing in the game. Player
        // fire needs no such help: you know where you shot.
        // Readability scale, tapered off up close: the exaggeration exists so a
        // 2.5 m round is visible at 400 m, and a shot passing the camera at
        // full exaggeration is a screen-filling balloon.
        const float camera_distance =
            core::distance(active_camera().position, projectile.position);
        const float taper = core::smoothstep(5.0f, 250.0f, camera_distance);
        const float scale = mine ? 1.0f : core::lerpf(0.8f, 2.2f, taper);
        // One bolt stretched along its velocity, not a string of spheres: the
        // earlier sphere tracer read as a volley of shrinking projectiles.
        const core::Quat heading = core::look_rotation(
            core::normalize_or(projectile.velocity, core::Vec3::forward()), core::Vec3::up());
        // One opaque bolt -- a nested "glow" shell just occludes anything inside
        // it in a forward opaque pipeline, leaving a flat pale balloon. Heat is
        // carried by emissive brightness instead.
        const core::Vec3 hot = mine ? core::Vec3{1.0f, 0.62f, 0.22f}
                                    : core::Vec3{0.62f, 0.82f, 1.0f};
        gfx::ModelUniforms model;
        const float radius = projectile.radius * 0.8f * scale;
        model.model = core::Mat4::trs(projectile.position, heading,
                                      core::Vec3{radius, radius, radius * 2.4f});
        model.tint = core::Vec4{hot.x, hot.y, hot.z, 2.2f};
        model.material.w = 1.0f;
        world_.draw_mesh(device_, pass, sphere_mesh_, model);
        // A short cooling streak behind the round -- continuous with the bolt,
        // so it reads as motion rather than as extra projectiles.
        const core::Vec3 back = core::normalize_or(projectile.velocity, core::Vec3::forward());
        for (int i = 1; i <= 2; ++i) {
            const float fade = 1.0f - float(i) * 0.35f;
            gfx::ModelUniforms trail;
            const float trail_radius = radius * fade * 0.8f;
            trail.model = core::Mat4::trs(projectile.position - back * (radius * 3.4f * float(i)),
                                          heading,
                                          core::Vec3{trail_radius, trail_radius,
                                                     trail_radius * 2.4f});
            trail.tint = core::Vec4{colour.x, colour.y, colour.z, 0.9f * fade};
            trail.material.w = 1.0f;
            world_.draw_mesh(device_, pass, sphere_mesh_, trail);
        }
    }

    // The flame: a line of spheres widening down the cone. It is drawn from the
    // same origin and axis the damage test uses, so what looks engulfed is.


    // Flames and explosions are particles now (emitted in update); nothing to
    // draw here but the solid bolts above.

    if (hit_marker_ > 0.0f) {
        draw_ball(hit_marker_position_, 4.0f * (1.0f + (0.35f - hit_marker_) * 6.0f),
                  core::Vec3{1.0f, 0.92f, 0.80f}, hit_marker_ * 4.0f, true);
    }
}

// Combat readouts. Everything here answers a question the player has while
// being shot at: how much have I got left, can I shoot yet, and where is the
// thing hitting me. A number they have to read is a number they will not read.
void App::draw_combat_hud() {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const float width = float(device_.width());
    const float height = float(device_.height());
    const core::Mat4 view_proj = active_camera().view_projection(device_.aspect());

    // ---- taking fire ----
    // A full-screen vignette rather than a number: peripheral, unmissable, and
    // it does not compete with the thing you are trying to aim at.
    if (damage_flash_ > 0.0f) {
        const float strength = core::saturate(damage_flash_);
        const ImU32 edge = IM_COL32(190, 30, 25, int(120.0f * strength));
        const float band = height * 0.22f;
        draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width, band), edge, edge,
                                      IM_COL32(190, 30, 25, 0), IM_COL32(190, 30, 25, 0));
        draw->AddRectFilledMultiColor(ImVec2(0, height - band), ImVec2(width, height),
                                      IM_COL32(190, 30, 25, 0), IM_COL32(190, 30, 25, 0), edge,
                                      edge);
    }

    // ---- health and breath ----
    const float bar_width = 260.0f;
    const float bar_height = 14.0f;
    const float x = width * 0.5f - bar_width * 0.5f;
    const float y = height - 96.0f;

    auto bar = [&](float top, float fraction, ImU32 fill, const char* label) {
        draw->AddRectFilled(ImVec2(x, top), ImVec2(x + bar_width, top + bar_height),
                            IM_COL32(10, 14, 20, 170), 3.0f);
        draw->AddRectFilled(ImVec2(x, top),
                            ImVec2(x + bar_width * core::saturate(fraction), top + bar_height),
                            fill, 3.0f);
        draw->AddRect(ImVec2(x, top), ImVec2(x + bar_width, top + bar_height),
                      IM_COL32(255, 255, 255, 60), 3.0f);
        draw->AddText(ImVec2(x - 62.0f, top - 1.0f), IM_COL32(220, 230, 245, 190), label);
    };

    const float health = combat_.health_fraction();
    // Red below a third: the threshold where disengaging is the right call.
    const ImU32 health_colour = health > 0.33f ? IM_COL32(90, 200, 110, 220)
                                               : IM_COL32(225, 70, 55, 235);
    bar(y, health, health_colour, "HEALTH");
    bar(y + bar_height + 6.0f, combat_.breath(),
        combat_.breathing() ? IM_COL32(255, 150, 40, 230) : IM_COL32(230, 190, 90, 190), "BREATH");

    // ---- ability readiness ----
    // Filling back to full is the cue, so it can be read at a glance without
    // parsing a countdown.
    auto pip = [&](float centre_x, float ready, const char* label, ImU32 colour) {
        const float radius = 15.0f;
        const ImVec2 middle(centre_x, y + 58.0f);
        draw->AddCircleFilled(middle, radius, IM_COL32(10, 14, 20, 170), 20);
        if (ready >= 1.0f) {
            draw->AddCircleFilled(middle, radius - 3.0f, colour, 20);
        } else {
            draw->PathArcTo(middle, radius - 3.0f, -core::HALF_PI,
                            -core::HALF_PI + core::TWO_PI * ready, 20);
            draw->PathLineTo(middle);
            draw->PathFillConvex(IM_COL32(colour >> IM_COL32_R_SHIFT & 0xff,
                                          colour >> IM_COL32_G_SHIFT & 0xff,
                                          colour >> IM_COL32_B_SHIFT & 0xff, 110));
        }
        draw->AddCircle(middle, radius, IM_COL32(255, 255, 255, 70), 20);
        const ImVec2 size = ImGui::CalcTextSize(label);
        draw->AddText(ImVec2(middle.x - size.x * 0.5f, middle.y - size.y * 0.5f),
                      IM_COL32(255, 255, 255, 230), label);
    };
    pip(width * 0.5f - 26.0f, 1.0f - combat_.fire_cooldown(), "G", IM_COL32(255, 140, 40, 230));
    pip(width * 0.5f + 26.0f, 1.0f - combat_.boost_cooldown(), "X", IM_COL32(90, 180, 255, 230));

    // ---- the match, writ large ----
    {
        char line[96];
        const ImU32 GOLD = IM_COL32(255, 205, 90, 245);
        if (match_.phase() == game::MatchPhase::Countdown) {
            std::snprintf(line, sizeof(line), "%d", int(std::ceil(match_.countdown_remaining())));
            const ImVec2 size = ImGui::CalcTextSize(line);
            draw->AddText(nullptr, 64.0f,
                          ImVec2(width * 0.5f - size.x * 2.0f, height * 0.30f), GOLD, line);
        } else if (match_.phase() == game::MatchPhase::Fighting) {
            if (match_.settings.time_limit > 0.0f) {
                std::snprintf(line, sizeof(line), "YOU %d : %d BOTS   first to %d   %d:%02d",
                              match_.player_kills(), match_.player_deaths(),
                              match_.settings.target_kills, int(match_.time_remaining()) / 60,
                              int(match_.time_remaining()) % 60);
            } else {
                std::snprintf(line, sizeof(line), "YOU %d : %d BOTS   first to %d",
                              match_.player_kills(), match_.player_deaths(),
                              match_.settings.target_kills);
            }
            const ImVec2 size = ImGui::CalcTextSize(line);
            draw->AddText(ImVec2(width * 0.5f - size.x * 0.5f, 14.0f), GOLD, line);
        } else if (match_.phase() == game::MatchPhase::Results) {
            const char* verdict =
                match_.draw() ? "DRAW" : (match_.player_won() ? "VICTORY" : "DEFEAT");
            const ImU32 colour = match_.draw() ? IM_COL32(220, 220, 220, 245)
                                : match_.player_won() ? IM_COL32(140, 235, 140, 245)
                                                      : IM_COL32(255, 90, 70, 245);
            ImVec2 size = ImGui::CalcTextSize(verdict);
            draw->AddText(nullptr, 56.0f, ImVec2(width * 0.5f - size.x * 3.4f, height * 0.30f),
                          colour, verdict);
            std::snprintf(line, sizeof(line), "%d : %d in %.0f s  --  ENTER to rematch",
                          match_.player_kills(), match_.player_deaths(),
                          match_.fight_duration());
            size = ImGui::CalcTextSize(line);
            draw->AddText(ImVec2(width * 0.5f - size.x * 0.5f, height * 0.30f + 64.0f),
                          IM_COL32(230, 235, 245, 220), line);
        }
    }

    // ---- aim marker ----
    // Where a shot leaves and where it goes: a small cross at the mouth's aim
    // direction. The head is hard to read from behind, and fire that leaves
    // somewhere you cannot see feels random.
    {
        const game::FlightState& player = flight_.state();
        const core::Vec3 aim_at =
            combat_.muzzle(player) + combat_.fireball_direction(player) * 260.0f;
        ImVec2 screen;
        if (project_to_screen(view_proj, aim_at, width, height, screen)) {
            const ImU32 colour = IM_COL32(255, 235, 200, combat_.has_lock() ? 245 : 170);
            const float arm = 14.0f;
            const float gap = 4.5f;
            draw->AddLine(ImVec2(screen.x - arm, screen.y), ImVec2(screen.x - gap, screen.y),
                          colour, 2.4f);
            draw->AddLine(ImVec2(screen.x + gap, screen.y), ImVec2(screen.x + arm, screen.y),
                          colour, 2.4f);
            draw->AddLine(ImVec2(screen.x, screen.y - arm), ImVec2(screen.x, screen.y - gap),
                          colour, 2.4f);
            draw->AddLine(ImVec2(screen.x, screen.y + gap), ImVec2(screen.x, screen.y + arm),
                          colour, 2.4f);
            draw->AddCircleFilled(ImVec2(screen.x, screen.y), 2.0f, colour, 10);
            // A ring when locked: the marker doubles as the "shots will bend"
            // cue, so it visibly changes state with the lock.
            if (combat_.has_lock() && !manual_aim_) {
                draw->AddCircle(ImVec2(screen.x, screen.y), arm * 0.75f, colour, 20, 1.5f);
            }
        }
    }

    // ---- target markers ----
    // Off-screen threats get an arrow at the screen edge. A 3D dogfight is
    // illegible without this: an enemy you cannot locate is not a fight, it is
    // damage arriving from nowhere.
    const core::Vec3 eye = active_camera().position;
    for (const game::Sentinel& sentinel : combat_.sentinels()) {
        if (!sentinel.alive) continue;
        const float range = core::distance(flight_.state().position, sentinel.position);

        ImVec2 screen;
        const bool on_screen =
            project_to_screen(view_proj, sentinel.position, width, height, screen) &&
            screen.x > 4.0f && screen.x < width - 4.0f && screen.y > 4.0f && screen.y < height - 4.0f;

        const int index = int(&sentinel - combat_.sentinels().data());
        const bool locked = combat_.locked_index() == index;
        // A bot holding its flame is the most urgent thing on screen.
        bool flaming = false;
        for (const auto& bot : bots_) {
            if (bot->slot == index && bot->breathing) flaming = true;
        }
        // The locked target is unmistakable. Everything else is a faint mark:
        // if every target looks equally important, none of them read.
        const ImU32 colour = flaming ? IM_COL32(255, 60, 20, 255)
                             : locked ? IM_COL32(255, 205, 70, 245)
                                      : IM_COL32(255, 110, 90, 150);
        if (on_screen) {
            // Brackets rather than a box: they read as a target at any size and
            // do not obscure what they surround.
            const float size = core::clampf(2600.0f / core::maxf(range, 1.0f), 10.0f, 60.0f);
            const float arm = size * 0.35f;
            const ImVec2 corners[4] = {ImVec2(screen.x - size, screen.y - size),
                                       ImVec2(screen.x + size, screen.y - size),
                                       ImVec2(screen.x + size, screen.y + size),
                                       ImVec2(screen.x - size, screen.y + size)};
            const ImVec2 steps[4] = {ImVec2(arm, arm), ImVec2(-arm, arm), ImVec2(-arm, -arm),
                                     ImVec2(arm, -arm)};
            for (int i = 0; i < 4; ++i) {
                draw->AddLine(corners[i], ImVec2(corners[i].x + steps[i].x, corners[i].y), colour,
                              1.8f);
                draw->AddLine(corners[i], ImVec2(corners[i].x, corners[i].y + steps[i].y), colour,
                              1.8f);
            }
            char label[32];
            std::snprintf(label, sizeof(label), flaming ? "%.0f m  FLAME" : "%.0f m", range);
            draw->AddText(ImVec2(screen.x + size + 5.0f, screen.y - 7.0f),
                          flaming ? IM_COL32(255, 90, 40, 255)
                          : locked ? IM_COL32(255, 225, 160, 230)
                                   : IM_COL32(255, 200, 190, 150),
                          label);
            if (locked) {
                draw->AddCircle(ImVec2(screen.x, screen.y), size * 1.35f, colour, 24, 1.4f);
                // Where the shot is actually going. Drawing the lead point makes
                // the assist legible instead of magic -- and when the assist is
                // turned down, it shows exactly how much lead is left to the
                // player.
                ImVec2 lead;
                if (project_to_screen(view_proj, combat_.lock_intercept(), width, height, lead)) {
                    draw->AddLine(ImVec2(screen.x, screen.y), lead,
                                  IM_COL32(255, 225, 160, 110), 1.2f);
                    draw->AddCircleFilled(lead, 3.5f, IM_COL32(255, 240, 190, 220), 12);
                }
            }
        } else {
            // Direction to it, projected onto the screen plane and pinned to the
            // edge of a circle around the centre.
            const core::Vec3 to_target = sentinel.position - eye;
            const gfx::Camera& camera = active_camera();
            const float right = core::dot(to_target, camera.right());
            const float up = core::dot(to_target, camera.up());
            const float ahead = core::dot(to_target, camera.forward());
            core::Vec2 direction{right, -up};
            // Behind the camera the projection flips, so it is mirrored back.
            if (ahead < 0.0f) direction = core::Vec2{-direction.x, -direction.y};
            const float span = core::length(direction);
            if (span < 1e-3f) continue;
            direction = core::Vec2{direction.x / span, direction.y / span};

            const float radius = core::minf(width, height) * 0.36f;
            const ImVec2 centre(width * 0.5f, height * 0.5f);
            const ImVec2 tip(centre.x + direction.x * radius, centre.y + direction.y * radius);
            const ImVec2 perpendicular(-direction.y, direction.x);
            const float wing = 8.0f;
            draw->AddTriangleFilled(
                tip,
                ImVec2(tip.x - direction.x * 16.0f + perpendicular.x * wing,
                       tip.y - direction.y * 16.0f + perpendicular.y * wing),
                ImVec2(tip.x - direction.x * 16.0f - perpendicular.x * wing,
                       tip.y - direction.y * 16.0f - perpendicular.y * wing),
                colour);
        }
    }

    // Where the last hit came from, held for a few seconds. A hit you cannot
    // locate is not a fight, it is damage arriving from nowhere -- and this is
    // the one indicator that turns "randomly hit" into "turn left".
    if (damage_marker_ > 0.0f) {
        const gfx::Camera& camera = active_camera();
        const core::Vec3 to_source = damage_direction_ - camera.position;
        const float right = core::dot(to_source, camera.right());
        const float up = core::dot(to_source, camera.up());
        const float ahead = core::dot(to_source, camera.forward());
        core::Vec2 direction{right, -up};
        if (ahead < 0.0f) direction = core::Vec2{-direction.x, -direction.y};
        const float span = core::length(direction);
        if (span > 1e-3f) {
            direction = core::Vec2{direction.x / span, direction.y / span};
            const float fade = core::saturate(damage_marker_ / 3.0f);
            const int alpha = int(210.0f * fade);
            const ImVec2 centre(width * 0.5f, height * 0.5f);
            const float radius = core::minf(width, height) * 0.30f;
            // A thick arc rather than an arrow: it reads at the very edge of
            // attention, which is where a player looking at their target is.
            const float angle = std::atan2(direction.y, direction.x);
            draw->PathArcTo(centre, radius, angle - 0.34f, angle + 0.34f, 20);
            draw->PathStroke(IM_COL32(255, 90, 70, alpha), 0, 7.0f);
        }
    }

    if (!combat_.alive()) {
        const char* text = "DOWNED";
        const ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(width * 0.5f - size.x * 0.5f, height * 0.42f),
                      IM_COL32(255, 80, 70, 240), text);
    }
}

void App::build_combat_ui() {
    game::CombatTuning& t = combat_.tuning;

    ImGui::SetNextWindowPos(ImVec2(408.0f, 396.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(384, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Combat");

    if (ImGui::Checkbox("combat enabled", &combat_enabled_) && combat_enabled_) {
        // Reset rebuilds the sentinel slots, so any existing bots would be left
        // pointing at freshly spawned drones -- driving them around while their
        // own dragons rendered on top. Both halves of that bug shipped once.
        bots_.clear();
        combat_.reset(&terrain_, flight_.state().position, 20260824u);
    }
    if (!combat_enabled_) {
        ImGui::TextDisabled("switch on to spawn sentinels and arm the dragon");
        ImGui::End();
        return;
    }

    // ---- the match ----
    switch (match_.phase()) {
        case game::MatchPhase::Idle:
            if (ImGui::Button("start match")) start_match();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderInt("first to", &match_.settings.target_kills, 1, 15);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderFloat("clock", &match_.settings.time_limit, 0.0f, 600.0f, "%.0f s");
            break;
        case game::MatchPhase::Countdown:
            ImGui::Text("match starting in %.0f...", std::ceil(match_.countdown_remaining()));
            break;
        case game::MatchPhase::Fighting:
            ImGui::Text("MATCH  you %d : %d bots  (first to %d)", match_.player_kills(),
                        match_.player_deaths(), match_.settings.target_kills);
            if (ImGui::SmallButton("abandon")) match_.abandon();
            break;
        case game::MatchPhase::Results:
            ImGui::Text("%s  %d : %d in %.0f s",
                        match_.draw() ? "DRAW" : (match_.player_won() ? "VICTORY" : "DEFEAT"),
                        match_.player_kills(), match_.player_deaths(),
                        match_.fight_duration());
            if (ImGui::Button("rematch (enter)")) start_match();
            ImGui::SameLine();
            if (ImGui::Button("free play")) match_.abandon();
            break;
    }
    ImGui::Separator();

    ImGui::Text("health %5.0f / %.0f   breath %3.0f%%", combat_.health(), t.max_health,
                combat_.breath() * 100.0f);
    ImGui::Text("sentinels %d alive   %d destroyed", combat_.sentinels_alive(), combat_.kills());
    if (combat_.has_lock()) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "LOCK on sentinel %d at %.0f m",
                           combat_.locked_index(),
                           core::distance(flight_.state().position, combat_.lock_position()));
    } else {
        ImGui::TextDisabled("no lock -- put a target inside %.0f deg of the nose",
                            t.lock_cone_deg);
    }
    ImGui::TextDisabled("F or LMB breath, G fireball, X boost, T relock");
    ImGui::TextDisabled("gamepad: LB breath, RB fireball, X boost, R-stick click relock");

    // The two dials that decide whether combat is fun, at the top level rather
    // than buried: aim assist is how easy hitting is, spread is how hard being
    // hit is. Both were previously inside a collapsed header, which is the same
    // as not existing.
    ImGui::Separator();
    if (ImGui::Checkbox("manual aim (no assist)", &manual_aim_)) {
        // The slider value survives the toggle, so switching back restores the
        // exact feel rather than a default.
        if (manual_aim_) {
            saved_aim_assist_ = t.aim_assist;
            t.aim_assist = 0.0f;
        } else {
            t.aim_assist = saved_aim_assist_;
        }
    }
    ImGui::SliderFloat("aim assist", &t.aim_assist, 0.0f, 1.0f);
    ImGui::SliderFloat("enemy aim spread", &t.sentinel_spread, 0.0f, 80.0f, "%.0f m");
    ImGui::TextDisabled("higher assist = easier to hit; higher spread = easier to survive");
    if (ImGui::Button("forgiving")) {
        t.aim_assist = 1.0f;
        t.sentinel_spread = 40.0f;
        t.sentinel_fire_interval = 4.5f;
    }
    ImGui::SameLine();
    if (ImGui::Button("standard")) {
        game::CombatTuning defaults;
        t.aim_assist = defaults.aim_assist;
        t.sentinel_spread = defaults.sentinel_spread;
        t.sentinel_fire_interval = defaults.sentinel_fire_interval;
    }
    ImGui::SameLine();
    if (ImGui::Button("sharp")) {
        t.aim_assist = 0.45f;
        t.sentinel_spread = 7.0f;
        t.sentinel_fire_interval = 2.0f;
    }
    ImGui::Separator();

    if (ImGui::RadioButton("rookie", bot_skill_ == 0)) apply_bot_skill(0);
    ImGui::SameLine();
    if (ImGui::RadioButton("veteran", bot_skill_ == 1)) apply_bot_skill(1);
    ImGui::SameLine();
    if (ImGui::RadioButton("ace", bot_skill_ == 2)) apply_bot_skill(2);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("##botcount", &bot_count_, 1, 4);
    ImGui::SameLine();
    if (ImGui::Button("spawn bots")) spawn_bots(bot_count_);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("bot recolour", &bot_recolour_, 0.0f, 1.0f);
    ImGui::SetNextItemWidth(120.0f);
    // Decides whether a fight opens as a long chase or as a merge, so it is a
    // feel dial rather than a constant. Takes effect on the next spawn.
    ImGui::SliderFloat("bot spawn range", &bot_spawn_range_, 80.0f, 1500.0f, "%.0f m");
    if (models_.size() > 1) {
        ImGui::TextDisabled("roster:");
        for (size_t i = 0; i < bots_.size(); ++i) {
            ImGui::TextDisabled("  bot %zu: %s", i, model_at(bots_[i]->model).path.c_str());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("sentinels")) {
        bots_.clear();
        combat_.clear_hostiles();
        combat_.spawn_wave(5);
    }
    ImGui::SameLine();
    if (ImGui::Button("heal")) combat_.revive();
    for (size_t i = 0; i < bots_.size(); ++i) {
        const auto& bot = bots_[i];
        if (bot->slot < 0 || size_t(bot->slot) >= combat_.sentinels().size()) continue;
        const game::Sentinel& slot = combat_.sentinels()[size_t(bot->slot)];
        ImGui::TextDisabled("bot %zu  %-7s  %5.0f hp  %4.0f m/s  %s", i,
                            slot.alive ? bot->pilot.state_name() : "down", slot.health,
                            bot->flight.state().airspeed,
                            combat_.locked_index() == bot->slot ? "LOCKED" : "");
    }

    if (ImGui::CollapsingHeader("Fireball")) {
        ImGui::SliderFloat("speed", &t.fireball_speed, 60.0f, 500.0f, "%.0f m/s");
        ImGui::SliderFloat("damage", &t.fireball_damage, 1.0f, 100.0f, "%.0f");
        ImGui::SliderFloat("hit radius", &t.fireball_radius, 0.5f, 12.0f, "%.1f m");
        ImGui::SliderFloat("blast radius", &t.fireball_blast_radius, 0.0f, 40.0f, "%.0f m");
        ImGui::SliderFloat("cooldown", &t.fireball_cooldown, 0.1f, 3.0f, "%.2f s");
        ImGui::SliderFloat("gravity", &t.fireball_gravity, 0.0f, 20.0f, "%.1f m/s2");
    }

    if (ImGui::CollapsingHeader("Breath")) {
        ImGui::SliderFloat("range", &t.breath_range, 20.0f, 400.0f, "%.0f m");
        ImGui::SliderFloat("assist cap", &t.breath_assist_max_deg, 0.0f, 45.0f, "%.0f deg");
        ImGui::SliderFloat("half angle", &t.breath_half_angle_deg, 2.0f, 40.0f, "%.0f deg");
        ImGui::SliderFloat("damage/s", &t.breath_damage_per_second, 5.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("drain/s", &t.breath_drain, 0.05f, 1.0f, "%.2f");
        ImGui::SliderFloat("regen/s", &t.breath_regen, 0.02f, 1.0f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Boost & survivability")) {
        ImGui::SliderFloat("boost force", &flight_.tuning.boost_force, 0.0f, 80000.0f, "%.0f N");
        ImGui::SliderFloat("boost duration", &t.boost_duration, 0.1f, 4.0f, "%.2f s");
        ImGui::SliderFloat("boost cooldown", &t.boost_cooldown, 0.5f, 20.0f, "%.1f s");
        ImGui::SliderFloat("max health", &t.max_health, 20.0f, 400.0f, "%.0f");
        ImGui::SliderFloat("health regen/s", &t.health_regen, 0.0f, 40.0f, "%.0f");
        ImGui::SliderFloat("regen delay", &t.regen_delay, 0.0f, 20.0f, "%.1f s");
    }

    if (ImGui::CollapsingHeader("Targeting")) {
        ImGui::SliderFloat("lock cone", &t.lock_cone_deg, 4.0f, 80.0f, "%.0f deg");
        ImGui::SliderFloat("lock hold cone", &t.lock_hold_cone_deg, 10.0f, 170.0f, "%.0f deg");
        ImGui::SliderFloat("lock range", &t.lock_range, 200.0f, 4000.0f, "%.0f m");
        ImGui::SliderFloat("lock distance weight", &t.lock_distance_weight, 0.0f, 0.1f,
                           "%.3f deg/m");
    }

    if (ImGui::CollapsingHeader("Sentinels")) {
        ImGui::SliderFloat("health", &t.sentinel_health, 10.0f, 400.0f, "%.0f");
        ImGui::SliderFloat("radius", &t.sentinel_radius, 2.0f, 25.0f, "%.0f m");
        ImGui::SliderFloat("fire interval", &t.sentinel_fire_interval, 0.3f, 10.0f, "%.1f s");
        ImGui::SliderFloat("shot speed", &t.sentinel_projectile_speed, 40.0f, 400.0f, "%.0f m/s");
        ImGui::SliderFloat("shot damage", &t.sentinel_damage, 1.0f, 60.0f, "%.0f");
        ImGui::SliderFloat("range", &t.sentinel_range, 100.0f, 3000.0f, "%.0f m");
    }

    ImGui::End();
}

void App::build_flight_ui() {
    const game::FlightState& s = flight_.state();
    game::FlightTuning& t = flight_.tuning;

    ImGui::SetNextWindowPos(ImVec2(float(device_.width()) - 402.0f, 12.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390, 0), ImGuiCond_FirstUseEver);
    // Collapsed by default: Combat is the panel a fight actually needs;
    // the rest stay one click away.
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
    ImGui::Begin("Flight");

    // --- the readout you actually fly by ---
    ImGui::Text("%5.1f m/s   %5.0f km/h", s.airspeed, s.airspeed * 3.6f);
    ImGui::Text("alt %6.0f m   clearance %6.0f m", s.position.y, s.ground_clearance);
    ImGui::Text("climb %+6.1f m/s   energy %6.0f m", s.climb_rate, s.specific_energy);
    ImGui::Text("AoA %+5.1f deg   slip %+5.1f deg   %.2f g",
                core::degrees(s.angle_of_attack), core::degrees(s.sideslip), s.g_load);

    if (s.stalling) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.3f, 1.0f), "STALL");
    if (s.grounded) ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "GROUNDED");
    if (!s.stalling && !s.grounded) ImGui::TextDisabled("flying");

    if (ImGui::Button("respawn (R)")) respawn_dragon();

    if (ImGui::CollapsingHeader("Assists", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::RadioButton("relaxed", assist_preset_ == 0)) apply_assist_preset(0);
        ImGui::SameLine();
        if (ImGui::RadioButton("standard", assist_preset_ == 1)) apply_assist_preset(1);
        ImGui::SameLine();
        if (ImGui::RadioButton("expert", assist_preset_ == 2)) apply_assist_preset(2);

        ImGui::SliderFloat("bank limit", &t.bank_limit_deg, 0.0f, 90.0f,
                           t.bank_limit_deg > 0.0f ? "%.0f deg" : "off");
        ImGui::Checkbox("auto flap", &assists_.auto_flap);
        if (assists_.auto_flap) {
            ImGui::SliderFloat("auto flap speed", &assists_.auto_flap_speed, 20.0f, 80.0f,
                               "%.0f m/s");
        }
        if (ImGui::SliderFloat("checkpoint size", &assists_.ring_radius_scale, 0.6f, 2.2f,
                               "x%.2f")) {
            select_course(current_course_);
        }
    }

    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("stick  roll %+.2f   pitch %+.2f", stick_.x, stick_.y);
        ImGui::Text("gamepad: %s", input_.gamepad_name());
        ImGui::Checkbox("invert pitch (W lowers the nose)", &controls_.invert_pitch);
        ImGui::Checkbox("invert free look Y (right stick)", &controls_.invert_free_look_y);
        ImGui::SliderFloat("stick smoothing", &controls_.stick_smoothing, 0.0f, 0.3f, "%.3f s");
        ImGui::SliderFloat("gamepad deadzone", &controls_.gamepad_deadzone, 0.0f, 0.4f);
        ImGui::SliderFloat("gamepad expo", &controls_.gamepad_expo, 1.0f, 3.0f);
        ImGui::Separator();
        ImGui::Checkbox("mouse steering (accumulates)", &controls_.mouse_stick);
        if (controls_.mouse_stick) {
            if (!mouse_captured_) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "click the world to capture");
            }
            ImGui::SliderFloat("mouse sensitivity", &controls_.mouse_sensitivity, 0.0003f, 0.008f,
                               "%.4f");
            ImGui::SliderFloat("mouse recentre", &controls_.mouse_return, 0.05f, 2.0f, "%.2f s");
        }
    }

    if (ImGui::CollapsingHeader("Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Overlaying the plots is how a change gets judged: a tighter turn is
        // only an improvement if it did not cost more energy than it was worth.
        ImGui::PlotLines("airspeed", telemetry_.airspeed, TELEMETRY_SAMPLES, telemetry_.cursor,
                         nullptr, 0.0f, 140.0f, ImVec2(0, 52));
        ImGui::PlotLines("AoA", telemetry_.aoa_deg, TELEMETRY_SAMPLES, telemetry_.cursor, nullptr,
                         -40.0f, 40.0f, ImVec2(0, 52));
        ImGui::PlotLines("g", telemetry_.g_load, TELEMETRY_SAMPLES, telemetry_.cursor, nullptr,
                         -3.0f, 9.0f, ImVec2(0, 52));
        ImGui::PlotLines("climb", telemetry_.climb, TELEMETRY_SAMPLES, telemetry_.cursor, nullptr,
                         -80.0f, 80.0f, ImVec2(0, 52));
        ImGui::PlotLines("energy", telemetry_.energy, TELEMETRY_SAMPLES, telemetry_.cursor,
                         nullptr, 0.0f, 900.0f, ImVec2(0, 52));
    }

    if (ImGui::CollapsingHeader("Presets")) {
        // Presets set the body and wing; heft is the player's and survives them.
        const float kept_heft = t.heft;
        if (ImGui::Button("glider")) t = game::tuning_preset_glider();
        ImGui::SameLine();
        if (ImGui::Button("agile")) t = game::tuning_preset_agile();
        ImGui::SameLine();
        if (ImGui::Button("heavy")) t = game::tuning_preset_heavy();
        t.heft = kept_heft;
        ImGui::SameLine();
        if (ImGui::Button("default")) t = game::FlightTuning();
        if (ImGui::Button("save to assets/flight_tuning.cfg")) {
            game::save_tuning(t, ASSET_ROOT "/flight_tuning.cfg");
        }
        ImGui::SameLine();
        if (ImGui::Button("load")) {
            game::load_tuning(t, ASSET_ROOT "/flight_tuning.cfg");
        }
        // Per-model handling: what this dragon feels like to fly.
        if (ImGui::Button("save for this model")) game::save_tuning(t, model_tuning_path_.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", model_tuning_path_.c_str());
        // Heft: one knob for "this one is heavier", composed on top of the
        // presets and sliders rather than baked into them.
        ImGui::SliderFloat("heft", &t.heft, 0.5f, 2.5f, "%.2fx");
        ImGui::TextDisabled("heft multiplies mass, and slows roll/pitch/yaw and control lag by its root");
        ImGui::SliderFloat("take-off jump", &t.takeoff_jump, 0.0f, 15.0f, "%.1f m/s");
    }

    if (ImGui::CollapsingHeader("Body & wing")) {
        ImGui::SliderFloat("mass", &t.mass, 200.0f, 3000.0f, "%.0f kg");
        ImGui::SliderFloat("wing area", &t.wing_area, 10.0f, 100.0f, "%.0f m2");
        ImGui::SliderFloat("CL max", &t.lift_coefficient_max, 0.5f, 4.0f);
        ImGui::SliderFloat("stall angle", &t.stall_angle_deg, 6.0f, 35.0f, "%.0f deg");
        ImGui::SliderFloat("post-stall lift", &t.post_stall_lift, 0.0f, 1.0f);
        ImGui::SliderFloat("parasitic drag", &t.parasitic_drag, 0.005f, 0.2f, "%.3f");
        ImGui::SliderFloat("induced drag", &t.induced_drag_factor, 0.005f, 0.2f, "%.3f");
    }

    if (ImGui::CollapsingHeader("Propulsion")) {
        ImGui::SliderFloat("flap force", &t.flap_peak_force, 0.0f, 30000.0f, "%.0f N");
        ImGui::SliderFloat("flap period", &t.flap_period, 0.3f, 2.5f, "%.2f s");
        ImGui::SliderFloat("glide thrust", &t.glide_thrust, 0.0f, 6000.0f, "%.0f N");
        ImGui::SliderFloat("tuck lift loss", &t.tuck_lift_loss, 0.0f, 1.0f);
        ImGui::SliderFloat("tuck drag loss", &t.tuck_drag_loss, 0.0f, 1.0f);
        ImGui::SliderFloat("brake drag gain", &t.brake_drag_gain, 0.0f, 8.0f);
    }

    if (ImGui::CollapsingHeader("Control")) {
        ImGui::SliderFloat("pitch rate", &t.pitch_rate, 0.2f, 4.0f, "%.2f rad/s");
        ImGui::SliderFloat("yaw rate", &t.yaw_rate, 0.05f, 2.0f, "%.2f rad/s");
        ImGui::SliderFloat("roll rate", &t.roll_rate, 0.5f, 8.0f, "%.2f rad/s");
        ImGui::SliderFloat("control lag", &t.control_lag, 0.0f, 0.4f, "%.3f s");
        ImGui::SliderFloat("low-speed authority", &t.low_speed_authority, 0.0f, 1.0f);
    }

    if (ImGui::CollapsingHeader("Stability & assists")) {
        ImGui::SliderFloat("yaw stability", &t.yaw_stability, 0.0f, 6.0f);
        ImGui::SliderFloat("pitch stability", &t.pitch_stability, 0.0f, 4.0f);
        ImGui::SliderFloat("pitch damping", &t.pitch_damping, 0.1f, 8.0f);
        ImGui::SliderFloat("yaw damping", &t.yaw_damping, 0.1f, 8.0f);
        ImGui::SliderFloat("roll damping", &t.roll_damping, 0.1f, 8.0f);
        ImGui::Separator();
        ImGui::SliderFloat("auto level", &t.auto_level, 0.0f, 3.0f);
        ImGui::SliderFloat("turn coordination", &t.turn_coordination, 0.0f, 3.0f);
        ImGui::SliderFloat("stall recovery", &t.stall_recovery, 0.0f, 4.0f);
        ImGui::SliderFloat("min airspeed", &t.min_airspeed, 0.0f, 40.0f, "%.0f m/s");
    }

    if (ImGui::CollapsingHeader("Debug draw", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("skeleton", &show_skeleton_);
        ImGui::TextDisabled("model: %s (%d joints)", player_model().path.c_str(),
                            player_model().skeleton.count());
        if (ImGui::TreeNode("Asset alignment")) {
            ImGui::SliderFloat("scale", &player_model().asset.scale, 0.01f, 4.0f, "%.4f");
            ImGui::SliderFloat("yaw", &player_model().asset.yaw_deg, -180.0f, 180.0f, "%.0f deg");
            ImGui::SliderFloat("pitch", &player_model().asset.pitch_deg, -180.0f, 180.0f, "%.0f deg");
            ImGui::SliderFloat("roll", &player_model().asset.roll_deg, -180.0f, 180.0f, "%.0f deg");
            ImGui::SliderFloat("offset y", &player_model().asset.offset.y, -10.0f, 10.0f, "%.2f m");
            ImGui::SliderFloat("offset z", &player_model().asset.offset.z, -10.0f, 10.0f, "%.2f m");
            ImGui::TreePop();
        }
        ImGui::Checkbox("force vectors", &show_forces_);
        ImGui::Checkbox("flight path", &show_flight_path_);
        ImGui::Checkbox("ground probe", &show_ground_probe_);
    }

    ImGui::Separator();
    ImGui::TextDisabled(controls_.invert_pitch ? "W nose down, S nose up, A/D roll"
                                              : "W nose up, S nose down, A/D roll");
    ImGui::TextDisabled("space flap, shift tuck-dive, ctrl brake");
    ImGui::TextDisabled("gamepad: left stick, A flap, triggers dive/brake, d-pad rudder");
    ImGui::TextDisabled("R respawn, V first person, 1/2/3 camera");
    ImGui::TextDisabled("right-drag or right stick to look around");
    ImGui::End();
}

void App::render() {
    const float aspect = device_.aspect();
    const gfx::Camera& camera = active_camera();

    shadow_.update(camera, lighting_.sun_direction());

    gfx::SceneUniforms scene =
        gfx::make_scene_uniforms(camera, aspect, lighting_, material_, time_seconds_);
    scene.light_view_proj = shadow_.light_view_proj();
    const float texel_world = shadow_.extent * 2.0f / float(shadow_.resolution());
    scene.shadow_params = core::Vec4{texel_world, shadow_.depth_bias,
                                     shadow_.enabled ? shadow_.strength : 0.0f,
                                     1.0f / float(shadow_.resolution())};
    world_.set_scene(scene);
    world_.set_material_toggles(material_toggles_);

    // Debug geometry has to be uploaded before any render pass opens, because
    // the upload itself is a copy pass.
    debug_.upload(device_);
    // Copy passes cannot open inside a render pass, so particle staging rides
    // alongside the debug-line upload.
    particles_.upload(device_, camera.view_projection(aspect), camera.right(), camera.up());
    // Grass is re-placed around the camera every frame, deterministically from
    // the ground cell, and streamed like the particles.
    vegetation_.grass_around(terrain_, vegetation_settings_, camera.position, grass_scratch_);
    foliage_.wind = vegetation_settings_.wind;
    foliage_.grass_fade_end = vegetation_settings_.grass_radius;
    foliage_.grass_fade_start = vegetation_settings_.grass_radius * 0.7f;
    for (int k = 0; k < gfx::GRASS_KINDS; ++k) {
        foliage_.upload_grass(device_, gfx::GrassKind(k), grass_scratch_[k]);
    }
    ui_.prepare_draw_data(device_);

    const gfx::ModelUniforms dragon_model = dragon_model_uniforms();

    // Shadow pass first: the main pass samples what it writes.
    if (shadow_.enabled) {
        SDL_GPURenderPass* shadow_pass = shadow_.begin_pass(device_);
        foliage_.draw_trees_depth(device_, shadow_pass, shadow_.light_view_proj(),
                                  world_.scene().view_params.z);
        world_.draw_mesh_depth(device_, shadow_pass, terrain_mesh_, shadow_.light_view_proj(),
                               gfx::ModelUniforms());
        world_.draw_skinned_depth(device_, shadow_pass, player_model().mesh, shadow_.light_view_proj(),
                                  dragon_model, dragon_rig_.skinning_matrices());
        for (const auto& bot : bots_) {
            if (bot->slot < 0 || !combat_.sentinels()[size_t(bot->slot)].alive) continue;
            const game::FlightState& s = bot->flight.state();
            const LoadedModel& worn = model_at(bot->model);
            gfx::ModelUniforms bot_model;
            bot_model.model =
                core::Mat4::trs(s.position, s.orientation, core::Vec3::one()) * worn.asset.matrix();
            world_.draw_skinned_depth(device_, shadow_pass, worn.mesh,
                                      shadow_.light_view_proj(), bot_model,
                                      bot->rig.skinning_matrices());
        }
        // Only the live checkpoint casts a shadow. Shadowing all of them costs
        // little but reads as clutter, and the shadow's job here is to tell you
        // where the next ring is relative to the ground.
        if (const game::Ring* next = rally_.next_ring()) {
            gfx::ModelUniforms model;
            model.model = core::Mat4::trs(next->position, next->orientation,
                                          core::Vec3(next->radius / RING_MESH_RADIUS));
            world_.draw_mesh_depth(device_, shadow_pass, ring_mesh_, shadow_.light_view_proj(),
                                   model);
        }
        device_.end_pass(shadow_pass);
    }

    // The clear colour is never seen: the sky covers every pixel. It is set to
    // the fog colour anyway so a frame where the sky pipeline is broken still
    // looks like a sky rather than a void.
    SDL_GPURenderPass* pass = device_.begin_main_pass(
        lighting_.fog_color[0], lighting_.fog_color[1], lighting_.fog_color[2]);
    world_.draw_sky(device_, pass);
    world_.draw_terrain(device_, pass, terrain_mesh_);
    if (terrain_skirt_mesh_.valid()) world_.draw_terrain(device_, pass, terrain_skirt_mesh_);
    if (water_mesh_.valid()) world_.draw_water(device_, pass, water_mesh_);
    foliage_.draw_trees(device_, pass, world_.scene());
    foliage_.draw_grass(device_, pass, world_.scene());
    world_.draw_skinned(device_, pass, player_model().mesh, dragon_model,
                        dragon_rig_.skinning_matrices(), player_model().textures, model_sampler_);

    // Checkpoints. One mesh, one draw per ring, tinted by state -- few enough
    // rings that instancing would be premature. Hidden in the studio, whose
    // whole point is an uncluttered look at the dragon.
    static const game::Course no_course;
    const game::Course& course = studio_active_ ? no_course : rally_.course();
    for (size_t i = 0; i < course.rings.size(); ++i) {
        const game::Ring& ring = course.rings[i];
        const int index = int(i);
        gfx::ModelUniforms model;
        model.model = core::Mat4::trs(ring.position, ring.orientation,
                                      core::Vec3(ring.radius / RING_MESH_RADIUS));
        if (index < rally_.next_ring_index()) {
            // Passed: still visible so the flown line can be read, but clearly
            // spent.
            model.tint = core::Vec4{0.16f, 0.20f, 0.22f, 0.0f};
        } else if (index == rally_.next_ring_index()) {
            // The live one pulses, which is what makes it findable at distance
            // against cluttered terrain.
            const float pulse = 0.55f + 0.45f * std::sin(time_seconds_ * 4.0f);
            model.tint = core::Vec4{1.0f, 0.72f, 0.22f, 0.5f + pulse * 0.9f};
        } else {
            model.tint = core::Vec4{0.30f, 0.45f, 0.62f, 0.06f};
        }
        world_.draw_mesh(device_, pass, ring_mesh_, model);
    }

    // Bot dragons: the real model, warmed slightly red so a target reads as a
    // target at a glance without a hint of UI.
    for (const auto& bot : bots_) {
        if (bot->slot < 0 || size_t(bot->slot) >= combat_.sentinels().size()) continue;
        const game::Sentinel& slot = combat_.sentinels()[size_t(bot->slot)];
        if (!slot.alive) continue;
        const game::FlightState& s = bot->flight.state();
        const LoadedModel& worn = model_at(bot->model);
        gfx::ModelUniforms bot_model;
        bot_model.model =
            core::Mat4::trs(s.position, s.orientation, core::Vec3::one()) * worn.asset.matrix();
        // The hit flash reddens rather than brightens: high emissive whitens
        // through the tonemap, and a white flash was unreadable as damage.
        // The hit flash reddens rather than brightens: high emissive whitens
        // through the tonemap, and a white flash was unreadable as damage.
        bot_model.tint = core::Vec4{1.0f, core::lerpf(1.0f, 0.3f, slot.hit_flash),
                                    core::lerpf(1.0f, 0.2f, slot.hit_flash),
                                    0.10f + slot.hit_flash * 0.45f};
        bot_model.recolour = core::Vec4{bot->hue.x, bot->hue.y, bot->hue.z, bot_recolour_};
        world_.draw_skinned(device_, pass, worn.mesh, bot_model, bot->rig.skinning_matrices(),
                            worn.textures, model_sampler_);
    }

    draw_combat(pass);

    // Particles last among world draws: additive light over everything opaque,
    // depth-tested against it, never writing. (Staged before the pass began.)
    particles_.draw(device_, pass);

    // Ghost of the best run, flying its own recording alongside.
    game::GhostSample ghost;
    if (show_ghost_ && rally_.ghost_pose(ghost)) {
        // The ghost is drawn untextured on purpose: its blue tint is what
        // distinguishes a replay from the living dragon.
        world_.draw_skinned(device_, pass, player_model().mesh, ghost_model_uniforms(ghost),
                            ghost_rig_.skinning_matrices());
    }

    debug_.draw(device_, pass, camera.view_projection(aspect));
    device_.end_pass(pass);

    SDL_GPURenderPass* ui_pass = device_.begin_ui_pass();
    ui_.render(device_, ui_pass);
    device_.end_pass(ui_pass);
}

// One line of flight state, for verifying a manoeuvre that a single screenshot
// cannot settle. A landing is the motivating case: whether the dragon touched
// down, and how long it spent hovering first, is a sequence, not a pose.
void App::log_telemetry() const {
    const game::FlightState& s = flight_.state();
    LOG_INFO("t=%6.2f y=%7.1f clr=%6.1f spd=%5.1f climb=%6.1f g=%4.1f aoa=%5.1f "
             "flap=%.2f tuck=%.2f brake=%.2f %s%s",
             double(frame_index_) / 60.0, double(s.position.y), double(s.ground_clearance),
             double(s.airspeed), double(s.climb_rate), double(s.g_load),
             double(core::degrees(s.angle_of_attack)), double(s.flap_amplitude),
             double(s.wing_tuck), double(s.wing_brake), s.grounded ? "GROUNDED " : "",
             s.stalling ? "STALL" : "");
}

void App::run() {
    uint64_t previous_ticks = SDL_GetTicksNS();

    while (running_) {
        uint64_t now = SDL_GetTicksNS();
        float dt = float(now - previous_ticks) * 1e-9f;
        previous_ticks = now;
        // A breakpoint or a window drag can produce a huge dt; clamping keeps
        // integration stable.
        dt = core::clampf(dt, 0.0f, 0.1f);
        // Headless runs have no wall-clock meaning, so a fixed step makes
        // captures reproducible.
        if (options_.headless) dt = 1.0f / 60.0f;

        frame_history_[frame_cursor_] = dt;
        frame_cursor_ = (frame_cursor_ + 1) % FRAME_HISTORY;
        if (frame_filled_ < FRAME_HISTORY) ++frame_filled_;

        pump_events();
        update(dt);

        if (options_.telemetry_interval > 0 &&
            frame_index_ % options_.telemetry_interval == 0) {
            log_telemetry();
        }

        // Capture on the last frame, once the scene has settled.
        if (!options_.screenshot.empty() && options_.frames > 0 &&
            frame_index_ == options_.frames - 1) {
            device_.request_screenshot(options_.screenshot);
        }

        if (device_.begin_frame()) {
            ui_.begin_frame();
            build_ui(dt);
            // The HUD goes on the foreground draw list, so it must be built
            // inside the ImGui frame but layers above every panel.
            draw_hud();
            render();
            device_.end_frame();
        }

        ++frame_index_;
        if (options_.frames > 0 && frame_index_ >= options_.frames) running_ = false;
    }
}

}  // namespace app
