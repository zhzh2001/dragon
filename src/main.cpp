#include <SDL3/SDL_main.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#endif

#include "app.h"
#include "core/paths.h"

namespace {

// DRAGON_LOG_FILE: every log line also goes to this file. A run on a desktop
// session started remotely (tools/x99/run_interactive.ps1, the G41's job
// runner) has no console or pipe for SDL's own output to reach.
struct LogFile {
    FILE* file = nullptr;
    SDL_LogOutputFunction previous = nullptr;
    void* previous_data = nullptr;
};
LogFile g_log_file;

void log_to_file(void*, int category, SDL_LogPriority priority, const char* message) {
    if (g_log_file.file) {
        std::fprintf(g_log_file.file, "%s\n", message);
        std::fflush(g_log_file.file);
    }
    if (g_log_file.previous) g_log_file.previous(g_log_file.previous_data, category, priority, message);
}

}  // namespace

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
    if (const char* path = SDL_getenv("DRAGON_LOG_FILE")) {
        g_log_file.file = std::fopen(path, "w");
        if (g_log_file.file) {
            SDL_GetLogOutputFunction(&g_log_file.previous, &g_log_file.previous_data);
            SDL_SetLogOutputFunction(log_to_file, nullptr);
        }
    }

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
