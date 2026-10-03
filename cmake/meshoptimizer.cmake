# meshoptimizer (MIT): mesh simplification for the retro tiers' creature LODs
# (anim/skin_lod.h). Built from source as a small static library.
include(FetchContent)

FetchContent_Declare(meshoptimizer
  GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
  GIT_TAG        v1.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(meshoptimizer)
