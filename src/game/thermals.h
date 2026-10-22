#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"

namespace game {

class Terrain;

// Rising air (docs/GAPS.md, G7): columns of warm air over sunlit ground, which
// a dragon can circle in to climb without flapping -- lift as a resource the
// valley offers, after Century's updrafts and the paraglider's thermals.
// Placed once per valley from its seed, over gentle dry ground that faces the
// sun, so they stand where a pilot would look for them.
struct ThermalSettings {
    int count = 9;              // columns per valley
    float min_spacing = 550.0f; // metres between two columns
    float radius = 85.0f;       // metres from the axis to where the lift is 1/e of its peak
    float strength = 6.5f;      // m/s up at the axis, mid-column
    float top = 1350.0f;        // metres above sea level where the column dies out (under the cloud base)
    float min_sun = 0.35f;      // the ground must face the sun at least this much (normal . to-sun)
};

struct Thermal {
    core::Vec3 base;  // on the ground
    float radius = 85.0f;
    float strength = 6.5f;
    float top = 1350.0f;
    float phase = 0.0f;  // 0..1: its own breathing, so no two pulse together
};

class Thermals {
public:
    // Deterministic for a terrain, a seed and a sun: the same valley has the
    // same thermals on every machine.
    void place(const Terrain& terrain, uint32_t seed, core::Vec3 to_sun, const ThermalSettings& settings);
    void clear() { columns_.clear(); }

    // Vertical air speed at a point (m/s, up positive) at time `t` (s): a
    // Gaussian across the column, faded in over the first 40 m above the
    // ground and out over the last 250 m below its top, breathing slowly.
    float lift_at(core::Vec3 position, float ground_height, float t) const;

    const std::vector<Thermal>& columns() const { return columns_; }

private:
    std::vector<Thermal> columns_;
};

}  // namespace game
