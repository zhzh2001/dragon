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
    // Keyboard and gamepad navigation are deliberately OFF. With them on, ImGui
    // claims those devices for widget navigation whenever a panel has focus,
    // which silently swallows flight controls -- and gamepad nav would take the
    // left stick outright. The panels are mouse-driven tools; the keyboard and
    // pad belong to the dragon.
    // No imgui.ini: window layout is part of the code, so a fresh clone looks
    // the same as a long-running one.
    io.IniFilename = nullptr;

    // The two faces of the HUD kit (ui/hud.h): a condensed display face for
    // numerals and a humanist sans for labels. Loaded from the system's TTFs
    // where they exist -- this is a hobby build on one machine, and shipping
    // fonts is an asset-policy question for later -- with ImGui's default as
    // the fallback so nothing depends on them being there. ImGui 1.92's
    // dynamic atlas lets one loaded face be drawn at any size, so each is
    // loaded once.
    {
        struct Candidate {
            const char* path;
            unsigned face;  // index within a .ttc
        };
        const Candidate numerals[] = {{"/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf", 0},
                                      {"/System/Library/Fonts/Supplemental/Impact.ttf", 0},
                                      {"/System/Library/Fonts/Supplemental/Futura.ttc", 0}};
        const Candidate labels[] = {{"/System/Library/Fonts/Avenir Next.ttc", 0},
                                    {"/System/Library/Fonts/Supplemental/GillSans.ttc", 0},
                                    {"/System/Library/Fonts/Helvetica.ttc", 0}};
        auto load = [&](const Candidate* list, size_t count) -> ImFont* {
            for (size_t i = 0; i < count; ++i) {
                ImFontConfig config;
                config.FontNo = list[i].face;
                if (ImFont* font = io.Fonts->AddFontFromFileTTF(list[i].path, 0.0f, &config)) {
                    LOG_INFO("ui font: %s", list[i].path);
                    return font;
                }
            }
            return nullptr;
        };
        numeral_font_ = load(numerals, sizeof(numerals) / sizeof(numerals[0]));
        label_font_ = load(labels, sizeof(labels) / sizeof(labels[0]));
        if (!label_font_) LOG_WARN("ui font: no label face found, using ImGui's default");
        if (label_font_) io.FontDefault = label_font_;
    }

    // The tool layer wears the HUD's tokens: charcoal plates, off-white text,
    // the one gold accent, so a panel opening looks like a drawer of the same
    // cabinet rather than a debugger over the game.
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 15.0f;
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(6.0f, 3.0f);
    style.ItemSpacing = ImVec2(8.0f, 5.0f);
    ImVec4* colours = style.Colors;
    const ImVec4 plate(0.07f, 0.08f, 0.10f, 0.86f);
    const ImVec4 plate_deep(0.05f, 0.055f, 0.07f, 0.95f);
    const ImVec4 well(0.12f, 0.13f, 0.15f, 0.9f);
    const ImVec4 text(0.93f, 0.90f, 0.84f, 1.0f);
    const ImVec4 text_dim(0.93f, 0.90f, 0.84f, 0.55f);
    const ImVec4 gold(0.886f, 0.675f, 0.235f, 1.0f);
    const ImVec4 gold_dim(0.886f, 0.675f, 0.235f, 0.55f);
    const ImVec4 gold_faint(0.886f, 0.675f, 0.235f, 0.25f);
    colours[ImGuiCol_Text] = text;
    colours[ImGuiCol_TextDisabled] = text_dim;
    colours[ImGuiCol_WindowBg] = plate;
    colours[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colours[ImGuiCol_PopupBg] = plate_deep;
    colours[ImGuiCol_Border] = ImVec4(1.0f, 0.92f, 0.78f, 0.11f);
    colours[ImGuiCol_FrameBg] = well;
    colours[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.19f, 0.22f, 0.9f);
    colours[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.23f, 0.26f, 0.9f);
    colours[ImGuiCol_TitleBg] = plate_deep;
    colours[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
    colours[ImGuiCol_TitleBgCollapsed] = ImVec4(0.05f, 0.055f, 0.07f, 0.7f);
    colours[ImGuiCol_CheckMark] = gold;
    colours[ImGuiCol_SliderGrab] = gold_dim;
    colours[ImGuiCol_SliderGrabActive] = gold;
    colours[ImGuiCol_Button] = ImVec4(0.16f, 0.17f, 0.20f, 0.9f);
    colours[ImGuiCol_ButtonHovered] = gold_faint;
    colours[ImGuiCol_ButtonActive] = gold_dim;
    colours[ImGuiCol_Header] = gold_faint;
    colours[ImGuiCol_HeaderHovered] = ImVec4(0.886f, 0.675f, 0.235f, 0.38f);
    colours[ImGuiCol_HeaderActive] = gold_dim;
    colours[ImGuiCol_Separator] = ImVec4(1.0f, 0.92f, 0.78f, 0.12f);
    colours[ImGuiCol_ResizeGrip] = gold_faint;
    colours[ImGuiCol_ResizeGripHovered] = gold_dim;
    colours[ImGuiCol_ResizeGripActive] = gold;
    colours[ImGuiCol_Tab] = well;
    colours[ImGuiCol_TabHovered] = gold_dim;
    colours[ImGuiCol_PlotLines] = gold;
    colours[ImGuiCol_PlotHistogram] = gold;
    colours[ImGuiCol_TextSelectedBg] = gold_faint;
    colours[ImGuiCol_NavHighlight] = gold;

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
