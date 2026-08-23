#include "core/noise.h"

namespace core {
namespace {

// Quintic fade curve: zero first and second derivatives at the ends, so
// adjacent noise cells join without visible creases in the terrain normals.
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

}  // namespace

void Noise::reseed(uint32_t seed) {
    for (int i = 0; i < SIZE; ++i) permutation_[i] = uint8_t(i);

    // Fisher-Yates with a small xorshift, so we do not depend on the platform's
    // rand() and the same seed gives the same terrain everywhere.
    uint32_t state = seed ? seed : 1u;
    auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    for (int i = SIZE - 1; i > 0; --i) {
        uint32_t j = next() % uint32_t(i + 1);
        uint8_t tmp = permutation_[i];
        permutation_[i] = permutation_[j];
        permutation_[j] = tmp;
    }
    // Duplicated so lookups at ix+1 never need a bounds check.
    for (int i = 0; i < SIZE; ++i) permutation_[SIZE + i] = permutation_[i];
}

float Noise::gradient_dot(int ix, int iy, float dx, float dy) const {
    // One of 8 gradient directions, chosen by the hash. Using 8 rather than 4
    // avoids the axis-aligned artifacts that show up on large flat terrain.
    uint8_t hash = permutation_[uint8_t(permutation_[ix & MASK] + iy) & MASK];
    switch (hash & 7) {
        case 0: return dx + dy;
        case 1: return dx - dy;
        case 2: return -dx + dy;
        case 3: return -dx - dy;
        case 4: return dx;
        case 5: return -dx;
        case 6: return dy;
        default: return -dy;
    }
}

float Noise::perlin(float x, float y) const {
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const float fx = x - float(ix);
    const float fy = y - float(iy);

    const float g00 = gradient_dot(ix, iy, fx, fy);
    const float g10 = gradient_dot(ix + 1, iy, fx - 1.0f, fy);
    const float g01 = gradient_dot(ix, iy + 1, fx, fy - 1.0f);
    const float g11 = gradient_dot(ix + 1, iy + 1, fx - 1.0f, fy - 1.0f);

    const float u = fade(fx);
    const float v = fade(fy);
    // Scaled so the result sits in roughly [-1, 1].
    return lerpf(lerpf(g00, g10, u), lerpf(g01, g11, u), v) * 0.7071f;
}

float Noise::fbm(float x, float y, int octaves, float lacunarity, float gain) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total_amplitude = 0.0f;
    float frequency = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += perlin(x * frequency, y * frequency) * amplitude;
        total_amplitude += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return total_amplitude > 0.0f ? sum / total_amplitude : 0.0f;
}

float Noise::ridged(float x, float y, int octaves, float lacunarity, float gain) const {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total_amplitude = 0.0f;
    float frequency = 1.0f;
    for (int i = 0; i < octaves; ++i) {
        float n = 1.0f - std::fabs(perlin(x * frequency, y * frequency));
        // Squaring sharpens the crests; without it ridges read as soft folds.
        sum += n * n * amplitude;
        total_amplitude += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }
    return total_amplitude > 0.0f ? sum / total_amplitude : 0.0f;
}

}  // namespace core
