cmake_minimum_required(VERSION 3.24)
project(dragon LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

include(cmake/sdl3.cmake)

include(cmake/imgui.cmake)
include(cmake/cgltf.cmake)
include(cmake/stb.cmake)
include(cmake/meshoptimizer.cmake)
include(cmake/miniaudio.cmake)

add_executable(dragon
  src/main.cpp
  src/app.cpp
  src/core/input.cpp
  src/core/paths.cpp
  src/rhi/device.cpp
  src/rhi/sdlgpu/sdlgpu_device.cpp
  src/gfx/device.cpp
  src/gfx/pipeline.cpp
  src/gfx/debug_draw.cpp
  src/gfx/mesh.cpp
  src/gfx/noise_lattice.cpp
  src/gfx/mesh_lod.cpp
  src/gfx/graphics_settings.cpp
  src/gfx/primitives.cpp
  src/gfx/texture.cpp
  src/gfx/particles.cpp
  src/gfx/foliage.cpp
  src/game/vegetation.cpp
  src/audio/audio.cpp
  src/anim/skeleton.cpp
  src/anim/skinned_mesh.cpp
  src/anim/skin_partition.cpp
  src/anim/skin_lod.cpp
  src/anim/dragon_rig.cpp
  src/anim/animation.cpp
  src/anim/gltf_loader.cpp
  src/gfx/scene_uniforms.cpp
  src/gfx/world_renderer.cpp
  src/gfx/static_model.cpp
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
  src/game/element.cpp
  src/game/bot.cpp
  src/game/studio.cpp
  src/game/match.cpp
  src/game/hoard_run.cpp
  src/game/demo_pilot.cpp
  src/game/prey.cpp
  src/editor/imgui_layer.cpp
  src/ui/hud.cpp
)

target_include_directories(dragon PRIVATE src)
target_link_libraries(dragon PRIVATE SDL3::SDL3 imgui cgltf stb miniaudio meshoptimizer)
if(WIN32)
  # The Direct3D 9 backend (docs/PORTING.md, R3): --gpu-driver direct3d9.
  target_sources(dragon PRIVATE src/rhi/d3d9/d3d9_device.cpp)
  target_link_libraries(dragon PRIVATE d3d9)
endif()

# Data is found beside the executable first (core/paths.h). A development
# build has none there, so it falls back to the source tree, and hot reload
# edits the same shader files we author. A package is configured with
# DRAGON_DEV_ROOTS=OFF: it carries its own data, and no source path from the
# build machine is baked into the binary.
option(DRAGON_DEV_ROOTS "Fall back to the source tree for assets and shaders" ON)
if(WIN32 AND NOT DRAGON_DEV_ROOTS)
  # A Windows package opens no console window (main.cpp attaches to the
  # parent terminal when there is one).
  set_target_properties(dragon PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
if(DRAGON_DEV_ROOTS)
  target_compile_definitions(dragon PRIVATE
    SHADER_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/assets"
  )
endif()

# Shaders are HLSL (shaders/common.hlsl). A development build compiles them
# at runtime through SDL_shadercross, DXC included, which is what keeps hot
# reload working; build it once and install it under ~/.local/opt/shadercross
# (docs/PORTING.md, P1). A package is built with DRAGON_SHADERCROSS=OFF and
# loads the shaders tools/release/bake_shaders.sh translated ahead of time, so
# it carries neither DXC nor a second SDL.
option(DRAGON_SHADERCROSS "Compile the HLSL shaders at runtime through SDL_shadercross" ON)
if(DRAGON_SHADERCROSS)
  find_package(SDL3_shadercross CONFIG REQUIRED
    HINTS "$ENV{HOME}/.local/opt/shadercross" "${DRAGON_SHADERCROSS_ROOT}")
  target_link_libraries(dragon PRIVATE SDL3_shadercross::SDL3_shadercross-shared)
  target_compile_definitions(dragon PRIVATE DRAGON_SHADERCROSS)
  get_target_property(_shadercross_lib SDL3_shadercross::SDL3_shadercross-shared IMPORTED_LOCATION_RELEASE)
  get_filename_component(_shadercross_lib_dir "${_shadercross_lib}" DIRECTORY)
  set_target_properties(dragon PROPERTIES BUILD_RPATH "${_shadercross_lib_dir}")
endif()

target_compile_options(dragon PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

# Tests are a plain executable rather than a framework: `ctest` or just run it.
enable_testing()
add_executable(test_breath tests/test_breath.cpp src/game/breath.cpp src/game/element.cpp)
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
)
target_include_directories(test_rally PRIVATE src)
target_link_libraries(test_rally PRIVATE SDL3::SDL3)
target_compile_options(test_rally PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME rally COMMAND test_rally)

add_executable(test_anim
  tests/test_anim.cpp
  src/gfx/static_model.cpp
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
)
target_include_directories(test_anim PRIVATE src)
target_link_libraries(test_anim PRIVATE SDL3::SDL3 cgltf stb)
target_compile_options(test_anim PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME anim COMMAND test_anim)

# Palette splitting: the split mesh must skin exactly like the whole.
add_executable(test_skin_partition
  tests/test_skin_partition.cpp
  src/anim/skin_partition.cpp
  src/anim/skinned_mesh.cpp
)
target_include_directories(test_skin_partition PRIVATE src)
target_link_libraries(test_skin_partition PRIVATE SDL3::SDL3)
target_compile_options(test_skin_partition PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME skin_partition COMMAND test_skin_partition)

# The baked noise lattice: the retro tiers' value noise must be the modern one.
add_executable(test_noise_lattice
  tests/test_noise_lattice.cpp
  src/gfx/noise_lattice.cpp
)
target_include_directories(test_noise_lattice PRIVATE src)
target_link_libraries(test_noise_lattice PRIVATE SDL3::SDL3)
target_compile_options(test_noise_lattice PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME noise_lattice COMMAND test_noise_lattice)

# The texture budget: capping, CPU mips and block compression keep content.
add_executable(test_texture_budget
  tests/test_texture_budget.cpp
  src/gfx/texture.cpp
)
target_include_directories(test_texture_budget PRIVATE src)
target_link_libraries(test_texture_budget PRIVATE SDL3::SDL3 stb)
target_compile_options(test_texture_budget PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME texture_budget COMMAND test_texture_budget)

# Creature LODs: a simplified skinned mesh keeps only original vertices.
add_executable(test_skin_lod
  tests/test_skin_lod.cpp
  src/anim/skin_lod.cpp
  src/anim/skinned_mesh.cpp
)
target_include_directories(test_skin_lod PRIVATE src)
target_link_libraries(test_skin_lod PRIVATE SDL3::SDL3 meshoptimizer)
target_compile_options(test_skin_lod PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME skin_lod COMMAND test_skin_lod)

# Graphics presets: allowed where offered, monotonic, saved and read back.
add_executable(test_graphics_settings
  tests/test_graphics_settings.cpp
  src/gfx/graphics_settings.cpp
)
target_include_directories(test_graphics_settings PRIVATE src)
target_compile_options(test_graphics_settings PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME graphics_settings COMMAND test_graphics_settings)

add_executable(test_combat
  tests/test_combat.cpp
  src/game/combat.cpp
  src/game/breath.cpp
  src/game/element.cpp
  src/game/bot.cpp
  src/game/autopilot.cpp
  src/game/bot.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
)
target_include_directories(test_combat PRIVATE src)
target_link_libraries(test_combat PRIVATE SDL3::SDL3)
target_compile_options(test_combat PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME combat COMMAND test_combat)

add_executable(test_element
  tests/test_element.cpp
  src/game/combat.cpp
  src/game/breath.cpp
  src/game/element.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
)
target_include_directories(test_element PRIVATE src)
target_link_libraries(test_element PRIVATE SDL3::SDL3)
target_compile_options(test_element PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME element COMMAND test_element)

add_executable(test_bot
  tests/test_bot.cpp
  src/game/bot.cpp
  src/game/autopilot.cpp
  src/game/combat.cpp
  src/game/breath.cpp
  src/game/element.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
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
  src/game/element.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
)
target_include_directories(test_match PRIVATE src)
target_link_libraries(test_match PRIVATE SDL3::SDL3)
target_compile_options(test_match PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME match COMMAND test_match)

add_executable(test_hoard_run
  tests/test_hoard_run.cpp
  src/game/hoard_run.cpp
  src/game/course.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
)
target_include_directories(test_hoard_run PRIVATE src)
target_link_libraries(test_hoard_run PRIVATE SDL3::SDL3)
target_compile_options(test_hoard_run PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME hoard_run COMMAND test_hoard_run)

add_executable(test_prey
  tests/test_prey.cpp
  src/game/prey.cpp
  src/game/combat.cpp
  src/game/breath.cpp
  src/game/element.cpp
  src/game/hoard_run.cpp
  src/game/course.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
)
target_include_directories(test_prey PRIVATE src)
target_link_libraries(test_prey PRIVATE SDL3::SDL3)
target_compile_options(test_prey PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME prey COMMAND test_prey)

add_executable(test_demo_pilot
  tests/test_demo_pilot.cpp
  src/game/demo_pilot.cpp
  src/game/bot.cpp
  src/game/autopilot.cpp
  src/game/maneuver.cpp
  src/game/combat.cpp
  src/game/breath.cpp
  src/game/element.cpp
  src/game/flight.cpp
  src/game/terrain.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
  src/gfx/primitives.cpp
)
target_include_directories(test_demo_pilot PRIVATE src)
target_link_libraries(test_demo_pilot PRIVATE SDL3::SDL3)
target_compile_options(test_demo_pilot PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME demo_pilot COMMAND test_demo_pilot)

add_executable(test_vegetation
  tests/test_vegetation.cpp
  src/game/terrain.cpp
  src/game/vegetation.cpp
  src/core/noise.cpp
  src/gfx/mesh.cpp
)
target_include_directories(test_vegetation PRIVATE src)
target_link_libraries(test_vegetation PRIVATE SDL3::SDL3)
target_compile_options(test_vegetation PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
add_test(NAME vegetation COMMAND test_vegetation)
