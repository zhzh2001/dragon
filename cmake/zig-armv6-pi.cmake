# Cross-building for the Raspberry Pi 1B (docs/porting/MOBILE.md, M5) with
# Zig's clang and its own libc++, linked statically, against a Bullseye
# sysroot (tools/pi/build.sh makes it). Use through tools/pi/build.sh.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv6l)
get_filename_component(_pi_tools "${CMAKE_CURRENT_LIST_DIR}/../tools/pi" ABSOLUTE)
set(CMAKE_C_COMPILER "${_pi_tools}/zig-cc")
set(CMAKE_CXX_COMPILER "${_pi_tools}/zig-c++")
set(CMAKE_AR "${_pi_tools}/zig-ar")
set(CMAKE_RANLIB "${_pi_tools}/zig-ranlib")
if(NOT DRAGON_PI_SYSROOT)
  get_filename_component(DRAGON_PI_SYSROOT "${CMAKE_CURRENT_LIST_DIR}/../build-pi/sysroot" ABSOLUTE)
endif()
set(CMAKE_FIND_ROOT_PATH "${DRAGON_PI_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_LIBRARY_ARCHITECTURE arm-linux-gnueabihf)
# No C++20 modules here, and Zig ships no clang-scan-deps for CMake to scan with.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# SDL asks 32-bit glibc for a 64-bit time_t (_TIME_BITS=64), which glibc
# 2.34 added: Bullseye's 2.31 has no __clock_gettime64, and Zig's generic
# glibc headers honour the define whatever the target version. The flags
# come after CMake's definitions, so this undoes it.
set(CMAKE_C_FLAGS_INIT "-U_TIME_BITS")
set(CMAKE_CXX_FLAGS_INIT "-U_TIME_BITS")
