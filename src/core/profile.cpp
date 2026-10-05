#include "core/profile.h"

#include <SDL3/SDL.h>

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace core::profile {
namespace {

struct Section {
    const char* name;
    uint64_t ns;
};
Section g_sections[64];
int g_count = 0;
int g_enabled = -1;

}  // namespace

bool enabled() {
    if (g_enabled < 0) g_enabled = SDL_getenv("DRAGON_FRAME_PROFILE") != nullptr;
    return g_enabled != 0;
}

void add(const char* name, uint64_t ns) {
    for (int i = 0; i < g_count; ++i) {
        if (g_sections[i].name == name || std::strcmp(g_sections[i].name, name) == 0) {
            g_sections[i].ns += ns;
            return;
        }
    }
    if (g_count < 64) g_sections[g_count++] = Section{name, ns};
}

void report(uint64_t frames, void (*print)(const char* name, double ms)) {
    if (frames == 0) return;
    for (int i = 0; i < g_count; ++i) print(g_sections[i].name, double(g_sections[i].ns) * 1e-6 / double(frames));
}

void reset() {
    for (int i = 0; i < g_count; ++i) g_sections[i].ns = 0;
}

Scope::Scope(const char* section) : name(section), start(enabled() ? SDL_GetTicksNS() : 0) {}

Scope::~Scope() {
    if (start) add(name, SDL_GetTicksNS() - start);
}

double process_cpu_seconds() {
#ifdef _WIN32
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0.0;
    const auto seconds = [](const FILETIME& t) {
        return double((uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) * 1e-7;
    };
    return seconds(kernel) + seconds(user);
#else
    rusage usage = {};
    getrusage(RUSAGE_SELF, &usage);
    return double(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
           double(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) * 1e-6;
#endif
}

}  // namespace core::profile
