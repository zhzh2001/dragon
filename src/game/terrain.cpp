#include "game/terrain.h"

#include "core/log.h"

#include <algorithm>
#include <chrono>

using core::Vec3;

namespace game {
void Terrain::generate(const TerrainSettings& settings) {
    settings_ = settings;
    noise_.reseed(settings.seed);
    run_erosion();
    build_mesh();
}

void Terrain::generate(const TerrainSettings& settings, const Terrain& eroded) {
    // The erosion's own inputs must agree; the shape around them is the
    // caller's to keep (a copy of the settings with another cell size).
    const TerrainSettings& theirs = eroded.settings_;
    if (settings.seed != theirs.seed || settings.half_extent != theirs.half_extent ||
        settings.erosion != theirs.erosion || settings.erosion_cell != theirs.erosion_cell ||
        settings.erosion_extent_factor != theirs.erosion_extent_factor ||
        settings.erosion_strength != theirs.erosion_strength ||
        settings.river_half_width != theirs.river_half_width ||
        settings.erosion_params.iterations != theirs.erosion_params.iterations) {
        generate(settings);
        return;
    }
    settings_ = settings;
    noise_.reseed(settings.seed);
    erosion_ = eroded.erosion_;
    build_mesh();
}

void Terrain::run_erosion() {
    erosion_ = ErosionGrid{};
    if (!settings_.erosion || settings_.erosion_strength <= 0.0f) return;
    const auto start = std::chrono::steady_clock::now();
    const float cell = core::maxf(settings_.erosion_cell, 2.0f);
    const float half = settings_.half_extent * core::maxf(settings_.erosion_extent_factor, 1.0f);
    const int n = int(std::lround(half * 2.0f / cell)) + 1;
    std::vector<float> base(size_t(n) * size_t(n));
    // The valley floor is the base level the mountains drain to, and is
    // never changed: the corridor is gameplay's.
    std::vector<uint8_t> fixed(base.size());
    // Rock hardness: broad patches of soft and hard ground, so gullies
    // crowd some faces and spare others.
    std::vector<float> softness(base.size());
    for (int iz = 0; iz < n; ++iz) {
        for (int ix = 0; ix < n; ++ix) {
            const float x = -half + float(ix) * cell, z = -half + float(iz) * cell;
            const size_t i = size_t(iz) * size_t(n) + size_t(ix);
            base[i] = base_height_at(x, z);
            fixed[i] = valley_mask(x, z) < 0.02f ? 1 : 0;
            softness[i] = 0.35f + 1.3f * core::saturate(noise_.fbm(x / 650.0f + 41.0f, z / 650.0f - 13.0f, 3) * 1.4f - 0.2f);
        }
    }
    const ErosionResult eroded = erode(base, fixed, n, cell, settings_.erosion_params, softness);

    erosion_.n = n;
    erosion_.half = half;
    erosion_.cell = cell;
    erosion_.delta.resize(base.size());
    erosion_.flow.resize(base.size());
    erosion_.deposit.resize(base.size());
    // Where the change is kept: off the valley floor and out of the river
    // (gameplay flies, lands and swims there, and the corridor is the
    // generator's, not the water's), and fading out over the outer sixth of
    // the grid so the skirt beyond meets it without a step.
    constexpr float CUT_LIMIT = 150.0f;
    // Drainage area on a log scale: 1 cell is a ridge, a few thousand a
    // stream bed.
    const float flow_scale = 1.0f / std::log(3000.0f);
    for (int iz = 0; iz < n; ++iz) {
        for (int ix = 0; ix < n; ++ix) {
            const size_t i = size_t(iz) * size_t(n) + size_t(ix);
            const float x = -half + float(ix) * cell, z = -half + float(iz) * cell;
            const float edge = core::maxf(std::fabs(x), std::fabs(z)) / half;
            const float river = settings_.river_half_width > 0.0f
                                    ? core::smoothstep(settings_.river_half_width * 2.6f,
                                                       settings_.river_half_width * 5.0f,
                                                       std::fabs(x - river_center_x(z)))
                                    : 1.0f;
            const float keep = core::smoothstep(0.02f, 0.35f, valley_mask(x, z)) * river *
                               (1.0f - core::smoothstep(0.83f, 0.98f, edge)) * settings_.erosion_strength;
            // A cut deeper than ~150 m is eased toward that: the biggest
            // catchments carved slots near 400 m deep and a few cells wide,
            // which no talus pass widens enough to read as a valley.
            float change = eroded.heights[i] - base[i];
            if (change < 0.0f) change = -CUT_LIMIT * (1.0f - std::exp(change / CUT_LIMIT));
            erosion_.delta[i] = change * keep;
            erosion_.flow[i] = core::clampf(std::log(core::maxf(eroded.flow[i], 1.0f)) * flow_scale, 0.0f, 1.0f) *
                               core::minf(keep, 1.0f);
            erosion_.deposit[i] = core::clampf(eroded.deposit[i] / 6.0f, 0.0f, 1.0f) * core::minf(keep, 1.0f);
        }
    }
    // A 1-2-1 blur on each layer. A channel one cell wide, sampled
    // bilinearly by a finer mesh, is a string of beads wherever it runs
    // diagonally; softened over its neighbours it is a groove.
    auto blur = [n](std::vector<float>& layer) {
        std::vector<float> tmp(layer.size());
        for (int pass = 0; pass < 2; ++pass) {
            const bool along_x = pass == 0;
            for (int iz = 0; iz < n; ++iz) {
                for (int ix = 0; ix < n; ++ix) {
                    const int a = along_x ? std::max(ix - 1, 0) : std::max(iz - 1, 0);
                    const int b = along_x ? std::min(ix + 1, n - 1) : std::min(iz + 1, n - 1);
                    const size_t i = size_t(iz) * size_t(n) + size_t(ix);
                    const size_t ia = along_x ? size_t(iz) * size_t(n) + size_t(a) : size_t(a) * size_t(n) + size_t(ix);
                    const size_t ib = along_x ? size_t(iz) * size_t(n) + size_t(b) : size_t(b) * size_t(n) + size_t(ix);
                    tmp[i] = (layer[ia] + 2.0f * layer[i] + layer[ib]) * 0.25f;
                }
            }
            layer.swap(tmp);
        }
    };
    blur(erosion_.delta);
    blur(erosion_.flow);
    blur(erosion_.deposit);

    float cut = 0.0f, fill = 0.0f, flow_max = 0.0f;
    for (size_t i = 0; i < base.size(); ++i) {
        cut = core::minf(cut, erosion_.delta[i]);
        fill = core::maxf(fill, erosion_.delta[i]);
        flow_max = core::maxf(flow_max, eroded.flow[i]);
    }
    LOG_INFO("terrain erosion: %d x %d cells of %.0f m, %d passes, cut %.0f m, fill %.0f m, widest catchment %.0f cells, %.0f ms",
             n - 1, n - 1, cell, settings_.erosion_params.iterations, double(cut), double(fill), double(flow_max),
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}

float Terrain::erosion_sample(const std::vector<float>& layer, float x, float z) const {
    if (erosion_.n < 2 || layer.empty()) return 0.0f;
    const float fx = (x + erosion_.half) / erosion_.cell;
    const float fz = (z + erosion_.half) / erosion_.cell;
    const int cells = erosion_.n - 1;
    if (!(fx >= 0.0f && fz >= 0.0f && fx <= float(cells) && fz <= float(cells))) return 0.0f;
    const int ix = fx >= float(cells) ? cells - 1 : int(fx);
    const int iz = fz >= float(cells) ? cells - 1 : int(fz);
    const float u = fx - float(ix), v = fz - float(iz);
    const size_t i = size_t(iz) * size_t(erosion_.n) + size_t(ix);
    const size_t n = size_t(erosion_.n);
    return layer[i] * (1.0f - u) * (1.0f - v) + layer[i + 1] * u * (1.0f - v) + layer[i + n] * (1.0f - u) * v +
           layer[i + n + 1] * u * v;
}

core::Vec2 Terrain::erosion_at(float x, float z) const {
    return core::Vec2{erosion_sample(erosion_.flow, x, z), erosion_sample(erosion_.deposit, x, z)};
}

float Terrain::valley_center_x(float z) const {
    // Two out-of-phase sines so the corridor does not read as a pure sine wave.
    const float t = z / core::maxf(settings_.valley_period, 1.0f) + settings_.valley_phase;
    return settings_.valley_meander * (std::sin(t * core::TWO_PI) * 0.7f +
                                       std::sin(t * core::TWO_PI * 0.37f + 1.3f) * 0.3f);
}

float Terrain::river_center_x(float z) const {
    // Off the corridor's centre by a slower wander, so the water crosses the
    // valley floor rather than tracing its axis.
    const float t = z / core::maxf(settings_.valley_period, 1.0f);
    return valley_center_x(z) + settings_.river_wander * std::sin(t * core::TWO_PI * 1.7f + 0.6f);
}

float Terrain::valley_mask(float x, float z) const {
    const float distance = std::fabs(x - valley_center_x(z));
    return core::smoothstep(settings_.valley_width, settings_.valley_width + settings_.valley_falloff,
                      distance);
}

bool Terrain::grid_height(const gfx::MeshData& mesh, int n, float half, float cell, float x,
                          float z, float& out) {
    // The cell under the point, and where in it. Same layout build_mesh writes:
    // row iz, column ix, two triangles split from top-right to bottom-left.
    const float fx = (x + half) / cell;
    const float fz = (z + half) / cell;
    const int cells = n - 1;
    // The far edge belongs to the last cell, so the boundary vertices are
    // reproduced exactly instead of falling through to the coarser grid.
    if (!(fx >= 0.0f && fz >= 0.0f && fx <= float(cells) && fz <= float(cells))) return false;
    const int ix = fx >= float(cells) ? cells - 1 : int(fx);
    const int iz = fz >= float(cells) ? cells - 1 : int(fz);
    const float u = fx - float(ix);
    const float v = fz - float(iz);
    auto h = [&](int cx, int cz) {
        return mesh.vertices[size_t(cz) * size_t(n) + size_t(cx)].position.y;
    };
    const float tl = h(ix, iz), tr = h(ix + 1, iz);
    const float bl = h(ix, iz + 1), br = h(ix + 1, iz + 1);
    out = (u + v <= 1.0f) ? tl + u * (tr - tl) + v * (bl - tl)
                          : br + (1.0f - u) * (bl - br) + (1.0f - v) * (tr - br);
    return true;
}

float Terrain::height_at(float x, float z) const {
    if (grid_ready_) {
        float h = 0.0f;
        if (grid_height(mesh_, verts_per_side_, settings_.half_extent,
                        core::maxf(settings_.cell_size, 0.5f), x, z, h)) {
            return h;
        }
        if (skirt_verts_per_side_ > 0 &&
            grid_height(skirt_, skirt_verts_per_side_,
                        settings_.half_extent * settings_.skirt_extent_factor,
                        core::maxf(settings_.skirt_cell_size, 1.0f), x, z, h)) {
            return h;
        }
    }
    return analytic_height_at(x, z);
}

float Terrain::analytic_height_at(float x, float z) const {
    return base_height_at(x, z) + erosion_sample(erosion_.delta, x, z);
}

float Terrain::base_height_at(float x, float z) const {
    const float mask = valley_mask(x, z);

    float mountains = noise_.ridged(x / settings_.mountain_scale, z / settings_.mountain_scale,
                                    settings_.mountain_octaves) *
                      settings_.mountain_height;
    const float up = settings_.mountain_height > 1e-3f ? mountains / settings_.mountain_height : 0.0f;
    if (settings_.ridge_height > 0.0f) {
        // Offset so the finer ridges do not line up with the broad ones.
        const float crest = noise_.ridged(x / settings_.ridge_scale + 17.3f, z / settings_.ridge_scale - 5.1f,
                                          settings_.ridge_octaves);
        mountains += crest * crest * settings_.ridge_height * core::smoothstep(0.2f, 0.65f, up);
    }
    if (settings_.strata_step > 1.0f && settings_.strata_strength > 0.0f) {
        // Ledges: within each band the height eases flat at both ends, so a
        // slope becomes a run of steps with steeper risers between.
        const float t = mountains / settings_.strata_step;
        const float band = std::floor(t);
        const float f = t - band;
        const float eased = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
        const float terraced = (band + eased) * settings_.strata_step;
        mountains = core::lerpf(mountains, terraced,
                                settings_.strata_strength * core::smoothstep(0.15f, 0.45f, up));
    }

    const float hills =
        noise_.fbm(x / settings_.hill_scale, z / settings_.hill_scale, settings_.hill_octaves) *
        settings_.hill_height;

    // Mountains only exist outside the corridor. Hills are damped but not
    // removed on the floor, so the valley still has relief to fly around.
    const float ground = settings_.valley_floor + mountains * mask + hills * (0.25f + 0.75f * mask);

    // The river: blend the ground down to a flat bed below the water line
    // inside the channel, with banks a couple of widths across. Only where the
    // corridor is (the mask is 0), so the river never climbs a mountain.
    if (settings_.river_half_width > 0.0f && mask < 0.999f) {
        const float distance = std::fabs(x - river_center_x(z));
        const float half = settings_.river_half_width;
        const float channel = 1.0f - core::smoothstep(half, half * 2.6f, distance);
        const float bed = settings_.water_level - settings_.river_depth;
        return core::lerpf(ground, core::minf(ground, bed), channel * (1.0f - mask));
    }
    return ground;
}

Vec3 Terrain::normal_at(float x, float z) const {
    // Central differences. The step is tied to cell size so the sampled normal
    // matches the mesh's faceting rather than the underlying function's detail.
    const float h = core::maxf(settings_.cell_size * 0.5f, 0.5f);
    const float dx = height_at(x + h, z) - height_at(x - h, z);
    const float dz = height_at(x, z + h) - height_at(x, z - h);
    return core::normalize(Vec3{-dx, 2.0f * h, -dz});
}

void Terrain::build_mesh() {
    const float extent = settings_.half_extent;
    const float cell = core::maxf(settings_.cell_size, 0.5f);
    const int cells = int((extent * 2.0f) / cell);
    const int verts_per_side = cells + 1;

    mesh_.vertices.clear();
    mesh_.indices.clear();
    mesh_.vertices.reserve(size_t(verts_per_side) * size_t(verts_per_side));
    mesh_.indices.reserve(size_t(cells) * size_t(cells) * 6);

    grid_ready_ = false;
    verts_per_side_ = verts_per_side;
    max_height_ = -1e9f;
    min_height_ = 1e9f;

    for (int iz = 0; iz < verts_per_side; ++iz) {
        const float z = -extent + float(iz) * cell;
        for (int ix = 0; ix < verts_per_side; ++ix) {
            const float x = -extent + float(ix) * cell;
            const float y = analytic_height_at(x, z);
            max_height_ = core::maxf(max_height_, y);
            min_height_ = core::minf(min_height_, y);

            gfx::MeshVertex vertex;
            vertex.position = Vec3{x, y, z};
            vertex.normal = Vec3::up();  // replaced by recompute_normals
            // Material is decided in the shader from height and slope, so it
            // stays hot-reloadable; the colour carries what erosion left
            // (x flow, y deposit), which a tier that tints by the vertex
            // colour resets to white (App::regenerate_terrain).
            const core::Vec2 eroded = erosion_at(x, z);
            vertex.color = Vec3{eroded.x, eroded.y, 1.0f};
            mesh_.vertices.push_back(vertex);
        }
    }

    for (int iz = 0; iz < cells; ++iz) {
        for (int ix = 0; ix < cells; ++ix) {
            const uint32_t top_left = uint32_t(iz * verts_per_side + ix);
            const uint32_t top_right = top_left + 1;
            const uint32_t bottom_left = top_left + uint32_t(verts_per_side);
            const uint32_t bottom_right = bottom_left + 1;

            // Counter-clockwise when viewed from above (+Y), matching the
            // pipeline's front-face setting.
            mesh_.indices.push_back(top_left);
            mesh_.indices.push_back(bottom_left);
            mesh_.indices.push_back(top_right);

            mesh_.indices.push_back(top_right);
            mesh_.indices.push_back(bottom_left);
            mesh_.indices.push_back(bottom_right);
        }
    }

    mesh_.recompute_normals();
    LOG_INFO("terrain: %d x %d cells over %.0f m, height %.0f..%.0f m", cells, cells,
             extent * 2.0f, min_height_, max_height_);
    build_skirt();
    grid_ready_ = true;
}

// The coarse ring of ground beyond the playable extent. A full coarse grid is
// sampled (so height queries index it simply) but triangles are only emitted
// for cells outside the fine mesh, plus one cell of overlap under its edge.
void Terrain::build_skirt() {
    skirt_.vertices.clear();
    skirt_.indices.clear();
    skirt_verts_per_side_ = 0;
    if (settings_.skirt_extent_factor <= 1.05f) return;

    const float half = settings_.half_extent * settings_.skirt_extent_factor;
    // The cell snapped to a divisor of the playable half extent, so the fine
    // mesh's edge falls on a grid line: a 1250 m cell that did not (the
    // fixed-function tier's Minimal) drew its partly-inside cells over a
    // kilometre of the valley, river included.
    // Only for a coarse skirt (past eight fine cells; the tiers' own skirts
    // are six): those keep the cell they were tuned and golden-captured with.
    const float asked = core::maxf(settings_.skirt_cell_size, 1.0f);
    const bool coarse = asked > settings_.cell_size * 8.0f;
    const int per_half = std::max(1, int(std::lround(settings_.half_extent / asked)));
    const float cell = coarse ? settings_.half_extent / float(per_half) : asked;
    const int cells = coarse ? int(std::lround((half * 2.0f) / cell)) : int((half * 2.0f) / cell);
    const int n = cells + 1;
    skirt_verts_per_side_ = n;
    skirt_.vertices.reserve(size_t(n) * size_t(n));
    for (int iz = 0; iz < n; ++iz) {
        const float z = -half + float(iz) * cell;
        for (int ix = 0; ix < n; ++ix) {
            const float x = -half + float(ix) * cell;
            gfx::MeshVertex vertex;
            vertex.position = Vec3{x, analytic_height_at(x, z), z};
            vertex.normal = Vec3::up();
            const core::Vec2 eroded = erosion_at(x, z);
            vertex.color = Vec3{eroded.x, eroded.y, 1.0f};
            skirt_.vertices.push_back(vertex);
        }
    }
    // Overlap the fine mesh by one cell to hide the seam, unless the cell
    // is coarse enough for that overlap to be a visible shelf; then the ring
    // meets the fine edge exactly (the terrain's flanges cover the gap).
    const float margin = coarse ? 0.0f : cell;
    const float inner = settings_.half_extent - margin + (coarse ? 0.01f : 0.0f);
    for (int iz = 0; iz < cells; ++iz) {
        for (int ix = 0; ix < cells; ++ix) {
            const float x0 = -half + float(ix) * cell, x1 = x0 + cell;
            const float z0 = -half + float(iz) * cell, z1 = z0 + cell;
            // Skip cells wholly inside the fine mesh.
            if (x0 > -inner && x1 < inner && z0 > -inner && z1 < inner) continue;
            const uint32_t top_left = uint32_t(iz * n + ix);
            const uint32_t top_right = top_left + 1;
            const uint32_t bottom_left = top_left + uint32_t(n);
            const uint32_t bottom_right = bottom_left + 1;
            skirt_.indices.push_back(top_left);
            skirt_.indices.push_back(bottom_left);
            skirt_.indices.push_back(top_right);
            skirt_.indices.push_back(top_right);
            skirt_.indices.push_back(bottom_left);
            skirt_.indices.push_back(bottom_right);
        }
    }
    skirt_.recompute_normals();
    LOG_INFO("terrain skirt: %d x %d coarse cells to %.0f m", cells, cells, half);
}

}  // namespace game
