#include "core/profile.h"

#include <SDL3/SDL.h>

#include <cstring>

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

}  // namespace core::profile
