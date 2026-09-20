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
include(cmake/cgltf.cmake)
include(cmake/stb.cmake)
include(cmake/miniaudio.cmake)

add_executable(dragon
  src/main.cpp
  src/app.cpp
  src/core/input.cpp
  src/gfx/device.cpp
  src/gfx/pipeline.cpp
  src/gfx/buffer.cpp
  src/gfx/debug_draw.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
  src/gfx/texture.cpp
  src/gfx/particles.cpp
  src/gfx/foliage.cpp
  src/game/vegetation.cpp
  src/audio/audio.cpp
  src/anim/skeleton.cpp
  src/anim/skinned_mesh.cpp
  src/anim/dragon_rig.cpp
  src/anim/animation.cpp
  src/anim/gltf_loader.cpp
  src/gfx/scene_uniforms.cpp
  src/gfx/world_renderer.cpp
  src/gfx/post.cpp
  src/gfx/shadow_map.cpp
  src/core/noise.cpp
  src/game/debug_camera.cpp
  src/game/terrain.cpp
  src/game/flight.cpp
  src/game/maneuver.cpp
  src/game/chase_camera.cpp

  src/game/course.cpp
  src/game/rally.cpp
  src/game/autopilot.cpp
  src/game/combat.cpp
  src/game/breath.cpp
  src/game/bot.cpp
  src/game/studio.cpp
  src/game/match.cpp
  src/editor/imgui_layer.cpp
  src/ui/hud.cpp
)

target_include_directories(dragon PRIVATE src)
target_link_libraries(dragon PRIVATE SDL3::SDL3 imgui cgltf stb miniaudio)

# Shaders are read from the source tree at runtime so hot reload edits the same
# files we author. A shipping build would copy them next to the binary instead.
target_compile_definitions(dragon PRIVATE
  SHADER_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
  ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

target_compile_options(dragon PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# Tests are a plain executable rather than a framework: `ctest` or just run it.
enable_testing()
add_executable(test_breath tests/test_breath.cpp src/game/breath.cpp)
target_include_directories(test_breath PRIVATE src)
target_link_libraries(test_breath PRIVATE SDL3::SDL3)
target_compile_options(test_breath PRIVATE -Wall -Wextra)
add_test(NAME breath COMMAND test_breath)

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
  src/game/maneuver.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
  src/anim/skeleton.cpp
  src/anim/skinned_mesh.cpp
  src/anim/dragon_rig.cpp
  src/anim/animation.cpp
  src/gfx/buffer.cpp
)
target_include_directories(test_flight PRIVATE src)
target_link_libraries(test_flight PRIVATE SDL3::SDL3)
target_compile_options(test_flight PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME flight COMMAND test_flight)

add_executable(test_camera_rig
  tests/test_camera_rig.cpp
  src/game/chase_camera.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
  src/anim/skeleton.cpp
  src/anim/skinned_mesh.cpp
  src/anim/dragon_rig.cpp
  src/anim/animation.cpp
  src/gfx/buffer.cpp
)
target_include_directories(test_camera_rig PRIVATE src)
target_link_libraries(test_camera_rig PRIVATE SDL3::SDL3)
target_compile_options(test_camera_rig PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME camera_rig COMMAND test_camera_rig)

add_executable(test_rally
  tests/test_rally.cpp
  src/game/course.cpp
  src/game/rally.cpp
  src/game/autopilot.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/buffer.cpp
)
target_include_directories(test_rally PRIVATE src)
target_link_libraries(test_rally PRIVATE SDL3::SDL3)
target_compile_options(test_rally PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME rally COMMAND test_rally)

add_executable(test_anim
  tests/test_anim.cpp
  src/game/studio.cpp
  src/game/match.cpp
  src/anim/animation.cpp
  src/anim/gltf_loader.cpp
  src/gfx/texture.cpp
  src/anim/skeleton.cpp
  src/anim/skinned_mesh.cpp
  src/anim/dragon_rig.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/buffer.cpp
)
target_include_directories(test_anim PRIVATE src)
target_link_libraries(test_anim PRIVATE SDL3::SDL3 cgltf stb)
target_compile_options(test_anim PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME anim COMMAND test_anim)

add_executable(test_combat
  tests/test_combat.cpp
  src/game/combat.cpp
  src/game/breath.cpp
  src/game/bot.cpp
  src/game/autopilot.cpp
  src/game/bot.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
  src/gfx/buffer.cpp
)
target_include_directories(test_combat PRIVATE src)
target_link_libraries(test_combat PRIVATE SDL3::SDL3)
target_compile_options(test_combat PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME combat COMMAND test_combat)

add_executable(test_bot
  tests/test_bot.cpp
  src/game/bot.cpp
  src/game/autopilot.cpp
  src/game/combat.cpp
  src/game/breath.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
  src/gfx/buffer.cpp
)
target_include_directories(test_bot PRIVATE src)
target_link_libraries(test_bot PRIVATE SDL3::SDL3)
target_compile_options(test_bot PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME bot COMMAND test_bot)

add_executable(test_match
  tests/test_match.cpp
  src/game/match.cpp
  src/game/combat.cpp
  src/game/breath.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
  src/gfx/buffer.cpp
)
target_include_directories(test_match PRIVATE src)
target_link_libraries(test_match PRIVATE SDL3::SDL3)
target_compile_options(test_match PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME match COMMAND test_match)

add_executable(test_vegetation
  tests/test_vegetation.cpp
  src/game/terrain.cpp
  src/game/vegetation.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/buffer.cpp
)
target_include_directories(test_vegetation PRIVATE src)
target_link_libraries(test_vegetation PRIVATE SDL3::SDL3)
target_compile_options(test_vegetation PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME vegetation COMMAND test_vegetation)
