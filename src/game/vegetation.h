#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "core/noise.h"
#include "gfx/foliage.h"
#include "gfx/rock_piece.h"

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
    float treeline_above_floor = 480.0f;
    // 0..1: how much of the eligible ground is forest rather than clearing.
    float forest_cover = 0.5f;
    // 0..1: how far the cover gathers into closed stands with open meadow
    // between, rather than trees sprinkled everywhere at varying density.
    // The modern tier's (App::vegetation_for_tier); 0 is the retro tiers'
    // forest, unchanged.
    float stand_contrast = 0.0f;
    // Steepest ground a tree will stand on, as the surface normal's y.
    float tree_max_slope = 0.68f;
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
    // The sward (gfx::GrassKind::Sward): patches of short blades on their
    // own grid, edge to edge, this far from the camera. The modern tier's.
    bool sward = true;
    float sward_radius = 46.0f;
    float sward_spacing = 1.7f;
    uint32_t max_sward = 6000;
    // Rocks (DIRECTION.md row 7): one candidate per `rock_spacing` cell,
    // kept by slope -- outcrops and crags on the steep faces past the rock
    // threshold, scree and boulders on the mid slopes, a sprinkle of boulders
    // on the floor. What turns clay mountains into mountains.
    bool rocks = true;
    float rock_spacing = 48.0f;
    float rock_slope_cover = 0.6f;   // keep chance on the steepest faces
    float rock_floor_cover = 0.035f; // on the flat floor
    float rock_extent = 1.15f;       // x the playable half extent

    // The scanned kit (gfx/rock_piece.h), the modern tier's rocks when the
    // files load (App::vegetation_for_tier); the dials above are then the
    // retro tiers' alone. Boulders and slabs lie in loose groups -- a large
    // one and a few small ones beside it -- one group per candidate cell;
    // cliff pieces stand on the steep faces, from a grid of their own.
    bool rock_kit = false;
    float rock_group_spacing = 72.0f;  // metres between group candidates
    float rock_group_slope_cover = 0.55f;  // keep chance on the steepest ground
    float rock_group_floor_cover = 0.05f;  // and on the flat floor
    float rock_group_size = 1.4f;      // the mean count of small ones beside the large one
    float rock_group_spread = 1.6f;    // how far out they lie, in the large one's radii
    float rock_large_scale = 1.6f;     // the large one's scale, middle of its range
    float rock_sink_min = 0.15f;       // a boulder's sink, as a share of its height
    float rock_sink_max = 0.30f;
    float rock_tilt = 0.75f;           // how far a boulder leans to the slope, 0 upright, 1 flush
    // Cliff pieces: the steepness they need, as 1 - the normal's y averaged
    // over their footprint, their grid, their keep chance, and how far they
    // sink past the file's own 2 m rim burial.
    float cliff_slope = 0.42f;
    float cliff_spacing = 70.0f;
    float cliff_cover = 0.8f;
    float cliff_bury = 0.3f;
    // How far a cliff's rim may stand over the ground (m, vertically) where
    // it is not the foot; the foot is always buried.
    float cliff_rim_tolerance = 2.0f;
    float cliff_scale = 1.7f;          // the middle of their scale range

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

    // The shade under the crowns that comes from the sky, not the sun: each
    // tree a soft blot of `crown_radius[kind]` metres (times its scale) on
    // an `n` x `n` grid over +-`half` metres, 0 open to 255 fully under.
    // In a mountain's shadow no tree casts a shadow, and without this a tree
    // there stood on evenly lit ground and floated (playtest report 0011).
    // The terrain shader reads the grid as a texture; the grass reads it
    // here, at its own root.
    void build_crown_shade(float half, int n, const float crown_radius[gfx::TREE_KINDS]);
    float crown_shade_at(float x, float z) const;  // 0..1, bilinear
    const std::vector<uint8_t>& crown_shade() const { return crown_shade_; }
    int crown_shade_size() const { return crown_shade_n_; }
    float crown_shade_half() const { return crown_shade_half_; }
    // The kit's pieces where rock_kit is on (empty otherwise).
    const std::vector<gfx::RockInstance>& kit_rocks(int piece) const { return kit_rocks_[piece]; }
    // Legacy rocks and kit pieces together.
    size_t rock_count() const;
    // How far the kit's placement had to sink pieces past their planned
    // sink so no check point floats (gfx::RockPieceInfo::check), the worst
    // one, metres: the log's, and the test's.
    float kit_extra_sink() const { return kit_extra_sink_; }
    size_t tree_count() const;

    // Fills `out[kind]` with the grass around `centre`. Cheap enough per frame
    // because terrain height queries read the mesh grid.
    void grass_around(const Terrain& terrain, const VegetationSettings& settings,
                      core::Vec3 centre, std::vector<PlantInstance> (&out)[gfx::GRASS_KINDS]) const;

private:
    std::vector<PlantInstance> trees_[gfx::TREE_KINDS];
    std::vector<uint8_t> crown_shade_;
    int crown_shade_n_ = 0;
    float crown_shade_half_ = 0.0f;
    std::vector<PlantInstance> rocks_[gfx::ROCK_KINDS];
    std::vector<gfx::RockInstance> kit_rocks_[gfx::ROCK_PIECES];
    float kit_extra_sink_ = 0.0f;
    void place_rocks(const Terrain& terrain, const VegetationSettings& settings);
    void place_kit(const Terrain& terrain, const VegetationSettings& settings);
    core::Noise forest_{7};
};

}  // namespace game
