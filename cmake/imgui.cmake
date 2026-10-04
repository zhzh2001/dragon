# Dear ImGui has no CMake build of its own, so we declare the target here.
# It is our editor: every tuning slider, telemetry plot, and debug panel.
include(FetchContent)

FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.6
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
if(DRAGON_XP)
  # The XP build: Win32 for the platform side (fed from the SDL3 subset's
  # window procedure hook) and D3D9 to draw.
  target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
                               ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp)
  target_link_libraries(imgui PUBLIC d3d9 gdi32 dwmapi)
  target_compile_features(imgui PUBLIC cxx_std_20)
  return()
endif()
target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
                             ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
if(WIN32)
  # The D3D9 backend's UI (editor/imgui_layer.cpp).
  target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp
                               ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
  # imgui_impl_dx11 compiles its two shaders at runtime through D3DCompile
  # (d3dcompiler_47.dll, in every Windows since 8.1 and 7's platform update).
  target_link_libraries(imgui PUBLIC d3d9 d3d11 d3dcompiler)
  # It asks for vs_4_0 and ps_4_0, which a feature level 9 device refuses
  # ("Invalid shader version provided"), so the UI drew nothing on the X550
  # at 9_2. The level_9_1 profiles build the same two shaders and run on
  # every feature level, so the fetched source is patched to ask for those.
  set(_imgui_dx11 ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
  file(READ ${_imgui_dx11} _imgui_dx11_src)
  string(REPLACE "\"vs_4_0\"" "\"vs_4_0_level_9_1\"" _imgui_dx11_src "${_imgui_dx11_src}")
  string(REPLACE "\"ps_4_0\"" "\"ps_4_0_level_9_1\"" _imgui_dx11_src "${_imgui_dx11_src}")
  # Its font sampler clamps MaxLOD to 0, which feature level 9 refuses
  # ("MaxLOD must be FLT_MAX"); the font atlas has one level, so the
  # clamp did nothing anyway.
  string(REPLACE "desc.MaxLOD = 0.f;" "desc.MaxLOD = D3D11_FLOAT32_MAX;" _imgui_dx11_src "${_imgui_dx11_src}")
  file(WRITE ${_imgui_dx11} "${_imgui_dx11_src}")
endif()
target_compile_features(imgui PUBLIC cxx_std_20)
