#include <SDL3/SDL_main.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#endif

#include "app.h"
#include "core/paths.h"

int main(int argc, char** argv) {
#ifdef _WIN32
    // A package is a GUI-subsystem program, so a double-click opens no
    // console window. Started from a terminal it borrows that terminal for
    // its log instead of printing nowhere -- unless the caller already handed
    // it somewhere to write (a pipe, a file), which must win.
    const HANDLE given = GetStdHandle(STD_ERROR_HANDLE);
    if ((given == nullptr || given == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
#endif
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_INFO);

    core::paths::init();
    app::Options options = app::parse_options(argc, argv);
    app::App application;
    if (!application.init(options)) {
        application.shutdown();
        return 1;
    }
    application.run();
    application.shutdown();
    return 0;
}
