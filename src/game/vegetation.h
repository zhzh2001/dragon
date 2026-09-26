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
    // Trees continue past the playable edge onto the skirt, out to this
    // multiple of the half extent and at this share of the cover. The spawn
    // sits at 90% of the extent, so without this the ground behind the start
    // was bare to the horizon.
    float skirt_trees = 1.6f;
    float skirt_cover = 0.6f;

    // Grass lives only near the camera: tufts this far out, thinned by distance.
    float grass_radius = 110.0f;
    float grass_spacing = 2.2f;
    float grass_above_floor = 450.0f;
    float grass_max_slope = 0.7f;
    uint32_t max_grass = 16000;
    // Rocks (DIRECTION.md row 7): one candidate per `rock_spacing` cell,
    // kept by slope -- outcrops and crags on the steep faces past the rock
    // threshold, scree and boulders on the mid slopes, a sprinkle of boulders
    // on the floor. What turns clay mountains into mountains.
    bool rocks = true;
    float rock_spacing = 42.0f;
    float rock_slope_cover = 0.6f;   // keep chance on the steepest faces
    float rock_floor_cover = 0.035f; // on the flat floor
    float rock_extent = 1.15f;       // x the playable half extent

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
    const std::vector<PlantInstance>& rocks(int kind) const { return rocks_[kind]; }
    size_t rock_count() const;
    size_t tree_count() const;

    // Fills `out[kind]` with the grass around `centre`. Cheap enough per frame
    // because terrain height queries read the mesh grid.
    void grass_around(const Terrain& terrain, const VegetationSettings& settings,
                      core::Vec3 centre, std::vector<PlantInstance> (&out)[gfx::GRASS_KINDS]) const;

private:
    std::vector<PlantInstance> trees_[gfx::TREE_KINDS];
    std::vector<PlantInstance> rocks_[gfx::ROCK_KINDS];
    void place_rocks(const Terrain& terrain, const VegetationSettings& settings);
    core::Noise forest_{7};
};

}  // namespace game
