#include "app.h"
#include "gfx/static_model.h"

#include <fstream>
#include <iterator>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

#include "core/log.h"
#include "core/math.h"
#include "gfx/primitives.h"
#include "imgui.h"

using core::Quat;
using core::Vec3;

namespace app {

namespace {

// A pounce's steering: velocity and nose turned onto `target` at `turn`
// rad/s, and held at `speed` at least. The player's and the bots' alike.
void steer_pounce(game::FlightState& st, core::Vec3 target, float turn, float speed, float dt) {
    const core::Vec3 want = core::normalize_or(target - st.position, st.forward());
    const float v = core::maxf(core::length(st.velocity), speed);
    const core::Vec3 heading = core::normalize_or(st.velocity, st.forward());
    const float angle = std::acos(core::clampf(core::dot(heading, want), -1.0f, 1.0f));
    const float step = turn * dt;
    const core::Vec3 turned = angle <= step ? want : core::normalize_or(core::lerp(heading, want, step / angle), want);
    st.velocity = turned * v;
    st.orientation = core::slerp(st.orientation, core::look_rotation(turned, core::Vec3::up()), core::saturate(6.0f * dt));
}

}  // namespace


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
        } else if (arg == "--hide-panels") {
            options.hide_panels = true;
        } else if (arg == "--no-post") {
            options.no_post = true;
        } else if (arg == "--maneuver" && i + 1 < argc) {
            const std::string which = argv[++i];
            options.maneuver = which == "roll" ? 1 : which == "flip" ? 2 : 0;
            if (options.maneuver == 0) LOG_WARN("--maneuver expects roll|flip");
        } else if (arg == "--frame-jitter" && i + 1 < argc) {
            options.frame_jitter = core::clampf(float(SDL_atof(argv[++i])), 0.0f, 0.9f);
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
        } else if (arg == "--studio-speed" && i + 1 < argc) {
            options.studio_speed = core::clampf(float(SDL_atof(argv[++i])), 0.05f, 2.0f);
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
        } else if (arg == "--training") {
            options.combat = true;
            options.training = true;
        } else if (arg == "--run") {
            options.run = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') options.seed = uint32_t(SDL_atoi(argv[++i]));
        } else if (arg == "--hunters-after" && i + 1 < argc) {
            options.hunters_after = float(SDL_atof(argv[++i]));
        } else if (arg == "--walk" && i + 1 < argc) {
            options.has_walk = true;
            std::sscanf(argv[++i], "%f,%f", &options.walk, &options.walk_turn);
        } else if (arg == "--element" && i + 1 < argc) {
            options.element = int(game::element_from_name(argv[++i], game::Element::Fire));
        } else if (arg == "--status" && i + 1 < argc) {
            options.status = int(game::element_from_name(argv[++i], game::Element::Frost));
        } else if (arg == "--valley" && i + 1 < argc) {
            options.valley = SDL_atoi(argv[++i]);
            options.run = true;
        } else if (arg == "--stage" && i + 1 < argc) {
            options.stage = SDL_atoi(argv[++i]);
            options.run = true;
        } else if (arg == "--demo") {
            options.demo = true;
            options.autopilot = true;
            options.run = true;
        } else if (arg == "--run-empty") {
            options.run = true;
            options.run_empty = true;
        } else if (arg == "--seed" && i + 1 < argc) {
            options.seed = uint32_t(SDL_atoi(argv[++i]));
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
    hud_.set_fonts(ui_.numeral_font(), ui_.label_font());
    if (options.hide_panels) show_panels_ = false;
    if (options.no_post) post_settings_.enabled = false;

    pipelines_.init(&device_, SHADER_ROOT);
    if (!debug_.init(&device_, &pipelines_)) return false;
    if (!world_.init(&device_, &pipelines_)) return false;
    if (!post_.init(&device_, &pipelines_)) return false;
    if (!shadow_.init(&device_, &pipelines_)) return false;
    world_.set_shadow_map(&shadow_);
    if (!foliage_.init(&device_, &pipelines_, &shadow_)) return false;
    // The terrain's detail tile (tools/rocks.md): linear data, not colour.
    {
        std::ifstream file(ASSET_ROOT "/textures/terrain_detail.png", std::ios::binary);
        if (file) {
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const gfx::ImageData image = gfx::decode_image(bytes.data(), bytes.size());
            terrain_detail_texture_ = gfx::create_texture_from_image(device_.gpu(), image, "terrain_detail", false);
            world_.set_terrain_detail(terrain_detail_texture_);
            LOG_INFO("terrain detail: %dx%d", image.width, image.height);
        } else {
            LOG_WARN("terrain detail: no terrain_detail.png; the ground stays untextured");
        }
    }
    // The rocks: six meshes from rocks.glb through the static loader, each
    // matched to its kind by name, generated for any the file does not have.
    {
        std::vector<gfx::StaticMesh> meshes;
        std::string error;
        const bool loaded = gfx::load_static_gltf(ASSET_ROOT "/props/rocks.glb", meshes, &error);
        int from_file = 0;
        for (int k = 0; k < gfx::ROCK_KINDS; ++k) {
            const std::string name = "rock_" + std::to_string(k);
            const gfx::StaticMesh* found = nullptr;
            for (const gfx::StaticMesh& m : meshes) {
                if (m.name == name) found = &m;
            }
            if (found) {
                foliage_.set_rock_mesh(device_, k, gfx::encode_rock_mesh(found->data),
                                       found->bounds_max.y);
                ++from_file;
            } else {
                foliage_.set_rock_mesh(device_, k, gfx::make_rock_mesh(k), gfx::rock_size(k).y);
            }
        }
        LOG_INFO("rocks: %d kind(s) from rocks.glb%s, %d generated", from_file,
                 loaded ? "" : (" (" + error + ")").c_str(), gfx::ROCK_KINDS - from_file);
    }

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
    for (int e = 0; e < game::ELEMENT_COUNT; ++e) element_breaths_[e] = game::element_breath(game::Element(e));
    player_element_choice_ = options.element;
    refresh_player_element();

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
    load_prop(ASSET_ROOT "/props/watchtower.glb", tower_prop_, "prop_tower");
    load_prop(ASSET_ROOT "/props/hoard_pile.glb", hoard_prop_, "prop_hoard");
    load_prop(ASSET_ROOT "/props/spire_tower.glb", spire_prop_, "prop_spire");
    load_prop(ASSET_ROOT "/props/hoard_trove.glb", trove_prop_, "prop_trove");
    if (load_prop(ASSET_ROOT "/props/grazer.glb", grazer_prop_, "prop_grazer", &grazer_clips_)) {
        static const char* const names[3] = {"graze", "walk", "run"};
        for (int c = 0; c < 3; ++c) {
            for (size_t i = 0; i < grazer_clips_.size(); ++i) {
                if (grazer_clips_[i].name == names[c]) grazer_clip_[c] = int(i);
            }
        }
        LOG_INFO("grazer clips: graze %d, walk %d, run %d", grazer_clip_[0], grazer_clip_[1],
                 grazer_clip_[2]);
    }
    sphere_mesh_.upload(device_.gpu(), gfx::make_sphere(1.0f, core::Vec3::one(), 18, 12),
                        "unit_sphere");
    if (!particles_.init(&device_, &pipelines_)) return false;
    // Headless runs have no ears; a machine without an output device plays on
    // silently rather than failing.
    if (!options.headless) audio_.init();
    rebuild_courses();
    best_times_.load(ASSET_ROOT "/best_times.txt");
    run_records_.load(ASSET_ROOT "/runs.txt");
    current_course_ = options.course_index;
    apply_assist_preset(0);

    autopilot_ = options.autopilot;
    demo_.fight_tuning = bot_tuning_;
    demo_.reset(20260925u);
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
        studio_time_scale_ = options.studio_speed;
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
        if (options.training) spawn_training_room();
        if (options.bots > 0) spawn_bots(options.bots);
        if (options.match) {
            bot_count_ = options.bots > 0 ? options.bots : bot_count_;
            start_match();
        }
    }
    if (options.run) {
        if (options.hunters_after > 0.0f) run_dials_.pressure_after = options.hunters_after;
        start_run(options.seed ? options.seed : fresh_seed());
        for (int v = 0; v < options.valley; ++v) {
            if (hoard_run_.skip_valley()) advance_valley();
        }
        if (options.stage > 0) {
            // Grown on the first frame, and at full size at once.
            hoard_run_.award(hoard_run_.settings.grow_threshold(std::min(options.stage, 4)));
            apply_growth(hoard_run_.growth_level());
            growth_scale_ = growth_scale_target_;
            wing_growth_ = wing_growth_target_;
            dragon_rig_.wing_growth = wing_growth_;
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
    // A run starts at the valley head the layout chose, facing down the
    // corridor; a death in a run puts you back there too (the run itself is
    // over -- the HUD says so -- and R or Enter starts the next).
    if (run_mode_ && hoard_run_.phase() != game::HoardPhase::Idle) {
        spawn = hoard_run_.layout().start;
        look = hoard_run_.layout().start_look;
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

game::FlightInput App::read_flight_input(float dt) {
    game::FlightInput in;

    if (demo_active()) {
        // The demo pilot: the run or the fight, through the player's controls.
        game::DemoWorld world;
        build_demo_world(world);
        // The demo's dogfighter reacts and aims like an ace: it carries a
        // drake through fights a veteran bot would lose.
        demo_.fight_tuning = bot_tuning_;
        demo_.fight_tuning.reaction_interval = 0.16f;
        demo_.fight_tuning.aim_spread_deg = 1.2f;
        demo_.fight_tuning.aggression = 0.7f;
        demo_decision_ = demo_.update(dt, flight_.state(), world);
        in = demo_decision_.flight;
        // This branch returns before the human input's boost mapping below.
        // Read the ability's active state, including its ordinary cooldown;
        // a requested boost alone must not inject thrust into FlightInput.
        in.boost = combat_.boost_active() ? 1.0f : 0.0f;
        if (demo_decision_.maneuver != game::ManeuverKind::None) {
            maneuver_.start(demo_decision_.maneuver, demo_decision_.maneuver_direction,
                            flight_.state(), maneuver_tuning_);
        }
        maneuver_.apply(in, flight_.state(), maneuver_tuning_, dt);
        return in;
    }

    if (autopilot_) {
        // Aim at the next checkpoint, or hold the last heading once the run is
        // over. The autopilot flies through the same FlightInput a player uses,
        // so it cannot cheat the flight model.
        const game::Ring* target = rally_.next_ring();
        core::Vec3 aim = target ? target->position
                                : flight_.state().position + flight_.state().forward() * 500.0f;
        // Passing the ring's normal makes it line up on the approach axis rather
        // than cutting across the plane and clipping the rim.
        core::Vec3 approach = target ? target->normal() : core::Vec3::zero();
        // In a run it flies the corridor's spine to the pass: the soak for the
        // run loop, and how a headless run gets through the gate.
        if (run_mode_) {
            aim = hoard_run_.next_waypoint(flight_.state().position);
            approach = core::Vec3::zero();
        }
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
        // The d-pad's left and right were the rudder; they are the roll
        // dodge now (below), because the playtest could not reach d-pad down
        // in a fight and a dodge has to be reachable. The rudder stays on Q/E
        // -- turn coordination yaws into a bank on its own, and the pad has
        // no other pair to give it.
        in.flap = core::maxf(in.flap, input_.gamepad_button(SDL_GAMEPAD_BUTTON_SOUTH) ? 1.0f : 0.0f);
        in.brake = core::maxf(in.brake, input_.gamepad_trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    }

    // Aerobatics. A roll goes the way the stick is held, right by default; a
    // flip needs no direction. Both are edges, and both run through the same
    // Maneuver the bots fly, overriding the stick while they last.
    if (!free_camera_ && !ui_.wants_keyboard()) {
        const bool pad_left = input_.gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        const bool pad_right = input_.gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        const bool roll = input_.pressed(SDL_SCANCODE_Z) || pad_left || pad_right;
        const bool flip = input_.pressed(SDL_SCANCODE_B) ||
                          input_.gamepad_button(SDL_GAMEPAD_BUTTON_DPAD_UP);
        if (roll) {
            // On the pad the button IS the direction; on the keyboard the
            // stick is, right by default.
            float direction = std::fabs(stick_.x) > 0.2f ? core::signf(stick_.x) : 1.0f;
            if (pad_left) direction = -1.0f;
            if (pad_right) direction = 1.0f;
            maneuver_.start(game::ManeuverKind::Roll, direction, flight_.state(), maneuver_tuning_);
        } else if (flip) {
            maneuver_.start(game::ManeuverKind::Flip, 1.0f, flight_.state(), maneuver_tuning_);
        }
    }
    // A capture's manoeuvre, on cue.
    if (options_.maneuver != 0 && frame_index_ == 30) {
        maneuver_.start(options_.maneuver == 1 ? game::ManeuverKind::Roll : game::ManeuverKind::Flip,
                        1.0f, flight_.state(), maneuver_tuning_);
    }
    maneuver_.apply(in, flight_.state(), maneuver_tuning_, dt);

    // Standing: the stick walks. W and the pad's stick pushed away both mean
    // "that way" -- on the ground there is no nose to raise, so the flight
    // inversion does not apply -- and A/D or the stick's x turns in place.
    // Pitch and roll are dropped so the body does not tip on its feet; Space
    // still leaps into the air.
    if (flight_.state().grounded) {
        if (options_.has_walk) {
            in.walk = options_.walk;
            in.walk_turn = options_.walk_turn;
        } else if (scripted) {
            in.walk = in.pitch;
            in.walk_turn = in.roll;
        } else if (!ui_.wants_keyboard()) {
            in.walk = input_.axis(SDL_SCANCODE_S, SDL_SCANCODE_W);
            in.walk_turn = input_.axis(SDL_SCANCODE_A, SDL_SCANCODE_D);
            if (input_.has_gamepad()) {
                in.walk -= input_.gamepad_axis(SDL_GAMEPAD_AXIS_LEFTY, controls_.gamepad_deadzone);
                in.walk_turn += input_.gamepad_axis(SDL_GAMEPAD_AXIS_LEFTX, controls_.gamepad_deadzone);
            }
        }
        in.pitch = 0.0f;
        in.roll = 0.0f;
    }
    return in;
}

gfx::ModelUniforms App::dragon_model_uniforms() const {
    const game::FlightState& s = dragon_state();
    gfx::ModelUniforms model;
    // The asset correction is applied inside the dragon's own frame, so it
    // aligns the model to the engine without disturbing the flight transform.
    model.model = player_to_world(s);
    model.recolour = core::Vec4{player_hue_.x, player_hue_.y, player_hue_.z, player_recolour_};
    return model;
}

void App::replant() {
    vegetation_.plant(terrain_, vegetation_settings_);
    for (int k = 0; k < gfx::TREE_KINDS; ++k) {
        foliage_.set_trees(device_, gfx::TreeKind(k), vegetation_.trees(gfx::TreeKind(k)));
    }
    for (int k = 0; k < gfx::ROCK_KINDS; ++k) foliage_.set_rocks(device_, k, vegetation_.rocks(k));
    LOG_INFO("rocks: %zu placed", vegetation_.rock_count());
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
    // The terrain shader dissolves the ground into the sky over the outermost
    // band of this extent. It is the SKIRT's edge, not the playable one: the
    // fade predates the skirt, and left at the playable extent it began 500 m
    // inside the map -- exactly where the default course puts the start, so
    // every run opened on a valley floor blended halfway to sky.
    material_.half_extent = terrain_settings_.half_extent * terrain_settings_.skirt_extent_factor;
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
    release_prop(tower_prop_);
    release_prop(hoard_prop_);
    release_prop(spire_prop_);
    release_prop(trove_prop_);
    release_prop(grazer_prop_);
    if (terrain_detail_texture_) SDL_ReleaseGPUTexture(device_.gpu(), terrain_detail_texture_);
    terrain_detail_texture_ = nullptr;
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
    if (input_.pressed(SDL_SCANCODE_R)) {
        // In a run, R flies the same valley again from the head.
        if (run_mode_) start_run(run_seed_);
        else respawn_dragon();
    }
    if (input_.pressed(SDL_SCANCODE_V)) chase_.first_person = !chase_.first_person;
    // U (gamepad Y): swap to the second breath and back, once learned.
    {
        const bool down = input_.has_gamepad() && input_.gamepad_button(SDL_GAMEPAD_BUTTON_NORTH);
        if ((input_.pressed(SDL_SCANCODE_U) || (down && !swap_button_was_down_)) && second_unlocked_) {
            const bool run = run_mode_ && hoard_run_.phase() != game::HoardPhase::Idle;
            if (run) {
                using_second_ = !using_second_;
            } else {
                // The arena is the sandbox: U walks every element in turn,
                // the player's own, then each of the other five.
                const game::Element first = player_element_choice_ >= 0
                                                ? game::Element(player_element_choice_)
                                                : player_model().breath.element;
                arena_breath_index_ = (arena_breath_index_ + 1) % game::ELEMENT_COUNT;
                using_second_ = arena_breath_index_ != 0;
                second_element_ = game::Element((int(first) + arena_breath_index_) % game::ELEMENT_COUNT);
            }
            refresh_player_element();
            audio_.play(audio::Clip::Boost, 0.5f, 1.5f);
        }
        swap_button_was_down_ = down;
        // H (left-stick click): the fury.
        const bool fury_down = input_.has_gamepad() && input_.gamepad_button(SDL_GAMEPAD_BUTTON_LEFT_STICK);
        fury_requested_ = input_.pressed(SDL_SCANCODE_H) || (fury_down && !fury_button_was_down_);
        fury_button_was_down_ = fury_down;
    }
    // Hands-off: the demo pilot takes (or gives back) the controls.
    if (input_.pressed(SDL_SCANCODE_P)) set_autopilot(!autopilot_);
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

void App::feed_first_person_head(float dt) {
    const core::Vec3 head_model = dragon_rig_.head_position();
    if (core::length_sq(head_model) < 1e-6f) {
        chase_.clear_first_person_head();
        return;
    }
    const game::FlightState& s = dragon_state();
    const LoadedModel& model = player_model();
    // At the grown size: the head is where the scaled model puts it, and the
    // eye is solved from head measurements scaled to match (they were taken
    // at the bind size, so a grown dragon's eye sat inside its own head).
    const core::Vec3 head_world = core::transform_point(player_to_world(s), head_model);

    // Composition, the same for every species: the eye sits a fraction of a
    // head length behind the head's rear, and exactly high enough that the
    // highest point of head ahead of it appears on a chosen line of the
    // frame -- the horn tips at the bottom, as on the original dragon. The
    // species profile can nudge the result. Without the measurements (a
    // generated rig), the nudge is added to a fixed offset.
    float up = dragon_rig_.tuning.first_person_up;
    float back = dragon_rig_.tuning.first_person_back;
    if (model.head_box_valid && model.joints.head != anim::NO_PARENT &&
        size_t(model.joints.head) < dragon_rig_.world_matrices().size()) {
        // The profile was measured in the bind pose; the neck streamlines in
        // flight and rears in a turn, pitching the head by tens of degrees,
        // so the profile is carried through the head's current rotation
        // (model space, then into the body frame through the asset
        // correction) before anything is solved against it.
        const core::Quat asset_rotation = core::quat_from_matrix(model.asset.matrix());
        const core::Quat head_now =
            core::quat_from_matrix(dragon_rig_.world_matrices()[size_t(model.joints.head)]);
        const core::Quat head_bind = core::quat_from_matrix(model.skeleton.world_bind(model.joints.head));
        const core::Quat head_delta =
            asset_rotation * (head_now * core::conjugate(head_bind)) * core::conjugate(asset_rotation);

        // The setback scales with the head's WIDTH, because that is what
        // decides how much of the frame a head this close fills sideways.
        // Blightmaw's horns span 2.2 m; one head length behind them they
        // still filled the frame, while the rule read as satisfied because it
        // only looked at height.
        const float width = model.head_box_max.x - model.head_box_min.x;
        // Depression of the head line below the view axis, at the base FOV
        // so speed does not move the head about the frame.
        const float slope = std::tan(core::radians(
            core::clampf(chase_.tuning.first_person_head_line, 0.0f, 1.0f) *
            0.5f * chase_.tuning.fov_base_deg));
        // The eye sits behind the rearmost point of the posed head.
        using TopProfile = LoadedModel::TopProfile;
        float rear = -1e9f;
        core::Vec3 posed[TopProfile::BINS];
        for (int bin = 0; bin < TopProfile::BINS; ++bin) {
            posed[bin] = core::rotate(head_delta, core::Vec3{0.0f, model.head_profile.top[bin],
                                                             model.head_profile.z_at(bin)});
            rear = core::maxf(rear, posed[bin].z);
        }
        const float solved_back = rear + chase_.tuning.first_person_setback * width;
        // ...and high enough that nothing ahead of it -- head or neck -- rises
        // above the head line. The neck is taken as authored: its base moves
        // little, and it is the part right under the eye that matters.
        float clear = -1e9f;
        const auto raise_over = [&](core::Vec3 p) {
            const float ahead = solved_back - p.z;  // +Z is aft: metres in front of the eye
            if (ahead < 0.3f) return;               // too close to be in frame
            clear = core::maxf(clear, p.y + slope * ahead);
        };
        for (const core::Vec3& p : posed) raise_over(p);
        if (model.neck_profile.valid) {
            for (int bin = 0; bin < TopProfile::BINS; ++bin) {
                raise_over(core::Vec3{0.0f, model.neck_profile.top[bin], model.neck_profile.z_at(bin)});
            }
        }
        if (clear < -1e8f) clear = model.head_box_max.y + 0.5f;
        // dt <= 0 means "do not advance the smoothing": the pre-rig call each
        // frame only re-feeds last frame's offsets so the snap path has them.
        if (!first_person_offsets_valid_) {
            first_person_up_smoothed_ = clear;
            first_person_back_smoothed_ = solved_back;
            first_person_offsets_valid_ = true;
        } else if (dt > 0.0f) {
            first_person_up_smoothed_ = core::damp(first_person_up_smoothed_, clear, 0.1f, dt);
            first_person_back_smoothed_ = core::damp(first_person_back_smoothed_, solved_back, 0.1f, dt);
        }
        up += first_person_up_smoothed_;
        back += first_person_back_smoothed_;
    } else {
        up += 1.1f;
        back += 1.6f;
    }
    up *= growth_scale_;
    back *= growth_scale_;
    chase_.set_first_person_head(head_world, up, back);
}

void App::spawn_training_room() {
    bots_.clear();
    const game::FlightState& s = flight_.state();
    combat_.spawn_training(s.position, s.forward(), s.right());
    LOG_INFO("training room: six passive dummies laid out ahead; R to fly the line again");
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
    {
        // The player's status on the wings: frozen, the flap is locked and
        // the stick half there (never gone -- the player keeps the controls);
        // chilled, a slack stick and thickening air.
        game::FlightInput input = read_flight_input(dt);
        const float slow = combat_enabled_ ? combat_.player_slow() : 0.0f;
        if (slow > 0.0f) {
            const bool frozen = combat_.player_status().frozen > 0.0f;
            const float stick = frozen ? 0.5f : 1.0f - 0.5f * slow;
            input.pitch *= stick;
            input.roll *= stick;
            input.yaw *= stick;
            if (frozen) input.flap = 0.0f;
        }
        flight_.update(input, &terrain_, dt);
        // The pounce: while an elder's boost is locked onto a target, the
        // flight is turned onto it -- velocity and nose together, at the
        // pounce's turn rate, and kept fast -- so a boost meets its mark.
        if (const int mark = combat_.pounce_target(); mark >= 0 && size_t(mark) < combat_.sentinels().size()) {
            steer_pounce(flight_.state(), combat_.sentinels()[size_t(mark)].position,
                         combat_.tuning.pounce_turn, combat_.tuning.pounce_speed, dt);
        }
        if (slow > 0.0f && !flight_.state().grounded) {
            flight_.state().velocity = flight_.state().velocity * std::exp(-0.3f * slow * dt);
        }
    }

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
        } else if (scenario == game::StudioScenario::Melee || scenario == game::StudioScenario::Claw ||
                   scenario == game::StudioScenario::Tail) {
            dragon_rig_.set_aim_target(game::studio_melee_target(studio_time_, studio_centre_));
        }
        rig_action_ = game::studio_action(scenario, studio_time_previous_, studio_time_);
    }

    // The eye of the first-person view rides the animated head. This is last
    // frame's head; it is fed again, with this frame's dt, after the rig runs.
    feed_first_person_head(0.0f);
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
                    player_to_world(s),
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
                player_to_world(s);
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

        if (options_.status >= 0) {
            // The capture hold: every enemy suffering the one status. Where
            // each one is at frame 10, for placing a camera on it.
            if (frame_index_ == 10) {
                for (const game::Sentinel& target : combat_.sentinels()) {
                    LOG_INFO("status hold: target at %.0f,%.0f,%.0f", double(target.position.x),
                             double(target.position.y), double(target.position.z));
                }
            }
            for (game::Sentinel& target : combat_.sentinels()) {
                game::Status& st = target.status;
                switch (game::Element(options_.status)) {
                    case game::Element::Fire: st.burn = 1.0f; break;
                    case game::Element::Frost: st.frozen = 1.0f; target.stun = 1.0f; break;
                    case game::Element::Blight: st.corrode = 1.0f; break;
                    case game::Element::Storm: st.shock = 1.0f; break;
                    case game::Element::Tide: st.drench = 1.0f; break;
                    case game::Element::Stone: st.stagger = 0.9f; st.since_stagger = 0.0f; break;
                    default: break;
                }
            }
        }
        update_abilities();
        update_bots(dt);
        const game::CombatEvents events = combat_.update(dt, flight_.state(), read_combat_input());
        match_.update(dt, events);
        if (run_mode_) update_run(dt, events);
        for (const game::Impact& impact : combat_.impacts()) {
            emit_impact(impact.position, impact.team == game::Team::Hostile, impact.on_terrain,
                        impact.element);
            // Loudness by proximity to the ear, not to the dragon: the chase
            // camera is where the player sits.
            const float d = core::distance(active_camera().position, impact.position);
            audio_.play(audio::Clip::Explosion, 1.2f / (1.0f + d * d / (170.0f * 170.0f)));
        }
        if (combat_.breathing()) {
            emit_flame(combat_.breath_origin(), combat_.breath_direction(),
                       combat_.tuning.breath_range * combat_.player_breath.range, false, dt,
                       &player_breath_);
        }
        for (const game::BreathCone& flame : combat_.hostile_breaths()) {
            // A bot tagged its cone with its model index; an untagged cone
            // (a sentinel) keeps the shared hostile blue.
            const game::BreathProfile* profile =
                flame.element != game::Element::None ? &breath_for(flame.element, flame.source)
                : flame.source >= 0 && size_t(flame.source) < models_.size()
                    ? &models_[size_t(flame.source)]->breath
                    : nullptr;
            emit_flame(flame.origin, flame.direction,
                       combat_.tuning.hostile_breath_range * flame.scales.range, true, dt,
                       profile);
        }
        // A thin ember trail off every live round, so its path lingers a beat
        // -- in its element's colours, so a frost bolt reads as frost coming.
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
            if (projectile.element != game::Element::None) {
                const game::BreathProfile& b = element_breaths_[int(projectile.element)];
                p.color_start = b.hot * 0.8f;
                p.color_end = b.cool;
            }
            p.brightness = 0.9f;
            particles_.spawn(p);
        }
        // Each tower's fire is its element: a plume at the brazier in that
        // element's own motion, so a frost tower is read as frost from a
        // kilometre, before its first bolt. Jammed, it gutters.
        for (size_t d = 0; d < run_defence_slots_.size(); ++d) {
            const int slot = run_defence_slots_[d];
            if (slot < 0 || size_t(slot) >= combat_.sentinels().size()) continue;
            const game::Sentinel& tower = combat_.sentinels()[size_t(slot)];
            if (!tower.alive || tower.element == game::Element::None) continue;
            const game::BreathProfile& b = element_breaths_[int(tower.element)];
            const float rate = tower.status.jammed() ? 6.0f : 28.0f;
            if (0.5f + 0.5f * particle_unit() > rate * dt) continue;
            gfx::Particle p;
            p.position = tower.position + core::Vec3{particle_unit() * 0.8f, tower.muzzle_height + 0.6f,
                                                     particle_unit() * 0.8f};
            if (!defence_is_spire(int(d))) p.position.z += 2.0f;
            const float rise = b.buoyancy >= 0.0f ? 1.0f : -0.4f;
            p.velocity = core::Vec3{particle_unit() * 0.8f, 3.5f * rise + 1.0f, particle_unit() * 0.8f};
            p.acceleration = core::Vec3{0.0f, b.buoyancy * 0.35f, 0.0f};
            p.drag = 1.0f;
            p.life = 0.9f;
            p.size_start = 2.4f;
            p.size_end = 0.8f;
            p.color_start = b.hot;
            p.color_end = b.cool;
            p.brightness = 0.8f;
            particles_.spawn(p);
        }
        // What everything is suffering, on its body; the storm's arcs; and
        // the bursts where a status landed hard.
        for (const game::Sentinel& sentinel : combat_.sentinels()) {
            if (!sentinel.alive || !sentinel.status.any()) continue;
            const float radius = sentinel.radius > 0.0f ? sentinel.radius : combat_.tuning.sentinel_radius;
            emit_status(sentinel.position, radius, sentinel.status, dt);
        }
        if (combat_.player_status().any()) {
            emit_status(flight_.state().position, 4.5f * growth_scale_, combat_.player_status(), dt);
        }
        for (const game::Arc& arc : combat_.arcs()) {
            emit_arc(arc.from, arc.to, game::element_colour(game::Element::Storm));
        }
        for (const game::StatusBurst& burst : combat_.status_bursts()) emit_status_burst(burst);
        if (!combat_.arcs().empty()) {
            play_status_sound(audio::Clip::Zap, 3, combat_.arcs().front().from, 0.9f);
        }
        for (float& c : status_sound_cooldown_) c = core::maxf(c - dt, 0.0f);
        // The abilities, seen and heard.
        if (events.charged_fired) {
            audio_.play(audio::Clip::Shot, 1.0f, 0.65f);
            audio_.play(audio::Clip::Ignite, 0.7f, 0.8f);
            chase_.kick(0.5f);
        }
        if (combat_.charge() > 0.05f) {
            // Gathering at the mouth: motes drawn in toward it, brighter as
            // the charge fills.
            const game::BreathProfile& b = player_breath_;
            const core::Vec3 mouth = combat_.muzzle(flight_.state());
            for (int i = 0; i < 3; ++i) {
                gfx::Particle p;
                const core::Vec3 off = core::normalize_or(
                    core::Vec3{particle_unit(), particle_unit(), particle_unit()}, core::Vec3::up()) * 4.0f;
                p.position = mouth + off;
                p.velocity = flight_.state().velocity - off * 8.0f;
                p.life = 0.12f;
                p.size_start = 0.5f + 1.4f * combat_.charge();
                p.size_end = 0.2f;
                p.color_start = b.hot;
                p.color_end = b.cool;
                p.brightness = 0.6f + 0.8f * combat_.charge();
                particles_.spawn(p);
            }
        }
        if (combat_.abilities.ram && combat_.boost_active()) {
            // The ram's aura: the body sheathed in its element while it boosts.
            emit_status(flight_.state().position, 4.0f * growth_scale_, [&] {
                game::Status s;
                switch (combat_.player_element) {
                    case game::Element::Frost: s.frozen = 0.1f; break;
                    case game::Element::Blight: s.corrode = 0.1f; break;
                    case game::Element::Storm: s.shock = 0.1f; break;
                    case game::Element::Tide: s.drench = 0.1f; break;
                    case game::Element::Stone: s.stagger = 1.0f; break;
                    default: s.burn = 0.1f; break;
                }
                return s;
            }(), dt);
        }
        rams_landed_ += events.rammed;
        furies_released_ += events.fury_released ? 1 : 0;
        if (events.rammed > 0) {
            audio_.play(audio::Clip::BiteHit, 1.0f, 0.75f);
            audio_.play(audio::Clip::Crack, 0.7f, 1.1f);
            chase_.kick(1.0f);
            emit_impact(events.ram_position, false, false, combat_.player_element);
        }
        // The fury filling up is an event: said once, heard once.
        {
            const bool full = combat_.abilities.fury && combat_.fury() >= 1.0f;
            if (full && !fury_was_full_) {
                fury_ready_flash_ = 2.5f;
                audio_.play(audio::Clip::Boost, 0.8f, 0.6f);
            }
            fury_was_full_ = full;
            fury_ready_flash_ = core::maxf(fury_ready_flash_ - dt, 0.0f);
        }
        if (events.fury_released) {
            LOG_INFO("fury released at frame %d", frame_index_);
            audio_.play(audio::Clip::Fury, 1.3f);
            chase_.kick(2.0f);
            emit_fury(flight_.state().position);
        }
        // A full stagger knocks the player off line like a bite does, without
        // the bite's bookkeeping.
        if (events.player_staggered && !events.bitten) {
            flight_.state().velocity = flight_.state().velocity + events.knockback;
            chase_.kick(1.0f);
        }
        // Enter starts the rematch from the results screen; R already means
        // respawn and stays out of it.
        if (match_.phase() == game::MatchPhase::Results &&
            input_.pressed(SDL_SCANCODE_RETURN)) {
            start_match();
        }
        // A run that has ended: Enter deals a new valley.
        if (run_mode_ && input_.pressed(SDL_SCANCODE_RETURN) &&
            (hoard_run_.phase() == game::HoardPhase::Banked ||
             hoard_run_.phase() == game::HoardPhase::Lost)) {
            start_run(fresh_seed());
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
        // The swing's gesture follows the nearest mark: jaws ahead, a claw
        // alongside, the tail behind.
        rig_action_.bite = events.melee_swung && events.melee_gesture == game::MeleeGesture::Bite;
        rig_action_.claw = events.melee_swung && events.melee_gesture == game::MeleeGesture::Claw;
        rig_action_.tail = events.melee_swung && events.melee_gesture == game::MeleeGesture::Tail;
        // A swooped meal is a snatch of the jaws.
        if (snatch_pending_) {
            rig_action_.bite = true;
            snatch_pending_ = false;
        }
        rig_action_.side = events.melee_side;
        rig_action_.boost = combat_.boost_active() ? 1.0f : 0.0f;

        if (events.melee_swung) {
            ++bites_swung_;
            audio_.play(audio::Clip::Bite, 0.9f);
            if (events.melee_hit != game::MeleeKind::None) {
                ++bites_landed_;
                // A landed bite is an event, not a number: the crunch, a burst
                // of embers where the jaws met, and a jolt through the camera
                // that grows with the chain.
                audio_.play(audio::Clip::BiteHit, 1.0f);
                emit_impact(events.melee_hit_position, false, false);
                chase_.kick(0.5f + 0.25f * float(events.melee_combo));
                hit_marker_ = 0.35f;
                hit_marker_position_ = events.melee_hit_position;
            }
            // The lunge spends airspeed: a bite is a commitment, not a free
            // action on a cooldown. Taken off the velocity directly, the way a
            // brake would take it, so the flight model sees a slower dragon.
            game::FlightState& st = flight_.state();
            const float speed = core::length(st.velocity);
            const float cost = combat_.tuning.melee_lunge_speed_cost;
            if (speed > cost + 1.0f) st.velocity = st.velocity * ((speed - cost) / speed);
        }

        if (events.had_hit) {
            hitmarker_ = 0.18f;
            // A tick for a shot or a bite landing -- not for a held breath,
            // which lands every frame and would buzz.
            if (!combat_.breathing() && hit_tick_cooldown_ <= 0.0f) {
                audio_.play(audio::Clip::BiteHit, 0.35f, 1.9f);
                hit_tick_cooldown_ = 0.1f;
            }
        }
        if (events.kills > 0) killmarker_ = 0.45f;
        hit_tick_cooldown_ = core::maxf(hit_tick_cooldown_ - dt, 0.0f);
        hitmarker_ = core::maxf(hitmarker_ - dt, 0.0f);
        killmarker_ = core::maxf(killmarker_ - dt, 0.0f);
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

        if (events.bitten) {
            ++bites_taken_;
            // Knocked: the shove goes straight into the velocity, and the
            // camera jolts hard. Never a stun for the player -- the controls
            // stay theirs.
            flight_.state().velocity = flight_.state().velocity + events.knockback;
            chase_.kick(1.2f);
            audio_.play(audio::Clip::BiteHit, 0.9f);
        }
        if (events.damage_taken > 0.0f) {
            // Rate-limited: a flame deals damage every frame, and forty
            // overlapping cries per second was the "strange loud flame" of the
            // playtest. One screech, then a beat before the next.
            if (hit_sound_cooldown_ <= 0.0f) {
                audio_.play(audio::Clip::Roar, 1.0f, 0.95f + 0.1f * particle_unit());
                hit_sound_cooldown_ = 0.45f;
            }
            damage_flash_ = 1.0f;
            // Held well past the flash: the point is to let the player turn and
            // find the shooter, which takes longer than the hit registers.
            damage_direction_ = events.damage_from;
            damage_marker_ = 3.0f;
        }
        if (events.player_died) {
            audio_.play(audio::Clip::RoarDown, 1.1f);
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
            player_to_world(s);
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
        {
            // The breath you hear is voiced by its element.
            game::Element voiced = combat_.player_element;
            if (!combat_.breathing() && !combat_.hostile_breaths().empty()) {
                voiced = combat_.hostile_breaths().front().element;
            }
            audio_.set_flame_style(voiced == game::Element::None ? 0 : int(voiced));
        }

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

    // The rally stands down in a run: the corridor is the course.
    if (!studio_active_ && !run_mode_) rally_.update(flight_.state(), dt);
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
    if (autopilot_ && !run_mode_ && rally_.phase() == game::RunPhase::Finished) respawn_dragon();

    // Hands-off, the game keeps going: a run that has ended deals a new
    // valley, a match on its results screen rematches, both after a few
    // seconds on the results.
    {
        const bool run_over = run_mode_ && (hoard_run_.phase() == game::HoardPhase::Banked ||
                                            hoard_run_.phase() == game::HoardPhase::Lost);
        const bool match_over = match_.phase() == game::MatchPhase::Results;
        if (autopilot_ && (run_over || match_over)) {
            demo_restart_timer_ += dt;
            if (demo_restart_timer_ > 6.0f) {
                demo_restart_timer_ = 0.0f;
                if (run_over) start_run(fresh_seed());
                else start_match();
                demo_.reset(uint32_t(frame_index_) * 2654435761u + 7u);
                for (float& t : demo_.time_in) t = 0.0f;
            }
        } else {
            demo_restart_timer_ = 0.0f;
        }
    }

    dragon_rig_.set_action(rig_action_);
    // Where the ground is under each foot: the rig plants the standing limbs
    // on the terrain they stand over, not on the one height under the body.
    {
        const game::FlightState& s = dragon_state();
        dragon_rig_.set_ground(
            player_to_world(s),
            [this](float x, float z) { return terrain_.surface_at(x, z); });
    }
    // The scenario and rig must share a clock: otherwise slow motion spaces
    // attacks farther apart but every actual swing still runs at full speed.
    if (!options_.bind_pose) dragon_rig_.update(dragon_state(),
                                               dt * (studio_active_ ? studio_time_scale_ : 1.0f));
    // Re-place the first-person eye on THIS frame's head. The camera ran
    // before the rig, so it was holding last frame's head while the mesh
    // draws this one; the gap is one frame of neck motion, invisible at a
    // steady 60 Hz and a visible twitch of the horns as soon as frame times
    // are uneven, because the gap then changes size every frame.
    if (chase_.first_person) {
        feed_first_person_head(dt);
        chase_.place_first_person(dragon_state());
    }

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
        player_to_world(s);
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
        draw->AddText(ImVec2(12.0f, ImGui::GetIO().DisplaySize.y - 22.0f),
                      IM_COL32(200, 210, 225, 130), "F1  panels");
        return;
    }

    ImGui::GetForegroundDrawList()->AddText(ImVec2(12.0f, ImGui::GetIO().DisplaySize.y - 22.0f),
                                            IM_COL32(200, 210, 225, 110), "F1  hide panels");

    build_flight_ui();
    build_rally_ui();
    build_dragon_ui();
    build_combat_ui();
    build_studio_ui();

    // Every panel docks to the right edge, stacked: the tool layer never
    // sits over the centre of the frame where the game is.
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 402.0f, 12.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390, 0), ImGuiCond_FirstUseEver);
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
        if (chase_.first_person) {
            // One composition for every species: how far behind the head's
            // measured rear the eye sits, and on which line of the frame the
            // head's highest visible point lands (0 = view centre, 1 = the
            // bottom edge).
            ImGui::SliderFloat("eye setback", &c.first_person_setback, 0.0f, 3.0f, "%.2f head widths");
            ImGui::SliderFloat("head line", &c.first_person_head_line, 0.0f, 1.0f, "%.2f of half-height");
            // Per-species nudge in metres, saved with the rig profile from the
            // Dragon panel. Zero for every shipped species so far.
            ImGui::SliderFloat("species eye up", &dragon_rig_.tuning.first_person_up, -2.0f, 2.0f, "%+.2f m");
            ImGui::SliderFloat("species eye back", &dragon_rig_.tuning.first_person_back, -2.0f, 2.0f, "%+.2f m");
        }

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
        dirty |= ImGui::SliderFloat("ridge octave height", &terrain_settings_.ridge_height, 0.0f, 300.0f, "%.0f m");
        dirty |= ImGui::SliderFloat("ridge octave scale", &terrain_settings_.ridge_scale, 100.0f, 2000.0f, "%.0f m");
        dirty |= ImGui::SliderFloat("strata step", &terrain_settings_.strata_step, 0.0f, 80.0f, "%.0f m");
        dirty |= ImGui::SliderFloat("strata strength", &terrain_settings_.strata_strength, 0.0f, 1.0f, "%.2f");
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
            replant_now |= ImGui::SliderFloat("skirt trees", &vegetation_settings_.skirt_trees, 1.0f,
                                              3.0f, "%.2f x extent");
            replant_now |= ImGui::SliderFloat("skirt cover", &vegetation_settings_.skirt_cover, 0.0f,
                                              1.0f);
            ImGui::SliderFloat("tree draw distance", &foliage_.tree_draw_distance, 500.0f, 9000.0f,
                               "%.0f m");
            replant_now |= ImGui::SliderFloat("slope sink", &vegetation_settings_.slope_sink, 0.0f,
                                              12.0f, "%.1f m");
            ImGui::SliderFloat("grass radius", &vegetation_settings_.grass_radius, 30.0f, 250.0f,
                               "%.0f m");
            ImGui::SliderFloat("grass spacing", &vegetation_settings_.grass_spacing, 1.0f, 8.0f,
                               "%.1f m");
            ImGui::SliderFloat("wind", &vegetation_settings_.wind, 0.0f, 3.0f);
            ImGui::SliderFloat("crown detail distance", &foliage_.lod_distance, 40.0f, 800.0f,
                               "%.0f m");
            ImGui::TextDisabled("%u trees (%u drawn), %u grass tufts near the camera", foliage_.tree_count(),
                                foliage_.trees_drawn(),
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
        ImGui::SliderFloat("sun wrap", &lighting_.sun_wrap, 0.0f, 0.6f, "%.2f");
        ImGui::SliderFloat("foliage translucency", &lighting_.foliage_translucency, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("ground bounce", &lighting_.ground_bounce, 0.0f, 3.0f, "%.2f");
    }

    // The last word on the picture: bloom and the grade. One grade on
    // everything is the strongest glue there is between assets of different
    // origins, and the tonemap's whitening of bright things is what the
    // bloom fixes properly.
    if (ImGui::CollapsingHeader("Grade & bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
        gfx::PostSettings& g = post_settings_;
        ImGui::Checkbox("enabled", &g.enabled);
        ImGui::SliderFloat("exposure", &g.exposure, 0.3f, 3.0f, "%.2f");
        ImGui::SliderFloat("contrast", &g.contrast, 0.6f, 1.6f, "%.2f");
        ImGui::SliderFloat("saturation", &g.saturation, 0.0f, 1.8f, "%.2f");
        ImGui::SliderFloat("temperature", &g.temperature, -0.3f, 0.3f, "%.2f");
        ImGui::SliderFloat("tint", &g.tint, -0.3f, 0.3f, "%.2f");
        ImGui::SliderFloat("lift", &g.lift, -0.1f, 0.2f, "%.3f");
        ImGui::SliderFloat("gamma", &g.gamma, 0.6f, 1.6f, "%.2f");
        ImGui::SliderFloat("vignette", &g.vignette, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("hue preserve", &g.hue_preserve, 0.0f, 1.0f, "%.2f");
        ImGui::ColorEdit3("shadows toward", g.shadows_rgb, ImGuiColorEditFlags_Float);
        ImGui::SliderFloat("shadows strength", &g.shadows_strength, 0.0f, 1.0f, "%.2f");
        ImGui::ColorEdit3("highlights toward", g.highlights_rgb, ImGuiColorEditFlags_Float);
        ImGui::SliderFloat("highlights strength", &g.highlights_strength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("bloom strength", &g.bloom_strength, 0.0f, 1.5f, "%.2f");
        ImGui::SliderFloat("bloom threshold", &g.bloom_threshold, 0.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("bloom knee", &g.bloom_knee, 0.01f, 2.0f, "%.2f");
    }

    // The world's colours, live. Drag a tree green toward the ground green
    // while flying; that is how the grade between them was found.
    if (ImGui::CollapsingHeader("Palette")) {
        for (int i = 0; i < gfx::PALETTE_COUNT; ++i) {
            ImGui::ColorEdit3(gfx::Palette::name(i), &lighting_.palette.colors[i].x,
                              ImGuiColorEditFlags_Float);
        }
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

    // ImGui's own display size, in its units (points). The render target is
    // in pixels, and on a display with a pixel density of 2 handing the HUD
    // the pixel size drew every readout at twice its place and size -- the
    // strip off the right edge, the reticle off the screen.
    hud_.begin(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
    const ui::Tokens& tk = hud_.tokens();
    const float width = hud_.width();
    const float height = hud_.height();
    const float margin = hud_.margin();
    char line[96];

    if (combat_enabled_) draw_combat_hud();
    if (run_mode_) draw_run_hud();
    if (autopilot_) {
        // Hands-off, and what the pilot is doing: a demo that explains itself.
        char text[96];
        std::snprintf(text, sizeof(text), demo_active() ? "AUTOPILOT  %s   P to take over"
                                                        : "AUTOPILOT%s   P to take over",
                      demo_active() ? game::demo_state_name(demo_.state()) : "");
        hud_.label(ImVec2(margin, height - margin - hud_.px(16.0f)), text, tk.accent, 14.0f);
    }

    // ---- airspeed, bottom centre ----
    // Diegetic first: speed is the wind and the FOV surge, so the numeral is
    // modest -- read at a glance, not studied. The ability pips sit beside
    // it when combat is on (draw_combat_hud adds them).
    {
        const float plate_w = hud_.px(150.0f);
        const float plate_h = hud_.px(56.0f);
        const ImVec2 min(width * 0.5f - plate_w * 0.5f, height - margin - plate_h);
        hud_.plate(min, ImVec2(min.x + plate_w, min.y + plate_h));
        std::snprintf(line, sizeof(line), "%.0f", flight_.state().airspeed);
        const float numeral_size = 40.0f;
        const float w = hud_.numeral_width(line, numeral_size);
        const float unit_w = hud_.label_width("m/s", 14.0f);
        const float total = w + hud_.px(6.0f) + unit_w;
        const float x = width * 0.5f - total * 0.5f;
        hud_.numeral(ImVec2(x, min.y + hud_.px(6.0f)), line, tk.text, numeral_size);
        hud_.label(ImVec2(x + w + hud_.px(6.0f), min.y + hud_.px(30.0f)), "m/s", tk.text_dim, 14.0f);
    }

    // During a match the rally readout stands down: you are fighting, not
    // racing, and the checkpoint marker collides with the scoreline.
    if (match_.phase() != game::MatchPhase::Idle || run_mode_) return;

    const game::Course& course = rally_.course();
    if (course.rings.empty()) return;

    const core::Mat4 view_proj = active_camera().view_projection(device_.aspect());

    // ---- next checkpoint marker ----
    if (const game::Ring* next = rally_.next_ring()) {
        ImVec2 screen;
        const bool on_screen = project_to_screen(view_proj, next->position, width, height, screen) &&
                               screen.x > 0.0f && screen.x < width && screen.y > 0.0f &&
                               screen.y < height;
        const float range = core::distance(flight_.state().position, next->position);
        std::snprintf(line, sizeof(line), "%.0f m", range);

        if (on_screen) {
            // A reticle scaled to the ring's apparent size, so it frames the
            // checkpoint instead of hiding it.
            const float apparent = core::clampf(next->radius / core::maxf(range, 1.0f) * height * 0.5f,
                                                hud_.px(14.0f), hud_.px(260.0f));
            hud_.draw()->AddCircle(screen, apparent, tk.accent, 48, hud_.px(2.0f));
            for (int i = 0; i < 4; ++i) {
                const float angle = core::PI * 0.25f + core::PI * 0.5f * float(i);
                const ImVec2 inner(screen.x + std::cos(angle) * apparent * 0.72f,
                                   screen.y + std::sin(angle) * apparent * 0.72f);
                const ImVec2 outer(screen.x + std::cos(angle) * apparent * 1.05f,
                                   screen.y + std::sin(angle) * apparent * 1.05f);
                hud_.draw()->AddLine(inner, outer, tk.accent, hud_.px(2.0f));
            }
            hud_.label(ImVec2(screen.x + apparent + hud_.px(8.0f), screen.y - hud_.px(8.0f)), line,
                       tk.accent, 14.0f);
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
            if (ahead < 0.0f && core::length(core::Vec3{direction.x, direction.y, 0.0f}) < 1e-3f) {
                direction = core::Vec2{1.0f, 0.0f};
            }
            const float length = core::length(core::Vec3{direction.x, direction.y, 0.0f});
            if (length > 1e-4f) direction *= 1.0f / length;
            const ImVec2 centre(width * 0.5f, height * 0.5f);
            const float radius = core::minf(width, height) * 0.36f;
            hud_.edge_arrow(centre, direction, radius, tk.accent, hud_.px(11.0f));
            const ImVec2 tip(centre.x + direction.x * radius, centre.y + direction.y * radius);
            hud_.label(ImVec2(tip.x, tip.y + hud_.px(22.0f)), line, tk.accent, 14.0f, ui::Align::Centre);
        }
    }

    // ---- the top strip: timer, checkpoints, progress ----
    {
        const float strip_w = hud_.px(420.0f);
        const float strip_h = hud_.px(46.0f);
        const ImVec2 min(width * 0.5f - strip_w * 0.5f, margin);
        hud_.plate(min, ImVec2(min.x + strip_w, min.y + strip_h));
        const float mid_y = min.y + strip_h * 0.5f;
        const std::string elapsed = game::format_time(rally_.elapsed());
        hud_.numeral(ImVec2(min.x + hud_.px(16.0f), min.y + hud_.px(7.0f)), elapsed.c_str(),
                     rally_.phase() == game::RunPhase::Running ? tk.text : tk.text_dim, 30.0f);
        // Divider, checkpoint count, divider, progress bar.
        float x = min.x + hud_.px(16.0f) + hud_.numeral_width(elapsed.c_str(), 30.0f) + hud_.px(14.0f);
        hud_.draw()->AddLine(ImVec2(x, min.y + hud_.px(10.0f)), ImVec2(x, min.y + strip_h - hud_.px(10.0f)),
                             tk.plate_edge, 1.0f);
        x += hud_.px(14.0f);
        std::snprintf(line, sizeof(line), "%d / %zu", rally_.rings_passed(), course.rings.size());
        hud_.numeral(ImVec2(x, min.y + hud_.px(9.0f)), line, tk.text, 24.0f);
        hud_.label(ImVec2(x, min.y + strip_h - hud_.px(14.0f)), "checkpoints", tk.text_dim, 10.0f);
        x += hud_.numeral_width(line, 24.0f) + hud_.px(22.0f);
        const float bar_w = min.x + strip_w - hud_.px(16.0f) - x;
        if (bar_w > hud_.px(40.0f)) {
            const float progress = course.rings.empty()
                                       ? 0.0f
                                       : float(rally_.rings_passed()) / float(course.rings.size());
            hud_.bar(ImVec2(x, mid_y - hud_.px(3.0f)), bar_w, hud_.px(6.0f), progress, tk.accent);
        }

        // Below the strip: the record and the state hint.
        float y = min.y + strip_h + hud_.px(6.0f);
        if (rally_.best_time() > 0.0f) {
            std::snprintf(line, sizeof(line), "best %s", game::format_time(rally_.best_time()).c_str());
            hud_.label(ImVec2(width * 0.5f, y), line, tk.text_dim, 13.0f, ui::Align::Centre);
            y += hud_.px(18.0f);
        }
        switch (rally_.phase()) {
            case game::RunPhase::Ready:
                hud_.label(ImVec2(width * 0.5f, y), "fly through the first ring to start", tk.accent,
                           13.0f, ui::Align::Centre);
                break;
            case game::RunPhase::Finished:
                hud_.label(ImVec2(width * 0.5f, y),
                           rally_.last_run_was_record() ? "NEW RECORD  --  R to run again"
                                                        : "finished  --  R to run again",
                           rally_.last_run_was_record() ? tk.ahead : tk.text_dim, 13.0f,
                           ui::Align::Centre);
                break;
            case game::RunPhase::Running:
                break;
        }
        y += hud_.px(22.0f);

        // ---- split delta and miss flashes ----
        if (split_flash_ > 0.0f && rally_.has_ghost() && rally_.last_split_delta() != 0.0f) {
            const float delta = rally_.last_split_delta();
            std::snprintf(line, sizeof(line), "%+.2f s", delta);
            const ImU32 colour = delta < 0.0f ? tk.ahead : tk.behind;
            const float alpha = core::saturate(split_flash_ / 1.6f);
            const ImU32 faded = (colour & 0x00FFFFFF) | (ImU32(alpha * 240.0f) << 24);
            hud_.numeral(ImVec2(width * 0.5f, y), line, faded, 28.0f, ui::Align::Centre);
            y += hud_.px(32.0f);
        }
        if (miss_flash_ > 0.0f) {
            std::snprintf(line, sizeof(line), "missed by %.0f m", rally_.last_miss_distance());
            const float alpha = core::saturate(miss_flash_ / 1.2f);
            const ImU32 faded = (tk.behind & 0x00FFFFFF) | (ImU32(alpha * 240.0f) << 24);
            hud_.label(ImVec2(width * 0.5f, y), line, faded, 15.0f, ui::Align::Centre);
        }
    }
}

void App::build_rally_ui() {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 402.0f, 52.0f),
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

    {
        bool on = autopilot_;
        if (ImGui::Checkbox("autopilot (P)", &on)) set_autopilot(on);
    }
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
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 402.0f, 92.0f),
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
            ImGui::SliderFloat("extra lift", &rig.ground_lift_m, -1.0f, 2.0f, "%.2f m");
            ImGui::SliderFloat("plant limbs on terrain", &rig.ground_ik, 0.0f, 1.0f);
            ImGui::SliderFloat("stride length", &rig.stride_length_m, 0.5f, 6.0f, "%.1f m");
            ImGui::SliderFloat("stride lift", &rig.stride_lift_m, 0.0f, 2.0f, "%.2f m");
            ImGui::SliderFloat("standing speed", &rig.standing_speed, 1.0f, 20.0f, "%.0f m/s");
            ImGui::SliderFloat("terrain tilt limit", &rig.ground_ik_tilt_max_deg, 0.0f, 30.0f,
                               "%.0f deg");
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
        if (scenario == game::StudioScenario::Melee || scenario == game::StudioScenario::Claw ||
            scenario == game::StudioScenario::Tail || scenario == game::StudioScenario::Attack) {
            ImGui::SeparatorText("whole-body melee");
            ImGui::SliderFloat("wind-up share", &rig.gesture_anticipation, 0.0f, 0.6f);
            ImGui::SliderFloat("bite body pitch", &rig.gesture_body_pitch_deg, 0.0f, 30.0f, "%.0f deg");
            ImGui::SliderFloat("strike body roll", &rig.gesture_body_roll_deg, 0.0f, 45.0f, "%.0f deg");
            ImGui::SliderFloat("strike body turn", &rig.gesture_body_yaw_deg, 0.0f, 60.0f, "%.0f deg");
            ImGui::SliderFloat("body reach", &rig.gesture_surge_m, 0.0f, 3.0f, "%.2f m");
            ImGui::SliderFloat("claw body shift", &rig.gesture_sway_m, 0.0f, 2.0f, "%.2f m");
            ImGui::SliderFloat("wing brace", &rig.gesture_wing_deg, 0.0f, 35.0f, "%.0f deg");
            ImGui::SliderFloat("neck counter", &rig.gesture_neck_deg, 0.0f, 40.0f, "%.0f deg");
            ImGui::SliderFloat("tail counter", &rig.gesture_tail_counter_deg, 0.0f, 60.0f, "%.0f deg");
            ImGui::SliderFloat("bite duration", &rig.bite_duration, 0.2f, 1.2f, "%.2f s");
            ImGui::SliderFloat("claw duration", &rig.claw_duration, 0.2f, 1.2f, "%.2f s");
            ImGui::SliderFloat("tail duration", &rig.tail_duration, 0.3f, 1.4f, "%.2f s");
            ImGui::SliderFloat("claw reach", &rig.claw_swing_deg, 0.0f, 110.0f, "%.0f deg");
            ImGui::SliderFloat("claw spread", &rig.claw_out_deg, 0.0f, 60.0f, "%.0f deg");
            ImGui::SliderFloat("tail whip", &rig.tail_whip_deg, 0.0f, 100.0f, "%.0f deg");
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
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 402.0f, 132.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390, 0), ImGuiCond_FirstUseEver);
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
        refresh_player_element();
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
        ImGui::SliderFloat("brake body pitch", &rig.brake_body_pitch_deg, 0.0f, 30.0f, "%.0f deg");
        ImGui::SliderFloat("brake raise", &rig.brake_raise_deg, 0.0f, 40.0f, "%.0f deg");
        ImGui::SliderFloat("brake protract", &rig.brake_protract_deg, 0.0f, 40.0f, "%.0f deg");
        ImGui::SliderFloat("brake tail drop", &rig.brake_tail_drop_deg, 0.0f, 40.0f, "%.0f deg");
        ImGui::SliderFloat("brake bank relief", &rig.brake_bank_relief, 0.0f, 1.0f);
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
        ImGui::SliderFloat("brake/dive leg sway", &rig.leg_posture_sway, 0.0f, 1.0f);
        ImGui::SliderFloat("dive leg trail", &rig.leg_dive_trail_deg, -30.0f, 45.0f, "%.0f deg");
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
            t.aggression = 0.35f;
            bot_health_ = 60.0f;
            combat_.tuning.hostile_regen = 0.0f;
            break;
        default:  // veteran
            t.reaction_interval = 0.30f;
            t.aim_spread_deg = 2.5f;
            t.fire_cooldown = 1.6f;
            t.aggression = 0.55f;
            bot_health_ = 80.0f;
            combat_.tuning.hostile_regen = 4.0f;
            break;
        case 2:  // ace: tough, and refuses to stay wounded.
            t.reaction_interval = 0.15f;
            t.aim_spread_deg = 1.2f;
            t.fire_cooldown = 1.1f;
            t.damage = 14.0f;
            t.fire_range = 650.0f;
            t.aggression = 0.8f;
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
    // A model without an idle must CLEAR the clip, not leave the previous
    // model's in place: the rig samples a clip by joint index, and another
    // skeleton's indices are a different creature's bones.
    if (model.idle_clip < 0 || size_t(model.idle_clip) >= model.animations.size()) {
        rig.set_base_clip(nullptr);
        return;
    }
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
    // Measure the head and the neck for the first-person eye, in body metres
    // relative to the head joint, bind pose. A vertex belongs to a part when
    // most of its weight is on that part's bones: for the head, the head
    // joint and everything under it (jaw, horns, crest); for the neck, the
    // neck chain and whatever hangs off it that is not the head. A pose can
    // pitch these but not resize them.
    if (out.joints.head != anim::NO_PARENT) {
        const int joint_count = out.skeleton.count();
        std::vector<bool> under_head(size_t(joint_count), false);
        std::vector<bool> under_neck(size_t(joint_count), false);
        under_head[size_t(out.joints.head)] = true;
        for (int neck : out.joints.neck) under_neck[size_t(neck)] = true;
        for (int j = 0; j < joint_count; ++j) {
            const int parent = out.skeleton.joint(j).parent;
            if (parent == anim::NO_PARENT) continue;
            if (under_head[size_t(parent)]) under_head[size_t(j)] = true;
            else if (under_neck[size_t(parent)] && !under_head[size_t(j)]) under_neck[size_t(j)] = true;
        }
        const core::Mat4 to_body = out.asset.matrix();
        const core::Vec3 head = core::transform_point(
            to_body, out.skeleton.world_bind(out.joints.head).translation_part());

        // Body-space positions of the vertices that belong to a part.
        const auto collect = [&](const std::vector<bool>& mask) {
            std::vector<core::Vec3> points;
            for (const anim::SkinnedVertex& v : mesh_data.vertices) {
                float weight = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    if (v.joints[k] < mask.size() && mask[v.joints[k]]) weight += v.weights[k];
                }
                if (weight >= 0.5f) points.push_back(core::transform_point(to_body, v.position) - head);
            }
            return points;
        };
        const auto profile = [](const std::vector<core::Vec3>& points, LoadedModel::TopProfile& out_profile,
                                core::Vec3* lo_out, core::Vec3* hi_out) {
            if (points.empty()) return;
            core::Vec3 lo{1e9f, 1e9f, 1e9f};
            core::Vec3 hi{-1e9f, -1e9f, -1e9f};
            for (const core::Vec3& p : points) {
                lo = core::Vec3{core::minf(lo.x, p.x), core::minf(lo.y, p.y), core::minf(lo.z, p.z)};
                hi = core::Vec3{core::maxf(hi.x, p.x), core::maxf(hi.y, p.y), core::maxf(hi.z, p.z)};
            }
            out_profile.z0 = lo.z;
            out_profile.z1 = hi.z;
            for (float& top : out_profile.top) top = lo.y;
            const float length = core::maxf(hi.z - lo.z, 1e-3f);
            for (const core::Vec3& p : points) {
                const int bin = std::clamp(int((p.z - lo.z) / length * LoadedModel::TopProfile::BINS), 0,
                                           LoadedModel::TopProfile::BINS - 1);
                out_profile.top[bin] = core::maxf(out_profile.top[bin], p.y);
            }
            out_profile.valid = true;
            if (lo_out) *lo_out = lo;
            if (hi_out) *hi_out = hi;
        };

        profile(collect(under_head), out.head_profile, &out.head_box_min, &out.head_box_max);
        out.head_box_valid = out.head_profile.valid;
        profile(collect(under_neck), out.neck_profile, nullptr, nullptr);
        if (out.head_box_valid) {
            LOG_INFO("%s: head mesh %.2f tall, %.2f long; top %+.2f, rear %+.2f from the head joint%s",
                     out.path.c_str(), out.head_box_max.y - out.head_box_min.y,
                     out.head_box_max.z - out.head_box_min.z, out.head_box_max.y, out.head_box_max.z,
                     out.neck_profile.valid ? "; neck measured" : "");
        }
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
    out.breath_file = game::load_breath_profile(out.breath, out.breath_path.c_str());
    return true;
}

// Re-points the player at another roster entry. Both rigs hold spring state
// indexed by joint, so they cannot simply be told about a different skeleton:
// they are re-initialised, which also resets the chains to their rest pose.
void App::set_player_model(int index) {
    if (index < 0 || size_t(index) >= models_.size() || index == player_model_) return;
    player_model_ = index;
    first_person_offsets_valid_ = false;  // a new head: snap the eye to it, do not glide from the old one
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
    refresh_player_element();
    LOG_INFO("player model: [%d] %s", index, model.path.c_str());
}

std::unique_ptr<App::BotShip> App::make_bot(int index) {
    auto bot = std::make_unique<BotShip>();
    bot->slot = combat_.spawn_external(bot_health_, 6.5f);
    // Deal the roster round-robin, skipping the player's own entry when
    // there is anything else to fly. With a one-model roster this is the
    // player's model for everyone, exactly as it was before.
    bot->model = models_.size() > 1
                     ? int((size_t(player_model_) + 1 + size_t(index)) % models_.size())
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
    bot->hue = palette[size_t(index) % 4];
    // Its element: the species' when the species has one, rolled otherwise,
    // and then the hide leans toward it so "the frost one" is a thing to say.
    bot->element = worn.breath_file ? worn.breath.element
                                    : roll_element(uint32_t(index) * 2654435761u + 0x51u);
    if (!worn.breath_file) bot->hue = game::element_hide(bot->element);
    bot->breath = breath_for(bot->element, bot->model);
    combat_.sentinels()[size_t(bot->slot)].element = bot->element;
    // An ace pounces, in the arena as in the run: the player's pounce is
    // strong enough there that the best rivals should answer it.
    bot->can_pounce = bot_skill_ >= 2;
    bot->last_health = bot_health_;
    LOG_INFO("bot %d: %s", index, worn.path.c_str());
    return bot;
}

void App::spawn_bots(int count) {
    combat_.clear_hostiles();
    bots_.clear();
    for (int i = 0; i < count; ++i) {
        auto bot = make_bot(i);
        place_bot(*bot, uint32_t(20260826 + i * 977));
        bots_.push_back(std::move(bot));
    }
}

// ---- props ----

bool App::load_prop(const char* path, PropModel& out, const char* tag,
                    std::vector<anim::AnimationClip>* clips) {
    anim::SkinnedMeshData data;
    const anim::GltfLoadResult loaded = anim::load_skinned_gltf(path, out.skeleton, data);
    if (loaded.ok && clips) *clips = loaded.animations;
    if (!loaded.ok) {
        LOG_WARN("prop %s: %s (the run falls back to the placeholder)", path, loaded.error.c_str());
        return false;
    }
    if (!out.mesh.upload(device_.gpu(), data, tag)) return false;
    for (size_t i = 0; i < loaded.textures.size(); ++i) {
        const bool srgb = i < loaded.texture_srgb.size() && loaded.texture_srgb[i] != 0;
        const std::string name = std::string(tag) + "_" + std::to_string(i);
        out.textures.push_back(
            gfx::create_texture_from_image(device_.gpu(), loaded.textures[i], name.c_str(), srgb));
    }
    out.joints.assign(size_t(std::max(out.skeleton.count(), 1)), core::Mat4::identity());
    out.ok = true;
    LOG_INFO("prop %s: %zu triangles, %d joint(s), %zu texture(s), %.1f x %.1f x %.1f m", path,
             loaded.triangle_count, loaded.joint_count, loaded.textures.size(),
             double(loaded.bounds_max.x - loaded.bounds_min.x),
             double(loaded.bounds_max.y - loaded.bounds_min.y),
             double(loaded.bounds_max.z - loaded.bounds_min.z));
    return true;
}

void App::release_prop(PropModel& prop) {
    prop.mesh.release(device_.gpu());
    for (SDL_GPUTexture* texture : prop.textures) {
        if (texture) SDL_ReleaseGPUTexture(device_.gpu(), texture);
    }
    prop.textures.clear();
    prop.ok = false;
}

// ---- the demo pilot ----

void App::set_autopilot(bool on) {
    autopilot_ = on;
    demo_.reset(uint32_t(frame_index_) * 2654435761u + 11u);
    demo_restart_timer_ = 0.0f;
}

// What the demo pilot can see: every live target, where to go, and in a run
// the next cache worth landing on and the tower guarding it.
void App::build_demo_world(game::DemoWorld& world) {
    const game::FlightState& self = flight_.state();
    world.terrain = &terrain_;
    world.health_fraction = combat_.health_fraction();
    world.locked_slot = combat_.locked_index();
    world.lock_cone_deg = combat_.tuning.lock_cone_deg;
    world.lock_range = combat_.tuning.lock_range;
    world.can_pounce = combat_.abilities.ram && combat_.boost_cooldown() <= 0.0f;
    world.pounce_range = combat_.tuning.pounce_range;
    world.pounce_cone_deg = combat_.tuning.pounce_cone_deg;
    world.targets.clear();
    const auto& sentinels = combat_.sentinels();
    for (size_t i = 0; i < sentinels.size(); ++i) {
        const game::Sentinel& s = sentinels[i];
        if (!s.alive || s.prey) continue;  // the herd is the hunt's, not a fight
        game::DemoTarget t;
        t.position = s.position;
        t.velocity = s.velocity;
        t.slot = int(i);
        t.health = s.max_health > 0.0f ? s.health / s.max_health : 1.0f;
        t.kind = s.ground ? game::DemoTargetKind::Tower : game::DemoTargetKind::Drone;
        if (s.external) {
            t.kind = game::DemoTargetKind::Rival;
            for (const auto& bot : bots_) {
                if (bot->slot != int(i)) continue;
                if (bot->hunter) t.kind = game::DemoTargetKind::Hunter;
                t.dormant = run_mode_ && bot->dormant;
            }
        }
        world.targets.push_back(t);
    }

    world.in_run = run_mode_ && hoard_run_.phase() == game::HoardPhase::Flying;
    if (run_mode_ && hoard_run_.phase() != game::HoardPhase::Idle) {
        const game::RunLayout& layout = hoard_run_.layout();
        world.waypoint = hoard_run_.next_waypoint(self.position);
        world.cache_radius = hoard_run_.settings.cache_radius;
        {
            // Back up the corridor a stretch, high over the floor.
            const float f = core::maxf(layout.fraction_at(self.position) - 0.12f, 0.0f);
            const size_t n = layout.spine.size();
            const size_t i = std::min(n - 1, size_t(f * float(n - 1)));
            world.safe_point = layout.spine[i] + core::Vec3{0.0f, 220.0f, 0.0f};
        }
        const int meal = prey_.nearest(self.position, 2000.0f);
        if (meal >= 0) {
            const game::Prey& p = prey_.animals()[size_t(meal)];
            world.has_prey = true;
            world.prey = p.position;
            world.prey_velocity = p.heading * p.speed;
        }
        // The next uncollected cache AHEAD down the corridor: the nearest one
        // can be behind, and turning back for it is not how a run flows.
        const float here = layout.fraction_at(self.position);
        int best = -1;
        float best_fraction = 1e9f;
        for (size_t c = 0; c < layout.caches.size(); ++c) {
            if (layout.caches[c].collected) continue;
            const float f = layout.fraction_at(layout.caches[c].position);
            if (f < here - 0.12f || f >= best_fraction) continue;
            best = int(c);
            best_fraction = f;
        }
        // Take a cache, then bank it. Repeated sieges, hunts and climb-outs
        // spent the whole first valley's hunter budget without any transit.
        // The pressure deadline still bounds unsuccessful cache attempts.
        world.rush = world.in_run &&
                     (hoard_run_.caches_collected() >= demo_.tuning.caches_before_rush ||
                      best < 0 || hoard_run_.elapsed() > hoard_run_.pressure_start());
        if (world.rush) best = -1;
        if (best >= 0) {
            world.has_cache = true;
            world.cache = layout.caches[size_t(best)].position;
            for (size_t i = 0; i < layout.defences.size() && i < run_defence_slots_.size(); ++i) {
                if (layout.defences[i].guards != best) continue;
                for (size_t k = 0; k < world.targets.size(); ++k) {
                    if (world.targets[k].slot == run_defence_slots_[i]) world.cache_guard = int(k);
                }
            }
        }
        return;
    }

    // The arena: patrol up and down the valley, turning back short of the
    // ends, and let the fight find the targets.
    const float extent = terrain_settings_.half_extent;
    if (self.position.z * patrol_sign_ > extent * 0.55f) patrol_sign_ = -patrol_sign_;
    const float z = core::clampf(self.position.z + patrol_sign_ * 600.0f, -extent * 0.7f, extent * 0.7f);
    const float x = terrain_.valley_center_x(z);
    world.waypoint = core::Vec3{x, terrain_.height_at(x, z) + 180.0f, z};
    world.safe_point = core::Vec3{self.position.x, terrain_.height_at(self.position.x, self.position.z) + 300.0f,
                                  self.position.z - patrol_sign_ * 400.0f};
}

// ---- the run ----

uint32_t App::fresh_seed() const {
    // The clock, folded: the design wants a random valley when none is named,
    // and the seed is printed so a good one can be flown again.
    return (uint32_t(SDL_GetTicksNS() / 1000000ull) * 2654435761u) | 1u;
}

void App::start_run(uint32_t seed) {
    if (!run_mode_) {
        // Entering from the arena: remember what to restore.
        arena_terrain_ = terrain_settings_;
        capture_growth_base();
    }
    run_mode_ = true;
    combat_enabled_ = true;
    match_.abandon();
    last_kill_element_ = game::Element::None;
    using_second_ = false;
    second_unlocked_ = false;
    arena_breath_index_ = 0;
    // Back to the dragon's own breath: clearing the flags alone left the
    // last run's second element in combat -- a fire dragon restarted as frost.
    refresh_player_element();
    run_seed_ = seed;
    run_seed_input_ = int(seed & 0x7fffffffu);
    last_run_record_ = false;
    collect_flash_ = hunter_flash_ = award_flash_ = grew_flash_ = 0.0f;

    // The seed's valley: its kind reshapes the terrain and the encounter mix,
    // both derived from the arena valley and the panel's dials.
    game::HoardRunSettings settings = run_dials_;
    settings.seed = seed;
    game::TerrainSettings terrain = arena_terrain_;
    const game::ValleyKind kind = game::apply_valley_kind(seed, terrain, settings);
    if (options_.run_empty) {
        settings.rivals = 0;
        settings.defences = 0;
        settings.guards = false;
        settings.max_hunters = 0;
    }
    if (terrain_run_seed_ != seed) {
        terrain_settings_ = terrain;
        regenerate_terrain();
        terrain_run_seed_ = seed;
    }
    hoard_run_.settings = settings;
    hoard_run_.start(terrain_, terrain_settings_.half_extent);
    const game::RunLayout& layout = hoard_run_.layout();

    // Every run starts as a drake -- at a drake's size at once, not shrinking
    // into it.
    apply_growth(0.0f);
    growth_scale_ = growth_scale_target_;
    wing_growth_ = wing_growth_target_;
    dragon_rig_.wing_growth = wing_growth_;
    populate_valley(seed, 0);
    LOG_INFO("run: seed %u, a %s, %.1f km of corridor, %zu caches, %zu towers, %zu rivals, "
             "first hunter at %.0f s",
             seed, game::valley_kind_name(kind), double(layout.length() / 1000.0f),
             layout.caches.size(), layout.defences.size(), layout.rivals.size(),
             double(hoard_run_.pressure_start()));
    // Where everything stands, for placing a capture camera on it.
    for (size_t i = 0; i < layout.caches.size(); ++i) {
        const core::Vec3 p = layout.caches[i].position;
        LOG_INFO("  cache %zu at %.0f,%.0f,%.0f worth %.0f", i, double(p.x), double(p.y), double(p.z),
                 double(layout.caches[i].value));
    }
    for (size_t i = 0; i < layout.defences.size(); ++i) {
        const core::Vec3 p = layout.defences[i].position;
        LOG_INFO("  tower %zu at %.0f,%.0f,%.0f%s", i, double(p.x), double(p.y), double(p.z),
                 layout.defences[i].guards >= 0 ? "  (guard)" : "");
    }
    for (size_t i = 0; i < layout.rivals.size(); ++i) {
        const core::Vec3 p = layout.rivals[i].position;
        LOG_INFO("  rival %zu at %.0f,%.0f,%.0f", i, double(p.x), double(p.y), double(p.z));
    }
    {
        const core::Vec3 p = layout.gate.position;
        LOG_INFO("  gate at %.0f,%.0f,%.0f  start at %.0f,%.0f,%.0f", double(p.x), double(p.y),
                 double(p.z), double(layout.start.x), double(layout.start.y), double(layout.start.z));
    }
}

// The valley's encounters, from its layout: towers (the keep beside each
// cache, the spire on the slopes, each of a rolled element and tougher per
// valley deeper), rivals at their posts, the herds, and the dragon at the
// head. No drones -- the first build left combat's default wave of five in.
void App::populate_valley(uint32_t seed, int depth) {
    const game::RunLayout& layout = hoard_run_.layout();
    bots_.clear();
    combat_.reset(&terrain_, layout.spine[layout.spine.size() / 2], seed, 0);
    combat_.hostile_damage_scale = 1.0f + hoard_run_.settings.depth_enemy_damage * float(depth);
    run_defence_slots_.clear();
    for (size_t i = 0; i < layout.defences.size(); ++i) {
        const game::RunDefence& defence = layout.defences[i];
        // The layout's point is 8 m up the old tower; the model stands on
        // the ground under it, and is hit round its middle.
        const core::Vec3 base = defence.position - core::Vec3{0.0f, 8.0f, 0.0f};
        const bool spire = defence_is_spire(int(i));
        const float middle = spire ? SPIRE_MIDDLE : KEEP_MIDDLE;
        const float top = spire ? SPIRE_ORB_Y : KEEP_BRAZIER_Y;
        const game::Element element = roll_element(seed * 40503u + uint32_t(i) * 2654435761u + 3u);
        const int slot = combat_.spawn_defence(base + core::Vec3{0.0f, middle, 0.0f},
                                               spire ? 7.0f : 9.0f, top - middle, element);
        game::Sentinel& tower = combat_.sentinels()[size_t(slot)];
        tower.max_health = tower.health =
            combat_.tuning.defence_health * (1.0f + hoard_run_.settings.depth_enemy_health * float(depth));
        run_defence_slots_.push_back(slot);
    }
    for (size_t i = 0; i < layout.rivals.size(); ++i) spawn_rival(int(i), false);
    run_alive_.assign(combat_.sentinels().size(), 1);
    prey_.reset(layout.herds, terrain_, seed * 7u + 101u);
    prey_slots_.clear();
    for (const game::Prey& p : prey_.animals()) {
        const int slot = combat_.spawn_external(prey_.tuning.health, prey_.tuning.body());
        game::Sentinel& s = combat_.sentinels()[size_t(slot)];
        s.prey = true;
        s.passive = true;
        s.element = game::Element::None;
        combat_.drive_external(slot, p.position + core::Vec3{0.0f, 1.5f * prey_.tuning.scale, 0.0f},
                               core::Vec3::zero());
        prey_slots_.push_back(slot);
    }
    run_alive_.assign(combat_.sentinels().size(), 1);
    for (const game::RunHerd& herd : layout.herds) {
        LOG_INFO("  herd of %d at %.0f,%.0f,%.0f", herd.count, double(herd.position.x),
                 double(herd.position.y), double(herd.position.z));
    }
    respawn_dragon();  // at the layout's start, facing down the corridor; full health
}

bool App::defence_is_spire(int defence) const {
    const auto& defences = hoard_run_.layout().defences;
    if (defence < 0 || size_t(defence) >= defences.size()) return false;
    // Guards are keeps; slope towers alternate, so a valley shows both.
    return spire_prop_.ok && defences[size_t(defence)].guards < 0 && defence % 2 == 1;
}

// Across a pass that is not the last: the next valley of the descent, a
// different kind, deeper and harder, the dragon as grown as it was.
void App::advance_valley() {
    if (!hoard_run_.awaiting_valley()) return;
    const int depth = hoard_run_.valley() + 1;
    const uint32_t seed = game::valley_seed(run_seed_, depth);
    game::HoardRunSettings settings = run_dials_;
    settings.seed = seed;
    game::TerrainSettings terrain = arena_terrain_;
    const game::ValleyKind kind = game::apply_valley_kind(seed, terrain, settings);
    game::apply_depth(depth, settings);
    if (options_.run_empty) {
        settings.rivals = 0;
        settings.defences = 0;
        settings.guards = false;
        settings.max_hunters = 0;
    }
    terrain_settings_ = terrain;
    regenerate_terrain();
    terrain_run_seed_ = seed;
    hoard_run_.settings = settings;
    hoard_run_.next_valley(terrain_, terrain_settings_.half_extent);
    populate_valley(seed, depth);
    if (autopilot_) demo_.reset(seed * 3u + 1u);
    valley_flash_ = 5.0f;
    audio_.play(audio::Clip::Boost, 1.0f, 0.6f);
    const game::RunLayout& layout = hoard_run_.layout();
    LOG_INFO("run: valley %d of %d, seed %u, a %s: %zu caches, %zu towers, %zu rivals, %zu herds; "
             "banked %.0f so far",
             depth + 1, settings.valleys, seed, game::valley_kind_name(kind), layout.caches.size(),
             layout.defences.size(), layout.rivals.size(), layout.herds.size(),
             double(hoard_run_.banked()));
}

// ---- elements ----

void App::refresh_player_element() {
    const LoadedModel& model = player_model();
    const game::Element species = model.breath.element;
    game::Element element = player_element_choice_ >= 0 && player_element_choice_ < game::ELEMENT_COUNT
                                ? game::Element(player_element_choice_)
                                : species;
    // The second breath, learned as an adult: U swaps to it and back.
    if (second_unlocked_ && using_second_) element = second_element_;
    player_breath_ = element == species ? model.breath : element_breaths_[int(element)];
    combat_.player_element = element;
    combat_.player_breath = player_breath_.scales;
}

const game::BreathProfile& App::breath_for(game::Element element, int model) const {
    if (model >= 0 && size_t(model) < models_.size() && models_[size_t(model)]->breath.element == element) {
        return models_[size_t(model)]->breath;
    }
    const int e = int(element);
    return element_breaths_[e >= 0 && e < game::ELEMENT_COUNT ? e : 0];
}

game::Element App::roll_element(uint32_t seed) const {
    uint32_t x = seed + 0x632be5abu;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return game::Element(int(x % uint32_t(game::ELEMENT_COUNT)));
}

// What a status looks like on a body: particles for each one it carries,
// in the element's own motion -- flame licks rise, frost motes sink, blight
// drips, sparks crawl, water runs off, grit puffs. All additive, so nothing
// here is dark: smoke would need a second particle pass.
void App::emit_status(core::Vec3 centre, float radius, const game::Status& status, float dt) {
    auto count = [&](float rate) {
        const float want = rate * dt;
        int n = int(want);
        if (0.5f + 0.5f * particle_unit() < want - float(n)) ++n;
        return n;
    };
    auto in_body = [&](float scale) {
        return centre + core::Vec3{particle_unit(), particle_unit() * 0.7f, particle_unit()} * (radius * scale);
    };
    auto on_body = [&]() {
        const core::Vec3 d = core::normalize_or(
            core::Vec3{particle_unit(), particle_unit(), particle_unit()}, core::Vec3::up());
        return centre + d * radius;
    };
    const float size = core::clampf(radius / 6.0f, 0.5f, 3.0f);
    if (status.burn > 0.0f) {
        for (int i = count(70.0f * size); i > 0; --i) {
            gfx::Particle p;
            p.position = in_body(0.8f);
            p.velocity = core::Vec3{particle_unit() * 1.5f, 6.0f + 3.0f * particle_unit(), particle_unit() * 1.5f};
            p.acceleration = core::Vec3{0.0f, 5.0f, 0.0f};
            p.drag = 1.5f;
            p.life = 0.45f + 0.15f * particle_unit();
            p.size_start = 1.5f * size;
            p.size_end = 0.3f * size;
            p.color_start = core::Vec3{2.2f, 0.95f, 0.25f};
            p.color_end = core::Vec3{0.8f, 0.14f, 0.03f};
            p.brightness = 0.95f;
            particles_.spawn(p);
        }
    }
    if (status.frozen > 0.0f) {
        // The ice shell: short-lived glints hugging the body, so it follows
        // a falling dragon without a mesh of its own.
        for (int i = count(150.0f * size); i > 0; --i) {
            gfx::Particle p;
            p.position = on_body();
            p.velocity = core::Vec3{particle_unit(), particle_unit(), particle_unit()} * 0.4f;
            p.life = 0.3f;
            p.size_start = 1.3f * size;
            p.size_end = 0.9f * size;
            p.color_start = core::Vec3{0.7f, 1.4f, 2.2f};
            p.color_end = core::Vec3{0.25f, 0.6f, 1.5f};
            p.brightness = 0.55f;
            particles_.spawn(p);
        }
    }
    if (status.chill > 0.05f || status.frozen > 0.0f) {
        const float chill = status.frozen > 0.0f ? 1.0f : status.chill;
        for (int i = count(55.0f * chill * size); i > 0; --i) {
            gfx::Particle p;
            p.position = in_body(1.0f);
            p.velocity = core::Vec3{particle_unit(), -1.0f, particle_unit()} * 1.5f;
            p.acceleration = core::Vec3{0.0f, -3.0f, 0.0f};
            p.drag = 0.8f;
            p.life = 1.0f;
            p.size_start = 0.8f * size;
            p.size_end = 0.2f * size;
            p.color_start = core::Vec3{0.6f, 1.2f, 2.0f};
            p.color_end = core::Vec3{0.1f, 0.3f, 0.9f};
            p.brightness = 0.7f;
            particles_.spawn(p);
        }
    }
    if (status.corrode > 0.0f) {
        for (int i = count(40.0f * size); i > 0; --i) {
            gfx::Particle p;
            const bool bubble = particle_unit() > 0.3f;
            p.position = in_body(0.8f);
            p.velocity = bubble ? core::Vec3{particle_unit(), 2.0f, particle_unit()}
                                : core::Vec3{particle_unit(), -2.0f, particle_unit()};
            p.acceleration = core::Vec3{0.0f, bubble ? 0.5f : -9.0f, 0.0f};
            p.drag = 1.0f;
            p.life = bubble ? 0.6f : 0.8f;
            p.size_start = (bubble ? 0.5f : 0.9f) * size;
            p.size_end = (bubble ? 1.5f : 0.4f) * size;
            p.color_start = core::Vec3{1.2f, 1.8f, 0.3f};
            p.color_end = core::Vec3{0.2f, 0.45f, 0.05f};
            p.brightness = 0.75f;
            particles_.spawn(p);
        }
    }
    if (status.shock > 0.0f) {
        for (int i = count(60.0f * size); i > 0; --i) {
            gfx::Particle p;
            p.position = on_body();
            p.velocity = core::Vec3{particle_unit(), particle_unit(), particle_unit()} * 14.0f;
            p.drag = 6.0f;
            p.life = 0.15f;
            p.size_start = 0.7f * size;
            p.size_end = 0.1f;
            p.color_start = core::Vec3{1.6f, 1.2f, 2.6f};
            p.color_end = core::Vec3{0.5f, 0.2f, 1.4f};
            p.brightness = 1.3f;
            particles_.spawn(p);
        }
        // A crawling arc across the body now and then.
        if (particle_unit() > 1.0f - 2.0f * 8.0f * dt) {
            emit_arc(on_body(), on_body(), game::element_colour(game::Element::Storm));
        }
    }
    if (status.drench > 0.0f) {
        for (int i = count(45.0f * size); i > 0; --i) {
            gfx::Particle p;
            p.position = in_body(0.9f) + core::Vec3{0.0f, radius * 0.3f, 0.0f};
            p.velocity = core::Vec3{particle_unit() * 1.5f, -1.0f, particle_unit() * 1.5f};
            p.acceleration = core::Vec3{0.0f, -14.0f, 0.0f};
            p.life = 0.6f;
            p.size_start = 0.5f * size;
            p.size_end = 0.3f * size;
            p.color_start = core::Vec3{0.3f, 1.3f, 1.1f};
            p.color_end = core::Vec3{0.05f, 0.4f, 0.35f};
            p.brightness = 0.55f;
            particles_.spawn(p);
        }
    }
    if (status.stagger > 0.2f) {
        for (int i = count(28.0f * status.stagger * size); i > 0; --i) {
            gfx::Particle p;
            p.position = in_body(0.9f);
            p.velocity = core::Vec3{particle_unit() * 3.0f, 1.0f, particle_unit() * 3.0f};
            p.drag = 2.0f;
            p.life = 0.7f;
            p.size_start = 1.2f * size;
            p.size_end = 2.6f * size;
            p.color_start = core::Vec3{1.0f, 0.7f, 0.4f};
            p.color_end = core::Vec3{0.25f, 0.16f, 0.08f};
            p.brightness = 0.35f;
            particles_.spawn(p);
        }
    }
}

// A lightning bolt: a jagged chain of bright points between two bodies,
// struck for a tenth of a second. Re-struck, it flickers the way lightning does.
void App::emit_arc(core::Vec3 from, core::Vec3 to, core::Vec3 colour) {
    const core::Vec3 span = to - from;
    const float length = core::length(span);
    if (length < 0.5f) return;
    const core::Vec3 axis = span / length;
    const core::Vec3 side = core::normalize_or(core::cross(axis, core::Vec3::up()), core::Vec3::right());
    const core::Vec3 lift = core::cross(side, axis);
    const int kinks = std::clamp(int(length / 7.0f), 4, 20);
    core::Vec3 previous = from;
    for (int k = 1; k <= kinks; ++k) {
        const float t = float(k) / float(kinks);
        const float amplitude = k == kinks ? 0.0f : length * 0.06f * std::sin(t * core::PI);
        const core::Vec3 next = from + span * t + (side * particle_unit() + lift * particle_unit()) * amplitude;
        const float piece = core::distance(previous, next);
        const int dots = std::max(1, int(piece / 1.2f));
        for (int d = 0; d < dots; ++d) {
            gfx::Particle p;
            p.position = core::lerp(previous, next, float(d) / float(dots));
            p.life = 0.12f;
            p.size_start = 1.2f;
            p.size_end = 0.5f;
            p.color_start = core::Vec3{1.5f, 1.3f, 2.6f};
            p.color_end = colour;
            p.brightness = 1.5f;
            particles_.spawn(p);
        }
        previous = next;
    }
}

// A status landing hard: ice shattering, rock bursting, steam off a doused
// flame, a flare as something catches.
void App::emit_status_burst(const game::StatusBurst& burst) {
    const core::Vec3 at = burst.on_player ? flight_.state().position : burst.position;
    const game::StatusReport& r = burst.report;
    auto spray = [&](int n, float speed, float up, float gravity, float life, float s0, float s1,
                     core::Vec3 hot, core::Vec3 cool, float brightness, float drag) {
        for (int i = 0; i < n; ++i) {
            gfx::Particle p;
            p.position = at + core::Vec3{particle_unit(), particle_unit(), particle_unit()} * 2.0f;
            p.velocity = core::normalize_or(core::Vec3{particle_unit(), particle_unit(), particle_unit()},
                                            core::Vec3::up()) * (speed * (0.5f + 0.5f * std::fabs(particle_unit()))) +
                         core::Vec3{0.0f, up, 0.0f};
            p.acceleration = core::Vec3{0.0f, -gravity, 0.0f};
            p.drag = drag;
            p.life = life * (0.8f + 0.3f * particle_unit());
            p.size_start = s0;
            p.size_end = s1;
            p.color_start = hot;
            p.color_end = cool;
            p.brightness = brightness;
            particles_.spawn(p);
        }
    };
    if (r.froze) {
        spray(70, 20.0f, 2.0f, 16.0f, 0.9f, 1.1f, 0.3f, {0.7f, 1.4f, 2.3f}, {0.2f, 0.5f, 1.4f}, 0.9f, 1.0f);
        spray(1, 0.0f, 0.0f, 0.0f, 0.3f, 6.0f, 16.0f, {0.6f, 1.1f, 2.0f}, {0.1f, 0.2f, 0.6f}, 0.8f, 0.0f);
    }
    if (r.staggered) {
        spray(45, 15.0f, 6.0f, 22.0f, 0.9f, 1.3f, 0.6f, {1.2f, 0.8f, 0.4f}, {0.3f, 0.18f, 0.08f}, 0.5f, 1.2f);
    }
    if (r.doused) {
        spray(40, 3.0f, 6.0f, -2.0f, 1.4f, 2.0f, 7.0f, {0.45f, 0.5f, 0.55f}, {0.1f, 0.12f, 0.14f}, 0.4f, 1.2f);
    }
    if (r.burned) {
        spray(24, 9.0f, 4.0f, -3.0f, 0.5f, 1.6f, 0.4f, {2.3f, 1.0f, 0.3f}, {0.9f, 0.15f, 0.03f}, 1.0f, 2.0f);
    }
    if (r.corroded) {
        spray(26, 8.0f, 1.0f, 14.0f, 0.8f, 1.0f, 0.4f, {1.2f, 1.8f, 0.3f}, {0.2f, 0.45f, 0.05f}, 0.8f, 1.2f);
    }
    if (r.drenched) {
        spray(30, 10.0f, 2.0f, 18.0f, 0.7f, 0.8f, 0.4f, {0.3f, 1.3f, 1.1f}, {0.05f, 0.4f, 0.35f}, 0.6f, 1.0f);
    }
    if (burst.on_player) {
        const char* text = r.froze       ? "FROZEN  --  wings locked"
                           : r.staggered ? "STAGGERED"
                           : r.corroded  ? "CORRODED  --  armour eaten"
                           : r.doused    ? "DOUSED"
                           : r.drenched  ? "DRENCHED  --  no healing"
                           : r.burned    ? "BURNING"
                                         : nullptr;
        if (text) {
            status_text_ = text;
            status_flash_ = 2.0f;
        }
    }
    if (r.froze) {
        LOG_INFO("status: %s frozen at frame %d, %.0f m from the player", burst.on_player ? "player" : "target",
                 frame_index_, double(core::distance(at, flight_.state().position)));
    }
    // Each element's landing has its own sound, so a status can be heard
    // without looking: ice shattering, rock cracking, acid, a splash, a flare.
    if (r.froze) play_status_sound(audio::Clip::Shatter, 0, at, 1.1f);
    if (r.staggered) play_status_sound(audio::Clip::Crack, 1, at, 1.0f);
    if (r.corroded) play_status_sound(audio::Clip::Hiss, 2, at, 0.9f);
    if (r.shocked && burst.on_player) play_status_sound(audio::Clip::Zap, 3, at, 0.9f);
    if (r.drenched) play_status_sound(audio::Clip::Splash, 4, at, 0.9f);
    if (r.burned) play_status_sound(audio::Clip::Ignite, 5, at, 0.8f);
    if (r.doused) play_status_sound(audio::Clip::Hiss, 6, at, 0.8f, 1.5f);
}

void App::play_status_sound(audio::Clip clip, int slot, core::Vec3 at, float gain, float rate) {
    if (slot < 0 || slot >= 8 || status_sound_cooldown_[slot] > 0.0f) return;
    status_sound_cooldown_[slot] = 0.18f;  // a held breath lands every frame
    const float d = core::distance(active_camera().position, at);
    audio_.play(clip, gain / (1.0f + d * d / (220.0f * 220.0f)), rate * (0.95f + 0.1f * particle_unit()));
}

const char* App::button_name(char key) const {
    if (!input_.has_gamepad()) {
        switch (key) {
            case 'C': return "C";
            case 'G': return "G";
            case 'X': return "X";
            case 'Z': return "Z";
            case 'U': return "U";
            case 'H': return "H";
            default: return "?";
        }
    }
    switch (key) {
        case 'C': return "B";
        case 'G': return "RB";
        case 'X': return "X";
        case 'Z': return "DP";   // the d-pad's left and right
        case 'U': return "Y";
        case 'H': return "LS";   // the left stick, clicked
        default: return "?";
    }
}

const char* App::stage_unlock_text(game::GrowthStage stage) const {
    static std::string text;
    char line[160];
    switch (stage) {
        case game::GrowthStage::Young:
            std::snprintf(line, sizeof(line), "NEW: charged shot -- hold %s, let go (or wait) for a heavy fireball",
                          button_name('G'));
            break;
        case game::GrowthStage::Adult:
            std::snprintf(line, sizeof(line), "NEW: the breath of your last kill (%s) -- %s swaps element",
                          game::element_name(second_element_), button_name('U'));
            break;
        case game::GrowthStage::Elder:
            std::snprintf(line, sizeof(line), "NEW: the ram -- boost (%s) through them, stunning and hurling",
                          button_name('X'));
            break;
        case game::GrowthStage::Ancient:
            std::snprintf(line, sizeof(line), "NEW: fury -- damage fills it; %s releases a nova of your element",
                          button_name('H'));
            break;
        default: line[0] = '\0'; break;
    }
    text = line;
    return text.c_str();
}

// What the player can do: a run grants one ability per stage (young the
// charged shot, adult the second breath, elder the ram, ancient the fury);
// the arena grants them all, so each can be tried without a run.
void App::update_abilities() {
    const bool run = run_mode_ && hoard_run_.phase() != game::HoardPhase::Idle;
    const int stage = run ? int(hoard_run_.stage()) : 99;
    game::Abilities& a = combat_.abilities;
    a.charged_shot = stage >= 1;
    a.ram = stage >= 3;
    a.fury = stage >= 4;
    const bool second = stage >= 2;
    if (second != second_unlocked_) {
        second_unlocked_ = second;
        if (!second) using_second_ = false;
        // The second breath: in a run, the breath of the last enemy killed
        // -- what you hunt is what you learn -- or a rolled one if nothing
        // of another element has fallen yet. (The first cut rolled per run
        // and dealt the arena the element after the player's: fire's is
        // frost, so it was "always frost".) The arena's U cycles them all.
        const game::Element first = player_element_choice_ >= 0
                                        ? game::Element(player_element_choice_)
                                        : player_model().breath.element;
        if (run && last_kill_element_ != game::Element::None && last_kill_element_ != first) {
            second_element_ = last_kill_element_;
        } else {
            second_element_ = run ? roll_element(run_seed_ * 131u + 17u) : game::Element((int(first) + 1) % game::ELEMENT_COUNT);
        }
        if (second_element_ == first) second_element_ = game::Element((int(first) + 3) % game::ELEMENT_COUNT);
        if (second) LOG_INFO("second breath: %s", game::element_name(second_element_));
        refresh_player_element();
    }
}

// The fury's nova: a ring of the element's fire rushing outward to the
// fury's radius, a second slower ring, and a flash at the centre.
void App::emit_fury(core::Vec3 centre) {
    const game::BreathProfile& b = player_breath_;
    const float radius = combat_.tuning.fury_radius;
    for (int ring = 0; ring < 2; ++ring) {
        const int n = ring == 0 ? 220 : 140;
        const float life = ring == 0 ? 0.7f : 1.1f;
        for (int i = 0; i < n; ++i) {
            const float a = core::TWO_PI * float(i) / float(n) + particle_unit() * 0.03f;
            const float tilt = particle_unit() * (ring == 0 ? 0.15f : 0.5f);
            const core::Vec3 dir = core::normalize(core::Vec3{std::cos(a), tilt, std::sin(a)});
            gfx::Particle p;
            p.position = centre + dir * 3.0f;
            // Solved against drag like the flame: it reaches the fury radius.
            const float drag = 1.5f;
            p.velocity = dir * (radius * drag / (1.0f - std::exp(-drag * life)));
            p.drag = drag;
            p.life = life;
            p.size_start = ring == 0 ? 4.0f : 7.0f;
            p.size_end = ring == 0 ? 7.0f : 14.0f;
            p.color_start = b.hot;
            p.color_end = b.cool;
            p.brightness = ring == 0 ? 1.2f : 0.6f;
            particles_.spawn(p);
        }
    }
    gfx::Particle flash;
    flash.position = centre;
    flash.life = 0.4f;
    flash.size_start = 20.0f;
    flash.size_end = 60.0f;
    flash.color_start = b.hot;
    flash.color_end = b.cool;
    flash.brightness = 1.4f;
    particles_.spawn(flash);
}

void App::end_run() {
    run_mode_ = false;
    combat_.hostile_damage_scale = 1.0f;
    hoard_run_.abandon();
    bots_.clear();
    restore_growth_base();
    // Back to the arena's valley.
    if (terrain_run_seed_ != 0) {
        terrain_settings_ = arena_terrain_;
        regenerate_terrain();
        rebuild_courses();
        select_course(current_course_);
        terrain_run_seed_ = 0;
    }
    combat_.reset(&terrain_, flight_.state().position, 20260824u);
    respawn_dragon();
}

// ---- growth ----

void App::capture_growth_base() {
    const game::CombatTuning& t = combat_.tuning;
    GrowthBase& g = growth_base_;
    g.heft = flight_.tuning.heft;
    g.flap = flight_.tuning.flap_peak_force;
    g.ground_offset = flight_.tuning.ground_offset;
    g.boost_force = flight_.tuning.boost_force;
    g.max_health = t.max_health;
    g.breath_drain = t.breath_drain;
    g.fireball_cooldown = t.fireball_cooldown;
    g.fireball_damage = t.fireball_damage;
    g.fireball_blast = t.fireball_blast_radius;
    g.bite = t.bite_damage;
    g.strike = t.strike_damage;
    g.bite_range = t.bite_range;
    g.strike_range = t.strike_range;
    g.melee_cooldown = t.melee_cooldown;
    g.melee_stun = t.melee_stun;
    g.melee_knockback = t.melee_knockback;
    g.breath_dps = t.breath_damage_per_second;
    g.boost_cooldown = t.boost_cooldown;
    g.boost_duration = t.boost_duration;
    g.dodge_impulse = maneuver_tuning_.roll_dodge_impulse;
    g.maneuver_cooldown = maneuver_tuning_.cooldown;
}

namespace {

// What each stage multiplies, from the player's own tuning. Every ability
// grows, not only the body: the playtest asked for the boost, the melee and
// the cooldowns to follow the growth as well. A bigger dragon's bite reaches
// further (the reach follows the body), hits and shoves harder, and swings
// sooner; its boost pushes a heavier body harder and comes back sooner.
struct GrowthScale {
    float heft, flap, health, drain, fireball, melee, breath, size, wings;
    float fireball_damage, fireball_blast;
    float melee_cooldown, melee_power, reach;
    float boost_force, boost_duration, boost_cooldown;
    float dodge, maneuver_cooldown;
};

// Size is the body; wings multiply on top, so an adult's span is about
// 1.45x a young dragon's while its body is 1.25x -- the wings are what
// grows most, and what reads first from the chase camera. Boost force runs
// ahead of heft, so a grown dragon's boost is a stronger boost, not the
// same one dragging more mass.
const GrowthScale GROWTH_ROWS[] = {
    // drake
    {.heft = 0.8f, .flap = 0.9f, .health = 0.85f, .drain = 1.4f, .fireball = 1.4f, .melee = 0.8f,
     .breath = 0.8f, .size = 0.8f, .wings = 0.85f, .fireball_damage = 0.85f, .fireball_blast = 0.85f,
     .melee_cooldown = 1.15f, .melee_power = 0.8f, .reach = 0.85f, .boost_force = 0.75f,
     .boost_duration = 0.9f, .boost_cooldown = 1.2f, .dodge = 0.9f, .maneuver_cooldown = 1.1f},
    // young
    {.heft = 1.0f, .flap = 1.0f, .health = 1.0f, .drain = 1.0f, .fireball = 1.0f, .melee = 1.0f,
     .breath = 1.0f, .size = 1.0f, .wings = 1.0f, .fireball_damage = 1.0f, .fireball_blast = 1.0f,
     .melee_cooldown = 1.0f, .melee_power = 1.0f, .reach = 1.0f, .boost_force = 1.0f,
     .boost_duration = 1.0f, .boost_cooldown = 1.0f, .dodge = 1.0f, .maneuver_cooldown = 1.0f},
    // adult. Fireballs 0.6 -> 0.8 of the cooldown: at 0.6 the adult's
    // fireball rate was the playtest's "a bit overpowered".
    {.heft = 1.3f, .flap = 1.15f, .health = 1.5f, .drain = 0.7f, .fireball = 0.8f, .melee = 1.3f,
     .breath = 1.25f, .size = 1.25f, .wings = 1.15f, .fireball_damage = 1.2f, .fireball_blast = 1.15f,
     .melee_cooldown = 0.9f, .melee_power = 1.25f, .reach = 1.15f, .boost_force = 1.45f,
     .boost_duration = 1.1f, .boost_cooldown = 0.88f, .dodge = 1.12f, .maneuver_cooldown = 0.92f},
    // elder: the descent's stages are smaller steps, so the power curve
    // flattens while the enemies' (the valley depth) keeps climbing.
    {.heft = 1.45f, .flap = 1.25f, .health = 1.8f, .drain = 0.62f, .fireball = 0.76f, .melee = 1.5f,
     .breath = 1.4f, .size = 1.38f, .wings = 1.22f, .fireball_damage = 1.35f, .fireball_blast = 1.25f,
     .melee_cooldown = 0.84f, .melee_power = 1.4f, .reach = 1.25f, .boost_force = 1.75f,
     .boost_duration = 1.18f, .boost_cooldown = 0.8f, .dodge = 1.2f, .maneuver_cooldown = 0.88f},
    // ancient
    {.heft = 1.6f, .flap = 1.33f, .health = 2.1f, .drain = 0.55f, .fireball = 0.72f, .melee = 1.7f,
     .breath = 1.55f, .size = 1.5f, .wings = 1.3f, .fireball_damage = 1.5f, .fireball_blast = 1.35f,
     .melee_cooldown = 0.78f, .melee_power = 1.55f, .reach = 1.35f, .boost_force = 2.0f,
     .boost_duration = 1.25f, .boost_cooldown = 0.72f, .dodge = 1.28f, .maneuver_cooldown = 0.84f},
};

}  // namespace

// A drake is light, fragile and short of breath; an adult is heavy, tough,
// long-breathed and quick with fireballs. Heft changes the flying, which is
// the point: the dragon you land at the pass is not the one you launched.
void App::apply_growth(float level) {
    constexpr int ROWS = int(sizeof(GROWTH_ROWS) / sizeof(GROWTH_ROWS[0]));
    // Between two rows by the fraction of the way from one to the next.
    const float l = core::clampf(level, 0.0f, float(ROWS - 1));
    const int lo = std::min(int(l), ROWS - 2);
    const float f = l - float(lo);
    const GrowthScale& a = GROWTH_ROWS[lo];
    const GrowthScale& b = GROWTH_ROWS[lo + 1];
    auto mix = [f](float x, float y) { return x + (y - x) * f; };
    const GrowthBase& g = growth_base_;
    const float old_max = combat_.tuning.max_health;
    flight_.tuning.heft = g.heft * mix(a.heft, b.heft);
    flight_.tuning.flap_peak_force = g.flap * mix(a.flap, b.flap);
    flight_.tuning.boost_force = g.boost_force * mix(a.boost_force, b.boost_force);
    // The body rests higher off the ground when it is bigger.
    const float size = mix(a.size, b.size);
    flight_.tuning.ground_offset = g.ground_offset * size;
    growth_scale_target_ = size;
    wing_growth_target_ = mix(a.wings, b.wings);
    game::CombatTuning& t = combat_.tuning;
    t.max_health = g.max_health * mix(a.health, b.health);
    t.breath_drain = g.breath_drain * mix(a.drain, b.drain);
    t.fireball_cooldown = g.fireball_cooldown * mix(a.fireball, b.fireball);
    t.fireball_damage = g.fireball_damage * mix(a.fireball_damage, b.fireball_damage);
    t.fireball_blast_radius = g.fireball_blast * mix(a.fireball_blast, b.fireball_blast);
    t.bite_damage = g.bite * mix(a.melee, b.melee);
    t.strike_damage = g.strike * mix(a.melee, b.melee);
    t.bite_range = g.bite_range * mix(a.reach, b.reach);
    t.strike_range = g.strike_range * mix(a.reach, b.reach);
    t.melee_cooldown = g.melee_cooldown * mix(a.melee_cooldown, b.melee_cooldown);
    t.melee_stun = g.melee_stun * mix(a.melee_power, b.melee_power);
    t.melee_knockback = g.melee_knockback * mix(a.melee_power, b.melee_power);
    t.breath_damage_per_second = g.breath_dps * mix(a.breath, b.breath);
    t.boost_duration = g.boost_duration * mix(a.boost_duration, b.boost_duration);
    t.boost_cooldown = g.boost_cooldown * mix(a.boost_cooldown, b.boost_cooldown);
    maneuver_tuning_.roll_dodge_impulse = g.dodge_impulse * mix(a.dodge, b.dodge);
    maneuver_tuning_.cooldown = g.maneuver_cooldown * mix(a.maneuver_cooldown, b.maneuver_cooldown);
    // A bigger body keeps its wounds but gains the new health on top.
    if (t.max_health > old_max) combat_.heal(t.max_health - old_max);
}

void App::restore_growth_base() {
    const GrowthBase& g = growth_base_;
    flight_.tuning.heft = g.heft;
    flight_.tuning.flap_peak_force = g.flap;
    flight_.tuning.ground_offset = g.ground_offset;
    flight_.tuning.boost_force = g.boost_force;
    growth_scale_target_ = growth_scale_ = 1.0f;
    wing_growth_target_ = wing_growth_ = 1.0f;
    dragon_rig_.wing_growth = 1.0f;
    combat_.player_size = 1.0f;
    game::CombatTuning& t = combat_.tuning;
    t.max_health = g.max_health;
    t.breath_drain = g.breath_drain;
    t.fireball_cooldown = g.fireball_cooldown;
    t.fireball_damage = g.fireball_damage;
    t.fireball_blast_radius = g.fireball_blast;
    t.bite_damage = g.bite;
    t.strike_damage = g.strike;
    t.bite_range = g.bite_range;
    t.strike_range = g.strike_range;
    t.melee_cooldown = g.melee_cooldown;
    t.melee_stun = g.melee_stun;
    t.melee_knockback = g.melee_knockback;
    t.breath_damage_per_second = g.breath_dps;
    t.boost_duration = g.boost_duration;
    t.boost_cooldown = g.boost_cooldown;
    maneuver_tuning_.roll_dodge_impulse = g.dodge_impulse;
    maneuver_tuning_.cooldown = g.maneuver_cooldown;
}

bool App::cache_guarded(int cache) const {
    const auto& defences = hoard_run_.layout().defences;
    for (size_t i = 0; i < defences.size() && i < run_defence_slots_.size(); ++i) {
        if (defences[i].guards != cache) continue;
        const int slot = run_defence_slots_[i];
        if (slot >= 0 && size_t(slot) < combat_.sentinels().size() &&
            combat_.sentinels()[size_t(slot)].alive) {
            return true;
        }
    }
    return false;
}

void App::spawn_rival(int rival_index, bool hunter) {
    auto bot = make_bot(int(bots_.size()));
    const uint32_t seed = run_seed_ * 7919u + uint32_t(bots_.size()) * 977u +
                          (hunter ? 31u : 0u);
    core::Vec3 position, facing;
    if (hunter) {
        // Loosed behind the player, already coming, in red. Veteran-tempered:
        // the first build's hunters never fled and pressed like aces, three
        // at once, and the only answer was to run. Now one at a time, and
        // one that can be beaten and pays for it.
        const game::FlightState& player = flight_.state();
        const core::Vec3 back = core::normalize_or(
            core::Vec3{-player.forward().x, 0.0f, -player.forward().z}, core::Vec3::forward());
        position = player.position + back * 700.0f + core::Vec3{0.0f, 80.0f, 0.0f};
        position.y = core::maxf(position.y, terrain_.height_at(position.x, position.z) + 150.0f);
        facing = back * -1.0f;
        bot->hunter = true;
        bot->pilot.tuning.aggression = 0.7f;
        bot->pilot.tuning.aggression_spread = 0.1f;
        bot->pilot.tuning.flee_health = 0.2f;
        bot->can_pounce = true;  // the dragonslayers have the elder's trick
    } else {
        const game::RunRival& rival = hoard_run_.layout().rivals[size_t(rival_index)];
        position = rival.position;
        facing = rival.facing;
        bot->dormant = true;
        // Rivals never wear the hunters' red: bone, moss, violet, steel.
        bot->post = rival.position;
        bot->rival = rival_index;
        bot->loiter_phase = float(rival_index) * 2.1f;
    }
    // Deeper valleys breed tougher, keener dragons.
    {
        const float depth = float(hoard_run_.valley());
        game::Sentinel& slot = combat_.sentinels()[size_t(bot->slot)];
        slot.max_health = slot.health =
            slot.max_health * (1.0f + hoard_run_.settings.depth_enemy_health * depth);
        bot->last_health = slot.health;
        bot->pilot.tuning.aggression = core::saturate(
            bot->pilot.tuning.aggression + hoard_run_.settings.depth_enemy_aggression * depth);
    }
    // Every enemy in a run rolls its element; the hide wears it, and the HUD
    // tag names it (hunters and rivals are told apart by the tag).
    bot->element = roll_element(seed * 2246822519u + 7u);
    bot->hue = game::element_hide(bot->element);
    bot->breath = breath_for(bot->element, bot->model);
    combat_.sentinels()[size_t(bot->slot)].element = bot->element;
    bot->flight.reset(position, core::look_rotation(facing, core::Vec3::up()), 42.0f);
    bot->pilot.reset(seed);
    bot->was_alive = true;
    bots_.push_back(std::move(bot));
}

void App::update_run(float dt, const game::CombatEvents& events) {
    hoard_run_.update(dt, flight_.state(), combat_.alive(), events);
    if (hoard_run_.just_crossed()) {
        // Banked at the pass; the next valley is laid out under the dragon.
        advance_valley();
    }
    update_prey(dt, events);
    // Growth, continuous: the tuning follows the hoard every frame, and the
    // size eases after it -- most of it in the first second.
    if (hoard_run_.phase() == game::HoardPhase::Flying) apply_growth(hoard_run_.growth_level());
    growth_scale_ = core::damp(growth_scale_, growth_scale_target_, 0.5f, dt);
    wing_growth_ = core::damp(wing_growth_, wing_growth_target_, 0.5f, dt);
    dragon_rig_.wing_growth = wing_growth_;

    // Kills, told apart: a tower, a rival or a hunter, each with its bounty
    // and a little health back. Fighting used to earn nothing, which made
    // avoiding everything the best way to play. The dead stay dead in a run.
    auto& sentinels = combat_.sentinels();
    if (run_alive_.size() < sentinels.size()) run_alive_.resize(sentinels.size(), 1);
    bool hunter_flying = false;
    // Only the player's kills pay: a rival that flies into a mountain is not
    // a kill (the first pass paid for those too).
    int kills_to_pay = events.kills;
    for (size_t i = 0; i < sentinels.size(); ++i) {
        game::Sentinel& s = sentinels[i];
        const BotShip* who = nullptr;
        for (const auto& bot : bots_) {
            if (bot->slot == int(i)) who = bot.get();
        }
        if (who && who->hunter && s.alive) hunter_flying = true;
        const bool was = run_alive_[i] != 0;
        run_alive_[i] = s.alive ? 1 : 0;
        if (!was || s.alive || hoard_run_.phase() != game::HoardPhase::Flying) continue;
        const game::HoardRunSettings& r = hoard_run_.settings;
        float bounty = 0.0f;
        const char* what = "";
        if (s.ground) {
            bounty = r.bounty_tower;
            what = "TOWER DOWN";
        } else if (who && who->hunter) {
            bounty = r.bounty_hunter;
            what = "HUNTER SLAIN";
        } else if (who) {
            bounty = r.bounty_rival;
            what = "RIVAL SLAIN";
        }
        if (s.external) s.respawn_timer = 1e9f;
        if (bounty > 0.0f && kills_to_pay > 0 && s.element != game::Element::None) last_kill_element_ = s.element;
        if (bounty <= 0.0f || kills_to_pay <= 0) continue;
        --kills_to_pay;
        hoard_run_.award(bounty);
        combat_.heal(r.heal_on_kill);
        char text[64];
        std::snprintf(text, sizeof(text), "+%.0f  %s", double(bounty), what);
        award_text_ = text;
        award_flash_ = 2.5f;
        audio_.play(audio::Clip::BiteHit, 0.7f, 0.7f);
    }
    hoard_run_.set_hunter_alive(hunter_flying);

    if (hoard_run_.take_hunter_request()) {
        spawn_rival(-1, true);
        run_alive_.resize(combat_.sentinels().size(), 1);
        // A distant cry, pitched down: something is coming.
        audio_.play(audio::Clip::Screech, 0.8f, 0.7f);
        hunter_flash_ = 4.0f;
    }
    if (hoard_run_.just_collected()) {
        audio_.play(audio::Clip::BiteHit, 0.6f, 0.8f);
        collect_flash_ = 2.0f;
    }
    combat_.player_size = growth_scale_;
    if (hoard_run_.just_grew()) {
        // Crossing a stage is a second wind on top of the steady growth.
        combat_.heal(25.0f);
        audio_.play(audio::Clip::Boost, 1.0f, 0.75f);
        grew_flash_ = 3.5f;
        LOG_INFO("run: grew into %s at %.0f s (growth %.0f)",
                 hoard_run_.stage() == game::GrowthStage::Young ? "a young dragon"
                 : hoard_run_.stage() == game::GrowthStage::Adult ? "an adult"
                 : hoard_run_.stage() == game::GrowthStage::Elder ? "an elder" : "an ancient",
                 double(hoard_run_.elapsed()), double(hoard_run_.growth()));
    }
    if (hoard_run_.just_banked() || hoard_run_.just_lost()) {
        const game::RunResult result = hoard_run_.result();
        last_run_record_ = run_records_.submit(result);
        run_records_.save(ASSET_ROOT "/runs.txt");
        audio_.play(hoard_run_.just_banked() ? audio::Clip::Boost : audio::Clip::KnockOut, 1.0f);
        if (autopilot_) {
            LOG_INFO("run over, demo time: cruise %.0f fight %.0f siege %.0f land %.0f walk %.0f "
                     "collect %.0f takeoff %.0f flee %.0f hunt %.0f",
                     double(demo_.time_in[0]), double(demo_.time_in[1]), double(demo_.time_in[2]),
                     double(demo_.time_in[3]), double(demo_.time_in[4]), double(demo_.time_in[5]),
                     double(demo_.time_in[6]), double(demo_.time_in[7]), double(demo_.time_in[8]));
        }
        LOG_INFO("run over: %s  valley %d/%d (a %s)  banked %.0f (lost %.0f)  caches %d  kills %d  "
                 "hunters %d  prey %d  %s  %.0f s  %.0f m",
                 result.banked ? "CLEARED" : "LOST", result.valley, result.valleys,
                 game::valley_kind_name(result.kind), double(result.hoard), double(result.carried),
                 result.caches, result.kills, result.hunters, result.prey,
                 game::growth_stage_name(result.stage), double(result.time), double(result.distance));
    }
    collect_flash_ = core::maxf(collect_flash_ - dt, 0.0f);
    hunter_flash_ = core::maxf(hunter_flash_ - dt, 0.0f);
    award_flash_ = core::maxf(award_flash_ - dt, 0.0f);
    grew_flash_ = core::maxf(grew_flash_ - dt, 0.0f);
    valley_flash_ = core::maxf(valley_flash_ - dt, 0.0f);
    prey_flash_ = core::maxf(prey_flash_ - dt, 0.0f);
    status_flash_ = core::maxf(status_flash_ - dt, 0.0f);
}

// The herds: behaviour and the swoop, then the player's weapons on them,
// then what was eaten turned into growth and health.
void App::update_prey(float dt, const game::CombatEvents& events) {
    if (hoard_run_.phase() != game::HoardPhase::Flying || hoard_run_.awaiting_valley()) return;
    const game::FlightState& s = flight_.state();
    game::PreyEvents eaten = prey_.update(dt, s, growth_scale_, terrain_);
    // The weapons reach the herd through combat: each animal is a slot, so
    // the lock, the assist and the seeking shot help the hunt. A slot that
    // died leaves a carcass -- or, killed by the jaws, a meal at once.
    auto& sentinels = combat_.sentinels();
    const auto& animals = prey_.animals();
    for (size_t i = 0; i < animals.size() && i < prey_slots_.size(); ++i) {
        const int slot = prey_slots_[i];
        if (slot < 0 || size_t(slot) >= sentinels.size()) continue;
        game::Sentinel& body = sentinels[size_t(slot)];
        const game::Prey& p = animals[i];
        const bool living = p.state != game::PreyState::Eaten && p.state != game::PreyState::Carcass;
        if (living && !body.alive) {
            const bool bitten = events.melee_hit != game::MeleeKind::None &&
                                core::distance(events.melee_hit_position, body.position) < 1.0f;
            if (bitten) {
                prey_.eat_now(int(i), eaten);
            } else {
                prey_.kill(int(i));
            }
        } else if (!living && body.alive) {
            body.alive = false;
            body.respawn_timer = 1e9f;
        } else if (living) {
            combat_.drive_external(slot, p.position + core::Vec3{0.0f, 1.5f * prey_.tuning.scale, 0.0f},
                                   p.heading * p.speed);
        }
    }
    if (eaten.eaten > 0) {
        for (int i = 0; i < eaten.eaten; ++i) hoard_run_.feed(prey_.tuning.growth);
        combat_.heal(prey_.tuning.heal * float(eaten.eaten));
        prey_flash_ = 2.0f;
        snatch_pending_ = snatch_pending_ || eaten.by_swoop;
        audio_.play(audio::Clip::BiteHit, 0.8f, 1.1f);
        for (int i = 0; i < 16; ++i) {
            gfx::Particle p;
            p.position = eaten.where + core::Vec3{0.0f, 1.5f, 0.0f};
            p.velocity = core::Vec3{particle_unit(), 0.6f + std::fabs(particle_unit()), particle_unit()} * 6.0f;
            p.acceleration = core::Vec3{0.0f, -12.0f, 0.0f};
            p.drag = 1.5f;
            p.life = 0.6f;
            p.size_start = 1.0f;
            p.size_end = 0.3f;
            p.color_start = core::Vec3{1.6f, 1.2f, 0.5f};
            p.color_end = core::Vec3{0.5f, 0.25f, 0.08f};
            p.brightness = 0.8f;
            particles_.spawn(p);
        }
    }
}

// ---- the herd, drawn ----

// One grazer's pose: its gait's clip at its own phase, a carcass lying on its
// side at the bind pose. Returns false for one that is not drawn.
bool App::pose_prey(const game::Prey& prey, core::Mat4& model, std::vector<core::Mat4>& skin) {
    if (prey.state == game::PreyState::Eaten) return false;
    const game::PreyClip clip = game::prey_clip(prey, prey_.tuning);
    // The grazer faces +Z; the engine's forward is -Z.
    core::Quat facing = core::look_rotation(core::normalize_or(prey.heading * -1.0f, core::Vec3{0.0f, 0.0f, 1.0f}),
                                            core::Vec3::up());
    if (clip == game::PreyClip::Dead) {
        facing = facing * core::Quat::from_axis_angle(core::Vec3::unit_z(), core::HALF_PI);
    }
    const float scale = prey_.tuning.scale;
    model = core::Mat4::trs(prey.position + core::Vec3{0.0f, clip == game::PreyClip::Dead ? 0.8f * scale : 0.0f, 0.0f},
                            facing, core::Vec3(scale));
    const anim::Skeleton& skeleton = grazer_prop_.skeleton;
    grazer_pose_.reset_to_bind(skeleton);
    const int which = clip == game::PreyClip::Run ? grazer_clip_[2]
                      : clip == game::PreyClip::Walk ? grazer_clip_[1]
                      : clip == game::PreyClip::Graze ? grazer_clip_[0]
                                                      : -1;
    if (which >= 0) grazer_clips_[size_t(which)].sample(prey.clip_time, grazer_pose_);
    anim::compute_world_matrices(skeleton, grazer_pose_, grazer_world_);
    anim::compute_skinning_matrices(skeleton, grazer_world_, skin);
    return true;
}

void App::draw_prey(SDL_GPURenderPass* pass) {
    if (!run_mode_ || prey_.animals().empty()) return;
    const core::Vec3 eye = active_camera().position;
    for (const game::Prey& prey : prey_.animals()) {
        if (core::distance(eye, prey.position) > 2200.0f) continue;
        core::Mat4 model;
        if (grazer_prop_.ok) {
            if (!pose_prey(prey, model, grazer_skin_)) continue;
            gfx::ModelUniforms m;
            m.model = model;
            // Lifted like the towers: the hide is dark, and a herd in a valley read as black.
            const float char_ = 1.5f * (1.0f - 0.75f * prey.burnt);
            m.tint = core::Vec4{char_, char_, char_, 0.0f};
            world_.draw_skinned(device_, pass, grazer_prop_.mesh, m, grazer_skin_,
                                grazer_prop_.textures, model_sampler_);
        } else if (sphere_mesh_.valid() && prey.state != game::PreyState::Eaten) {
            // No prop: a hide-brown body the size of one.
            gfx::ModelUniforms m;
            const core::Quat facing = core::look_rotation(core::normalize_or(prey.heading, core::Vec3::forward()),
                                                          core::Vec3::up());
            m.model = core::Mat4::trs(prey.position + core::Vec3{0.0f, 1.4f, 0.0f}, facing,
                                      core::Vec3{1.1f, 1.0f, 2.2f});
            m.tint = core::Vec4{0.42f, 0.33f, 0.22f, 0.0f};
            world_.draw_mesh(device_, pass, sphere_mesh_, m);
        }
    }
}

void App::draw_prey_shadows(SDL_GPURenderPass* shadow_pass) {
    if (!run_mode_ || !grazer_prop_.ok) return;
    const core::Vec3 eye = active_camera().position;
    for (const game::Prey& prey : prey_.animals()) {
        if (core::distance(eye, prey.position) > 900.0f) continue;
        core::Mat4 model;
        if (!pose_prey(prey, model, grazer_skin_)) continue;
        gfx::ModelUniforms m;
        m.model = model;
        world_.draw_skinned_depth(device_, shadow_pass, grazer_prop_.mesh, shadow_.light_view_proj(), m,
                                  grazer_skin_);
    }
}

// The run's things in the world: the pass gate as a ring, and each cache as a
// landing ring on the ground with the pile in it.
void App::draw_run_world(SDL_GPURenderPass* pass) {
    if (!run_mode_ || hoard_run_.phase() == game::HoardPhase::Idle || !ring_mesh_.valid()) return;
    const game::RunLayout& layout = hoard_run_.layout();
    const bool flying = hoard_run_.phase() == game::HoardPhase::Flying;
    const float pulse = 0.55f + 0.45f * std::sin(time_seconds_ * 3.0f);
    {
        gfx::ModelUniforms model;
        model.model = core::Mat4::trs(layout.gate.position, layout.gate.orientation,
                                      core::Vec3(layout.gate.radius / RING_MESH_RADIUS));
        model.tint = flying ? core::Vec4{1.0f, 0.72f, 0.22f, 0.4f + pulse * 0.8f}
                            : core::Vec4{0.16f, 0.20f, 0.22f, 0.0f};
        world_.draw_mesh(device_, pass, ring_mesh_, model);
    }
    // A ring lies flat when its normal points up.
    const core::Quat flat = core::look_rotation(core::Vec3::up(), core::Vec3::forward());
    for (size_t i = 0; i < layout.caches.size(); ++i) {
        const game::RunCache& cache = layout.caches[i];
        gfx::ModelUniforms ring;
        ring.model = core::Mat4::trs(cache.position + core::Vec3{0.0f, 0.8f, 0.0f}, flat,
                                     core::Vec3(hoard_run_.settings.cache_radius / RING_MESH_RADIUS));
        ring.tint = cache.collected ? core::Vec4{0.16f, 0.20f, 0.22f, 0.0f}
                                    : core::Vec4{1.0f, 0.72f, 0.22f, 0.3f + pulse * 0.6f};
        world_.draw_mesh(device_, pass, ring_mesh_, ring);
        if (!cache.collected && hoard_prop_.ok) {
            // The pile, flattening as it is taken. Every other cache is the
            // trove -- a ruined ring round a smaller heap -- turned by its
            // index, so two caches in one valley are not one shape.
            const PropModel& prop = (i % 2 == 1 && trove_prop_.ok) ? trove_prop_ : hoard_prop_;
            const float left = 1.0f - 0.85f * cache.progress;
            gfx::ModelUniforms pile;
            pile.model = core::Mat4::trs(
                cache.position + core::Vec3{0.0f, 0.1f, 0.0f},
                core::Quat::from_axis_angle(core::Vec3::up(), float(i) * 2.3f), core::Vec3{1.0f, left, 1.0f});
            pile.tint = core::Vec4{1.0f, 1.0f, 1.0f, 0.08f + 0.12f * pulse};
            world_.draw_skinned(device_, pass, prop.mesh, pile, prop.joints, prop.textures,
                                model_sampler_);
        } else if (!cache.collected && sphere_mesh_.valid()) {
            // The pile: it sinks as it is taken.
            const float left = 1.0f - 0.85f * cache.progress;
            gfx::ModelUniforms pile;
            pile.model = core::Mat4::trs(cache.position + core::Vec3{0.0f, 1.2f * left, 0.0f},
                                         core::Quat::identity(),
                                         core::Vec3{7.0f * left, 3.2f * left, 7.0f * left});
            pile.tint = core::Vec4{1.0f, 0.78f, 0.28f, 0.35f + 0.4f * pulse};
            world_.draw_mesh(device_, pass, sphere_mesh_, pile);
        }
    }
}

void App::draw_run_hud() {
    const ui::Tokens& tk = hud_.tokens();
    ImDrawList* draw = hud_.draw();
    const float width = hud_.width();
    const float height = hud_.height();
    const float margin = hud_.margin();
    const game::HoardPhase phase = hoard_run_.phase();
    if (phase == game::HoardPhase::Idle) return;
    const game::RunLayout& layout = hoard_run_.layout();
    const core::Mat4 view_proj = active_camera().view_projection(device_.aspect());
    const game::FlightState& player = flight_.state();
    char line[160];

    // ---- the run strip, top centre: hoard, caches, the pass, the hunters ----
    {
        const float strip_w = hud_.px(780.0f);
        const float strip_h = hud_.px(46.0f);
        const ImVec2 min(width * 0.5f - strip_w * 0.5f, margin);
        hud_.plate(min, ImVec2(min.x + strip_w, min.y + strip_h));
        float x = min.x + hud_.px(16.0f);
        // How deep: the valley of the descent, and what the passes banked.
        {
            hud_.label(ImVec2(x, min.y + hud_.px(5.0f)), "VALLEY", tk.text_dim, 10.0f);
            std::snprintf(line, sizeof(line), "%d/%d", hoard_run_.valley() + 1, hoard_run_.settings.valleys);
            hud_.numeral(ImVec2(x, min.y + hud_.px(13.0f)), line, valley_flash_ > 0.0f ? tk.accent : tk.text,
                         28.0f);
            x += core::maxf(hud_.numeral_width(line, 28.0f), hud_.px(40.0f)) + hud_.px(18.0f);
        }
        hud_.label(ImVec2(x, min.y + hud_.px(5.0f)), "HOARD", tk.text_dim, 10.0f);
        std::snprintf(line, sizeof(line), "%.0f", hoard_run_.hoard());
        hud_.numeral(ImVec2(x, min.y + hud_.px(13.0f)), line,
                     collect_flash_ > 0.0f || award_flash_ > 0.0f ? tk.accent : tk.text, 28.0f);
        if (hoard_run_.banked() > 0.0f) {
            std::snprintf(line, sizeof(line), "+%.0f banked", double(hoard_run_.banked()));
            hud_.label(ImVec2(x, min.y + strip_h - hud_.px(13.0f)), line, tk.text_dim, 9.0f);
        }
        x += core::maxf(hud_.numeral_width(line, 28.0f), hud_.px(40.0f)) + hud_.px(20.0f);
        // Growth: the stage, and a bar toward the next.
        {
            const char* stage = game::growth_stage_name(hoard_run_.stage());
            hud_.label(ImVec2(x, min.y + hud_.px(8.0f)), stage,
                       grew_flash_ > 0.0f ? tk.accent : tk.text, 13.0f);
            hud_.bar(ImVec2(x, min.y + hud_.px(28.0f)), hud_.px(96.0f), hud_.px(5.0f),
                     hoard_run_.growth_progress(), tk.accent);
            x += hud_.px(112.0f);
        }
        // One pip per cache: filled when taken, filling while you sit on it.
        for (size_t i = 0; i < layout.caches.size(); ++i) {
            const float ready = layout.caches[i].collected ? 1.0f
                                : int(i) == hoard_run_.collecting() ? hoard_run_.collect_progress()
                                                                     : 0.0f;
            hud_.pip(ImVec2(x + hud_.px(9.0f), min.y + strip_h * 0.5f), hud_.px(9.0f), ready, "",
                     tk.accent);
            x += hud_.px(24.0f);
        }
        x += hud_.px(10.0f);
        draw->AddLine(ImVec2(x, min.y + hud_.px(10.0f)), ImVec2(x, min.y + strip_h - hud_.px(10.0f)),
                      tk.plate_edge, 1.0f);
        x += hud_.px(14.0f);
        const float to_pass = core::distance(player.position, layout.gate.position);
        std::snprintf(line, sizeof(line), "%.1f km", double(to_pass / 1000.0f));
        hud_.numeral(ImVec2(x, min.y + hud_.px(7.0f)), line, tk.text, 30.0f);
        hud_.label(ImVec2(x, min.y + strip_h - hud_.px(14.0f)), "to the pass", tk.text_dim, 10.0f);
        x += hud_.numeral_width(line, 30.0f) + hud_.px(20.0f);
        if (hoard_run_.hunters_loosed() > 0) {
            draw->AddLine(ImVec2(x, min.y + hud_.px(10.0f)),
                          ImVec2(x, min.y + strip_h - hud_.px(10.0f)), tk.plate_edge, 1.0f);
            x += hud_.px(14.0f);
            const int n = hoard_run_.hunters_loosed();
            std::snprintf(line, sizeof(line), "%d HUNTER%s", n, n > 1 ? "S" : "");
            // Blinks for a few seconds when one is loosed.
            const bool blink = hunter_flash_ > 0.0f && std::fmod(time_seconds_, 0.5f) < 0.25f;
            hud_.label(ImVec2(x, min.y + hud_.px(15.0f)), line, blink ? tk.text : tk.danger, 13.0f);
        }
    }

    // ---- objective markers: the nearest hoard, and the pass ----
    auto marker = [&](core::Vec3 position, float world_radius, const char* text, ImU32 colour,
                      float progress) {
        ImVec2 screen;
        const bool on_screen =
            project_to_screen(view_proj, position, width, height, screen) && screen.x > 0.0f &&
            screen.x < width && screen.y > 0.0f && screen.y < height;
        const float range = core::distance(player.position, position);
        if (on_screen) {
            const float apparent = core::clampf(world_radius / core::maxf(range, 1.0f) * height * 0.5f,
                                                hud_.px(12.0f), hud_.px(220.0f));
            draw->AddCircle(screen, apparent, colour, 40, hud_.px(1.8f));
            if (progress > 0.0f) {
                hud_.arc(screen, apparent + hud_.px(6.0f), -core::HALF_PI, core::HALF_PI * 3.0f, progress,
                         tk.accent, hud_.px(4.0f));
            }
            hud_.label(ImVec2(screen.x + apparent + hud_.px(8.0f), screen.y - hud_.px(8.0f)), text,
                       colour, 14.0f);
        } else {
            const gfx::Camera& camera = active_camera();
            const core::Vec3 to = position - camera.position;
            core::Vec2 direction{core::dot(to, camera.right()), -core::dot(to, camera.up())};
            if (core::dot(to, camera.forward()) < 0.0f) direction = core::Vec2{-direction.x, -direction.y};
            const float span = core::length(direction);
            if (span < 1e-3f) return;
            direction = core::Vec2{direction.x / span, direction.y / span};
            const ImVec2 centre(width * 0.5f, height * 0.5f);
            const float radius = core::minf(width, height) * 0.40f;
            hud_.edge_arrow(centre, direction, radius, colour, hud_.px(10.0f));
            const ImVec2 tip(centre.x + direction.x * radius, centre.y + direction.y * radius);
            hud_.label(ImVec2(tip.x, tip.y + hud_.px(18.0f)), text, colour, 13.0f, ui::Align::Centre);
        }
    };
    if (phase == game::HoardPhase::Flying) {
        const int nearest = hoard_run_.nearest_cache(player.position);
        if (nearest >= 0) {
            const game::RunCache& cache = layout.caches[size_t(nearest)];
            const float range = core::distance(player.position, cache.position);
            if (hoard_run_.collecting() == nearest) {
                std::snprintf(line, sizeof(line), "TAKING THE HOARD");
            } else if (range < 160.0f) {
                std::snprintf(line, sizeof(line), cache_guarded(nearest) ? "LAND HERE  %.0f  (GUARDED)"
                                                                          : "LAND HERE  %.0f",
                              double(cache.value));
            } else {
                std::snprintf(line, sizeof(line), cache_guarded(nearest) ? "HOARD  %.0f m  (guarded)"
                                                                          : "HOARD  %.0f m",
                              double(range));
            }
            marker(cache.position + core::Vec3{0.0f, 3.0f, 0.0f}, hoard_run_.settings.cache_radius, line,
                   tk.accent, hoard_run_.collecting() == nearest ? hoard_run_.collect_progress() : 0.0f);
        }
        const bool all_taken = hoard_run_.caches_collected() == int(layout.caches.size());
        std::snprintf(line, sizeof(line), "THE PASS  %.0f m",
                      double(core::distance(player.position, layout.gate.position)));
        marker(layout.gate.position, layout.gate.radius, line, all_taken ? tk.accent : tk.text_dim, 0.0f);

        if (hoard_run_.elapsed() < 9.0f) {
            std::snprintf(line, sizeof(line),
                          "a %s -- fly to the pass -- land on a hoard to take it, its tower guards it -- "
                          "kills pay, hoard and prey grow you",
                          game::valley_kind_name(layout.kind));
            hud_.label(ImVec2(width * 0.5f, margin + hud_.px(56.0f)), line, tk.text_dim, 13.0f,
                       ui::Align::Centre);
            hud_.label(ImVec2(width * 0.5f, margin + hud_.px(74.0f)),
                       "the herds are prey: swoop low through one, bite it, or burn it and pick it up",
                       tk.text_dim, 12.0f, ui::Align::Centre);
        }
        // Into a new valley.
        if (valley_flash_ > 0.0f) {
            const float fade = core::saturate(valley_flash_ * 0.7f);
            const ImU32 colour = (tk.accent & 0x00FFFFFF) | (ImU32(245.0f * fade) << 24);
            std::snprintf(line, sizeof(line), "VALLEY %d OF %d", hoard_run_.valley() + 1,
                          hoard_run_.settings.valleys);
            hud_.numeral(ImVec2(width * 0.5f, height * 0.18f), line, colour, 48.0f, ui::Align::Centre);
            std::snprintf(line, sizeof(line),
                          "a %s  --  %.0f banked  --  deeper: more towers, richer hoards, hunters sooner",
                          game::valley_kind_name(layout.kind), double(hoard_run_.banked()));
            hud_.label(ImVec2(width * 0.5f, height * 0.18f + hud_.px(54.0f)), line,
                       (tk.text & 0x00FFFFFF) | (ImU32(220.0f * fade) << 24), 14.0f, ui::Align::Centre);
        }
        if (prey_flash_ > 0.0f) {
            const float fade = core::saturate(prey_flash_);
            const ImU32 colour = (tk.ahead & 0x00FFFFFF) | (ImU32(235.0f * fade) << 24);
            std::snprintf(line, sizeof(line), "+%.0f  PREY", double(prey_.tuning.growth));
            hud_.numeral(ImVec2(width * 0.5f, height * 0.36f), line, colour, 26.0f, ui::Align::Centre);
        }
        if (fury_ready_flash_ > 0.0f) {
            const float fade = core::saturate(fury_ready_flash_);
            const core::Vec3 c = game::element_colour(combat_.player_element);
            std::snprintf(line, sizeof(line), "FURY READY  --  %s", button_name('H'));
            hud_.numeral(ImVec2(width * 0.5f, height * 0.42f), line,
                         IM_COL32(int(c.x * 255.0f), int(c.y * 255.0f), int(c.z * 255.0f), int(240.0f * fade)), 30.0f,
                         ui::Align::Centre);
        }
        if (status_flash_ > 0.0f) {
            const float fade = core::saturate(status_flash_);
            hud_.label(ImVec2(width * 0.5f, height * 0.62f), status_text_.c_str(),
                       (tk.text & 0x00FFFFFF) | (ImU32(235.0f * fade) << 24), 18.0f, ui::Align::Centre);
        }
        // The call-outs: a bounty, and growing.
        if (award_flash_ > 0.0f) {
            const float fade = core::saturate(award_flash_);
            const ImU32 colour = (tk.accent & 0x00FFFFFF) | (ImU32(240.0f * fade) << 24);
            hud_.numeral(ImVec2(width * 0.5f, height * 0.30f), award_text_.c_str(), colour, 30.0f,
                         ui::Align::Centre);
        }
        if (grew_flash_ > 0.0f) {
            const float fade = core::saturate(grew_flash_);
            const ImU32 colour = (tk.accent & 0x00FFFFFF) | (ImU32(245.0f * fade) << 24);
            std::snprintf(line, sizeof(line), "YOU GREW: %s", game::growth_stage_name(hoard_run_.stage()));
            hud_.numeral(ImVec2(width * 0.5f, height * 0.20f), line, colour, 44.0f, ui::Align::Centre);
            hud_.label(ImVec2(width * 0.5f, height * 0.20f + hud_.px(50.0f)),
                       stage_unlock_text(hoard_run_.stage()),
                       (tk.text & 0x00FFFFFF) | (ImU32(220.0f * fade) << 24), 14.0f, ui::Align::Centre);
        }
    }

    // ---- prey: a marker per herd, and a mark over each animal close in ----
    // A 6 m grazer is a few pixels from a dragon's height; the playtest could
    // not find them. Herds are marked out to 1.8 km, on screen only (the edge
    // is for threats), and inside 450 m every animal gets a chevron over it:
    // bright while it runs, dim while it grazes, gold for a carcass to take.
    if (phase == game::HoardPhase::Flying && !prey_.animals().empty()) {
        const ImU32 green = tk.ahead;
        const ImU32 green_dim = (tk.ahead & 0x00FFFFFF) | (ImU32(150) << 24);
        const auto& animals = prey_.animals();
        int herds = 0;
        for (const game::Prey& p : animals) herds = std::max(herds, p.herd + 1);
        for (int h = 0; h < herds; ++h) {
            core::Vec3 sum = core::Vec3::zero();
            int n = 0;
            for (const game::Prey& p : animals) {
                if (p.herd != h || p.state == game::PreyState::Eaten) continue;
                sum += p.position;
                ++n;
            }
            if (n == 0) continue;
            const core::Vec3 centre = sum / float(n) + core::Vec3{0.0f, 12.0f, 0.0f};
            const float range = core::distance(player.position, centre);
            if (range > 1800.0f || range < 350.0f) continue;
            ImVec2 screen;
            if (!project_to_screen(view_proj, centre, width, height, screen) || screen.x < 0.0f ||
                screen.x > width || screen.y < 0.0f || screen.y > height) {
                continue;
            }
            const float r = hud_.px(7.0f);
            draw->AddQuad(ImVec2(screen.x, screen.y - r), ImVec2(screen.x + r, screen.y),
                          ImVec2(screen.x, screen.y + r), ImVec2(screen.x - r, screen.y), green, hud_.px(1.8f));
            std::snprintf(line, sizeof(line), "HERD x%d  %.0f m", n, double(range));
            hud_.label(ImVec2(screen.x + r + hud_.px(6.0f), screen.y - hud_.px(7.0f)), line, green, 12.0f);
        }
        for (const game::Prey& p : animals) {
            if (p.state == game::PreyState::Eaten) continue;
            const float range = core::distance(player.position, p.position);
            if (range > 450.0f) continue;
            ImVec2 screen;
            const core::Vec3 above = p.position + core::Vec3{0.0f, 5.5f * prey_.tuning.scale, 0.0f};
            if (!project_to_screen(view_proj, above, width, height, screen) || screen.x < 0.0f ||
                screen.x > width || screen.y < 0.0f || screen.y > height) {
                continue;
            }
            const bool carcass = p.state == game::PreyState::Carcass;
            const ImU32 c = carcass ? tk.accent : p.state == game::PreyState::Flee ? green : green_dim;
            const float w = hud_.px(5.0f);
            draw->AddTriangleFilled(ImVec2(screen.x - w, screen.y - w), ImVec2(screen.x + w, screen.y - w),
                                    ImVec2(screen.x, screen.y + w * 0.4f), c);
        }
    }

    // ---- results ----
    if (phase == game::HoardPhase::Banked || phase == game::HoardPhase::Lost) {
        const bool banked = phase == game::HoardPhase::Banked;
        const game::RunResult result = hoard_run_.result();
        float y = height * 0.24f;
        hud_.numeral(ImVec2(width * 0.5f, y), banked ? "THE DESCENT CLEARED" : "RUN LOST",
                     banked ? tk.accent : tk.danger, 64.0f, ui::Align::Centre);
        y += hud_.px(76.0f);
        if (last_run_record_) {
            hud_.label(ImVec2(width * 0.5f, y), "NEW RECORD", tk.ahead, 15.0f, ui::Align::Centre);
            y += hud_.px(22.0f);
        }
        std::snprintf(line, sizeof(line),
                      "banked %.0f   valley %d of %d   %d kills   %d prey   %s   %d:%02d   %.1f km",
                      double(result.hoard),
                      result.valley, result.valleys, result.kills, result.prey,
                      game::growth_stage_name(result.stage), int(result.time) / 60,
                      int(result.time) % 60, double(result.distance / 1000.0f));
        if (!banked && result.carried > 0.0f) {
            hud_.label(ImVec2(width * 0.5f, y + hud_.px(24.0f)),
                       (std::string("lost with you: ") + std::to_string(int(result.carried))).c_str(),
                       tk.danger, 14.0f, ui::Align::Centre);
        }
        hud_.label(ImVec2(width * 0.5f, y), line, tk.text, 16.0f, ui::Align::Centre);
        y += hud_.px(!banked && result.carried > 0.0f ? 48.0f : 24.0f);
        std::snprintf(line, sizeof(line), "best hoard %.0f   fastest clear %d:%02d   %d runs, %d cleared",
                      double(run_records_.best_hoard), int(run_records_.best_time) / 60,
                      int(run_records_.best_time) % 60, run_records_.runs, run_records_.banked);
        hud_.label(ImVec2(width * 0.5f, y), line, tk.text_dim, 14.0f, ui::Align::Centre);
        y += hud_.px(30.0f);
        std::snprintf(line, sizeof(line), "R  this valley again      ENTER  a new valley      seed %u",
                      layout.seed);
        hud_.label(ImVec2(width * 0.5f, y), line, tk.text_dim, 13.0f, ui::Align::Centre);
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
                // Bots screech -- high, reedy, falling -- where the player
                // roars; each a little different so a flight never chorus.
                audio_.play(audio::Clip::Screech, 0.9f / (1.0f + d * d / (240.0f * 240.0f)),
                            1.05f + 0.15f * particle_unit());
                bot->hit_cry_cooldown = 0.5f;
            }
        }
        bot->hit_cry_cooldown = core::maxf(bot->hit_cry_cooldown - dt, 0.0f);
        // Shooting a rival at its post wakes it, range or no range.
        if (run_mode_ && bot->rival >= 0 && slot.health < bot->last_health - 0.01f) {
            hoard_run_.engage_rival(bot->rival);
        }
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
        const float health_fraction =
            slot.max_health > 0.0f ? core::saturate(slot.health / slot.max_health) : 1.0f;
        // A run's rival holds its post until the run wakes it: a slow circle
        // over the post through the rally autopilot's steering, weapons cold.
        // Then it is a bot like any other.
        if (run_mode_ && bot->dormant) {
            const auto& rivals = hoard_run_.layout().rivals;
            if (bot->rival >= 0 && size_t(bot->rival) < rivals.size() &&
                rivals[size_t(bot->rival)].engaged) {
                bot->dormant = false;
            }
        }
        game::BotDecision decision;
        if (run_mode_ && bot->dormant) {
            bot->loiter_phase += dt * 0.22f;
            const core::Vec3 aim =
                bot->post + core::Vec3{std::cos(bot->loiter_phase), 0.0f, std::sin(bot->loiter_phase)} *
                                170.0f;
            decision.flight = game::steer_through(self, aim, core::Vec3::zero(),
                                                  bot->pilot.tuning.steering, ground);
        } else {
            decision = bot->pilot.update(dt, self, flight_.state(), combat_.alive(), ground,
                                         health_fraction);
        }
        // The manoeuvre the pilot asked for, flown by the same code the
        // player's Z and B run through.
        if (decision.maneuver != game::ManeuverKind::None) {
            bot->maneuver.start(decision.maneuver, decision.maneuver_direction,
                                bot->flight.state(), maneuver_tuning_);
        }
        bot->maneuver.apply(decision.flight, bot->flight.state(), maneuver_tuning_, dt);
        // Bitten: the shove goes into the flight model, and while the stun
        // lasts nobody is flying -- controls centred, wings limp, weapons
        // cold. The flight model tumbles it honestly from there.
        if (core::length_sq(slot.knockback) > 0.0f) {
            bot->flight.state().velocity = bot->flight.state().velocity + slot.knockback;
            slot.knockback = core::Vec3::zero();
            bot->pilot.notify_hit();
        }
        if (slot.stun > 0.0f) {
            decision.flight = game::FlightInput{};
            decision.fire = false;
            decision.breathe = false;
            decision.melee = false;
        }
        // Shocked or frozen: weapons cold. Chilled: stiff wings, a slack
        // stick and air that seems to thicken.
        const float slow = slot.status.slow(combat_.tuning.elements);
        if (slot.status.jammed()) {
            decision.fire = false;
            decision.breathe = false;
            decision.melee = false;
        }
        if (slow > 0.0f) {
            decision.flight.pitch *= 1.0f - 0.6f * slow;
            decision.flight.roll *= 1.0f - 0.6f * slow;
            decision.flight.yaw *= 1.0f - 0.6f * slow;
            decision.flight.flap *= 1.0f - slow;
        }
        const float sink_before = bot->flight.state().climb_rate;
        bot->flight.update(decision.flight, &terrain_, dt);
        if (slow > 0.0f && !bot->flight.state().grounded) {
            bot->flight.state().velocity =
                bot->flight.state().velocity * std::exp(-0.35f * slow * dt);
        }
        // The bot's pounce: a charging boost with the player in reach and near
        // its nose steers onto the player and rams on contact -- the answer
        // in kind to the player's, for the dragons that carry it.
        bot->pounce_cooldown = core::maxf(bot->pounce_cooldown - dt, 0.0f);
        if (bot->can_pounce && combat_.alive() && slot.stun <= 0.0f && !slot.status.jammed()) {
            const game::FlightState& me = bot->flight.state();
            const core::Vec3 to = flight_.state().position - me.position;
            const float r = core::length(to);
            const game::CombatTuning& ct = combat_.tuning;
            // On the charge, not the boost: a bot does not boost within
            // 160 m of the ground, which in a valley is nearly always.
            if (bot->pounce_timer <= 0.0f && bot->pounce_cooldown <= 0.0f && bot->pilot.charging() &&
                r < ct.pounce_range * 0.8f && r > 30.0f &&
                core::dot(me.forward(), to / core::maxf(r, 1e-3f)) > std::cos(core::radians(ct.pounce_cone_deg))) {
                bot->pounce_timer = ct.pounce_time;
            }
            if (bot->pounce_timer > 0.0f) {
                bot->pounce_timer -= dt;
                steer_pounce(bot->flight.state(), flight_.state().position, ct.pounce_turn, ct.pounce_speed, dt);
                const float contact = ct.ram_radius + ct.player_radius * growth_scale_;
                if (r < contact) {
                    combat_.hostile_ram(me.position, me.velocity, bot->element,
                                        slot.status.weaken(ct.elements));
                    bot->pounce_timer = 0.0f;
                    audio_.play(audio::Clip::Crack, 0.9f, 0.9f);
                    LOG_INFO("bot pounce landed (%s)", bot->hunter ? "hunter" : "ace");
                }
                if (bot->pounce_timer <= 0.0f) bot->pounce_cooldown = 6.0f;
            }
        }

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
            decision.melee = false;
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
            combat_.fire_hostile(muzzle, decision.fire_velocity,
                                 bot->pilot.tuning.damage * slot.status.weaken(combat_.tuning.elements),
                                 bot->element);
        }
        bot->breathing = decision.breathe;
        if (decision.breathe) {
            game::BreathScales scales = bot->breath.scales;
            scales.damage *= slot.status.weaken(combat_.tuning.elements);
            combat_.hostile_breath(muzzle, bot->flight.state().forward(), bot->model, scales,
                                   bot->element);
        }
        if (decision.melee) {
            combat_.hostile_melee(muzzle, bot->flight.state().forward(), self.position,
                                  bot->element, slot.status.weaken(combat_.tuning.elements));
            // The same lunge cost the player pays.
            game::FlightState& st = bot->flight.state();
            const float speed = core::length(st.velocity);
            const float cost = combat_.tuning.melee_lunge_speed_cost;
            if (speed > cost + 1.0f) st.velocity = st.velocity * ((speed - cost) / speed);
        }
        anim::RigAction action;
        action.breath = decision.breathe ? 1.0f : 0.0f;
        action.fire = decision.fire;
        if (decision.melee) {
            const game::MeleeGesture gesture = game::melee_gesture_for(
                self.position, self.forward(), self.right(), flight_.state().position, action.side);
            action.bite = gesture == game::MeleeGesture::Bite;
            action.claw = gesture == game::MeleeGesture::Claw;
            action.tail = gesture == game::MeleeGesture::Tail;
        }
        bot->rig.set_action(action);

        // The head tracks the player when close and hunting -- the tell that a
        // flame is coming, and where the flame visually comes from.
        if (bot->pilot.state() == game::BotState::Attack &&
            core::distance(self.position, flight_.state().position) < 350.0f) {
            bot->rig.set_aim_target(flight_.state().position);
        }

        {
            const game::FlightState& bs = bot->flight.state();
            bot->rig.set_ground(core::Mat4::trs(bs.position, bs.orientation, core::Vec3::one()) *
                                    model_at(bot->model).asset.matrix(),
                                [this](float x, float z) { return terrain_.surface_at(x, z); });
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
void App::emit_impact(core::Vec3 position, bool hostile, bool on_terrain, game::Element element) {
    core::Vec3 hot = hostile ? core::Vec3{1.2f, 1.6f, 2.4f} : core::Vec3{2.4f, 1.4f, 0.5f};
    core::Vec3 cool = hostile ? core::Vec3{0.15f, 0.3f, 0.9f} : core::Vec3{0.9f, 0.2f, 0.04f};
    if (element != game::Element::None) {
        hot = element_breaths_[int(element)].hot;
        cool = element_breaths_[int(element)].cool;
    }
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
        in.fire_held = true;  // and a charged shot every charge, once unlocked
        in.fury = true;       // released the moment it fills
        in.boost = true;  // likewise: a burn at t=0 and every cooldown after
        in.melee = true;  // and a bite every cooldown, for the lunge on a capture
        return in;
    }

    if (demo_active()) {
        in.cycle_target = demo_decision_.cycle_target;
        in.fire = demo_decision_.fire;
        // Its fire is a held wish: with the charged shot it charges and
        // lets go at full. The fury goes the moment it fills with a target
        // inside two thirds of its reach.
        in.fire_held = demo_decision_.fire;
        if (combat_.fury() >= 1.0f) {
            for (const game::Sentinel& s : combat_.sentinels()) {
                if (s.alive && !s.prey && core::distance(s.position, flight_.state().position) <
                                   combat_.tuning.fury_radius * 0.66f) {
                    in.fury = true;
                }
            }
        }
        in.breath = demo_decision_.breath;
        in.melee = demo_decision_.melee;
        in.boost = demo_decision_.boost;
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
    in.fire_held = input_.down(SDL_SCANCODE_G) || input_.gamepad_button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    in.fury = fury_requested_;
    in.boost = input_.pressed(SDL_SCANCODE_LSHIFT) && false;  // shift is tuck-dive
    in.boost = input_.pressed(SDL_SCANCODE_X) ||
               input_.gamepad_button(SDL_GAMEPAD_BUTTON_WEST);
    // Bite is edge-triggered like the fireball; the gamepad button is held
    // state, and the combat cooldown makes a held button a swing per cooldown,
    // which is the same thing the fireball does.
    in.melee = input_.pressed(SDL_SCANCODE_C) || input_.gamepad_button(SDL_GAMEPAD_BUTTON_EAST);
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
        if (sentinel.ground && tower_prop_.ok) {
            // The keep or the spire: its base on the ground under the slot
            // (which is the tower's middle), warmed when hit, rimed when
            // frozen, and its fire -- the element's colour, in the keep's
            // brazier or the spire's cradle -- shrinking as it dies.
            const int index = int(&sentinel - combat_.sentinels().data());
            int defence = -1;
            for (size_t d = 0; d < run_defence_slots_.size(); ++d) {
                if (run_defence_slots_[d] == index) defence = int(d);
            }
            const bool spire = defence_is_spire(defence);
            const PropModel& prop = spire ? spire_prop_ : tower_prop_;
            const float middle = spire ? SPIRE_MIDDLE : KEEP_MIDDLE;
            const float top = spire ? SPIRE_ORB_Y : KEEP_BRAZIER_Y;
            const core::Vec3 base = sentinel.position - core::Vec3{0.0f, middle, 0.0f};
            gfx::ModelUniforms tower;
            tower.model = core::Mat4::trs(base, core::Quat::identity(), core::Vec3::one());
            // Lifted a little: the atlas is dark stone, and a tower in the
            // shadow of a slope read as a black post at range.
            tower.tint = core::Vec4{1.35f + 0.6f * flash, 1.3f + 0.3f * flash, 1.25f, flash * 0.4f};
            if (sentinel.status.frozen > 0.0f) tower.tint = core::Vec4{0.8f, 1.0f, 1.4f, 0.15f};
            world_.draw_skinned(device_, pass, prop.mesh, tower, prop.joints, prop.textures,
                                model_sampler_);
            const core::Vec3 fire = game::element_colour(sentinel.element);
            const float jammed = sentinel.status.jammed() ? 0.4f : 1.0f;
            draw_ball(base + core::Vec3{0.0f, top + (spire ? 0.0f : 0.9f), (spire ? 0.0f : 2.0f)},
                      (spire ? 1.6f : 1.1f) * (0.6f + 0.6f * health) *
                          (1.0f + 0.08f * std::sin(time_seconds_ * 7.0f + float(index))),
                      core::lerp(fire, core::Vec3{1.0f, 0.8f, 0.3f}, flash),
                      (1.1f + flash) * jammed, true);
            continue;
        }
        if (sentinel.ground) {
            // Fallback when the prop is missing: a stone shaft and a brazier.
            gfx::ModelUniforms shaft;
            shaft.model = core::Mat4::trs(sentinel.position - core::Vec3{0.0f, 6.0f, 0.0f},
                                          core::Quat::identity(), core::Vec3{5.0f, 15.0f, 5.0f});
            const float stone = 0.30f + 0.5f * flash;
            shaft.tint = core::Vec4{stone, stone * 0.95f, stone * 0.9f, flash * 0.6f};
            world_.draw_mesh(device_, pass, sphere_mesh_, shaft);
            draw_ball(sentinel.position + core::Vec3{0.0f, 9.5f, 0.0f}, 2.2f + 1.8f * health,
                      core::lerp(core::Vec3{1.0f, 0.42f, 0.10f}, core::Vec3{1.0f, 0.8f, 0.3f}, flash),
                      0.9f + flash, true);
            continue;
        }
        const bool frozen = sentinel.status.frozen > 0.0f;
        const core::Vec3 base = frozen ? core::Vec3{0.55f, 0.8f, 1.0f} : core::Vec3{0.72f, 0.24f, 0.18f};
        const core::Vec3 colour = core::lerp(base, core::Vec3{1.0f, 0.55f, 0.10f}, frozen ? 0.0f : flash);
        draw_ball(sentinel.position, combat_.tuning.sentinel_radius * (1.0f + 0.18f * flash),
                  colour, frozen ? 0.25f : 0.18f + flash * 1.6f);
        // A smaller inner sphere shrinks as it takes damage: a health readout
        // that needs no UI and works at any distance or angle.
        draw_ball(sentinel.position, combat_.tuning.sentinel_radius * 0.55f * health,
                  core::Vec3{1.0f, 0.70f, 0.16f}, 1.0f, true);
    }

    for (const game::Projectile& projectile : combat_.projectiles()) {
        if (!projectile.alive) continue;
        const bool mine = projectile.team == game::Team::Player;
        core::Vec3 colour = mine ? core::Vec3{1.0f, 0.45f, 0.10f} : core::Vec3{0.45f, 0.80f, 1.0f};
        if (projectile.element != game::Element::None) colour = game::element_colour(projectile.element);
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
        core::Vec3 hot = mine ? core::Vec3{1.0f, 0.62f, 0.22f} : core::Vec3{0.62f, 0.82f, 1.0f};
        if (projectile.element != game::Element::None) {
            hot = core::lerp(game::element_colour(projectile.element), core::Vec3::one(), 0.25f);
        }
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
    // Called from draw_hud after hud_.begin().
    const ui::Tokens& tk = hud_.tokens();
    ImDrawList* draw = hud_.draw();
    const float width = hud_.width();
    const float height = hud_.height();
    const float margin = hud_.margin();
    const core::Mat4 view_proj = active_camera().view_projection(device_.aspect());
    char line[96];

    // ---- taking fire ----
    // A full-screen vignette rather than a number: peripheral, unmissable, and
    // it does not compete with the thing you are trying to aim at.
    if (damage_flash_ > 0.0f) {
        const float strength = core::saturate(damage_flash_);
        const ImU32 edge = (tk.danger & 0x00FFFFFF) | (ImU32(120.0f * strength) << 24);
        const ImU32 clear = tk.danger & 0x00FFFFFF;
        const float band = height * 0.22f;
        draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width, band), edge, edge, clear, clear);
        draw->AddRectFilledMultiColor(ImVec2(0, height - band), ImVec2(width, height), clear, clear,
                                      edge, edge);
    }

    // The player's status at the screen's edge: rime creeping in while
    // frozen or chilled, a flicker of heat while burning. Read without
    // looking away from the fight, like the damage flash.
    {
        const game::Status& st = combat_.player_status();
        auto edge_glow = [&](core::Vec3 rgb, float strength) {
            if (strength <= 0.01f) return;
            const ImU32 c = IM_COL32(int(rgb.x * 255.0f), int(rgb.y * 255.0f), int(rgb.z * 255.0f),
                                     int(core::saturate(strength) * 110.0f));
            const ImU32 clear = c & 0x00FFFFFF;
            const float band = height * 0.18f;
            draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(width, band), c, c, clear, clear);
            draw->AddRectFilledMultiColor(ImVec2(0, height - band), ImVec2(width, height), clear, clear, c, c);
            const float side = width * 0.12f;
            draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(side, height), c, clear, clear, c);
            draw->AddRectFilledMultiColor(ImVec2(width - side, 0), ImVec2(width, height), clear, c, c, clear);
        };
        edge_glow(game::element_colour(game::Element::Frost),
                  st.frozen > 0.0f ? 0.9f : st.chill * 0.45f);
        if (st.burn > 0.0f) {
            edge_glow(game::element_colour(game::Element::Fire),
                      0.35f + 0.2f * std::sin(time_seconds_ * 17.0f));
        }
        if (st.corrode > 0.0f) edge_glow(game::element_colour(game::Element::Blight), 0.16f);
    }

    // ---- health and breath, bottom centre, above the airspeed ----
    // They spent one build in the top-left corner and came back: in a fight
    // the eye lives at the bottom centre -- the airspeed, the pips, the
    // dragon's own body -- and a bar in the corner was read only once it was
    // already empty. So the cluster is one stack: bars, airspeed, pips.
    {
        const float plate_w = hud_.px(330.0f);
        const float plate_h = hud_.px(64.0f);
        const float speed_h = hud_.px(56.0f);
        const ImVec2 min(width * 0.5f - plate_w * 0.5f,
                         height - margin - speed_h - hud_.px(6.0f) - plate_h);
        hud_.plate(min, ImVec2(min.x + plate_w, min.y + plate_h));
        const float label_x = min.x + hud_.px(12.0f);
        const float bar_x = min.x + hud_.px(84.0f);
        const float bar_w = plate_w - hud_.px(96.0f);
        const float bar_h = hud_.px(12.0f);
        const float health = combat_.health_fraction();
        // The bar dims toward the danger red as it empties; below a third,
        // the threshold where disengaging is the right call, it flashes.
        ImU32 health_colour = tk.health;
        if (health < 0.33f) {
            const float pulse = 0.65f + 0.35f * std::sin(time_seconds_ * 6.0f);
            health_colour = (tk.danger & 0x00FFFFFF) | (ImU32(255.0f * pulse) << 24);
        }
        hud_.label(ImVec2(label_x, min.y + hud_.px(10.0f)), "HEALTH", tk.text_dim, 12.0f);
        hud_.bar(ImVec2(bar_x, min.y + hud_.px(11.0f)), bar_w, bar_h, health, health_colour);
        // The breath is labelled with what it is made of, in its colour.
        const core::Vec3 ec = game::element_colour(combat_.player_element);
        const ImU32 element_colour = IM_COL32(int(ec.x * 255.0f), int(ec.y * 255.0f), int(ec.z * 255.0f), 235);
        char element_label[16];
        std::snprintf(element_label, sizeof(element_label), "%s", game::element_name(combat_.player_element));
        for (char* c = element_label; *c; ++c) *c = char(std::toupper(static_cast<unsigned char>(*c)));
        hud_.label(ImVec2(label_x, min.y + hud_.px(36.0f)), element_label, element_colour, 12.0f);
        hud_.bar(ImVec2(bar_x, min.y + hud_.px(37.0f)), bar_w, bar_h, combat_.breath(),
                 combat_.breathing() ? tk.breath_hot : tk.breath);
        // The fury: a bar along the plate's foot, in the element's colour,
        // pulsing when full. The H pip alone was, in the playtest, "hard to
        // notice".
        if (combat_.abilities.fury) {
            const float fury = combat_.fury();
            const core::Vec3 c = game::element_colour(combat_.player_element);
            const bool full = fury >= 1.0f;
            const float pulse = full ? 0.6f + 0.4f * std::sin(time_seconds_ * 8.0f) : 0.85f;
            const ImU32 col = IM_COL32(int(c.x * 255.0f), int(c.y * 255.0f), int(c.z * 255.0f), int(255.0f * pulse));
            const float y = min.y + plate_h - hud_.px(7.0f);
            hud_.label(ImVec2(label_x, y - hud_.px(5.0f)), full ? "FURY!" : "FURY", full ? col : tk.text_dim, 9.0f);
            hud_.bar(ImVec2(bar_x, y), bar_w, hud_.px(4.0f), fury, col);
        }

        // What the player is suffering: one chip per status, over the plate.
        const game::Status& st = combat_.player_status();
        struct Chip {
            const char* text;
            game::Element element;
            float amount;  // 0..1 for the fill
        };
        Chip chips[7];
        int n = 0;
        char chill_text[24];
        if (st.frozen > 0.0f) {
            chips[n++] = {"FROZEN", game::Element::Frost, 1.0f};
        } else if (st.chill > 0.05f) {
            std::snprintf(chill_text, sizeof(chill_text), "CHILLED %d%%", int(st.chill * 100.0f));
            chips[n++] = {chill_text, game::Element::Frost, st.chill};
        }
        if (st.burn > 0.0f) chips[n++] = {"BURNING", game::Element::Fire, st.burn / combat_.tuning.elements.burn_time};
        if (st.corrode > 0.0f) chips[n++] = {"CORRODED", game::Element::Blight, st.corrode / combat_.tuning.elements.corrode_time};
        if (st.shock > 0.0f) chips[n++] = {"SHOCKED", game::Element::Storm, 1.0f};
        if (st.drench > 0.0f) chips[n++] = {"DRENCHED", game::Element::Tide, st.drench / combat_.tuning.elements.drench_time};
        if (st.stagger > 0.05f) chips[n++] = {"STAGGER", game::Element::Stone, st.stagger};
        float cx = min.x;
        const float chip_h = hud_.px(18.0f);
        for (int i = 0; i < n; ++i) {
            const float w = hud_.label_width(chips[i].text, 11.0f) + hud_.px(14.0f);
            const ImVec2 a(cx, min.y - chip_h - hud_.px(5.0f));
            const ImVec2 b(cx + w, min.y - hud_.px(5.0f));
            hud_.plate(a, b);
            const core::Vec3 c = game::element_colour(chips[i].element);
            const ImU32 col = IM_COL32(int(c.x * 255.0f), int(c.y * 255.0f), int(c.z * 255.0f), 235);
            draw->AddRectFilled(ImVec2(a.x, b.y - hud_.px(2.5f)),
                                ImVec2(a.x + w * core::saturate(chips[i].amount), b.y), col);
            hud_.label(ImVec2(a.x + hud_.px(7.0f), a.y + hud_.px(3.0f)), chips[i].text, col, 11.0f);
            cx += w + hud_.px(6.0f);
        }
    }

    // ---- ability readiness, beside the airspeed plate ----
    // Filling back to full is the cue, so it can be read at a glance without
    // parsing a countdown. Bite, fireball, boost, manoeuvre.
    {
        const float radius = hud_.px(14.0f);
        const float step = hud_.px(34.0f);
        const float y = height - margin - hud_.px(28.0f);
        const float x0 = width * 0.5f + hud_.px(75.0f) + hud_.px(24.0f);
        // Labelled with the pad's buttons when one is connected: "G" on a
        // controller was a key nobody could find.
        hud_.pip(ImVec2(x0, y), radius, 1.0f - combat_.melee_cooldown(), button_name('C'), tk.danger);
        hud_.pip(ImVec2(x0 + step, y), radius, 1.0f - combat_.fire_cooldown(), button_name('G'), tk.breath_hot);
        hud_.pip(ImVec2(x0 + step * 2.0f, y), radius, 1.0f - combat_.boost_cooldown(), button_name('X'), tk.cool);
        const float maneuver_ready =
            maneuver_.active() ? 0.0f
            : maneuver_tuning_.cooldown > 0.0f
                ? 1.0f - core::saturate(maneuver_.cooldown / maneuver_tuning_.cooldown)
                : 1.0f;
        hud_.pip(ImVec2(x0 + step * 3.0f, y), radius, maneuver_ready, button_name('Z'), tk.accent);
        // The learned abilities: U the second breath (in the colour of the
        // element it swaps to), H the fury filling.
        if (second_unlocked_) {
            const game::Element first = player_element_choice_ >= 0 ? game::Element(player_element_choice_)
                                                                    : player_model().breath.element;
            const bool run = run_mode_ && hoard_run_.phase() != game::HoardPhase::Idle;
            // What U turns to next: in a run the other of the two, in the
            // arena the next round the cycle.
            const game::Element other =
                run ? (using_second_ ? first : second_element_)
                    : game::Element((int(first) + (arena_breath_index_ + 1) % game::ELEMENT_COUNT) %
                                    game::ELEMENT_COUNT);
            const core::Vec3 c = game::element_colour(other);
            hud_.pip(ImVec2(x0 + step * 4.0f, y), radius, 1.0f, button_name('U'),
                     IM_COL32(int(c.x * 255.0f), int(c.y * 255.0f), int(c.z * 255.0f), 235));
        }
        if (combat_.abilities.fury) {
            const float fury = combat_.fury();
            const bool ready = fury >= 1.0f && std::fmod(time_seconds_, 0.6f) < 0.4f;
            hud_.pip(ImVec2(x0 + step * 5.0f, y), radius, fury, button_name('H'), ready ? tk.lock : tk.flame);
        }
        if (combat_.melee_combo() > 1) {
            std::snprintf(line, sizeof(line), "x%d", combat_.melee_combo());
            hud_.numeral(ImVec2(x0, y - radius - hud_.px(20.0f)), line, tk.breath_hot, 20.0f,
                         ui::Align::Centre);
        }
    }

    // ---- the match, in the top strip and writ large ----
    {
        if (match_.phase() == game::MatchPhase::Countdown) {
            std::snprintf(line, sizeof(line), "%d", int(std::ceil(match_.countdown_remaining())));
            hud_.numeral(ImVec2(width * 0.5f, height * 0.28f), line, tk.accent, 96.0f, ui::Align::Centre);
            hud_.label(ImVec2(width * 0.5f, height * 0.28f + hud_.px(100.0f)), "weapons live at zero",
                       tk.text_dim, 14.0f, ui::Align::Centre);
        } else if (match_.phase() == game::MatchPhase::Fighting ||
                   match_.phase() == game::MatchPhase::Results) {
            const float strip_w = hud_.px(420.0f);
            const float strip_h = hud_.px(46.0f);
            const ImVec2 min(width * 0.5f - strip_w * 0.5f, margin);
            hud_.plate(min, ImVec2(min.x + strip_w, min.y + strip_h));
            // Score, as numerals: YOU n : m BOTS.
            std::snprintf(line, sizeof(line), "%d : %d", match_.player_kills(), match_.player_deaths());
            const float score_w = hud_.numeral_width(line, 30.0f);
            const float score_x = min.x + hud_.px(70.0f);
            hud_.label(ImVec2(score_x - hud_.px(8.0f), min.y + hud_.px(17.0f)), "YOU", tk.text_dim, 11.0f,
                       ui::Align::Right);
            hud_.numeral(ImVec2(score_x, min.y + hud_.px(7.0f)), line, tk.text, 30.0f);
            hud_.label(ImVec2(score_x + score_w + hud_.px(8.0f), min.y + hud_.px(17.0f)), "BOTS",
                       tk.text_dim, 11.0f);
            float x = score_x + score_w + hud_.px(52.0f);
            draw->AddLine(ImVec2(x, min.y + hud_.px(10.0f)), ImVec2(x, min.y + strip_h - hud_.px(10.0f)),
                          tk.plate_edge, 1.0f);
            x += hud_.px(14.0f);
            // The clock: time remaining with a limit, the fight's duration without.
            const float seconds = match_.phase() == game::MatchPhase::Fighting
                                      ? (match_.settings.time_limit > 0.0f ? match_.time_remaining()
                                                                           : match_.fight_duration())
                                      : match_.fight_duration();
            std::snprintf(line, sizeof(line), "%d:%02d", int(seconds) / 60, int(seconds) % 60);
            hud_.numeral(ImVec2(x, min.y + hud_.px(7.0f)), line, tk.text, 30.0f);
            x += hud_.numeral_width(line, 30.0f) + hud_.px(14.0f);
            draw->AddLine(ImVec2(x, min.y + hud_.px(10.0f)), ImVec2(x, min.y + strip_h - hud_.px(10.0f)),
                          tk.plate_edge, 1.0f);
            x += hud_.px(14.0f);
            std::snprintf(line, sizeof(line), "first to %d", match_.settings.target_kills);
            hud_.label(ImVec2(x, min.y + hud_.px(15.0f)), line, tk.text_dim, 13.0f);

            if (match_.phase() == game::MatchPhase::Results) {
                const char* verdict = match_.draw() ? "DRAW" : (match_.player_won() ? "VICTORY" : "DEFEAT");
                const ImU32 colour = match_.draw() ? tk.text : match_.player_won() ? tk.accent : tk.danger;
                hud_.numeral(ImVec2(width * 0.5f, height * 0.28f), verdict, colour, 72.0f, ui::Align::Centre);
                std::snprintf(line, sizeof(line), "%d : %d in %.0f s   --   ENTER to rematch",
                              match_.player_kills(), match_.player_deaths(), match_.fight_duration());
                hud_.label(ImVec2(width * 0.5f, height * 0.28f + hud_.px(80.0f)), line, tk.text, 15.0f,
                           ui::Align::Centre);
            }
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
            const ImU32 colour = combat_.has_lock() ? tk.lock : tk.text_dim;
            const float arm = hud_.px(14.0f);
            const float gap = hud_.px(4.5f);
            const float thick = hud_.px(2.0f);
            draw->AddLine(ImVec2(screen.x - arm, screen.y), ImVec2(screen.x - gap, screen.y), colour, thick);
            draw->AddLine(ImVec2(screen.x + gap, screen.y), ImVec2(screen.x + arm, screen.y), colour, thick);
            draw->AddLine(ImVec2(screen.x, screen.y - arm), ImVec2(screen.x, screen.y - gap), colour, thick);
            draw->AddLine(ImVec2(screen.x, screen.y + gap), ImVec2(screen.x, screen.y + arm), colour, thick);
            draw->AddCircleFilled(ImVec2(screen.x, screen.y), hud_.px(2.0f), colour, 10);
            // A ring when locked: the marker doubles as the "shots will bend"
            // cue, so it visibly changes state with the lock.
            if (combat_.has_lock() && !manual_aim_) {
                draw->AddCircle(ImVec2(screen.x, screen.y), arm * 0.75f, colour, 24, hud_.px(1.5f));
            }
            // A direct hit: an X round the marker; a kill, a bigger red one.
            if (hitmarker_ > 0.0f || killmarker_ > 0.0f) {
                const bool kill = killmarker_ > 0.0f;
                const float r0 = hud_.px(kill ? 10.0f : 7.0f), r1 = hud_.px(kill ? 20.0f : 14.0f);
                const ImU32 c = kill ? tk.danger : IM_COL32(255, 255, 255, 230);
                for (int q = 0; q < 4; ++q) {
                    const float sx = (q & 1) ? 1.0f : -1.0f, sy = (q & 2) ? 1.0f : -1.0f;
                    draw->AddLine(ImVec2(screen.x + sx * r0, screen.y + sy * r0),
                                  ImVec2(screen.x + sx * r1, screen.y + sy * r1), c, hud_.px(kill ? 3.0f : 2.2f));
                }
            }
            // The charge gathering: an arc round the marker, full at a
            // charged shot, in the breath's element.
            if (combat_.charge() > 0.0f) {
                const core::Vec3 c = game::element_colour(combat_.player_element);
                const ImU32 col = IM_COL32(int(c.x * 255.0f), int(c.y * 255.0f), int(c.z * 255.0f),
                                           combat_.charge() >= 1.0f ? 255 : 200);
                hud_.arc(ImVec2(screen.x, screen.y), arm * 1.5f, -core::HALF_PI, core::HALF_PI * 3.0f,
                         combat_.charge(), col, hud_.px(3.0f));
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
        if (!locked && range > combat_.tuning.mark_range) continue;
        // Prey carry their own chevrons; a bracket only on the one locked.
        if (sentinel.prey && !locked) continue;
        // A bot holding its flame is the most urgent thing on screen.
        bool flaming = false;
        for (const auto& bot : bots_) {
            if (bot->slot == index && bot->breathing) flaming = true;
        }
        // Who it is, in a run: a hunter is the danger red and says so, a
        // rival says RIVAL (and whether it is still circling its post), a
        // tower says TOWER. The playtest could not tell a rival from a
        // hunter -- both were a bracket and a range.
        const BotShip* who = nullptr;
        for (const auto& bot : bots_) {
            if (bot->slot == index) who = bot.get();
        }
        const bool hunter = who && who->hunter;
        const char* role = sentinel.prey ? "PREY "
                           : !run_mode_ ? ""
                           : hunter ? "HUNTER "
                           : who ? (who->dormant ? "RIVAL (at post) " : "RIVAL ")
                           : sentinel.ground ? "TOWER " : "";
        // And what it is made of, so a frost tower is avoided by a player who
        // cannot afford a freeze -- and shrugged off by one who breathes frost.
        char tag[64];
        if (sentinel.element != game::Element::None) {
            char name[16];
            std::snprintf(name, sizeof(name), "%s", game::element_name(sentinel.element));
            for (char* c = name; *c; ++c) *c = char(std::toupper(static_cast<unsigned char>(*c)));
            std::snprintf(tag, sizeof(tag), "%s%s%s  ", role, *role ? "- " : "", name);
        } else {
            std::snprintf(tag, sizeof(tag), "%s%s", role, *role ? " " : "");
        }
        // The locked target is unmistakable. Everything else is a faint mark:
        // if every target looks equally important, none of them read.
        const ImU32 colour = flaming ? tk.flame : locked ? tk.lock : hunter ? tk.danger : tk.mark;
        if (on_screen) {
            // Brackets rather than a box: they read as a target at any size and
            // do not obscure what they surround.
            const float half = core::clampf(2600.0f / core::maxf(range, 1.0f), hud_.px(10.0f), hud_.px(60.0f));
            hud_.bracket(screen, half, colour, hud_.px(1.8f));
            std::snprintf(line, sizeof(line), flaming ? "%s%.0f m  FLAME" : "%s%.0f m", tag, range);
            hud_.label(ImVec2(screen.x + half + hud_.px(5.0f), screen.y - hud_.px(7.0f)), line,
                       flaming ? tk.flame : locked ? tk.lock : hunter ? tk.danger : tk.mark, 12.0f);
            // A rival's health under the bracket: whether to press the attack.
            // (Its aggression was drawn there too for one build and told the
            // player nothing they acted on; the Combat panel still lists it.)
            const BotShip* ship = nullptr;
            for (const auto& bot : bots_) {
                if (bot->slot == index) ship = bot.get();
            }
            float below = screen.y + half + hud_.px(4.0f);
            if (ship || sentinel.ground) {
                const float bar_w = core::maxf(half * 2.0f, hud_.px(40.0f));
                const float health_h = hud_.px(4.5f);
                const float health = sentinel.max_health > 0.0f ? sentinel.health / sentinel.max_health : 0.0f;
                hud_.bar(ImVec2(screen.x - bar_w * 0.5f, below), bar_w, health_h, health, tk.health);
                // What its burn or corrosion will still take, pulsing in the
                // element's colour at the end of the bar: the damage over
                // time, told apart from the hits that land.
                {
                    const game::Status& st = sentinel.status;
                    const game::ElementTuning& et = combat_.tuning.elements;
                    const float pending = st.burn * et.burn_dps + st.corrode * et.corrode_dps;
                    if (pending > 0.0f && sentinel.max_health > 0.0f) {
                        const float share = core::minf(pending / sentinel.max_health, health);
                        const game::Element e = st.burn > 0.0f ? game::Element::Fire : game::Element::Blight;
                        const core::Vec3 ec = game::element_colour(e);
                        const float pulse = 0.55f + 0.45f * std::sin(time_seconds_ * 9.0f);
                        const ImU32 col = IM_COL32(int(ec.x * 255.0f), int(ec.y * 255.0f), int(ec.z * 255.0f),
                                                   int(255.0f * pulse));
                        const float x1 = screen.x - bar_w * 0.5f + bar_w * health;
                        draw->AddRectFilled(ImVec2(x1 - bar_w * share, below), ImVec2(x1, below + health_h), col);
                    }
                }
                below += health_h + hud_.px(3.0f);
            }
            // Its status, in the element's colour: the payoff of an element
            // made visible at any range.
            const game::Status& st = sentinel.status;
            const char* suffering = st.frozen > 0.0f   ? "FROZEN"
                                    : st.shock > 0.0f  ? "SHOCKED"
                                    : st.burn > 0.0f   ? "BURNING"
                                    : st.corrode > 0.0f ? "CORRODED"
                                    : st.drench > 0.0f ? "DRENCHED"
                                    : st.chill > 0.3f  ? "CHILLED"
                                    : sentinel.stun > 0.0f ? "STUNNED"
                                                          : nullptr;
            if (suffering) {
                const game::Element e = st.frozen > 0.0f || st.chill > 0.3f ? game::Element::Frost
                                        : st.shock > 0.0f  ? game::Element::Storm
                                        : st.burn > 0.0f   ? game::Element::Fire
                                        : st.corrode > 0.0f ? game::Element::Blight
                                        : st.drench > 0.0f ? game::Element::Tide
                                                           : game::Element::Stone;
                const core::Vec3 c = sentinel.stun > 0.0f && !st.any() ? core::Vec3{0.89f, 0.67f, 0.24f}
                                                                       : game::element_colour(e);
                hud_.label(ImVec2(screen.x, below), suffering,
                           IM_COL32(int(c.x * 255.0f), int(c.y * 255.0f), int(c.z * 255.0f), 235), 11.0f,
                           ui::Align::Centre);
            }
            if (locked) {
                draw->AddCircle(ImVec2(screen.x, screen.y), half * 1.35f, colour, 28, hud_.px(1.4f));
                // Where the shot is actually going. Drawing the lead point makes
                // the assist legible instead of magic -- and when the assist is
                // turned down, it shows exactly how much lead is left to the
                // player.
                ImVec2 lead;
                if (project_to_screen(view_proj, combat_.lock_intercept(), width, height, lead)) {
                    draw->AddLine(ImVec2(screen.x, screen.y), lead, tk.accent_dim, hud_.px(1.2f));
                    draw->AddCircleFilled(lead, hud_.px(3.5f), tk.lock, 12);
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
            hud_.edge_arrow(ImVec2(width * 0.5f, height * 0.5f), direction,
                            core::minf(width, height) * 0.36f, colour, hud_.px(8.0f));
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
            const ImU32 colour = (tk.danger & 0x00FFFFFF) | (ImU32(210.0f * fade) << 24);
            // A thick arc rather than an arrow: it reads at the very edge of
            // attention, which is where a player looking at their target is.
            hud_.edge_arc(ImVec2(width * 0.5f, height * 0.5f), direction,
                          core::minf(width, height) * 0.30f, colour, hud_.px(7.0f));
        }
    }

    if (!combat_.alive()) {
        hud_.numeral(ImVec2(width * 0.5f, height * 0.40f), "DOWNED", tk.danger, 56.0f, ui::Align::Centre);
    }
}

void App::build_combat_ui() {
    game::CombatTuning& t = combat_.tuning;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 402.0f, 212.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390, 0), ImGuiCond_FirstUseEver);
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

    // ---- the element ----
    // At the top: what you breathe decides what you shrug off and what your
    // hits leave behind, which is the first thing to try when playtesting.
    ImGui::SeparatorText("element");
    {
        const LoadedModel& model = player_model();
        char species[48];
        std::snprintf(species, sizeof(species), "the species' (%s)", game::element_name(model.breath.element));
        const char* current = player_element_choice_ < 0 ? species : game::element_name(game::Element(player_element_choice_));
        if (ImGui::BeginCombo("you breathe", current)) {
            if (ImGui::Selectable(species, player_element_choice_ < 0)) {
                player_element_choice_ = -1;
                refresh_player_element();
            }
            for (int e = 0; e < game::ELEMENT_COUNT; ++e) {
                if (ImGui::Selectable(game::element_name(game::Element(e)), player_element_choice_ == e)) {
                    player_element_choice_ = e;
                    refresh_player_element();
                }
            }
            ImGui::EndCombo();
        }
        const game::Status& st = combat_.player_status();
        ImGui::TextDisabled("you: burn %.1f  chill %.2f  frozen %.1f  corrode %.1f  shock %.1f  drench %.1f  stagger %.2f",
                            st.burn, st.chill, st.frozen, st.corrode, st.shock, st.drench, st.stagger);
        if (ImGui::TreeNode("element dials")) {
            game::ElementTuning& et = combat_.tuning.elements;
            ImGui::SliderFloat("own element lands", &et.same_resist, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("status per breath second", &et.breath_weight, 0.1f, 4.0f, "%.2f");
            ImGui::SliderFloat("status per bite", &et.melee_weight, 0.0f, 2.0f, "%.2f");
            ImGui::SeparatorText("fire: burn");
            ImGui::SliderFloat("burn time", &et.burn_time, 0.0f, 10.0f, "%.1f s");
            ImGui::SliderFloat("burn dps", &et.burn_dps, 0.0f, 30.0f, "%.1f");
            ImGui::SeparatorText("frost: chill, freeze");
            ImGui::SliderFloat("chill per hit", &et.chill_per_hit, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("thaw per second", &et.chill_thaw, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("slow at full chill", &et.chill_slow, 0.0f, 0.9f, "%.2f");
            ImGui::SliderFloat("freeze (enemy)", &et.freeze_time, 0.0f, 5.0f, "%.1f s");
            ImGui::SliderFloat("freeze (you)", &et.player_freeze_time, 0.0f, 3.0f, "%.1f s");
            ImGui::SeparatorText("blight: corrode");
            ImGui::SliderFloat("corrode time", &et.corrode_time, 0.0f, 12.0f, "%.1f s");
            ImGui::SliderFloat("extra damage taken", &et.corrode_vulnerability, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("corrode dps", &et.corrode_dps, 0.0f, 15.0f, "%.1f");
            ImGui::SeparatorText("storm: shock, arc");
            ImGui::SliderFloat("jam", &et.shock_jam, 0.0f, 2.0f, "%.2f s");
            ImGui::SliderFloat("arc range", &et.shock_chain_range, 0.0f, 250.0f, "%.0f m");
            ImGui::SliderFloat("arc share", &et.shock_chain_share, 0.0f, 1.0f, "%.2f");
            ImGui::SeparatorText("tide: drench");
            ImGui::SliderFloat("drench time", &et.drench_time, 0.0f, 10.0f, "%.1f s");
            ImGui::SliderFloat("fireball shove", &et.drench_push, 0.0f, 40.0f, "%.0f m/s");
            ImGui::SeparatorText("stone: stagger");
            ImGui::SliderFloat("stagger per hit", &et.stagger_per_hit, 0.05f, 1.0f, "%.2f");
            ImGui::SliderFloat("stagger recovery", &et.stagger_recover, 0.0f, 2.0f, "%.2f /s");
            ImGui::SliderFloat("stagger stun", &et.stagger_stun, 0.0f, 3.0f, "%.1f s");
            ImGui::SliderFloat("stagger knock (you)", &et.stagger_knock, 0.0f, 40.0f, "%.0f m/s");
            ImGui::TreePop();
        }
    }

    // ---- the run (DIRECTION.md row 3) ----
    // Above the match because it is the question the build is asking now: is
    // the corridor under pressure more fun than the arena?
    ImGui::SeparatorText("run");
    if (!run_mode_) {
        if (ImGui::Button("start run")) start_run(uint32_t(std::max(run_seed_input_, 1)));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputInt("seed", &run_seed_input_);
        ImGui::SameLine();
        if (ImGui::SmallButton("random")) start_run(fresh_seed());
    } else {
        ImGui::Text("a %s, %s   growth %.0f", game::valley_kind_name(hoard_run_.layout().kind),
                    game::growth_stage_name(hoard_run_.stage()), hoard_run_.growth());
        ImGui::Text("run %s   seed %u   hoard %.0f   caches %d/%zu   kills %d   hunters %d   %.0f s",
                    hoard_run_.phase_name(), hoard_run_.layout().seed, hoard_run_.hoard(),
                    hoard_run_.caches_collected(), hoard_run_.layout().caches.size(),
                    hoard_run_.kills(), hoard_run_.hunters_loosed(), hoard_run_.elapsed());
        if (ImGui::Button("retry (R)")) start_run(run_seed_);
        ImGui::SameLine();
        if (ImGui::Button("new valley (enter)")) start_run(fresh_seed());
        ImGui::SameLine();
        if (ImGui::Button("leave run")) end_run();
        ImGui::SameLine();
        if (ImGui::Button("skip to next valley") && hoard_run_.skip_valley()) advance_valley();
        ImGui::Text("valley %d of %d   banked %.0f   prey eaten %d, %d grazing",
                    hoard_run_.valley() + 1, hoard_run_.settings.valleys, hoard_run_.banked(),
                    hoard_run_.prey_eaten(), prey_.alive());
    }
    ImGui::TextDisabled("records: %d runs, %d banked, best hoard %.0f, fastest %.0f s",
                        run_records_.runs, run_records_.banked, run_records_.best_hoard,
                        run_records_.best_time);
    if (ImGui::TreeNode("run dials (apply at the next start)")) {
        game::HoardRunSettings& r = run_dials_;
        ImGui::SeparatorText("the descent");
        ImGui::SliderInt("valleys", &r.valleys, 1, 6);
        ImGui::SliderInt("towers per valley deeper", &r.depth_towers, 0, 4);
        ImGui::SliderInt("rivals per valley deeper", &r.depth_rivals, 0, 3);
        ImGui::SliderFloat("hoard value per valley deeper", &r.depth_value, 0.0f, 1.0f, "+%.2f");
        ImGui::SliderFloat("hunter clock per valley deeper", &r.depth_pressure, 0.3f, 1.2f, "x%.2f");
        ImGui::SliderFloat("enemy damage per valley deeper", &r.depth_enemy_damage, 0.0f, 1.5f, "+%.2f");
        ImGui::SliderFloat("enemy health per valley deeper", &r.depth_enemy_health, 0.0f, 1.5f, "+%.2f");
        ImGui::SliderFloat("enemy aggression per valley deeper", &r.depth_enemy_aggression, 0.0f, 0.4f, "+%.2f");
        ImGui::SeparatorText("prey");
        ImGui::SliderInt("herds", &r.herds, 0, 6);
        ImGui::SliderInt("herd size", &r.herd_size, 1, 16);
        ImGui::SeparatorText("the valley");
        ImGui::SliderInt("rivals", &r.rivals, 0, 6);
        ImGui::SliderInt("slope towers", &r.defences, 0, 8);
        ImGui::Checkbox("a tower guards each cache", &r.guards);
        ImGui::SliderInt("caches", &r.caches, 1, 6);
        ImGui::SliderFloat("cache value", &r.cache_value, 10.0f, 500.0f, "%.0f");
        ImGui::SliderFloat("cache radius", &r.cache_radius, 8.0f, 60.0f, "%.0f m");
        ImGui::SliderFloat("collect time", &r.collect_time, 0.5f, 8.0f, "%.1f s");
        ImGui::SliderFloat("pass radius", &r.pass_radius, 30.0f, 150.0f, "%.0f m");
        ImGui::SliderFloat("engage range", &r.engage_range, 100.0f, 1200.0f, "%.0f m");
        ImGui::SliderFloat("first hunter (0 = by corridor)", &r.pressure_after, 0.0f, 400.0f, "%.0f s");
        ImGui::SliderFloat("hunter delay x flight time", &r.pressure_scale, 0.5f, 4.0f, "%.2f");
        ImGui::SliderFloat("pressure interval", &r.pressure_interval, 5.0f, 120.0f, "%.0f s");
        ImGui::SliderInt("max hunters", &r.max_hunters, 0, 6);
        ImGui::SliderFloat("cache offset", &r.cache_offset_max, 20.0f, 300.0f, "%.0f m");
        ImGui::SliderFloat("defence offset min", &r.defence_offset_min, 50.0f, 400.0f, "%.0f m");
        ImGui::SliderFloat("defence offset max", &r.defence_offset_max, 100.0f, 600.0f, "%.0f m");
        ImGui::SeparatorText("bounties and growth");
        ImGui::SliderFloat("tower bounty", &r.bounty_tower, 0.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("rival bounty", &r.bounty_rival, 0.0f, 300.0f, "%.0f");
        ImGui::SliderFloat("hunter bounty", &r.bounty_hunter, 0.0f, 400.0f, "%.0f");
        ImGui::SliderFloat("heal on kill", &r.heal_on_kill, 0.0f, 80.0f, "%.0f");
        ImGui::SliderFloat("grow young at", &r.grow_young, 20.0f, 600.0f, "%.0f");
        ImGui::SliderFloat("grow adult at", &r.grow_adult, 50.0f, 1200.0f, "%.0f");
        ImGui::SliderFloat("grow elder at", &r.grow_elder, 100.0f, 2000.0f, "%.0f");
        ImGui::SliderFloat("grow ancient at", &r.grow_ancient, 150.0f, 3000.0f, "%.0f");
        ImGui::SliderFloat("guard distance min", &r.guard_min, 20.0f, 250.0f, "%.0f m");
        ImGui::SliderFloat("guard distance max", &r.guard_max, 30.0f, 400.0f, "%.0f m");
        ImGui::SeparatorText("towers (live)");
        ImGui::SliderFloat("tower range", &t.defence_range, 100.0f, 900.0f, "%.0f m");
        ImGui::SliderFloat("tower damage", &t.defence_damage, 1.0f, 40.0f, "%.0f");
        ImGui::SliderFloat("tower interval", &t.defence_fire_interval, 0.5f, 8.0f, "%.1f s");
        ImGui::SliderFloat("bolt speed", &t.defence_projectile_speed, 60.0f, 400.0f, "%.0f m/s");
        ImGui::SliderFloat("bolt gravity", &t.defence_gravity, 0.0f, 40.0f, "%.0f m/s2");
        ImGui::SliderFloat("bolt spread", &t.defence_spread, 0.0f, 40.0f, "%.0f m");
        ImGui::SliderFloat("tower health", &t.defence_health, 20.0f, 300.0f, "%.0f");
        ImGui::SliderFloat("breath that lands on stone", &t.defence_breath_resist, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("close / grounded fire rate", &t.defence_close_rate, 0.5f, 4.0f, "%.2fx");
        ImGui::SliderFloat("close / grounded spread", &t.defence_close_spread, 0.05f, 1.0f, "%.2fx");
        ImGui::SliderFloat("close range", &t.defence_close_range, 50.0f, 500.0f, "%.0f m");
        ImGui::SliderFloat("bolt splash", &t.defence_splash, 0.0f, 30.0f, "%.0f m");
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("prey (live)")) {
        game::PreyTuning& p = prey_.tuning;
        ImGui::SliderFloat("gallop", &p.run_speed, 4.0f, 40.0f, "%.1f m/s");
        ImGui::SliderFloat("notices within", &p.notice_range, 20.0f, 600.0f, "%.0f m");
        ImGui::SliderFloat("ignores a dragon above", &p.notice_height, 10.0f, 300.0f, "%.0f m");
        ImGui::SliderFloat("bolts within", &p.bolt_range, 10.0f, 400.0f, "%.0f m");
        ImGui::SliderFloat("calm after", &p.calm_time, 0.5f, 30.0f, "%.1f s");
        ImGui::SliderFloat("swoop reach", &p.grab_radius, 2.0f, 20.0f, "%.1f m");
        ImGui::SliderFloat("swoop height", &p.grab_height, 2.0f, 25.0f, "%.1f m");
        ImGui::SliderFloat("growth per meal", &p.growth, 0.0f, 80.0f, "%.0f");
        ImGui::SliderFloat("health per meal", &p.heal, 0.0f, 60.0f, "%.0f");
        ImGui::SliderFloat("prey health", &p.health, 1.0f, 100.0f, "%.0f");
        ImGui::SliderFloat("carcass lasts", &p.carcass_time, 5.0f, 240.0f, "%.0f s");
        ImGui::TreePop();
    }
    ImGui::Separator();

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
    ImGui::TextDisabled("F or LMB breath, G fireball, C bite, X boost, T relock");
    ImGui::TextDisabled("gamepad: LB breath, RB fireball, B bite, X boost, R-stick click relock");

    // Melee first: it is the newest answer to the playtest's complaint (close
    // to the rival with the wrong heading, nothing to do but circle), and its
    // reach and cost are what decide whether a close fight resolves.
    ImGui::Separator();
    ImGui::Text("melee  (C / B)  %s%s", combat_.melee_cooldown() > 0.0f ? "recovering" : "ready",
                combat_.melee_combo() > 1 ? "  CHAIN" : "");
    // The room to learn it in: six passive dummies laid out ahead of the
    // current heading, no return fire; R flies the line again.
    if (ImGui::Button("training room")) spawn_training_room();
    ImGui::SameLine();
    if (ImGui::Button("sentinel wave")) {
        bots_.clear();
        combat_.reset(&terrain_, flight_.state().position, 20260824u);
    }
    ImGui::SliderFloat("bite range", &t.bite_range, 5.0f, 60.0f, "%.0f m");
    ImGui::SliderFloat("bite cone", &t.bite_half_angle_deg, 10.0f, 90.0f, "%.0f deg half");
    ImGui::SliderFloat("strike range", &t.strike_range, 0.0f, 40.0f, "%.0f m");
    ImGui::SliderFloat("bite damage", &t.bite_damage, 0.0f, 80.0f, "%.0f");
    ImGui::SliderFloat("strike damage", &t.strike_damage, 0.0f, 60.0f, "%.0f");
    ImGui::SliderFloat("melee cooldown", &t.melee_cooldown, 0.2f, 4.0f, "%.2f s");
    ImGui::SliderFloat("stun", &t.melee_stun, 0.0f, 4.0f, "%.1f s");
    ImGui::SliderFloat("knockback", &t.melee_knockback, 0.0f, 40.0f, "%.0f m/s");
    ImGui::SliderFloat("combo bonus", &t.melee_combo_bonus, 0.0f, 1.0f, "+%.2f per hit");
    ImGui::SliderFloat("lunge speed cost", &t.melee_lunge_speed_cost, 0.0f, 12.0f, "%.1f m/s");
    ImGui::SliderFloat("bot melee damage", &t.hostile_melee_damage, 0.0f, 40.0f, "%.0f");
    ImGui::SliderFloat("bot charge range", &bot_tuning_.charge_range, 0.0f, 600.0f, "%.0f m");

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

    // Personality: how much of a fight the bots want. Every pilot draws its
    // own around this at spawn; health moves it live (the panel shows each
    // bot's nerve in the roster below).
    ImGui::SliderFloat("bot aggression", &bot_tuning_.aggression, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("aggression spread", &bot_tuning_.aggression_spread, 0.0f, 0.5f, "%.2f");
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
        ImGui::TextDisabled("bot %zu  %-7s  %5.0f hp  aggr %.2f nerve %.2f  %4.0f m/s  %s", i,
                            slot.alive ? bot->pilot.state_name() : "down", slot.health,
                            bot->pilot.aggression(), bot->pilot.nerve(), bot->flight.state().airspeed,
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
        ImGui::SliderFloat("bracket range", &t.mark_range, 200.0f, 5000.0f, "%.0f m");
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

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 402.0f, 172.0f),
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

    if (ImGui::CollapsingHeader("Aerobatics")) {
        ImGui::TextDisabled("Z (the way the stick is held) or d-pad left/right: roll; B / d-pad up: flip");
        ImGui::SliderFloat("roll dodge push", &maneuver_tuning_.roll_dodge_push, 0.0f, 120.0f, "%.0f m/s2");
        ImGui::Text("%s", maneuver_.active() ? (maneuver_.kind == game::ManeuverKind::Roll ? "ROLLING" : "FLIPPING")
                                             : (maneuver_.cooldown > 0.0f ? "recovering" : "ready"));
        ImGui::SliderFloat("roll duration", &maneuver_tuning_.roll_duration, 0.3f, 2.0f, "%.2f s");
        ImGui::SliderFloat("roll agility", &maneuver_tuning_.roll_agility, 1.0f, 4.0f, "x%.1f");
        ImGui::SliderFloat("roll dodge", &maneuver_tuning_.roll_dodge_impulse, 0.0f, 20.0f, "%.0f m/s");
        ImGui::SliderFloat("flip agility", &maneuver_tuning_.flip_agility, 1.0f, 4.0f, "x%.1f");
        ImGui::SliderFloat("flip min speed", &maneuver_tuning_.flip_min_airspeed, 10.0f, 50.0f, "%.0f m/s");
        ImGui::SliderFloat("manoeuvre cooldown", &maneuver_tuning_.cooldown, 0.0f, 4.0f, "%.1f s");
    }
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
        ImGui::SliderFloat("walk speed", &t.walk_speed, 0.0f, 20.0f, "%.1f m/s");
        ImGui::SliderFloat("walk turn", &t.walk_turn_rate, 0.2f, 4.0f, "%.1f rad/s");
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
    ImGui::TextDisabled("gamepad: left stick, A flap, triggers dive/brake, d-pad left/right roll, up flip");
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
                                  world_.scene().view_params.z, active_camera().position);
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
        // The run's props cast shadows: a tower's shadow on the slope is how
        // its height reads from the air.
        if (run_mode_) {
            if (tower_prop_.ok) {
                for (size_t d = 0; d < run_defence_slots_.size(); ++d) {
                    const int slot = run_defence_slots_[d];
                    if (slot < 0 || size_t(slot) >= combat_.sentinels().size()) continue;
                    const game::Sentinel& s = combat_.sentinels()[size_t(slot)];
                    if (!s.alive || !s.ground) continue;
                    const bool spire = defence_is_spire(int(d));
                    const PropModel& prop = spire ? spire_prop_ : tower_prop_;
                    gfx::ModelUniforms m;
                    m.model = core::Mat4::trs(
                        s.position - core::Vec3{0.0f, spire ? SPIRE_MIDDLE : KEEP_MIDDLE, 0.0f},
                        core::Quat::identity(), core::Vec3::one());
                    world_.draw_skinned_depth(device_, shadow_pass, prop.mesh,
                                              shadow_.light_view_proj(), m, prop.joints);
                }
            }
            if (hoard_prop_.ok) {
                const auto& caches = hoard_run_.layout().caches;
                for (size_t i = 0; i < caches.size(); ++i) {
                    const game::RunCache& cache = caches[i];
                    if (cache.collected) continue;
                    const PropModel& prop = (i % 2 == 1 && trove_prop_.ok) ? trove_prop_ : hoard_prop_;
                    gfx::ModelUniforms m;
                    m.model = core::Mat4::trs(
                        cache.position + core::Vec3{0.0f, 0.1f, 0.0f},
                        core::Quat::from_axis_angle(core::Vec3::up(), float(i) * 2.3f),
                        core::Vec3{1.0f, 1.0f - 0.85f * cache.progress, 1.0f});
                    world_.draw_skinned_depth(device_, shadow_pass, prop.mesh,
                                              shadow_.light_view_proj(), m, prop.joints);
                }
            }
            draw_prey_shadows(shadow_pass);
        }
        // Only the live checkpoint casts a shadow. Shadowing all of them costs
        // little but reads as clutter, and the shadow's job here is to tell you
        // where the next ring is relative to the ground.
        if (const game::Ring* next = run_mode_ ? nullptr : rally_.next_ring()) {
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
    const game::Course& course = (studio_active_ || run_mode_) ? no_course : rally_.course();
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
    draw_run_world(pass);
    draw_prey(pass);

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
        // Frozen: the hide ices over, whatever colour it was.
        if (slot.status.frozen > 0.0f) {
            bot_model.recolour = core::Vec4{0.8f, 1.35f, 2.1f, 0.95f};
            bot_model.tint = core::Vec4{1.15f, 1.35f, 1.75f, 0.28f};
        }
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

    // The world is in the linear HDR target; bloom it, tonemap it and grade
    // it into the 8-bit target, which the UI then draws onto ungraded.
    post_.run(device_, post_settings_);

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
             "flap=%.2f tuck=%.2f brake=%.2f %s%s%s",
             double(frame_index_) / 60.0, double(s.position.y), double(s.ground_clearance),
             double(s.airspeed), double(s.climb_rate), double(s.g_load),
             double(core::degrees(s.angle_of_attack)), double(s.flap_amplitude),
             double(s.wing_tuck), double(s.wing_brake), s.grounded ? "GROUNDED " : "",
             s.stalling ? "STALL " : "", "");
    if (combat_enabled_) {
        LOG_INFO("   combat: health %.0f  kills %d  bites swung %d landed %d taken %d  fury %.2f  charge %.2f  rams %d  furies %d",
                 double(combat_.health()), combat_.kills(), bites_swung_, bites_landed_,
                 bites_taken_, double(combat_.fury()), double(combat_.charge()),
                 rams_landed_, furies_released_);
    }
    if (demo_active()) {
        LOG_INFO("   demo time: cruise %.0f fight %.0f siege %.0f land %.0f walk %.0f collect %.0f "
                 "takeoff %.0f flee %.0f hunt %.0f",
                 double(demo_.time_in[0]), double(demo_.time_in[1]), double(demo_.time_in[2]),
                 double(demo_.time_in[3]), double(demo_.time_in[4]), double(demo_.time_in[5]),
                 double(demo_.time_in[6]), double(demo_.time_in[7]), double(demo_.time_in[8]));
        LOG_INFO("   demo: %s  siege shots %d  best off-axis %.1f deg  range %.0f  lock %d",
                 game::demo_state_name(demo_.state()), demo_.siege_shots,
                 double(demo_.siege_best_off_axis_deg), double(demo_.siege_last_range),
                 combat_.locked_index());
        const_cast<game::DemoPilot&>(demo_).siege_best_off_axis_deg = 180.0f;
        const int slot = demo_.target();
        if (slot >= 0 && size_t(slot) < combat_.sentinels().size()) {
            int shots = 0;
            for (const game::Projectile& p : combat_.projectiles()) {
                if (p.alive && p.team == game::Team::Player) ++shots;
            }
            LOG_INFO("   demo target: slot %d health %.0f  player rounds in flight %d", slot,
                     double(combat_.sentinels()[size_t(slot)].health), shots);
        }
    }
    if (run_mode_) {
        LOG_INFO("   run: %s  seed %u  hoard %.0f  caches %d/%zu  kills %d  hunters %d  %.0f s  %.0f m",
                 hoard_run_.phase_name(), hoard_run_.layout().seed, double(hoard_run_.hoard()),
                 hoard_run_.caches_collected(), hoard_run_.layout().caches.size(),
                 hoard_run_.kills(), hoard_run_.hunters_loosed(), double(hoard_run_.elapsed()),
                 double(hoard_run_.distance()));
    }
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
        if (options_.headless) {
            dt = 1.0f / 60.0f;
            // Uneven pacing on demand: a live window's frames are not equal,
            // and anything that reads one frame's state from another shows
            // up as a twitch only then.
            if (options_.frame_jitter > 0.0f) {
                dt *= (frame_index_ % 2 == 0) ? 1.0f + options_.frame_jitter
                                              : 1.0f - options_.frame_jitter;
            }
        }

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
            ui_.begin_frame(float(device_.width()), float(device_.height()));
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
