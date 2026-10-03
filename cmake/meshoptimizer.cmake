# meshoptimizer (MIT): mesh simplification for the retro tiers' creature LODs
# (anim/skin_lod.h). Built from source as a small static library.
include(FetchContent)

FetchContent_Declare(meshoptimizer
  GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
  GIT_TAG        v1.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(meshoptimizer)

# A preset must build the same LOD on every platform (docs/PORTING.md, R7).
# clang on arm64 fuses a multiply and an add into one FMA by default, x86-64
# without -mfma cannot, and the simplifier's collapse errors then round
# differently and pick different edges: the Very low dragon came out a
# different shape on the Mac than on Windows. Round every operation alone.
if(NOT MSVC)
  target_compile_options(meshoptimizer PRIVATE -ffp-contract=off)
  set_source_files_properties(src/anim/skin_lod.cpp src/gfx/mesh_lod.cpp
    DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTIES COMPILE_OPTIONS -ffp-contract=off)
endif()
