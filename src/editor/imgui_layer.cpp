#include "editor/imgui_layer.h"

#include "core/log.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

namespace editor {

bool ImGuiLayer::init(gfx::Device& device) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    // No imgui.ini: window layout is part of the code, so a fresh clone looks
    // the same as a long-running one.
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;

    if (!ImGui_ImplSDL3_InitForSDLGPU(device.window())) {
        LOG_ERROR("ImGui_ImplSDL3_InitForSDLGPU failed");
        return false;
    }

    ImGui_ImplSDLGPU3_InitInfo info = {};
    info.Device = device.gpu();
    // The UI is drawn into the offscreen scene target alongside the world, so
    // it must match that format -- not the swapchain's. A useful side effect:
    // headless screenshots include the UI, which makes them verifiable.
    info.ColorTargetFormat = device.scene_color_format();
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&info)) {
        LOG_ERROR("ImGui_ImplSDLGPU3_Init failed");
        return false;
    }

    initialized_ = true;
    return true;
}

void ImGuiLayer::shutdown() {
    if (!initialized_) return;
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    initialized_ = false;
}

bool ImGuiLayer::process_event(const SDL_Event& event) {
    if (!initialized_) return false;
    ImGui_ImplSDL3_ProcessEvent(&event);
    const ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_MOUSE_MOTION:
            return io.WantCaptureMouse;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
            return io.WantCaptureKeyboard;
        default:
            return false;
    }
}

void ImGuiLayer::begin_frame() {
    if (!initialized_) return;
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    has_draw_data_ = false;
}

void ImGuiLayer::prepare_draw_data(gfx::Device& device) {
    if (!initialized_) return;
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data) return;
    ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, device.cmd());
    has_draw_data_ = true;
}

void ImGuiLayer::render(gfx::Device& device, SDL_GPURenderPass* pass) {
    if (!initialized_ || !has_draw_data_ || !pass) return;
    ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), device.cmd(), pass);
}

bool ImGuiLayer::wants_mouse() const {
    return initialized_ && ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wants_keyboard() const {
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

}  // namespace editor
