#include "editor/imgui_layer.h"

#include <fstream>
#include <string>

#include "core/log.h"
#include "core/paths.h"
#include "imgui.h"
#ifdef DRAGON_XP
// The XP build (docs/PORTING.md, R6): ImGui's Win32 platform backend, fed
// from the SDL3 subset's window procedure, and D3D9 to draw.
#include "rhi/d3d9/d3d9_backend.h"  // first: brings <windows.h> and <d3d9.h>
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#else
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "rhi/sdlgpu/sdlgpu.h"
#ifdef _WIN32
#include "imgui_impl_dx11.h"
#include "imgui_impl_dx9.h"
#include "rhi/d3d11/d3d11_backend.h"
#include "rhi/d3d9/d3d9_backend.h"
#endif
#endif

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
    // numerals and a humanist sans for labels. The game ships its own, two
    // OFL faces in assets/fonts (Barlow Condensed SemiBold, Fira Sans Medium),
    // so the HUD reads the same on every OS; the macOS system faces it was
    // first drawn in are the fallback, and ImGui's default after that, so
    // nothing depends on any of them being there. ImGui 1.92's dynamic atlas
    // lets one loaded face be drawn at any size, so each is loaded once.
    {
        struct Candidate {
            std::string path;
            unsigned face;  // index within a .ttc
        };
        const Candidate numerals[] = {{core::paths::asset("fonts/BarlowCondensed-SemiBold.ttf"), 0},
                                      {"/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf", 0},
                                      {"/System/Library/Fonts/Supplemental/Impact.ttf", 0}};
        const Candidate labels[] = {{core::paths::asset("fonts/FiraSans-Medium.ttf"), 0},
                                    {"/System/Library/Fonts/Avenir Next.ttc", 0},
                                    {"/System/Library/Fonts/Helvetica.ttc", 0}};
        auto load = [&](const Candidate* list, size_t count) -> ImFont* {
            for (size_t i = 0; i < count; ++i) {
                // AddFontFromFileTTF asserts on a missing file in a debug
                // ImGui, so look before asking it.
                if (!std::ifstream(list[i].path).good()) continue;
                ImFontConfig config;
                config.FontNo = list[i].face;
                if (ImFont* font = io.Fonts->AddFontFromFileTTF(list[i].path.c_str(), 0.0f, &config)) {
                    LOG_INFO("ui font: %s", list[i].path.c_str());
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

    // Dear ImGui draws through the backend's own API: SDL GPU, or D3D9's
    // fixed function (imgui_impl_dx9), which restores the device state it
    // changes.
    d3d9_ = device.rhi().backend() == rhi::Backend::Direct3D9;
    d3d11_ = device.rhi().backend() == rhi::Backend::Direct3D11;
#ifdef DRAGON_XP
    {
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(device.window()),
                                                             SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!d3d9_ || !ImGui_ImplWin32_Init(hwnd) || !ImGui_ImplDX9_Init(rhi::d3d9::native_device(device.rhi()))) {
            LOG_ERROR("ImGui Win32/D3D9 init failed");
            return false;
        }
        SDLXP_SetWndProcHook([](void* h, unsigned message, uintptr_t wparam, intptr_t lparam) -> intptr_t {
            return intptr_t(ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(h), message, wparam, lparam));
        });
        initialized_ = true;
        return true;
    }
#else
#ifdef _WIN32
    if (d3d9_) {
        if (!ImGui_ImplSDL3_InitForD3D(device.window()) ||
            !ImGui_ImplDX9_Init(rhi::d3d9::native_device(device.rhi()))) {
            LOG_ERROR("ImGui D3D9 init failed");
            return false;
        }
        initialized_ = true;
        return true;
    }
    if (d3d11_) {
        if (!ImGui_ImplSDL3_InitForD3D(device.window()) ||
            !ImGui_ImplDX11_Init(rhi::d3d11::native_device(device.rhi()), rhi::d3d11::native_context(device.rhi()))) {
            LOG_ERROR("ImGui D3D11 init failed");
            return false;
        }
        // The renderer reports D3D11's 16384-texel limit for the font atlas,
        // which a feature level 9 device cannot create (2048 at 9_2): the
        // atlas grows past it and the fonts vanish. Tell it the device's.
        ImGui::GetPlatformIO().Renderer_TextureMaxWidth = int(device.rhi().max_texture_size());
        ImGui::GetPlatformIO().Renderer_TextureMaxHeight = int(device.rhi().max_texture_size());
        initialized_ = true;
        return true;
    }
#endif
    if (!ImGui_ImplSDL3_InitForSDLGPU(device.window())) {
        LOG_ERROR("ImGui_ImplSDL3_InitForSDLGPU failed");
        return false;
    }

    ImGui_ImplSDLGPU3_InitInfo info = {};
    info.Device = rhi::sdlgpu::native_device(device.rhi());
    // The UI is drawn into the offscreen scene target alongside the world, so
    // it must match that format -- not the swapchain's. A useful side effect:
    // headless screenshots include the UI, which makes them verifiable.
    info.ColorTargetFormat = rhi::sdlgpu::native_format(device.scene_color_format());
    info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    if (!ImGui_ImplSDLGPU3_Init(&info)) {
        LOG_ERROR("ImGui_ImplSDLGPU3_Init failed");
        return false;
    }

    initialized_ = true;
    return true;
#endif
}

void ImGuiLayer::shutdown() {
    if (!initialized_) return;
#ifdef DRAGON_XP
    SDLXP_SetWndProcHook(nullptr);
    ImGui_ImplDX9_Shutdown();
    ImGui_ImplWin32_Shutdown();
#else
#ifdef _WIN32
    if (d3d9_) ImGui_ImplDX9_Shutdown();
    else if (d3d11_) ImGui_ImplDX11_Shutdown();
    else
#endif
        ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
#endif
    ImGui::DestroyContext();
    initialized_ = false;
}

bool ImGuiLayer::process_event(const SDL_Event& event) {
    if (!initialized_) return false;
#ifndef DRAGON_XP
    // (The XP build feeds ImGui from the window procedure instead.)
    ImGui_ImplSDL3_ProcessEvent(&event);
#endif
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

void ImGuiLayer::begin_frame(float target_width, float target_height) {
    if (!initialized_) return;
#ifdef DRAGON_XP
    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
#else
#ifdef _WIN32
    if (d3d9_) ImGui_ImplDX9_NewFrame();
    else if (d3d11_) ImGui_ImplDX11_NewFrame();
    else
#endif
        ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
#endif
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f && target_width > 0.0f &&
        target_height > 0.0f) {
        io.DisplayFramebufferScale =
            ImVec2(target_width / io.DisplaySize.x, target_height / io.DisplaySize.y);
    }
    ImGui::NewFrame();
    has_draw_data_ = false;
}

void ImGuiLayer::prepare_draw_data(gfx::Device& device) {
    if (!initialized_) return;
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data) return;
    has_draw_data_ = true;
    if (d3d9_ || d3d11_) return;  // D3D9 and D3D11 record nothing ahead of the pass
#ifndef DRAGON_XP
    ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, rhi::sdlgpu::native_command_buffer(device.rhi()));
#endif
    
}

void ImGuiLayer::render(gfx::Device& device, rhi::Pass* pass) {
    if (!initialized_ || !has_draw_data_ || !pass) return;
    // Dear ImGui's renderer is backend-specific by nature: it records into
    // SDL's own command buffer and pass (rhi/sdlgpu/sdlgpu.h), or draws on the
    // D3D9 device into the pass's render target.
#ifdef _WIN32
    if (d3d9_) {
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        rhi::d3d9::forget_state(device.rhi());
        return;
    }
#ifndef DRAGON_XP
    if (d3d11_) {
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        return;
    }
#endif
#endif
#ifndef DRAGON_XP
    ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), rhi::sdlgpu::native_command_buffer(device.rhi()),
                                     rhi::sdlgpu::native_pass(pass));
#else
    (void)device;
#endif
}

bool ImGuiLayer::wants_mouse() const {
    return initialized_ && ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::wants_keyboard() const {
    return initialized_ && ImGui::GetIO().WantCaptureKeyboard;
}

}  // namespace editor
