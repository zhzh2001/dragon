// The terrain the player touches is the terrain the player sees, and the
// plants stand where plants can stand.
#include <cmath>
#include <cstdio>

#include "core/math.h"
#include "game/terrain.h"
#include "game/vegetation.h"

namespace {

int failures = 0;
int checks = 0;
void check(bool condition, const char* what, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("  FAIL (line %d): %s\n", line, what);
    }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

game::TerrainSettings small_terrain() {
    game::TerrainSettings settings;
    settings.half_extent = 700.0f;  // enough valley to plant in, quick to build
    settings.cell_size = 6.0f;
    return settings;
}

void test_surface_query_matches_mesh() {
    std::printf("height queries read the rendered triangles\n");
    game::Terrain terrain;
    terrain.generate(small_terrain());
    CHECK(terrain.has_mesh());
    const auto& mesh = terrain.mesh_data();

    // Every vertex is reproduced exactly.
    float worst_vertex = 0.0f;
    for (size_t i = 0; i < mesh.vertices.size(); i += 37) {
        const core::Vec3 p = mesh.vertices[i].position;
        worst_vertex = std::fmax(worst_vertex, std::fabs(terrain.height_at(p.x, p.z) - p.y));
    }
    CHECK(worst_vertex < 1e-3f);

    // Inside a cell the query lies on the triangle plane: the midpoint of any
    // edge is the mean of its ends, whichever way the diagonal runs.
    const float cell = terrain.settings().cell_size;
    const float extent = terrain.settings().half_extent;
    float worst_edge = 0.0f;
    for (int k = 0; k < 200; ++k) {
        const int ix = 3 + (k * 37) % 200;
        const int iz = 5 + (k * 53) % 200;
        const float x0 = -extent + float(ix) * cell, z0 = -extent + float(iz) * cell;
        const float h00 = terrain.height_at(x0, z0);
        const float h10 = terrain.height_at(x0 + cell, z0);
        const float h01 = terrain.height_at(x0, z0 + cell);
        const float mid_x = terrain.height_at(x0 + cell * 0.5f, z0);
        const float mid_z = terrain.height_at(x0, z0 + cell * 0.5f);
        worst_edge = std::fmax(worst_edge, std::fabs(mid_x - 0.5f * (h00 + h10)));
        worst_edge = std::fmax(worst_edge, std::fabs(mid_z - 0.5f * (h00 + h01)));
    }
    CHECK(worst_edge < 1e-3f);

    // The analytic function is still there underneath and differs by a little
    // (that difference is the invisible bump the surface query removes).
    float worst_gap = 0.0f;
    for (int k = 0; k < 2000; ++k) {
        const float x = -extent + 40.0f + float((k * 131) % 1200);
        const float z = -extent + 40.0f + float((k * 71) % 1200);
        worst_gap = std::fmax(worst_gap, std::fabs(terrain.height_at(x, z) -
                                                    terrain.analytic_height_at(x, z)));
    }
    CHECK(worst_gap > 0.01f);
    CHECK(worst_gap < 6.0f);
    std::printf("  analytic-vs-surface gap up to %.2f m over %d samples\n", worst_gap, 2000);
}

void test_trees_stand_where_trees_can() {
    std::printf("trees stand on gentle ground below the treeline and above the water\n");
    game::Terrain terrain;
    terrain.generate(small_terrain());
    game::VegetationSettings settings;
    game::Vegetation vegetation;
    vegetation.plant(terrain, settings);
    const auto& trees = vegetation.trees();
    CHECK(trees.size() > 200);

    const float treeline = terrain.settings().valley_floor + settings.treeline_above_floor;
    bool all_rooted = true, all_below_line = true, all_dry = true, all_gentle = true;
    for (const auto& tree : trees) {
        const float x = tree.position_scale.x, z = tree.position_scale.z;
        const float ground = terrain.height_at(x, z);
        if (std::fabs(tree.position_scale.y + 0.3f - ground) > 0.05f) all_rooted = false;
        if (ground > treeline) all_below_line = false;
        if (ground < terrain.settings().water_level + 2.0f) all_dry = false;
        if (terrain.normal_at(x, z).y < settings.tree_max_slope) all_gentle = false;
        if (tree.position_scale.w < 1.0f || tree.position_scale.w > 1.6f) all_rooted = false;
    }
    CHECK(all_rooted);
    CHECK(all_below_line);
    CHECK(all_dry);
    CHECK(all_gentle);

    // Deterministic: the same seed plants the same forest.
    game::Vegetation again;
    again.plant(terrain, settings);
    CHECK(again.trees().size() == trees.size());
    CHECK(core::distance(again.trees().front().position_scale.xyz(),
                         trees.front().position_scale.xyz()) < 1e-4f);

    // Cover 0 plants nothing; trees off plants nothing.
    settings.forest_cover = 0.0f;
    again.plant(terrain, settings);
    CHECK(again.trees().empty());
}

void test_grass_follows_the_camera() {
    std::printf("grass surrounds the camera and is the same grass when you return\n");
    game::Terrain terrain;
    terrain.generate(small_terrain());
    game::VegetationSettings settings;
    game::Vegetation vegetation;
    const core::Vec3 here{terrain.valley_center_x(0.0f), 0.0f, 0.0f};
    std::vector<game::PlantInstance> a, b, c;
    vegetation.grass_around(terrain, settings, here, a);
    CHECK(a.size() > 500);
    CHECK(a.size() <= settings.max_grass);
    bool inside = true, grounded = true;
    for (const auto& tuft : a) {
        const float dx = tuft.position_scale.x - here.x, dz = tuft.position_scale.z - here.z;
        if (dx * dx + dz * dz > settings.grass_radius * settings.grass_radius) inside = false;
        if (std::fabs(tuft.position_scale.y -
                      terrain.height_at(tuft.position_scale.x, tuft.position_scale.z)) > 0.05f) {
            grounded = false;
        }
    }
    CHECK(inside);
    CHECK(grounded);

    // Walk away and back: the tufts in the overlap are identical.
    vegetation.grass_around(terrain, settings, here + core::Vec3{40.0f, 0.0f, 0.0f}, b);
    vegetation.grass_around(terrain, settings, here, c);
    CHECK(c.size() == a.size());
    bool identical = true;
    for (size_t i = 0; i < a.size(); ++i) {
        if (core::distance(a[i].position_scale.xyz(), c[i].position_scale.xyz()) > 1e-5f) {
            identical = false;
        }
    }
    CHECK(identical);
    // And the overlap between the two positions shares tufts.
    int shared = 0;
    for (const auto& tuft : b) {
        for (size_t i = 0; i < a.size(); i += 1) {
            if (core::distance(a[i].position_scale.xyz(), tuft.position_scale.xyz()) < 1e-4f) {
                ++shared;
                break;
            }
        }
        if (shared > 50) break;
    }
    CHECK(shared > 50);
}

}  // namespace

int main() {
    test_surface_query_matches_mesh();
    test_trees_stand_where_trees_can();
    test_grass_follows_the_camera();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
