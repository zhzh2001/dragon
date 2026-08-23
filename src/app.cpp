#include "app.h"

#include <SDL3/SDL.h>

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
        } else if (arg == "--hide-ui") {
            options.hide_ui = true;
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
    config.title = "Dragon Engine -- M3";
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
    if (options.has_camera) {
        camera_.set_position(options.camera_position, options.camera_target);
    } else {
        frame_camera_on_valley();
    }
    // Fast enough to fly a valley at scale; the debug camera is for surveying.
    camera_.speed = 90.0f;

    running_ = true;
    return true;
}

void App::regenerate_terrain() {
    terrain_.generate(terrain_settings_);
    terrain_mesh_.release(device_.gpu());
    terrain_mesh_.upload(device_.gpu(), terrain_.mesh_data(), "terrain");
    // Snow should sit sensibly relative to whatever the peaks came out at.
    material_.water_level = terrain_settings_.water_level;
    material_.rock_slope = 0.62f;
    // High enough that snow reads as mountain caps rather than covering the
    // whole upper valley.
    material_.snow_line = core::lerpf(terrain_.min_height(), terrain_.max_height(), 0.74f);
}

void App::frame_camera_on_valley() {
    // Stand in the valley corridor, a little above the floor, looking along it.
    const float z = -terrain_settings_.half_extent * 0.55f;
    const float x = terrain_.valley_center_x(z);
    const float ground = terrain_.height_at(x, z);
    Vec3 eye{x, ground + 120.0f, z};
    const float look_z = z + 500.0f;
    Vec3 target{terrain_.valley_center_x(look_z), ground + 40.0f, look_z};
    camera_.set_position(eye, target);
}

void App::shutdown() {
    if (device_.gpu()) SDL_WaitForGPUIdle(device_.gpu());
    terrain_mesh_.release(device_.gpu());
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

    if (input_.pressed(SDL_SCANCODE_ESCAPE)) running_ = false;

    // Hold right mouse to look. Relative mode hides the cursor and delivers
    // unbounded deltas, so the view can turn past the window edge.
    bool want_look = input_.mouse_down(SDL_BUTTON_RIGHT) && !ui_.wants_mouse();
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

    camera_.update(input_, dt, mouse_look_);

    if (show_grid_) {
        debug_.grid(400.0f, 25.0f, Vec3{0.11f, 0.13f, 0.16f});
    }

    if (show_probes_) {
        debug_.axes(Vec3::zero(), Quat::identity(), 60.0f);
        // Mark the valley corridor so its shape is visible from the air. This
        // is also how we confirm the mesh and the analytic height agree.
        const float extent = terrain_settings_.half_extent;
        for (float z = -extent; z <= extent; z += 60.0f) {
            const float x = terrain_.valley_center_x(z);
            const float y = terrain_.height_at(x, z);
            debug_.cross(Vec3{x, y + 3.0f, z}, 8.0f, Vec3{0.9f, 0.7f, 0.25f});
        }
    }

    if (show_ground_probe_) {
        // Ground clearance directly below the camera, sampled analytically.
        // When the flight model lands, this is the query it will use, so it is
        // worth being able to see it.
        const Vec3 eye = camera_.camera().position;
        const float ground = terrain_.height_at(eye.x, eye.z);
        const Vec3 below{eye.x, ground, eye.z};
        debug_.line(eye, below, Vec3{0.35f, 0.8f, 0.45f}, true);
        debug_.circle(below, terrain_.normal_at(eye.x, eye.z), 12.0f, Vec3{0.35f, 0.8f, 0.45f}, 24,
                      true);
        debug_.arrow(below, below + terrain_.normal_at(eye.x, eye.z) * 25.0f,
                     Vec3{0.9f, 0.45f, 0.3f}, true);
    }
}

void App::build_ui(float dt) {
    (void)dt;
    if (options_.hide_ui) return;
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine");

    const float ms = average_frame_ms();
    ImGui::Text("%.2f ms  (%.0f fps)   %ux%u", ms, ms > 0.0f ? 1000.0f / ms : 0.0f,
                device_.width(), device_.height());

    const Vec3 eye = camera_.camera().position;
    const float clearance = terrain_.clearance_at(eye);
    ImGui::Text("altitude %.0f m   ground clearance %.0f m", eye.y, clearance);

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("pos  %.0f  %.0f  %.0f", eye.x, eye.y, eye.z);
        ImGui::SliderFloat("speed", &camera_.speed, 1.0f, 400.0f, "%.0f m/s");
        ImGui::SliderFloat("sensitivity", &camera_.look_sensitivity, 0.02f, 0.5f);
        ImGui::SliderFloat("fov", &camera_.camera().fov_y_deg, 30.0f, 110.0f, "%.0f deg");
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
        dirty |= ImGui::SliderFloat("valley width", &terrain_settings_.valley_width, 60.0f, 800.0f,
                                    "%.0f m");
        dirty |= ImGui::SliderFloat("valley falloff", &terrain_settings_.valley_falloff, 100.0f,
                                    1200.0f, "%.0f m");
        dirty |= ImGui::SliderFloat("valley meander", &terrain_settings_.valley_meander, 0.0f,
                                    900.0f, "%.0f m");
        dirty |= ImGui::SliderFloat("cell size", &terrain_settings_.cell_size, 2.0f, 16.0f,
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
        ImGui::SliderFloat("extent", &shadow_.extent, 200.0f, 2000.0f, "%.0f m");
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
    ImGui::TextDisabled("WASD move, QE up/down, shift boost");
    ImGui::TextDisabled("hold right-mouse to look, esc quits");
    ImGui::End();
}

void App::render() {
    const float aspect = device_.aspect();

    shadow_.update(camera_.camera(), lighting_.sun_direction());

    gfx::SceneUniforms scene =
        gfx::make_scene_uniforms(camera_.camera(), aspect, lighting_, material_, time_seconds_);
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

    // Shadow pass first: the main pass samples what it writes.
    if (shadow_.enabled) {
        SDL_GPURenderPass* shadow_pass = shadow_.begin_pass(device_);
        world_.draw_mesh_depth(device_, shadow_pass, terrain_mesh_, shadow_.light_view_proj());
        device_.end_pass(shadow_pass);
    }

    // The clear colour is never seen: the sky covers every pixel. It is set to
    // the fog colour anyway so a frame where the sky pipeline is broken still
    // looks like a sky rather than a void.
    SDL_GPURenderPass* pass = device_.begin_main_pass(
        lighting_.fog_color[0], lighting_.fog_color[1], lighting_.fog_color[2]);
    world_.draw_sky(device_, pass);
    world_.draw_terrain(device_, pass, terrain_mesh_);
    debug_.draw(device_, pass, camera_.camera().view_projection(aspect));
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
