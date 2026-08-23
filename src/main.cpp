// M1: window, GPU device, reversed-Z depth, one hot-reloadable MSL pipeline,
// spinning triangle, and Dear ImGui. The foundation everything else builds on.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <string>

#include "core/log.h"
#include "core/math.h"
#include "editor/imgui_layer.h"
#include "gfx/buffer.h"
#include "gfx/device.h"
#include "gfx/pipeline.h"
#include "imgui.h"

using core::Mat4;
using core::Quat;
using core::Vec3;

namespace {

// Must match `Uniforms` in shaders/triangle.msl.
struct TriangleUniforms {
    Mat4 view_proj;
    Mat4 model;
};

struct Vertex {
    float px, py, pz;
    float r, g, b;
};

constexpr Vertex TRIANGLE[] = {
    {0.0f, 0.8f, 0.0f, 1.0f, 0.25f, 0.15f},    // top, ember
    {-0.8f, -0.6f, 0.0f, 0.95f, 0.75f, 0.2f},  // left, gold
    {0.8f, -0.6f, 0.0f, 0.3f, 0.5f, 0.9f},     // right, sky
};

gfx::PipelineDesc make_triangle_pipeline_desc() {
    gfx::PipelineDesc desc;
    desc.name = "triangle";
    desc.shader_path = "triangle.msl";
    desc.vs_entry = "vs_main";
    desc.fs_entry = "fs_main";
    desc.vs_uniform_buffers = 1;

    SDL_GPUVertexBufferDescription vb = {};
    vb.slot = 0;
    vb.pitch = sizeof(Vertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    desc.vertex_buffers.push_back(vb);

    SDL_GPUVertexAttribute pos = {};
    pos.location = 0;
    pos.buffer_slot = 0;
    pos.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    pos.offset = 0;
    desc.vertex_attributes.push_back(pos);

    SDL_GPUVertexAttribute color = {};
    color.location = 1;
    color.buffer_slot = 0;
    color.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    color.offset = sizeof(float) * 3;
    desc.vertex_attributes.push_back(color);

    // A single unlit triangle should be visible from behind too.
    desc.cull = SDL_GPU_CULLMODE_NONE;
    return desc;
}

// Command line options. `--frames` and `--headless` make the app scriptable, so
// a build can be smoke-tested and visually verified with nobody at the keyboard.
struct Options {
    int frames = 0;  // 0 = run until quit
    bool headless = false;
    std::string screenshot;  // empty = none
};

Options parse_options(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--headless") {
            opts.headless = true;
        } else if (arg == "--frames" && i + 1 < argc) {
            opts.frames = SDL_atoi(argv[++i]);
        } else if (arg == "--screenshot" && i + 1 < argc) {
            opts.screenshot = argv[++i];
        } else {
            LOG_WARN("unknown option '%s'", arg.c_str());
        }
    }
    return opts;
}

