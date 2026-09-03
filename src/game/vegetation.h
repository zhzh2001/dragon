#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "core/noise.h"
#include "gfx/foliage.h"

namespace game {

class Terrain;

// One planted thing: where, how big, and the per-instance variation the shader
// reads. The renderer's own instance layout, so the vector uploads as-is.
using PlantInstance = gfx::FoliageInstance;

struct VegetationSettings {
    bool trees = true;
    bool grass = true;
    // Mean spacing between candidate tree sites, in metres. Density comes from
    // the forest-cover noise thinning these candidates, so this is the ceiling.
    float tree_spacing = 15.0f;
    // Trees stop this far above the valley floor. Real treelines are about
    // climate; ours is about the mountains reading as mountains -- a forested
    // peak is a hill.
    float treeline_above_floor = 330.0f;
    // 0..1: how much of the eligible ground is forest rather than clearing.
    float forest_cover = 0.5f;
    // Steepest ground a tree will stand on, as the surface normal's y.
    float tree_max_slope = 0.78f;
    // A plant on a slope sinks this many metres per unit of (1 - normal.y), so
    // its downhill skirt meets the ground instead of hanging in the air.
    float slope_sink = 6.0f;

    // Grass lives only near the camera: tufts this far out, thinned by distance.
    float grass_radius = 110.0f;
    float grass_spacing = 2.2f;
    float grass_above_floor = 450.0f;
    float grass_max_slope = 0.7f;
    uint32_t max_grass = 16000;

    float wind = 1.0f;
    uint32_t seed = 7;
};

// Decides where plants stand, and which kind. Pure placement -- it reads the
// terrain and writes instance lists; the renderer draws them and knows nothing
// about slopes.
//
// Trees are planted once per terrain (they are the landscape); grass is
// re-placed every frame around whatever the camera is, deterministically from
// the ground cell it sits in, so a tuft is always in the same place when you
// come back to it.
class Vegetation {
public:
    void plant(const Terrain& terrain, const VegetationSettings& settings);
    const std::vector<PlantInstance>& trees(gfx::TreeKind kind) const {
        return trees_[int(kind)];
    }
    size_t tree_count() const;

    // Fills `out[kind]` with the grass around `centre`. Cheap enough per frame
    // because terrain height queries read the mesh grid.
    void grass_around(const Terrain& terrain, const VegetationSettings& settings,
                      core::Vec3 centre, std::vector<PlantInstance> (&out)[gfx::GRASS_KINDS]) const;

private:
    std::vector<PlantInstance> trees_[gfx::TREE_KINDS];
    core::Noise forest_{7};
};

}  // namespace game
