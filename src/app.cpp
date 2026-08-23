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
        } else {
            LOG_WARN("unknown option '%s'", arg.c_str());
        }
    }
    return options;
}

bool App::init(const Options& options) {
    options_ = options;

    gfx::Device::Config config;
    config.title = "Dragon Engine -- M2";
    config.width = 1280;
    config.height = 720;
    config.headless = options.headless;
    if (!device_.init(config)) return false;

    if (!ui_.init(device_)) return false;

    pipelines_.init(&device_, SHADER_ROOT);
    if (!debug_.init(&device_, &pipelines_)) return false;

    // Off to one side and above, looking back at the origin: enough to show
    // that perspective, depth, and handedness are all behaving.
    camera_.set_position(Vec3{18.0f, 12.0f, 26.0f}, Vec3{0.0f, 2.0f, 0.0f});

    running_ = true;
    return true;
}

void App::shutdown() {
    if (device_.gpu()) SDL_WaitForGPUIdle(device_.gpu());
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
    reload_timer_ += dt;
    if (reload_timer_ >= 0.25f) {
        reload_timer_ = 0.0f;
        shader_reloads_ += pipelines_.poll_hot_reload();
    }

    camera_.update(input_, dt, mouse_look_);

    if (show_grid_) {
        debug_.grid(grid_half_extent_, grid_spacing_, Vec3{0.11f, 0.13f, 0.16f});
    }

    if (show_probes_) {
        // Reference geometry at known positions and sizes. If the projection,
        // depth compare, or handedness is wrong, it shows up here first.
        debug_.axes(Vec3::zero(), Quat::identity(), 8.0f);

        // A 10 m ladder up the Y axis: checks vertical scale and depth ordering.
        for (int i = 1; i <= 4; ++i) {
            float y = float(i) * 10.0f;
            debug_.circle(Vec3{0, y, 0}, Vec3::up(), 3.0f, Vec3{0.2f, 0.35f, 0.5f}, 32);
        }

        // Unit-scale solids at +X and +Z, so the two horizontal axes are
        // distinguishable at a glance.
        debug_.box(Vec3{20.0f, 2.0f, 0.0f}, Vec3{2.0f, 2.0f, 2.0f}, Quat::identity(),
                   Vec3{0.85f, 0.4f, 0.25f});
        debug_.sphere(Vec3{0.0f, 2.0f, 20.0f}, 2.0f, Vec3{0.3f, 0.55f, 0.85f}, 32);

        // Overlay arrow: always visible, even behind the boxes. Confirms the
        // no-depth-test path works.
        debug_.arrow(Vec3{0, 0.5f, 0}, Vec3{20.0f, 0.5f, 20.0f}, Vec3{0.95f, 0.8f, 0.3f}, true);

        // A ring of markers at 60 m to judge distance falloff and grid scale.
        for (int i = 0; i < 12; ++i) {
            float angle = core::TWO_PI * float(i) / 12.0f;
            Vec3 p{std::cos(angle) * 60.0f, 0.5f, std::sin(angle) * 60.0f};
            debug_.cross(p, 1.5f, Vec3{0.35f, 0.3f, 0.22f});
        }
    }
}

void App::build_ui(float dt) {
    (void)dt;
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Engine");

    const float ms = average_frame_ms();
    ImGui::Text("%.2f ms  (%.0f fps)   %ux%u", ms, ms > 0.0f ? 1000.0f / ms : 0.0f,
                device_.width(), device_.height());
    ImGui::Text("debug lines: %d", debug_.line_count());

    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        const gfx::Camera& cam = camera_.camera();
        ImGui::Text("pos  %.1f  %.1f  %.1f", cam.position.x, cam.position.y, cam.position.z);
        Vec3 fwd = cam.forward();
        ImGui::Text("fwd  %.2f  %.2f  %.2f", fwd.x, fwd.y, fwd.z);
        ImGui::SliderFloat("speed", &camera_.speed, 1.0f, 200.0f, "%.0f m/s");
        ImGui::SliderFloat("sensitivity", &camera_.look_sensitivity, 0.02f, 0.5f);
        ImGui::SliderFloat("smoothing", &camera_.move_smoothing, 0.0f, 0.3f, "%.3f s");
        ImGui::SliderFloat("fov", &camera_.camera().fov_y_deg, 30.0f, 110.0f, "%.0f deg");
        if (ImGui::Button("reset view")) {
            camera_.set_position(Vec3{18.0f, 12.0f, 26.0f}, Vec3{0.0f, 2.0f, 0.0f});
        }
    }

    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("clear", clear_color_);
        ImGui::Checkbox("grid", &show_grid_);
        ImGui::SameLine();
        ImGui::Checkbox("probes", &show_probes_);
        ImGui::SliderFloat("grid extent", &grid_half_extent_, 20.0f, 800.0f, "%.0f m");
        ImGui::SliderFloat("grid spacing", &grid_spacing_, 1.0f, 25.0f, "%.0f m");
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
    // Debug geometry has to be uploaded before any render pass opens, because
    // the upload itself is a copy pass.
    debug_.upload(device_);
    ui_.prepare_draw_data(device_);

    SDL_GPURenderPass* pass =
        device_.begin_main_pass(clear_color_[0], clear_color_[1], clear_color_[2]);
    debug_.draw(device_, pass, camera_.camera().view_projection(device_.aspect()));
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
