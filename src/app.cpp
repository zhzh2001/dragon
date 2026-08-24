#include "app.h"

#include <SDL3/SDL.h>

#include <cstdio>

#include "core/log.h"
#include "core/math.h"
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
        } else if (arg == "--hide-ui") {
            options.hide_ui = true;
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
    config.title = "Dragon Engine -- M5";
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

    regenerate_terrain();

    dragon_mesh_.upload(device_.gpu(), game::make_dragon_proxy(dragon_dims_), "dragon_proxy");

    // A tuning file next to the assets overrides the built-in defaults, so a
    // good session's numbers survive a rebuild.
    game::load_tuning(flight_.tuning, ASSET_ROOT "/flight_tuning.cfg");

    apply_camera_preset(options.camera_mode);
    chase_.first_person = options.first_person;
    respawn_dragon();

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

void App::respawn_dragon() {
    // Airborne in the valley corridor, already at a comfortable cruise, facing
    // along the valley. Starting from a stall on the ground would make every
    // test begin with a recovery.
    const float z = -terrain_settings_.half_extent * 0.55f;
    const float x = terrain_.valley_center_x(z);
    const float ground = terrain_.height_at(x, z);

    const float ahead_z = z + 700.0f;
    const core::Vec3 spawn{x, ground + 170.0f, z};
    const core::Vec3 look{terrain_.valley_center_x(ahead_z), ground + 150.0f, ahead_z};

    flight_.reset(spawn, core::look_rotation(look - spawn, core::Vec3::up()), 42.0f);
    chase_.snap_to(flight_.state());
    trail_count_ = 0;
    trail_cursor_ = 0;
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
        look.x += input_.gamepad_axis(SDL_GAMEPAD_AXIS_RIGHTX, controls_.gamepad_deadzone) *
                  controls_.free_look_gamepad * dt;
        look.y += input_.gamepad_axis(SDL_GAMEPAD_AXIS_RIGHTY, controls_.gamepad_deadzone) *
                  controls_.free_look_gamepad * dt;
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
}

game::FlightInput App::read_flight_input() const {
    game::FlightInput in;

    if (options_.has_input_override) {
        const float* v = options_.input_override;
        in.pitch = v[0];
        in.roll = v[1];
        in.yaw = v[2];
        in.flap = v[3];
        in.tuck = v[4];
        in.brake = v[5];
        return in;
    }

    if (free_camera_) return in;

    // W pitches the nose up. invert_pitch gives the flight-sim
    // pull-back-to-climb feel instead.
    in.pitch = stick_.y * (controls_.invert_pitch ? -1.0f : 1.0f);
    in.roll = stick_.x;

    const bool keyboard_free = !ui_.wants_keyboard();
    if (keyboard_free) {
        in.yaw = input_.axis(SDL_SCANCODE_Q, SDL_SCANCODE_E);
        in.flap = input_.down(SDL_SCANCODE_SPACE) ? 1.0f : 0.0f;
        in.tuck =
            (input_.down(SDL_SCANCODE_LSHIFT) || input_.down(SDL_SCANCODE_RSHIFT)) ? 1.0f : 0.0f;
        in.brake =
            (input_.down(SDL_SCANCODE_LCTRL) || input_.down(SDL_SCANCODE_RCTRL)) ? 1.0f : 0.0f;
    }

    if (input_.has_gamepad()) {
        // Shoulders rudder, triggers are the two energy verbs, A flaps. Laid out
        // so the things you hold continuously sit under the fingers that can
        // hold them.
        const float pad_yaw = (input_.gamepad_button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) ? 1.0f : 0.0f) -
                              (input_.gamepad_button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ? 1.0f : 0.0f);
        in.yaw = core::clampf(in.yaw + pad_yaw, -1.0f, 1.0f);
        in.flap = core::maxf(in.flap, input_.gamepad_button(SDL_GAMEPAD_BUTTON_SOUTH) ? 1.0f : 0.0f);
        in.tuck = core::maxf(in.tuck, input_.gamepad_trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        in.brake = core::maxf(in.brake, input_.gamepad_trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    }
    return in;
}

gfx::ModelUniforms App::dragon_model_uniforms() const {
    const game::FlightState& s = flight_.state();
    gfx::ModelUniforms model;
    model.model = core::Mat4::trs(s.position, s.orientation, core::Vec3::one());

    // The flight model already produced the wing angle, so the wing you see and
    // the thrust it generated cannot disagree.
    model.wing = core::Vec4{s.wing_angle, s.wing_tuck, dragon_dims_.wing_root,
                            dragon_dims_.wing_span};

    // Tail and neck lag into the turn. A crude stand-in for the spring chains
    // that will drive them properly, but it already makes turns read as intent.
    const float bend = -s.control.z * 0.30f - s.control.y * 0.20f;
    model.pose = core::Vec4{bend, s.angle_of_attack, s.wing_brake,
                            game::dragon_wing_hinge_y(dragon_dims_)};
    return model;
}

void App::regenerate_terrain() {
    terrain_.generate(terrain_settings_);
    terrain_mesh_.release(device_.gpu());
    terrain_mesh_.upload(device_.gpu(), terrain_.mesh_data(), "terrain");
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
    dragon_mesh_.release(device_.gpu());
    shadow_.shutdown(device_);
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
    chase_.update(flight_.state(), &terrain_, read_free_look(dt), dt);
    if (free_camera_) camera_.update(input_, dt, mouse_look_);

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

    // In free-camera mode the dragon needs a marker, or it is easy to lose.
    if (free_camera_) {
        debug_.sphere(s.position, 6.0f, Vec3{0.9f, 0.75f, 0.35f}, 20, true);
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

    build_flight_ui();

    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine");

    const float ms = average_frame_ms();
    ImGui::Text("%.2f ms  (%.0f fps)   %ux%u", ms, ms > 0.0f ? 1000.0f / ms : 0.0f,
                device_.width(), device_.height());

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
        dirty |= ImGui::SliderFloat("half extent", &terrain_settings_.half_extent, 500.0f, 4000.0f,
                                    "%.0f m");
        dirty |= ImGui::SliderFloat("cell size", &terrain_settings_.cell_size, 3.0f, 20.0f,
                                    "%.0f m");
        if (dirty && !ImGui::IsAnyItemActive()) regenerate_terrain();

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

void App::build_flight_ui() {
    const game::FlightState& s = flight_.state();
    game::FlightTuning& t = flight_.tuning;

    ImGui::SetNextWindowPos(ImVec2(float(device_.width()) - 402.0f, 12.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(390, 0), ImGuiCond_FirstUseEver);
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

    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("stick  roll %+.2f   pitch %+.2f", stick_.x, stick_.y);
        ImGui::Text("gamepad: %s", input_.gamepad_name());
        ImGui::Checkbox("invert pitch", &controls_.invert_pitch);
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
        if (ImGui::Button("glider")) t = game::tuning_preset_glider();
        ImGui::SameLine();
        if (ImGui::Button("agile")) t = game::tuning_preset_agile();
        ImGui::SameLine();
        if (ImGui::Button("heavy")) t = game::tuning_preset_heavy();
        ImGui::SameLine();
        if (ImGui::Button("default")) t = game::FlightTuning();
        if (ImGui::Button("save to assets/flight_tuning.cfg")) {
            game::save_tuning(t, ASSET_ROOT "/flight_tuning.cfg");
        }
        ImGui::SameLine();
        if (ImGui::Button("load")) {
            game::load_tuning(t, ASSET_ROOT "/flight_tuning.cfg");
        }
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
        ImGui::Checkbox("force vectors", &show_forces_);
        ImGui::Checkbox("flight path", &show_flight_path_);
        ImGui::Checkbox("ground probe", &show_ground_probe_);
    }

    ImGui::Separator();
    ImGui::TextDisabled("W/S pitch, A/D roll, Q/E rudder");
    ImGui::TextDisabled("space flap, shift tuck-dive, ctrl brake");
    ImGui::TextDisabled("gamepad: left stick, A flap, triggers dive/brake");
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

    // Debug geometry has to be uploaded before any render pass opens, because
    // the upload itself is a copy pass.
    debug_.upload(device_);
    ui_.prepare_draw_data(device_);

    const gfx::ModelUniforms dragon_model = dragon_model_uniforms();

    // Shadow pass first: the main pass samples what it writes.
    if (shadow_.enabled) {
        SDL_GPURenderPass* shadow_pass = shadow_.begin_pass(device_);
        world_.draw_mesh_depth(device_, shadow_pass, terrain_mesh_, shadow_.light_view_proj(),
                               gfx::ModelUniforms());
        world_.draw_mesh_depth(device_, shadow_pass, dragon_mesh_, shadow_.light_view_proj(),
                               dragon_model);
        device_.end_pass(shadow_pass);
    }

    // The clear colour is never seen: the sky covers every pixel. It is set to
    // the fog colour anyway so a frame where the sky pipeline is broken still
    // looks like a sky rather than a void.
    SDL_GPURenderPass* pass = device_.begin_main_pass(
        lighting_.fog_color[0], lighting_.fog_color[1], lighting_.fog_color[2]);
    world_.draw_sky(device_, pass);
    world_.draw_terrain(device_, pass, terrain_mesh_);
    world_.draw_mesh(device_, pass, dragon_mesh_, dragon_model);
    debug_.draw(device_, pass, camera.view_projection(aspect));
    device_.end_pass(pass);

    SDL_GPURenderPass* ui_pass = device_.begin_ui_pass();
    ui_.render(device_, ui_pass);
    device_.end_pass(ui_pass);
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

        // Capture on the last frame, once the scene has settled.
        if (!options_.screenshot.empty() && options_.frames > 0 &&
            frame_index_ == options_.frames - 1) {
            device_.request_screenshot(options_.screenshot);
        }

        if (device_.begin_frame()) {
            ui_.begin_frame();
            build_ui(dt);
            render();
            device_.end_frame();
        }

        ++frame_index_;
        if (options_.frames > 0 && frame_index_ >= options_.frames) running_ = false;
    }
}

}  // namespace app
