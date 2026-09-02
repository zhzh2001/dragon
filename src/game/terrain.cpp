#include "game/terrain.h"

#include "core/log.h"

using core::Vec3;

namespace game {
void Terrain::generate(const TerrainSettings& settings) {
    settings_ = settings;
    noise_.reseed(settings.seed);
    build_mesh();
}

float Terrain::valley_center_x(float z) const {
    // Two out-of-phase sines so the corridor does not read as a pure sine wave.
    const float t = z / core::maxf(settings_.valley_period, 1.0f);
    return settings_.valley_meander * (std::sin(t * core::TWO_PI) * 0.7f +
                                       std::sin(t * core::TWO_PI * 0.37f + 1.3f) * 0.3f);
}

float Terrain::valley_mask(float x, float z) const {
    const float distance = std::fabs(x - valley_center_x(z));
    return core::smoothstep(settings_.valley_width, settings_.valley_width + settings_.valley_falloff,
                      distance);
}

float Terrain::height_at(float x, float z) const {
    if (grid_ready_) {
        // The cell under the point, and where in it. Same layout build_mesh
        // wrote: row iz, column ix, two triangles split from top-right to
        // bottom-left.
        const float extent = settings_.half_extent;
        const float cell = core::maxf(settings_.cell_size, 0.5f);
        const float fx = (x + extent) / cell;
        const float fz = (z + extent) / cell;
        const int cells = verts_per_side_ - 1;
        if (fx >= 0.0f && fz >= 0.0f && fx < float(cells) && fz < float(cells)) {
            const int ix = int(fx);
            const int iz = int(fz);
            const float u = fx - float(ix);
            const float v = fz - float(iz);
            auto h = [&](int cx, int cz) {
                return mesh_.vertices[size_t(cz) * size_t(verts_per_side_) + size_t(cx)].position.y;
            };
            const float tl = h(ix, iz), tr = h(ix + 1, iz);
            const float bl = h(ix, iz + 1), br = h(ix + 1, iz + 1);
            if (u + v <= 1.0f) return tl + u * (tr - tl) + v * (bl - tl);
            return br + (1.0f - u) * (bl - br) + (1.0f - v) * (tr - br);
        }
    }
    return analytic_height_at(x, z);
}

float Terrain::analytic_height_at(float x, float z) const {
    const float mask = valley_mask(x, z);

    const float mountains = noise_.ridged(x / settings_.mountain_scale, z / settings_.mountain_scale,
                                          settings_.mountain_octaves) *
                            settings_.mountain_height;

    const float hills =
        noise_.fbm(x / settings_.hill_scale, z / settings_.hill_scale, settings_.hill_octaves) *
        settings_.hill_height;

    // Mountains only exist outside the corridor. Hills are damped but not
    // removed on the floor, so the valley still has relief to fly around.
    return settings_.valley_floor + mountains * mask + hills * (0.25f + 0.75f * mask);
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
            // stays hot-reloadable. White here means "no vertex tint".
            vertex.color = Vec3::one();
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
    grid_ready_ = true;

    LOG_INFO("terrain: %d x %d cells over %.0f m, height %.0f..%.0f m", cells, cells,
             extent * 2.0f, min_height_, max_height_);
}

}  // namespace game
