# The Linux release (tools/release/package_linux.sh): Zig's clang targeting
# glibc 2.31, so a build on a rolling distro (x99's CachyOS has glibc 2.43)
# still starts on Ubuntu 20.04 and later. SDL opens X11, Wayland, Vulkan and
# the audio servers at run time, so only their headers are needed, and those
# come from the build machine (tools/linux/zig-cc).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
get_filename_component(_linux_tools "${CMAKE_CURRENT_LIST_DIR}/../tools/linux" ABSOLUTE)
set(CMAKE_C_COMPILER "${_linux_tools}/zig-cc")
set(CMAKE_CXX_COMPILER "${_linux_tools}/zig-c++")
set(CMAKE_AR "${_linux_tools}/zig-ar")
set(CMAKE_RANLIB "${_linux_tools}/zig-ranlib")
# No C++20 modules here, and Zig ships no clang-scan-deps for CMake to scan with.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
