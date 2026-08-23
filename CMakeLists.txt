cmake_minimum_required(VERSION 3.24)
project(dragon LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

find_package(SDL3 REQUIRED CONFIG)

include(cmake/imgui.cmake)

add_executable(dragon
  src/main.cpp
  src/app.cpp
  src/core/input.cpp
  src/gfx/device.cpp
  src/gfx/pipeline.cpp
  src/gfx/buffer.cpp
  src/gfx/debug_draw.cpp
  src/gfx/mesh.cpp
  src/gfx/scene_uniforms.cpp
  src/gfx/world_renderer.cpp
  src/gfx/shadow_map.cpp
  src/core/noise.cpp
  src/game/debug_camera.cpp
  src/game/terrain.cpp
  src/game/flight.cpp
  src/game/chase_camera.cpp
  src/game/dragon_proxy.cpp
  src/editor/imgui_layer.cpp
)

target_include_directories(dragon PRIVATE src)
target_link_libraries(dragon PRIVATE SDL3::SDL3 imgui)

# Shaders are read from the source tree at runtime so hot reload edits the same
# files we author. A shipping build would copy them next to the binary instead.
target_compile_definitions(dragon PRIVATE
  SHADER_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

target_compile_options(dragon PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# Tests are a plain executable rather than a framework: `ctest` or just run it.
enable_testing()
add_executable(test_math tests/test_math.cpp)
target_include_directories(test_math PRIVATE src)
target_compile_options(test_math PRIVATE -Wall -Wextra)
add_test(NAME math COMMAND test_math)

add_executable(test_camera tests/test_camera.cpp src/game/debug_camera.cpp src/core/input.cpp)
target_include_directories(test_camera PRIVATE src)
target_link_libraries(test_camera PRIVATE SDL3::SDL3)
target_compile_options(test_camera PRIVATE -Wall -Wextra)
add_test(NAME camera COMMAND test_camera)

add_executable(test_flight
  tests/test_flight.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/buffer.cpp
)
target_include_directories(test_flight PRIVATE src)
target_link_libraries(test_flight PRIVATE SDL3::SDL3)
target_compile_options(test_flight PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME flight COMMAND test_flight)
