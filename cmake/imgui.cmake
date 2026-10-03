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
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlgpu3.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)
if(WIN32)
  # The D3D9 backend's UI (editor/imgui_layer.cpp).
  target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp)
  target_link_libraries(imgui PUBLIC d3d9)
endif()
target_compile_features(imgui PUBLIC cxx_std_20)
