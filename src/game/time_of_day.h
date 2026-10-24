#pragma once

#include <cstdint>

#include "core/math.h"

namespace game {

// A time of day for each valley of a run (docs/GAPS.md, G8): the descent
// starts in the morning and ends at dusk, each valley's hour jittered by its
// seed. Every frame of the targets is a low sun, backlit, warm on one side and
// cool on the other; the afternoon the build was tuned at is one of them, and
// a run now passes through three.
struct TimeOfDay {
    const char* name = "afternoon";
    float sun_elevation_deg = 19.0f;
    float sun_azimuth_deg = 235.0f;
    // The valley mist's density (scene_common.hlsl, valley_mist), 0..1: thick
    // at dawn, burnt off by the afternoon, a little back at dusk.
    float mist = 0.0f;
};

// Valley `depth` (0 the first) of `valleys`, seeded `seed`: morning, then
// the afternoon, then the evening, interpolated for a run of another length.
// Deterministic, one draw per statement.
TimeOfDay time_of_day_for(uint32_t seed, int depth, int valleys);

// The sun's colour at `elevation_deg`, relative to the colour it has at
// `reference_deg`: the air's transmittance along the two paths, divided
// (Rayleigh and aerosol, Earth's coefficients times the Atmosphere panel's
// `rayleigh` and `mie` dials, as gfx/atmosphere.cpp has them). Times the
// tuned late-afternoon colour, a lower sun is redder and dimmer and a higher
// one whiter, and 19 degrees is exactly the colour the build was tuned with.
core::Vec3 sun_colour_ratio(float elevation_deg, float reference_deg, float rayleigh, float mie);

}  // namespace game
