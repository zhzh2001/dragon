# SDL3: the system package by default (`brew install sdl3`), which is fastest to
# build against. A release package builds it from source instead
# (DRAGON_FETCH_SDL=ON) and links it statically. A package must not depend on
# whatever SDL the user has installed, and Homebrew's is built for the build
# machine's own macOS, so it will not load on older systems. The static
# library is also simply part of the binary: no dylib to bundle or relink.
option(DRAGON_FETCH_SDL "Build SDL3 from source and link it statically" OFF)
if(DRAGON_FETCH_SDL)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  # Cross-compiling, SDL's HIDAPI check would find the build machine's own
  # libusb (Homebrew's, on the Mac) for a Windows target. Windows needs none.
  if(CMAKE_CROSSCOMPILING)
    set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
  endif()
  FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(SDL3)
  if(NOT TARGET SDL3::SDL3)
    add_library(SDL3::SDL3 ALIAS SDL3-static)
  endif()
else()
  find_package(SDL3 REQUIRED CONFIG)
endif()
