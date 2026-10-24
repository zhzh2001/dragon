#include "game/time_of_day.h"

#include <cmath>

namespace game {

namespace {

// The thermals' mix (game/thermals.cpp): one draw per statement.
uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float unit(uint32_t& state) {
    state = mix(state + 0x9e3779b9u);
    return float(state & 0xffffffu) / float(0xffffff);
}

// The three hours a run passes through. The run flies toward -Z, so an
// azimuth between 90 and 270 degrees puts the sun ahead: the backlit view of
// every target. Morning from ahead and to the right, evening from ahead and
// to the left, both low; a sun more from the side at 10-13 degrees left the
// whole floor in the walls' shadow. The afternoon spans the build's tuned
// 19 / 235, and has no mist: at 0.12 a raised camera looking 2-3 km across
// the floor saw it milky, the arena's tuned picture with it.
struct Key {
    const char* name;
    float elevation_lo, elevation_hi;
    float azimuth_lo, azimuth_hi;
    float mist;
};
constexpr Key KEYS[3] = {
    {"morning", 12.0f, 16.0f, 135.0f, 155.0f, 1.0f},
    {"afternoon", 19.0f, 26.0f, 205.0f, 235.0f, 0.0f},
    {"evening", 12.0f, 15.0f, 218.0f, 236.0f, 0.4f},
};

// Earth's air per km (gfx/atmosphere.cpp, Hillaire 2020).
constexpr float RAYLEIGH[3] = {5.802e-3f, 13.558e-3f, 33.1e-3f};
constexpr float RAYLEIGH_SCALE_HEIGHT_KM = 8.0f;
constexpr float MIE_EXTINCTION = 3.996e-3f + 0.444e-3f;
constexpr float MIE_SCALE_HEIGHT_KM = 1.2f;
constexpr float PLANET_RADIUS_KM = 6360.0f;

// The optical depth from the valley (0.2 km up) to the top of the air along
// a ray at `elevation_deg`, Rayleigh and aerosol separately, on the sphere.
void optical_depth(float elevation_deg, float& rayleigh, float& mie) {
    const float e = core::radians(elevation_deg);
    const float r0 = PLANET_RADIUS_KM + 0.2f;
    const float dx = std::cos(e), dy = std::sin(e);
    rayleigh = mie = 0.0f;
    // Out to 400 km along the ray, finer near the start where the air is.
    const int steps = 400;
    float t = 0.0f;
    for (int i = 0; i < steps; ++i) {
        const float u = (float(i) + 0.5f) / float(steps);
        const float t_mid = 400.0f * u * u;
        const float t_next = 400.0f * (float(i) + 1.0f) * (float(i) + 1.0f) / float(steps * steps);
        const float dt = t_next - t;
        t = t_next;
        const float x = dx * t_mid, y = r0 + dy * t_mid;
        const float h = std::sqrt(x * x + y * y) - PLANET_RADIUS_KM;
        if (h > 100.0f) break;
        rayleigh += std::exp(-h / RAYLEIGH_SCALE_HEIGHT_KM) * dt;
        mie += std::exp(-h / MIE_SCALE_HEIGHT_KM) * dt;
    }
}

}  // namespace

TimeOfDay time_of_day_for(uint32_t seed, int depth, int valleys) {
    uint32_t state = mix(seed * 2246822519u + 101u);
    const float jitter_elevation = unit(state);
    const float jitter_azimuth = unit(state);
    // Where in the day this valley falls, 0 morning .. 2 evening.
    const float at = valleys > 1 ? 2.0f * float(depth) / float(valleys - 1) : 0.0f;
    const int a = at >= 1.0f ? 1 : 0;
    const float f = core::clampf(at - float(a), 0.0f, 1.0f);
    const Key& k0 = KEYS[a];
    const Key& k1 = KEYS[a + 1];
    const float elevation_lo = core::lerpf(k0.elevation_lo, k1.elevation_lo, f);
    const float elevation_hi = core::lerpf(k0.elevation_hi, k1.elevation_hi, f);
    const float azimuth_lo = core::lerpf(k0.azimuth_lo, k1.azimuth_lo, f);
    const float azimuth_hi = core::lerpf(k0.azimuth_hi, k1.azimuth_hi, f);
    TimeOfDay t;
    t.name = f < 0.5f ? k0.name : k1.name;
    t.sun_elevation_deg = core::lerpf(elevation_lo, elevation_hi, jitter_elevation);
    t.sun_azimuth_deg = core::lerpf(azimuth_lo, azimuth_hi, jitter_azimuth);
    t.mist = core::lerpf(k0.mist, k1.mist, f);
    return t;
}

core::Vec3 sun_colour_ratio(float elevation_deg, float reference_deg, float rayleigh, float mie) {
    float r_at, m_at, r_ref, m_ref;
    optical_depth(core::maxf(elevation_deg, 0.5f), r_at, m_at);
    optical_depth(core::maxf(reference_deg, 0.5f), r_ref, m_ref);
    core::Vec3 ratio;
    float* out[3] = {&ratio.x, &ratio.y, &ratio.z};
    for (int c = 0; c < 3; ++c) {
        const float tau_at = RAYLEIGH[c] * rayleigh * r_at + MIE_EXTINCTION * mie * m_at;
        const float tau_ref = RAYLEIGH[c] * rayleigh * r_ref + MIE_EXTINCTION * mie * m_ref;
        *out[c] = std::exp(tau_ref - tau_at);
    }
    return ratio;
}

}  // namespace game
