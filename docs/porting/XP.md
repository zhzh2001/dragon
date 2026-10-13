# Porting: the Windows XP platform layer (R6)

How the 32-bit XP build is made without SDL3: the toolchain, the SDL3
subset, the thunks and the import gate. What it measured on real cards is
in [`CARDS.md`](CARDS.md).

Part of the porting record; [`../PORTING.md`](../PORTING.md) is the index, with the
state of every phase and a map from the section names cited in code
comments ("R6, as built", "The G550 on the G41") to the file that holds them.

## R6, as built (2026-10-04): the XP platform layer

The Windows XP build is `build-xp/dragon.exe`: 32-bit, the D3D9 backend
alone, and no SDL3, which needs Windows 7.

```sh
cmake -S . -B build-xp -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-i686-xp.cmake \
  -DDRAGON_XP=ON -DDRAGON_SHADERCROSS=OFF -DDRAGON_DEV_ROOTS=OFF
cmake --build build-xp
tools/xp/check_imports.py build-xp/dragon.exe     # the gate
tools/release/package_xp.sh 0.2.0                 # dist/Dragon-0.2.0-xp.zip
```

**The toolchain** is R0's choice, now hosted on the Mac: llvm-mingw's
msvcrt target files (mingw-w64's headers and import libraries for
msvcrt.dll, libc++, libunwind, compiler-rt) unpacked from its Linux-hosted
release into `~/.local/opt/llvm-mingw-msvcrt` -- target files are the same
whatever the host -- and compiled by Homebrew's clang 21 with lld, given a
resource directory of its own that holds the release's i386 builtins. The
release's LLVM 23 libc++ headers compile under clang 21 unchanged.
`cmake/llvm-mingw-i686-xp.cmake` has the recipe. SSE2 maths keeps the
content identical to the x86-64 builds: the XP build's frame under Wine is
**pixel-identical** to the 64-bit Windows build's D3D9 frame (sm3, the same
Wine), LOD fingerprints and all. All 21 test suites pass as 32-bit
libc++ executables under Wine.

**The platform layer is an SDL3 subset, not a new interface.** The engine
calls about sixty SDL functions outside the SDL GPU backend -- one window,
its events, scancodes, the mouse with a relative mode, gamepads, logging,
files, paths, ticks and a BMP writer. Rather than put an interface under
twenty-odd files, the XP build compiles the same sources against
`src/platform/xp/include/SDL3/`, which declares exactly those, with SDL3's
names, signatures and values (HID scancodes, button and axis order), and
`src/platform/xp/sdl3_xp.cpp` implements them on XP SP3's Win32: a
`CreateWindowExW` window and a message pump into SDL events, raw-input
mouse motion with a clipped, hidden cursor for relative mode, XInput
loaded from whichever DLL exists (9.1.0 ships with XP's DirectX), UTF-8
paths through the wide-character APIs, `SHGetFolderPathW` for the user
directory, `QueryPerformanceCounter` for ticks. CMake aliases it as
`SDL3::SDL3`, so every target, tests included, links it unchanged. The only
extension is `SDLXP_SetWndProcHook`, through which Dear ImGui's Win32
backend sees the window's messages (`imgui_impl_win32` and `imgui_impl_dx9`
replace the SDL3 and SDL GPU ones in that build). A call the shared code
starts making that the subset does not declare fails the XP build -- which
is the intended guard. This is hand-rolled platform code against the
third-party policy's letter; it is the "thin Win32 layer" R6 planned, kept
to SDL3's shape so nothing else changes, and every other build still links
SDL3.

**What XP lacks is thunked in the executable.** llvm-mingw's libc++ is
built for Windows 7 and its msvcrt for Vista's msvcrt.dll: the first link
imported SRW locks and condition variables, `InitOnceExecuteOnce`, fiber
storage, `GetLocaleInfoEx`, `GetThreadId`, `K32EnumProcessModules`,
nineteen `_l` locale functions and `wcrtomb_s` from msvcrt.dll, and Dear
ImGui's Win32 backend pulled in dwmapi.dll, which XP does not have. Any one
of those and XP's loader refuses the program before `main`.
`src/platform/xp/xp_thunks.cpp` defines each import itself -- the
`__imp__Name@N` pointer and the `_Name@N` stub, compiled into the
executable so the linker takes them before the import libraries -- and
each calls the system's function when it exists and otherwise does the job
with what XP has (a spin lock in the SRW word, a condition variable that
wakes spuriously, a three-state call-once, TLS for fiber storage, LCIDs for
locale names, ntdll for a thread's id, psapi.dll, the plain CRT functions
under the "C" locale). The system set of locks is used only when all of it
is there, so a lock is never taken by one implementation and released by
the other. `DRAGON_XP_THUNKS=fallback` forces the XP side on any Windows:
under Wine the frame is identical either way and the suites pass. The file
is compiled with `-fno-threadsafe-statics`, since libc++abi's static guard
would call back into the locks it defines.

**The gate** is `tools/xp/check_imports.py`: the executable's import table
against what XP SP3's own DLLs export. The lists come from the real files:
`--dlls DIR` reads kernel32, user32, gdi32, msvcrt, shell32 and DirectX
9.0c's d3d9 directly, `--record DIR` writes them to
`tools/xp/xp-sp3-exports.txt` for the packager. **That list is not
recorded yet**: x99 (whose qemu-gpu XP image has the files) and the G41
were both off when R6 was built, so the check has run only against Wine's
DLLs, which proves the parser and nothing about XP. Recording it is the
first thing to do with either machine; the packager refuses to run
without it.

**Picking the tier.** A D3D9 device now asks the card's caps before the
tier is fixed (`rhi::d3d9::probe_shader_caps`): vs/ps 3.0 is sm3, ps 2.0
sm2, less fixed function, and a tier asked for or saved above that comes
down to it. On XP that is what makes a double-click work on any card.

**On the G41** (operated as `~/src/qemu-gpu/docs/G41_TESTBENCH.md`
describes: `agent.ahk` runs job scripts dropped in XP's shared `bench`
folder, `g41job.sh` submits them from the Mac over the direct cable, OBS
watches the VGA output), two jobs are ready in `tools/xp/g41/`:
`xp-dlls.ahk` copies XP's system DLLs to `bench\results\xp-dlls\` for
`check_imports.py --record`, and `dragon-run.ahk` runs the unzipped package
from `bench\dragon\` with the arguments in its `args.txt`, the game's log
through `DRAGON_LOG_FILE`, and its own `--screenshot` (the pixel oracle; the
OBS capture is analog VGA). The G41 is shared with the qemu-gpu sessions,
so jobs go in only when the bench is free.
