#include "game/vegetation.h"

#include <cmath>

#include "core/log.h"
#include "game/terrain.h"

using core::Vec3;
using core::Vec4;

namespace game {

namespace {

// A stateless hash of a grid cell, so every query of the same cell agrees
// without storing anything. Four decorrelated values per cell.
struct CellHash {
    uint32_t state;
    CellHash(int ix, int iz, uint32_t seed) {
        uint32_t h = uint32_t(ix) * 0x8da6b343u ^ uint32_t(iz) * 0xd8163841u ^ seed * 0xcb1ab31fu;
        h ^= h >> 15;
        h *= 0x2c1b3c6du;
        h ^= h >> 12;
        h *= 0x297a2d39u;
        h ^= h >> 15;
        state = h | 1u;
    }
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return float(state & 0xffffffu) / float(0xffffff);
    }
};

}  // namespace

void Vegetation::plant(const Terrain& terrain, const VegetationSettings& settings) {
    trees_.clear();
    forest_ = core::Noise(settings.seed * 31u + 11u);
    if (!settings.trees) return;

    const TerrainSettings& t = terrain.settings();
    const float extent = t.half_extent - 30.0f;
    const float spacing = core::maxf(settings.tree_spacing, 3.0f);
    const float treeline = t.valley_floor + settings.treeline_above_floor;
    const int cells = int((2.0f * extent) / spacing);
    trees_.reserve(size_t(cells) * size_t(cells) / 3);

    for (int iz = 0; iz < cells; ++iz) {
        for (int ix = 0; ix < cells; ++ix) {
            CellHash hash(ix, iz, settings.seed);
            const float x = -extent + (float(ix) + hash.next()) * spacing;
            const float z = -extent + (float(iz) + hash.next()) * spacing;

            // Forest cover: broad noise makes stands and clearings rather than
            // an even sprinkle, which is what a forest looks like from the air.
            const float cover = forest_.fbm(x / 380.0f, z / 380.0f, 3);
            const float density = core::saturate((cover + 0.6f) * 0.9f) * settings.forest_cover * 1.6f;
            if (hash.next() > density) continue;

            const float height = terrain.height_at(x, z);
            if (height < t.water_level + 2.0f || height > treeline) continue;
            // Thin out toward the treeline instead of stopping at a ruler line.
            const float to_line = (treeline - height) / 80.0f;
            if (hash.next() > core::saturate(to_line)) continue;
            if (terrain.normal_at(x, z).y < settings.tree_max_slope) continue;

            PlantInstance tree;
            const float scale = 1.0f + 0.6f * hash.next();
            tree.position_scale = Vec4{x, height - 0.3f, z, scale};  // roots in the ground
            tree.params = Vec4{core::TWO_PI * hash.next(), 0.75f + 0.5f * hash.next(),
                               core::TWO_PI * hash.next(), 0.0f};
            trees_.push_back(tree);
        }
    }
    LOG_INFO("vegetation: %zu trees (spacing %.0f m, treeline %.0f m)", trees_.size(), spacing,
             treeline);
}

void Vegetation::grass_around(const Terrain& terrain, const VegetationSettings& settings,
                              Vec3 centre, std::vector<PlantInstance>& out) const {
    out.clear();
    if (!settings.grass) return;

    const TerrainSettings& t = terrain.settings();
    const float spacing = core::maxf(settings.grass_spacing, 0.5f);
    const float radius = settings.grass_radius;
    const float ceiling = t.valley_floor + settings.grass_above_floor;
    const int ix0 = int(std::floor((centre.x - radius) / spacing));
    const int ix1 = int(std::ceil((centre.x + radius) / spacing));
    const int iz0 = int(std::floor((centre.z - radius) / spacing));
    const int iz1 = int(std::ceil((centre.z + radius) / spacing));
    const float limit = t.half_extent - 10.0f;

    for (int iz = iz0; iz <= iz1; ++iz) {
        for (int ix = ix0; ix <= ix1; ++ix) {
            if (out.size() >= settings.max_grass) return;
            CellHash hash(ix, iz, settings.seed ^ 0x5bd1e995u);
            if (hash.next() > 0.8f) continue;
            const float x = (float(ix) + hash.next()) * spacing;
            const float z = (float(iz) + hash.next()) * spacing;
            if (std::fabs(x) > limit || std::fabs(z) > limit) continue;
            const float dx = x - centre.x, dz = z - centre.z;
            if (dx * dx + dz * dz > radius * radius) continue;

            const float height = terrain.height_at(x, z);
            if (height < t.water_level + 1.0f || height > ceiling) continue;
            if (terrain.normal_at(x, z).y < settings.grass_max_slope) continue;

            PlantInstance tuft;
            tuft.position_scale = Vec4{x, height, z, 0.8f + 0.6f * hash.next()};
            tuft.params = Vec4{core::TWO_PI * hash.next(), 0.8f + 0.4f * hash.next(),
                               core::TWO_PI * hash.next(), 0.0f};
            out.push_back(tuft);
        }
    }
}

}  // namespace game
