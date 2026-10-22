#include "game/thermals.h"

#include <algorithm>
#include <cmath>

#include "game/terrain.h"

namespace game {

namespace {

// One step of a 32-bit mix: the placement's only source of randomness, one
// draw per statement (CLAUDE.md), so every compiler places the same columns.
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

}  // namespace

void Thermals::place(const Terrain& terrain, uint32_t seed, core::Vec3 to_sun, const ThermalSettings& s) {
    columns_.clear();
    const core::Vec3 sun = core::normalize_or(to_sun, core::Vec3::up());
    const float extent = terrain.settings().half_extent * 0.9f;
    const float water = terrain.settings().water_level;

    // Candidates on a jittered grid, scored by how squarely the ground faces
    // the sun, how gentle and how low it is: warm air rises off sunlit
    // meadows and sun-facing slopes, not off water, shade or cliffs.
    struct Candidate {
        core::Vec3 at;
        float score;
    };
    std::vector<Candidate> candidates;
    uint32_t state = mix(seed * 2654435761u + 77u);
    const int side = 24;
    const float cell = extent * 2.0f / float(side);
    for (int iz = 0; iz < side; ++iz) {
        for (int ix = 0; ix < side; ++ix) {
            const float jx = unit(state);
            const float jz = unit(state);
            const float x = -extent + (float(ix) + jx) * cell;
            const float z = -extent + (float(iz) + jz) * cell;
            const float ground = terrain.height_at(x, z);
            if (ground < water + 2.0f) continue;
            const core::Vec3 normal = terrain.normal_at(x, z);
            const float facing = core::dot(normal, sun);
            if (facing < s.min_sun || normal.y < 0.55f) continue;
            const float noise = unit(state);
            // Low ground first: the meadows and lower slopes along the
            // corridor are where a dragon flies; a column on a far peak was
            // never met.
            const float low = 1.0f - core::saturate((ground - terrain.settings().valley_floor) / 450.0f);
            candidates.push_back({core::Vec3{x, ground, z}, facing + 0.3f * normal.y + 1.2f * low + 0.25f * noise});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.at.x != b.at.x ? a.at.x < b.at.x : a.at.z < b.at.z;  // a total order, for every compiler
    });
    for (const Candidate& c : candidates) {
        if (int(columns_.size()) >= s.count) break;
        bool crowded = false;
        for (const Thermal& t : columns_) {
            const float dx = t.base.x - c.at.x, dz = t.base.z - c.at.z;
            crowded = crowded || dx * dx + dz * dz < s.min_spacing * s.min_spacing;
        }
        if (crowded) continue;
        Thermal t;
        t.base = c.at;
        const float size = unit(state);
        t.radius = s.radius * (0.8f + 0.4f * size);
        const float power = unit(state);
        t.strength = s.strength * (0.75f + 0.5f * power);
        t.top = core::maxf(s.top, c.at.y + 400.0f);
        t.phase = unit(state);
        columns_.push_back(t);
    }
}

float Thermals::lift_at(core::Vec3 p, float ground_height, float t) const {
    float lift = 0.0f;
    for (const Thermal& c : columns_) {
        const float dx = p.x - c.base.x, dz = p.z - c.base.z;
        const float r2 = (dx * dx + dz * dz) / (c.radius * c.radius);
        if (r2 > 9.0f) continue;
        const float above = p.y - ground_height;
        const float rise = core::smoothstep(0.0f, 40.0f, above) * (1.0f - core::smoothstep(c.top - 250.0f, c.top, p.y));
        // Breathing: a column strengthens and weakens over about a minute.
        const float breath = 0.8f + 0.2f * std::sin((t / 60.0f + c.phase) * core::TWO_PI);
        lift += c.strength * breath * std::exp(-r2) * rise;
    }
    return lift;
}

}  // namespace game