// Rolling frame-time average, so the readout is steady enough to read.
class FrameTimer {
public:
    void push(float dt) {
        history_[cursor_] = dt;
        cursor_ = (cursor_ + 1) % COUNT;
        if (filled_ < COUNT) ++filled_;
    }
    float average_ms() const {
        if (filled_ == 0) return 0.0f;
        float sum = 0.0f;
        for (int i = 0; i < filled_; ++i) sum += history_[i];
        return (sum / float(filled_)) * 1000.0f;
    }

private:
    static constexpr int COUNT = 60;
    float history_[COUNT] = {};
    int cursor_ = 0;
    int filled_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);
    Options opts = parse_options(argc, argv);

    gfx::Device::Config config;
    config.title = "Dragon Engine -- M1";
    config.width = 1280;
    config.height = 720;
    config.headless = opts.headless;

    gfx::Device device;
    if (!device.init(config)) return 1;

    editor::ImGuiLayer ui;
    if (!ui.init(device)) return 1;

    gfx::PipelineCache pipelines;
    pipelines.init(&device, SHADER_ROOT);
    gfx::PipelineHandle triangle_pipeline = pipelines.create(make_triangle_pipeline_desc());

    SDL_GPUBuffer* vertex_buffer = gfx::create_buffer_with_data(
        device.gpu(), TRIANGLE, sizeof(TRIANGLE), SDL_GPU_BUFFERUSAGE_VERTEX, "triangle_vb");
    if (!vertex_buffer) return 1;

    uint64_t prev_ticks = SDL_GetTicksNS();
    float spin = 0.0f;
    float spin_speed = 0.9f;
    float clear_color[3] = {0.05f, 0.06f, 0.09f};
    bool paused = false;
    bool running = true;
    int frame_index = 0;
    int reloads = 0;
    FrameTimer frame_timer;

    // Hot reload stats the filesystem, which is wasteful at frame rate.
    float reload_timer = 0.0f;
    constexpr float RELOAD_INTERVAL = 0.25f;

    while (running) {
        uint64_t now = SDL_GetTicksNS();
        float dt = float(now - prev_ticks) * 1e-9f;
        prev_ticks = now;
        // A breakpoint or a window drag can produce a huge dt; clamping keeps
        // integration stable.
        dt = core::clampf(dt, 0.0f, 0.1f);
        // Headless runs have no wall-clock meaning, so a fixed step makes
        // captures reproducible.
        if (opts.headless) dt = 1.0f / 60.0f;
        frame_timer.push(dt);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            bool consumed = ui.process_event(event);
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (!consumed && event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (event.key.key == SDLK_SPACE) paused = !paused;
            }
        }

        reload_timer += dt;
        if (reload_timer >= RELOAD_INTERVAL) {
            reload_timer = 0.0f;
            reloads += pipelines.poll_hot_reload();
        }

        if (!paused) spin += dt * spin_speed;

        // Capture on the last frame, once the scene has settled.
        if (!opts.screenshot.empty() && opts.frames > 0 && frame_index == opts.frames - 1) {
            device.request_screenshot(opts.screenshot);
        }

        if (!device.begin_frame()) continue;

        ui.begin_frame();
        {
            ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
            ImGui::Begin("Engine");
            ImGui::Text("%.2f ms  (%.0f fps)", frame_timer.average_ms(),
                        frame_timer.average_ms() > 0.0f ? 1000.0f / frame_timer.average_ms() : 0.0f);
            ImGui::Text("%ux%u", device.width(), device.height());
            ImGui::Separator();
            ImGui::SliderFloat("spin speed", &spin_speed, -4.0f, 4.0f);
            ImGui::ColorEdit3("clear", clear_color);
            ImGui::Checkbox("paused (space)", &paused);
            ImGui::Separator();
            ImGui::Text("shader reloads: %d", reloads);
            if (pipelines.broken_count() > 0) {
                ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%d pipeline(s) broken",
                                   pipelines.broken_count());
            }
            ImGui::TextDisabled("edit shaders/triangle.msl and save");
            ImGui::End();
        }
        ui.prepare_draw_data(device);

        SDL_GPURenderPass* pass =
            device.begin_main_pass(clear_color[0], clear_color[1], clear_color[2]);

        SDL_GPUGraphicsPipeline* pipeline = pipelines.get(triangle_pipeline);
        if (pipeline) {
            SDL_BindGPUGraphicsPipeline(pass, pipeline);

            TriangleUniforms uniforms;
            uniforms.view_proj =
                core::perspective_reverse_z(core::radians(60.0f), device.aspect(), 0.1f) *
                core::look_at(Vec3{0, 0, 2.5f}, Vec3::zero(), Vec3::up());
            uniforms.model = Mat4::from_quat(Quat::from_axis_angle(Vec3::unit_y(), spin));
            SDL_PushGPUVertexUniformData(device.cmd(), 0, &uniforms, sizeof(uniforms));

            SDL_GPUBufferBinding binding = {};
            binding.buffer = vertex_buffer;
            binding.offset = 0;
            SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);

            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        }

        device.end_pass(pass);

        SDL_GPURenderPass* ui_pass = device.begin_ui_pass();
        ui.render(device, ui_pass);
        device.end_pass(ui_pass);

        device.end_frame();

        ++frame_index;
        if (opts.frames > 0 && frame_index >= opts.frames) running = false;
    }

    SDL_WaitForGPUIdle(device.gpu());
    SDL_ReleaseGPUBuffer(device.gpu(), vertex_buffer);
    pipelines.shutdown();
    ui.shutdown();
    device.shutdown();
    return 0;
}
