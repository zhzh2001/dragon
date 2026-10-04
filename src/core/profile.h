#pragma once

// A frame profiler for machines with no profiler on them (the G41 under XP):
// named sections accumulate wall-clock time, and App::run prints the
// per-frame averages at exit. Off unless DRAGON_FRAME_PROFILE is set, when a
// section costs two clock reads.
//
//   { CORE_PROFILE("terrain draws"); world_.draw_terrain(...); }

#include <cstdint>

namespace core::profile {

bool enabled();
void add(const char* name, uint64_t ns);
// The sections in first-seen order, averaged over `frames`, through `print`.
void report(uint64_t frames, void (*print)(const char* name, double ms));
void reset();

struct Scope {
    explicit Scope(const char* section);
    ~Scope();
    const char* name;
    uint64_t start;
};

}  // namespace core::profile

#define CORE_PROFILE_CAT2(a, b) a##b
#define CORE_PROFILE_CAT(a, b) CORE_PROFILE_CAT2(a, b)
#define CORE_PROFILE(section) ::core::profile::Scope CORE_PROFILE_CAT(core_profile_scope_, __LINE__)(section)
