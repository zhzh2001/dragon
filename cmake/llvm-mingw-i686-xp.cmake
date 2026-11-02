# The Windows XP build (docs/PORTING.md, R6): 32-bit, msvcrt, no SDL3.
#
#   cmake -S . -B build-xp -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-i686-xp.cmake \
#     -DDRAGON_XP=ON -DDRAGON_SHADERCROSS=OFF -DDRAGON_DEV_ROOTS=OFF
#
# The compiler is the Mac's own Homebrew clang and lld; the target files --
# mingw-w64's headers and import libraries for msvcrt.dll, libc++,
# libunwind, compiler-rt's builtins -- come from llvm-mingw's msvcrt release
# (R0 chose it: the Mac's i686 MinGW is UCRT, which XP has only through a
# redistributable). Those files are the same whatever the host, so the
# Linux-hosted tarball is unpacked into ~/.local/opt/llvm-mingw-msvcrt
# (DRAGON_LLVM_MINGW overrides), and clang gets a resource directory of its
# own whose lib/windows holds the release's i386 builtins:
#
#   gh release download 20260922 -R mstorsjo/llvm-mingw \
#     -p 'llvm-mingw-20260922-msvcrt-ubuntu-22.04-x86_64.tar.xz'
#   (sha256 17bbc667b96f7b1f02ce4a172d8cac4fa480927a1f649e788dee5914919566a1)
#   tar -xJf ... into ~/.local/opt/llvm-mingw-msvcrt, then
#   mkdir clang22-resource/lib; ln -s <homebrew llvm>/lib/clang/22/include clang22-resource/include
#   ln -s ../../lib/clang/23/lib/windows clang22-resource/lib/windows
#
# What libc++ and the CRT import that XP lacks is supplied by
# src/platform/xp/xp_thunks.cpp, and tools/xp/check_imports.py is the gate.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(DRAGON_LLVM "/opt/homebrew/opt/llvm@22" CACHE PATH "the clang that compiles the XP build")
set(DRAGON_LLD "/opt/homebrew/opt/lld@22" CACHE PATH "the lld that links it")
if(DEFINED ENV{DRAGON_LLVM_MINGW})
  set(DRAGON_LLVM_MINGW "$ENV{DRAGON_LLVM_MINGW}" CACHE PATH "")
else()
  set(DRAGON_LLVM_MINGW "$ENV{HOME}/.local/opt/llvm-mingw-msvcrt" CACHE PATH "llvm-mingw's msvcrt release, unpacked")
endif()

set(CMAKE_C_COMPILER "${DRAGON_LLVM}/bin/clang")
set(CMAKE_CXX_COMPILER "${DRAGON_LLVM}/bin/clang++")
set(CMAKE_RC_COMPILER "${DRAGON_LLVM}/bin/llvm-windres")
set(CMAKE_AR "${DRAGON_LLVM}/bin/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${DRAGON_LLVM}/bin/llvm-ranlib" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_TARGET i686-w64-mingw32)
set(CMAKE_CXX_COMPILER_TARGET i686-w64-mingw32)
set(CMAKE_SYSROOT "${DRAGON_LLVM_MINGW}")

# SSE2 and SSE maths: the same floating point as the x86-64 builds, so a
# preset builds the same content (CLAUDE.md, "One random draw per
# statement"); x87 would round differently. Every XP-era target CPU has it
# (the Pentium 4 benches, the G41's Core 2). _WIN32_WINNT=0x0501: the headers
# declare only what XP has, so a newer API fails to compile here rather than
# to load there.
set(_xp_flags "-resource-dir=${DRAGON_LLVM_MINGW}/clang22-resource -stdlib=libc++ -msse2 -mfpmath=sse -D_WIN32_WINNT=0x0501 -DWINVER=0x0501")
set(CMAKE_C_FLAGS_INIT "${_xp_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_xp_flags}")
# One self-contained .exe: libc++, libunwind and the builtins linked in.
# lld stamps an executable for Windows 6.0 (Vista) unless told otherwise, and
# XP's loader then refuses to start it at all ("not a valid Win32
# application"): the OS and subsystem versions say 5.1.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=${DRAGON_LLD}/bin/ld.lld -rtlib=compiler-rt -unwindlib=libunwind -static -Wl,--major-os-version,5,--minor-os-version,1,--major-subsystem-version,5,--minor-subsystem-version,1")

set(CMAKE_FIND_ROOT_PATH "${DRAGON_LLVM_MINGW}/i686-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
