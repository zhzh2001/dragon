#include "game/vegetation.h"

#include <cmath>

#include "core/log.h"
#include "game/terrain.h"

using core::Vec3;
using core::Vec4;
using gfx::GrassKind;
using gfx::TreeKind;

namespace game {

namespace {

// A stateless hash of a grid cell, so every query of the same cell agrees
// without storing anything. Decorrelated values per cell.
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

// How far a plant sinks so its downhill side meets the slope.
float sink_for(Vec3 normal, float slope_sink) {
    return (1.0f - core::saturate(normal.y)) * slope_sink;
}

}  // namespace

size_t Vegetation::tree_count() const {
    size_t total = 0;
    for (const auto& list : trees_) total += list.size();
    return total;
}

void Vegetation::plant(const Terrain& terrain, const VegetationSettings& settings) {
    for (auto& list : trees_) list.clear();
    forest_ = core::Noise(settings.seed * 31u + 11u);
    if (!settings.trees) return;

    const TerrainSettings& t = terrain.settings();
    const float extent = t.half_extent * core::maxf(settings.skirt_trees, 1.0f) - 30.0f;
    const float spacing = core::maxf(settings.tree_spacing, 3.0f);
    const float floor = t.valley_floor;
    const float treeline = floor + settings.treeline_above_floor;
    const int cells = int((2.0f * extent) / spacing);

    for (int iz = 0; iz < cells; ++iz) {
        for (int ix = 0; ix < cells; ++ix) {
            CellHash hash(ix, iz, settings.seed);
            const float x = -extent + (float(ix) + hash.next()) * spacing;
            const float z = -extent + (float(iz) + hash.next()) * spacing;

            // Two scales of cover: broad stands and clearings, and a finer
            // clumping inside them, so a stand is groves and gaps rather than
            // an even sprinkle. Both read from the air.
            const float stands = forest_.fbm(x / 420.0f, z / 420.0f, 3);
            const float clumps = forest_.fbm(x / 55.0f + 17.0f, z / 55.0f - 9.0f, 2);
            // Past the playable edge the cover thins to the skirt share, over
            // a band wide enough that the edge is not a ruler line.
            const float outside =
                core::saturate((core::maxf(std::fabs(x), std::fabs(z)) - t.half_extent) / 150.0f);
            const float density = core::saturate((stands + 0.55f) * 0.95f) *
                                  core::saturate(0.55f + clumps * 1.4f) * settings.forest_cover * 2.0f *
                                  core::lerpf(1.0f, core::saturate(settings.skirt_cover), outside);
            if (hash.next() > density) continue;

            const float height = terrain.height_at(x, z);
            if (height < t.water_level + 2.0f || height > treeline) continue;
            const Vec3 normal = terrain.normal_at(x, z);
            if (normal.y < settings.tree_max_slope) continue;
            // Thin out toward the treeline instead of stopping at a ruler line.
            const float to_line = (treeline - height) / 90.0f;
            if (hash.next() > core::saturate(to_line + 0.1f)) continue;

            // Which kind. Broadleaves on the warm low ground, spruce through the
            // middle, pines high and on the rougher ground, and a few dead snags
            // in the last stretch below the treeline where nothing is happy.
            // Mixed everywhere, with the mix shifting by altitude: the floor is
            // half broadleaf, the middle is spruce country, the top is pines.
            // A band that is all one kind reads as a plantation.
            const float above = height - floor;
            const float band = core::saturate(above / settings.treeline_above_floor);  // 0 floor, 1 line
            const float p_broadleaf = core::lerpf(0.5f, 0.0f, core::saturate(band * 2.2f));
            const float p_pine = core::lerpf(0.08f, 0.65f, core::saturate((band - 0.3f) * 1.8f));
            const float pick = hash.next();
            TreeKind kind = TreeKind::Spruce;
            if (pick < p_broadleaf) {
                kind = TreeKind::Broadleaf;
            } else if (pick < p_broadleaf + p_pine) {
                kind = TreeKind::Pine;
            }
            if (to_line < 0.9f && hash.next() < 0.35f * (1.0f - to_line)) kind = TreeKind::Dead;
            if (normal.y < 0.86f && hash.next() < 0.12f) kind = TreeKind::Dead;

            PlantInstance tree;
            // Size spread is wide on purpose: an even-aged plantation is the
            // most artificial forest there is.
            float scale = 0.7f + 1.0f * hash.next();
            if (kind == TreeKind::Dead) scale *= 0.85f;
            const float y = height - 0.3f - sink_for(normal, settings.slope_sink);
            tree.position_scale = Vec4{x, y, z, scale};
            // w is the warm push (toward the palette's plant_warm): a forest
            // reads as many trees when its colour varies tree by tree, and
            // broadleaf turns yellow far more than a spruce does.
            const float warm_range = kind == TreeKind::Broadleaf ? 1.0f
                                     : kind == TreeKind::Pine    ? 0.45f
                                                                 : 0.25f;
            tree.params = Vec4{core::TWO_PI * hash.next(), 0.7f + 0.6f * hash.next(),
                               core::TWO_PI * hash.next(), warm_range * hash.next()};
            trees_[int(kind)].push_back(tree);
        }
    }
    LOG_INFO("vegetation: %zu trees (%zu spruce, %zu pine, %zu broadleaf, %zu dead; spacing %.0f m, "
             "treeline %.0f m)",
             tree_count(), trees_[0].size(), trees_[1].size(), trees_[2].size(), trees_[3].size(),
             spacing, treeline);
}

void Vegetation::grass_around(const Terrain& terrain, const VegetationSettings& settings,
                              Vec3 centre,
                              std::vector<PlantInstance> (&out)[gfx::GRASS_KINDS]) const {
    for (auto& list : out) list.clear();
    if (!settings.grass) return;

    const TerrainSettings& t = terrain.settings();
    const float spacing = core::maxf(settings.grass_spacing, 0.5f);
    const float radius = settings.grass_radius;
    const float ceiling = t.valley_floor + settings.grass_above_floor;
    const int ix0 = int(std::floor((centre.x - radius) / spacing));
    const int ix1 = int(std::ceil((centre.x + radius) / spacing));
    const int iz0 = int(std::floor((centre.z - radius) / spacing));
    const int iz1 = int(std::ceil((centre.z + radius) / spacing));
    const float limit = t.half_extent * core::maxf(settings.skirt_trees, 1.0f) - 10.0f;
    size_t total = 0;

    for (int iz = iz0; iz <= iz1; ++iz) {
        for (int ix = ix0; ix <= ix1; ++ix) {
            if (total >= settings.max_grass) return;
            CellHash hash(ix, iz, settings.seed ^ 0x5bd1e995u);
            // Patchy, not a lawn: a fine noise leaves bare ground between the
            // patches.
            const float x = (float(ix) + hash.next()) * spacing;
            const float z = (float(iz) + hash.next()) * spacing;
            if (std::fabs(x) > limit || std::fabs(z) > limit) continue;
            const float dx = x - centre.x, dz = z - centre.z;
            if (dx * dx + dz * dz > radius * radius) continue;
            const float patch = forest_.fbm(x / 18.0f + 3.0f, z / 18.0f + 5.0f, 2);
            if (hash.next() > core::saturate(0.55f + patch * 1.6f)) continue;

            const float height = terrain.height_at(x, z);
            if (height < t.water_level + 0.5f || height > ceiling) continue;
            const Vec3 normal = terrain.normal_at(x, z);
            if (normal.y < settings.grass_max_slope) continue;

            // Reeds crowd the waterside, bushes are scattered, tufts are the rest.
            const float pick = hash.next();
            GrassKind kind = GrassKind::Tuft;
            const float near_water = core::saturate(1.0f - (height - t.water_level) / 14.0f);
            if (pick < near_water * 0.8f) {
                kind = GrassKind::Reed;
            } else if (pick > 0.93f) {
                kind = GrassKind::Bush;
            }

            PlantInstance plant;
            const float scale = kind == GrassKind::Bush ? 0.6f + 0.8f * hash.next()
                                                        : 0.75f + 0.7f * hash.next();
            plant.position_scale =
                Vec4{x, height - 0.05f - sink_for(normal, settings.slope_sink * 0.5f), z, scale};
            plant.params = Vec4{core::TWO_PI * hash.next(), 0.75f + 0.5f * hash.next(),
                                core::TWO_PI * hash.next(), 0.5f * hash.next()};
            out[int(kind)].push_back(plant);
            ++total;
        }
    }
}

}  // namespace game
