// The baked noise lattice (gfx/noise_lattice.h): the retro tiers' value noise,
// read from a texture, must be the modern tiers' hashed value noise to 8 bits
// inside the window, and must repeat without a seam outside it.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "gfx/noise_lattice.h"

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

constexpr int N = int(gfx::NOISE_LATTICE_SIZE);

float smooth(float f) { return f * f * (3.0f - 2.0f * f); }
float mix(float a, float b, float t) { return a + (b - a) * t; }

// scene_common.hlsl's value_noise, modern: four hashes.
float hashed_noise(float x, float y) {
    const float cx = std::floor(x), cy = std::floor(y);
    const float fx = smooth(x - cx), fy = smooth(y - cy);
    const float a = gfx::noise_hash21(cx, cy), b = gfx::noise_hash21(cx + 1, cy);
    const float c = gfx::noise_hash21(cx, cy + 1), d = gfx::noise_hash21(cx + 1, cy + 1);
    return mix(mix(a, b, fx), mix(c, d, fx), fy);
}

// The same, BAKED_NOISE: one point sample of the lattice, repeat addressing.
float baked_noise(const std::vector<uint8_t>& lattice, float x, float y) {
    const float cx = std::floor(x), cy = std::floor(y);
    const float fx = smooth(x - cx), fy = smooth(y - cy);
    const int i = ((int(cx) % N) + N) % N, j = ((int(cy) % N) + N) % N;
    const uint8_t* k = &lattice[(size_t(j) * N + size_t(i)) * 4];
    return mix(mix(k[0] / 255.0f, k[1] / 255.0f, fx), mix(k[2] / 255.0f, k[3] / 255.0f, fx), fy);
}

}  // namespace

int main() {
    const std::vector<uint8_t> lattice = gfx::bake_noise_lattice(gfx::NOISE_LATTICE_SIZE);
    CHECK(lattice.size() == size_t(N) * N * 4);

    // Seamless: every texel's right, upper and diagonal corners are its
    // neighbours' own corner, across the wrap too.
    int seams = 0;
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            const uint8_t* t = &lattice[(size_t(j) * N + i) * 4];
            const uint8_t* right = &lattice[(size_t(j) * N + (i + 1) % N) * 4];
            const uint8_t* up = &lattice[(size_t((j + 1) % N) * N + i) * 4];
            if (t[1] != right[0] || t[3] != right[2] || t[2] != up[0] || t[3] != up[1]) ++seams;
        }
    }
    CHECK(seams == 0);

    // The same noise inside the window: within the 8-bit quantisation.
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> inside(-N / 2.0f, N / 2.0f - 1.0f);
    float worst = 0.0f;
    double sum = 0.0, sum_sq = 0.0;
    const int samples = 200000;
    for (int s = 0; s < samples; ++s) {
        const float x = inside(rng), y = inside(rng);
        const float h = hashed_noise(x, y);
        worst = std::fmax(worst, std::fabs(h - baked_noise(lattice, x, y)));
        sum += h;
        sum_sq += double(h) * h;
    }
    std::printf("  window: worst |hashed - baked| = %.5f (half a step is %.5f)\n", worst, 0.5f / 255.0f);
    CHECK(worst <= 0.5f / 255.0f + 1e-5f);

    // And it is noise: a mean near a half and a spread like a uniform's.
    const double mean = sum / samples, sd = std::sqrt(sum_sq / samples - mean * mean);
    std::printf("  mean %.3f, sd %.3f\n", mean, sd);
    CHECK(std::fabs(mean - 0.5) < 0.02);
    CHECK(sd > 0.15 && sd < 0.3);

    // Outside the window it repeats with the lattice's period.
    CHECK(baked_noise(lattice, 900.3f, -1200.7f) == baked_noise(lattice, 900.3f - N * 2, -1200.7f + N * 2));

    std::printf("noise_lattice: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
