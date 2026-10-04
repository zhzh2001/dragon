#pragma once
// The XP build's SDL3 subset (SDL.h). As in SDL, the program's main becomes
// SDL_main and the library supplies the entry points: main for a console
// build, WinMain for a GUI-subsystem package (src/platform/xp/sdl3_xp.cpp).
#include "SDL.h"
#define main SDL_main
#ifdef __cplusplus
extern "C"
#endif
int SDL_main(int argc, char* argv[]);
