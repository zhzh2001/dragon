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

    float half_extent = 1024.0f;  // metres from centre to edge
    float cell_size = 4.0f;       // metres per quad edge

    // Mountain walls: ridged noise, which gives sharp crests rather than dunes.
    float mountain_height = 680.0f;
    float mountain_scale = 1350.0f;  // metres per noise unit; larger = broader
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
    float valley_width = 230.0f;     // half-width of flat floor
    float valley_falloff = 520.0f;   // distance over which walls rise
    float valley_meander = 430.0f;   // how far the corridor wanders in X
    float valley_period = 1700.0f;   // metres per meander cycle

    float water_level = 34.0f;
};

// Procedural valley. Height is defined by an analytic function of (x, z), so
// gameplay queries -- ground clearance, landing, camera collision -- sample the
// same function the mesh was built from and never need the triangles.
class Terrain {
public:
    void generate(const TerrainSettings& settings);

    float height_at(float x, float z) const;
    core::Vec3 normal_at(float x, float z) const;

    // Signed height above the ground. Negative means underground.
    float clearance_at(core::Vec3 position) const {
        return position.y - height_at(position.x, position.z);
    }

    // Centre of the valley corridor at a given Z. Useful for placing courses,
    // spawns, and the initial camera.
    float valley_center_x(float z) const;

    const gfx::MeshData& mesh_data() const { return mesh_; }
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
    float max_height_ = 0.0f;
    float min_height_ = 0.0f;
};

}  // namespace game
