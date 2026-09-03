#pragma once

#include <cstdint>

#include "core/math.h"
#include "core/noise.h"
#include "gfx/mesh.h"

namespace game {

// Shape controls for the valley. All live-editable; regenerating is fast enough
// to drag a slider and watch the landscape change.
struct TerrainSettings {
    uint32_t seed = 1337;

    // 5 km across. At the dragon's cruise speed the previous 2 km valley took
    // well under a minute to cross end to end, and the floor was only about 460 m
    // wide -- roughly ten seconds at cruise, which read as a corridor rather
    // than a landscape.
    float half_extent = 2500.0f;  // metres from centre to edge
    // 6 m cells over the larger extent cost about the same triangle budget as
    // 4 m cells did over the old one.
    float cell_size = 6.0f;       // metres per quad edge

    // Mountain walls: ridged noise, which gives sharp crests rather than dunes.
    float mountain_height = 900.0f;
    // Scaled with the map, or the same noise frequency would turn the walls
    // into a field of small bumps instead of a mountain range.
    float mountain_scale = 2300.0f;  // metres per noise unit; larger = broader
    int mountain_octaves = 6;

    // Rolling detail laid over everything.
    float hill_height = 34.0f;
    float hill_scale = 210.0f;
    int hill_octaves = 4;

    // The valley itself: a meandering corridor of low ground. This is what
    // makes the terrain flyable rather than just bumpy.
    // Floor sits well above the water line so the valley reads as green
    // lowland rather than beach, leaving the shoreline material for the river.
    float valley_floor = 46.0f;
    float valley_width = 420.0f;     // half-width of flat floor
    float valley_falloff = 950.0f;   // distance over which walls rise
    float valley_meander = 820.0f;   // how far the corridor wanders in X
    float valley_period = 3300.0f;   // metres per meander cycle

    float water_level = 34.0f;
    // The river: a channel carved along the corridor, wandering off the
    // corridor's own centre line so the two do not read as one curve. Half
    // width at the water line, bed depth below the water, and how far it
    // wanders. The floor sits above the water everywhere else, so this is the
    // only water in the valley.
    float river_half_width = 26.0f;
    float river_depth = 7.0f;
    float river_wander = 110.0f;

    // Beyond the playable extent the same function continues as a coarse
    // "skirt" mesh out to this multiple of the half extent, so there is
    // ground under the sky all the way to the fog. Without it the analytic
    // height carried on past the last visible triangle: an invisible mountain
    // range you could land on.
    float skirt_extent_factor = 3.0f;
    float skirt_cell_size = 36.0f;
};

// Procedural valley. Height is defined by an analytic function of (x, z), so
// gameplay queries -- ground clearance, landing, camera collision -- sample the
// same function the mesh was built from and never need the triangles.
class Terrain {
public:
    void generate(const TerrainSettings& settings);

    // Height of the RENDERED surface: once the mesh exists, this interpolates
    // the exact triangle under (x, z), so nothing the player can touch differs
    // from what they can see. Before generation, and outside the mesh, it is
    // the analytic function the mesh was built from.
    float height_at(float x, float z) const;
    // The generating function itself, for building the mesh and for tests.
    float analytic_height_at(float x, float z) const;
    core::Vec3 normal_at(float x, float z) const;

    // The surface you can stand on or splash into: the ground, or the water
    // where the ground is below the water line.
    float surface_at(float x, float z) const {
        return core::maxf(height_at(x, z), settings_.water_level);
    }
    // Signed height above the surface. Negative means under it.
    float clearance_at(core::Vec3 position) const {
        return position.y - surface_at(position.x, position.z);
    }
    // Centre line of the river at a given Z.
    float river_center_x(float z) const;

    // Centre of the valley corridor at a given Z. Useful for placing courses,
    // spawns, and the initial camera.
    float valley_center_x(float z) const;

    const gfx::MeshData& mesh_data() const { return mesh_; }
    const gfx::MeshData& skirt_mesh_data() const { return skirt_; }
    bool has_mesh() const { return grid_ready_; }
    const TerrainSettings& settings() const { return settings_; }

    // Highest point found while building the mesh -- for framing the camera and
    // sizing the sky.
    float max_height() const { return max_height_; }
    float min_height() const { return min_height_; }

private:
    // Blend from 0 on the valley floor to 1 out in the mountains.
    float valley_mask(float x, float z) const;
    void build_mesh();

    TerrainSettings settings_;
    core::Noise noise_;
    gfx::MeshData mesh_;
    gfx::MeshData skirt_;
    bool grid_ready_ = false;
    int verts_per_side_ = 0;
    int skirt_verts_per_side_ = 0;
    void build_skirt();
    // Triangle-exact height on a square grid of `n` vertices per side spanning
    // [-half, half] at `cell` metres. Returns false outside it.
    static bool grid_height(const gfx::MeshData& mesh, int n, float half, float cell, float x,
                            float z, float& out);
    float max_height_ = 0.0f;
    float min_height_ = 0.0f;
};

}  // namespace game
