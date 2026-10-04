// The XP build's entry points (include/SDL3/SDL_main.h renamed the program's
// main to SDL_main). Their own object file, so a program that defines main
// itself -- the test suites -- does not pull these in beside it.

#include <SDL3/SDL_main.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>

#undef main

extern "C" int main(int argc, char* argv[]) { return SDL_main(argc, argv); }

extern "C" int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return SDL_main(__argc, __argv); }
