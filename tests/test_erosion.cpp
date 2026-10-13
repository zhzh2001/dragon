// Terrain erosion (game/erosion.h): deterministic, never touches a base
// level, and cuts a mountain into the valleys its drainage carves.

#include <cmath>
#include <cstdio>
#include <vector>

#include "game/erosion.h"

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

constexpr int N = 129;
constexpr float CELL = 12.0f;

// A cone with a little ridged texture on it, standing in a flat floor that
// is the base level.
void make_mountain(std::vector<float>& h, std::vector<uint8_t>& fixed) {
    h.assign(size_t(N) * size_t(N), 0.0f);
    fixed.assign(h.size(), 0);
    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            const float dx = float(x - N / 2), dz = float(z - N / 2);
            const float r = std::sqrt(dx * dx + dz * dz);
            const float cone = std::fmax(0.0f, 600.0f - r * 12.0f);
            const float texture = 6.0f * std::sin(float(x) * 0.7f) * std::cos(float(z) * 0.53f);
            const size_t i = size_t(z) * size_t(N) + size_t(x);
            h[i] = 40.0f + cone + (cone > 0.0f ? texture : 0.0f);
            fixed[i] = cone <= 0.0f ? 1 : 0;
        }
    }
}

}  // namespace

int main() {
    std::vector<float> h;
    std::vector<uint8_t> fixed;
    make_mountain(h, fixed);
    game::ErosionSettings s;

    const game::ErosionResult a = game::erode(h, fixed, N, CELL, s);
    const game::ErosionResult b = game::erode(h, fixed, N, CELL, s);
    CHECK(a.heights == b.heights);
    CHECK(a.flow == b.flow);
    CHECK(a.deposit == b.deposit);

    // Base levels are never changed.
    bool floor_kept = true;
    for (size_t i = 0; i < h.size(); ++i) {
        if (fixed[i] && a.heights[i] != h[i]) floor_kept = false;
    }
    CHECK(floor_kept);

    // It cut: the mountain lost material, some cells by tens of metres, and
    // the drainage gathered into channels far wider than one cell.
    double before = 0.0, after = 0.0;
    float deepest = 0.0f, widest = 0.0f;
    for (size_t i = 0; i < h.size(); ++i) {
        before += h[i];
        after += a.heights[i];
        deepest = std::fmin(deepest, a.heights[i] - h[i]);
        widest = std::fmax(widest, a.flow[i]);
    }
    std::printf("erosion: removed %.0f m3 a cell on average, deepest cut %.1f m, widest catchment %.0f cells\n",
                (before - after) / double(h.size()) * double(CELL) * double(CELL), double(deepest), double(widest));
    CHECK(after < before);
    CHECK(deepest < -10.0f);
    CHECK(widest > 200.0f);
    // Nothing exploded: every height finite and inside the original range.
    bool sane = true;
    for (float v : a.heights) {
        if (!std::isfinite(v) || v < -1.0f || v > 700.0f) sane = false;
    }
    CHECK(sane);

    // Turned off, it is the identity.
    game::ErosionSettings off = s;
    off.iterations = 0;
    CHECK(game::erode(h, fixed, N, CELL, off).heights == h);

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
