#pragma once

#include <cstdint>

#include "core/math.h"

namespace core {

// Deterministic gradient noise for terrain. Seeded from an integer so a given
// seed always produces the same valley -- reproducibility matters when you are
// tuning a course or chasing a bug at a specific spot on the map.
class Noise {
public:
    explicit Noise(uint32_t seed = 1337u) { reseed(seed); }
    void reseed(uint32_t seed);

    // Perlin gradient noise, roughly in [-1, 1].
    float perlin(float x, float y) const;

    // Fractal sum of `octaves` Perlin layers, each at double the frequency and
    // `gain` times the amplitude. Normalized to roughly [-1, 1].
    float fbm(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;

    // Ridged multifractal: 1 - |noise| per octave, which turns the smooth peaks
    // of fbm into sharp crests. This is what makes mountains look like
    // mountains rather than dunes. Output is roughly [0, 1].
    float ridged(float x, float y, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;

private:
    float gradient_dot(int ix, int iy, float dx, float dy) const;

    // Power-of-two table so wrapping is a mask rather than a modulo.
    static constexpr int SIZE = 256;
    static constexpr int MASK = SIZE - 1;
    uint8_t permutation_[SIZE * 2] = {};
};

}  // namespace core
