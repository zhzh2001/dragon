# Cross-compiling Windows binaries on macOS or Linux with MinGW-w64
# (`brew install mingw-w64`). docs/PORTING.md: every Windows binary, modern
# and retro, is built on the Mac and only run on Windows.
#
#   cmake -S . -B build-win -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
#     -DDRAGON_FETCH_SDL=ON -DDRAGON_SHADERCROSS=OFF -DDRAGON_DEV_ROOTS=OFF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# One self-contained .exe: the GCC runtime, libstdc++ and winpthreads are
# linked in, so the package needs no MinGW DLLs beside it.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
