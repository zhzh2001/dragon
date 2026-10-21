// Dynamic resolution (gfx/dynamic_resolution.h): a GPU over its budget
// drops the scale to what fits, the scale climbs back only with room to
// spare, it never leaves its bounds, and it settles instead of swinging.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gfx/dynamic_resolution.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("  FAIL (line %d): %s\n", __LINE__, #cond);       \
        }                                                                 \
    } while (0)

namespace {

constexpr float DT = 1.0f / 60.0f;

// A GPU whose time is a fixed part plus a part that grows with the pixels.
float gpu_ms_at(float scale, float fixed, float per_pixel) { return fixed + per_pixel * scale * scale; }

// Runs `seconds` of frames against that GPU; returns the scale at the end
// and counts the changes.
float run(gfx::DynamicResolution& d, const gfx::DynamicResolutionSettings& s, float seconds, float fixed,
          float per_pixel, int* changes) {
    float scale = d.scale();
    for (int i = 0; i < int(seconds / DT); ++i) {
        const float next = d.update(s, gpu_ms_at(scale, fixed, per_pixel), DT, 1.0f);
        if (next != scale && changes) ++*changes;
        scale = next;
    }
    return scale;
}

}  // namespace

int main() {
    gfx::DynamicResolutionSettings s;  // 60 fps, 0.88 of 16.7 ms: a 14.7 ms budget
    {
        // A light frame stays at full scale.
        gfx::DynamicResolution d;
        CHECK(run(d, s, 10.0f, 1.0f, 8.0f, nullptr) == 1.0f);
    }
    {
        // 21 ms at full scale (the M5 at its panel's resolution): it drops,
        // fits the budget, and stays put.
        gfx::DynamicResolution d;
        int changes = 0;
        const float scale = run(d, s, 30.0f, 1.0f, 20.0f, &changes);
        CHECK(scale < 1.0f);
        CHECK(scale >= s.min_scale);
        CHECK(gpu_ms_at(scale, 1.0f, 20.0f) <= 1000.0f / 60.0f * s.headroom);
        CHECK(changes <= 4);
        // And once settled, nothing more moves.
        int later = 0;
        run(d, s, 30.0f, 1.0f, 20.0f, &later);
        CHECK(later == 0);
    }
    {
        // A heavy moment, then a light one: it climbs back to full.
        gfx::DynamicResolution d;
        run(d, s, 10.0f, 1.0f, 30.0f, nullptr);
        CHECK(d.scale() < 0.8f);
        CHECK(run(d, s, 60.0f, 1.0f, 6.0f, nullptr) == 1.0f);
    }
    {
        // Never under the floor however heavy, never over the ceiling.
        gfx::DynamicResolution d;
        CHECK(std::fabs(run(d, s, 20.0f, 1.0f, 200.0f, nullptr) - s.min_scale) < 1e-4f);
        CHECK(d.update(s, 2.0f, DT, 0.7f) <= 0.7f);
    }
    {
        // A frame four times over the budget drops two steps at a time, not
        // straight to the estimate (the picture went soft in one frame).
        gfx::DynamicResolution d;
        float scale = d.scale();
        float largest = 0.0f;
        for (int i = 0; i < int(20.0f / DT); ++i) {
            const float next = d.update(s, gpu_ms_at(scale, 1.0f, 60.0f), DT, 1.0f);
            largest = std::max(largest, scale - next);
            scale = next;
        }
        CHECK(largest <= 2.0f * s.step + 1e-4f);
        CHECK(std::fabs(scale - s.min_scale) < 1e-4f);
    }
    {
        // Off, or with no GPU time to steer by: the ceiling.
        gfx::DynamicResolution d;
        gfx::DynamicResolutionSettings off = s;
        off.enabled = false;
        CHECK(d.update(off, 40.0f, DT, 0.85f) == 0.85f);
        CHECK(d.update(s, 0.0f, DT, 1.0f) == 1.0f);
    }
    std::printf("dynamic resolution: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
