#include "gfx/primitives.h"

using core::Vec3;

namespace gfx {

MeshData make_torus(float radius, float tube_radius, Vec3 color, int ring_segments,
                    int tube_segments) {
    MeshData mesh;
    if (ring_segments < 3) ring_segments = 3;
    if (tube_segments < 3) tube_segments = 3;

    mesh.vertices.reserve(size_t(ring_segments) * size_t(tube_segments));
    mesh.indices.reserve(size_t(ring_segments) * size_t(tube_segments) * 6);

    for (int i = 0; i < ring_segments; ++i) {
        const float u = core::TWO_PI * float(i) / float(ring_segments);
        const float cos_u = std::cos(u), sin_u = std::sin(u);
        // Centre of the tube at this point around the ring, and the outward
        // direction in the ring's plane.
        const Vec3 centre{cos_u * radius, sin_u * radius, 0.0f};
        const Vec3 outward{cos_u, sin_u, 0.0f};

        for (int j = 0; j < tube_segments; ++j) {
            const float v = core::TWO_PI * float(j) / float(tube_segments);
            const Vec3 normal = outward * std::cos(v) + Vec3::unit_z() * std::sin(v);

            MeshVertex vertex;
            vertex.position = centre + normal * tube_radius;
            vertex.normal = normal;
            vertex.color = color;
            mesh.vertices.push_back(vertex);
        }
    }

    for (int i = 0; i < ring_segments; ++i) {
        const int next_i = (i + 1) % ring_segments;
        for (int j = 0; j < tube_segments; ++j) {
            const int next_j = (j + 1) % tube_segments;
            const uint32_t a = uint32_t(i * tube_segments + j);
            const uint32_t b = uint32_t(next_i * tube_segments + j);
            const uint32_t c = uint32_t(next_i * tube_segments + next_j);
            const uint32_t d = uint32_t(i * tube_segments + next_j);
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }
    return mesh;
}

MeshData make_annulus(float inner_radius, float outer_radius, Vec3 color, int segments) {
    MeshData mesh;
    if (segments < 3) segments = 3;

    for (int i = 0; i < segments; ++i) {
        const float u = core::TWO_PI * float(i) / float(segments);
        const float cos_u = std::cos(u), sin_u = std::sin(u);
        for (int edge = 0; edge < 2; ++edge) {
            const float r = edge == 0 ? inner_radius : outer_radius;
            MeshVertex vertex;
            vertex.position = Vec3{cos_u * r, sin_u * r, 0.0f};
            vertex.normal = Vec3::unit_z();
            vertex.color = color;
            mesh.vertices.push_back(vertex);
        }
    }

    for (int i = 0; i < segments; ++i) {
        const uint32_t next = uint32_t(((i + 1) % segments) * 2);
        const uint32_t current = uint32_t(i * 2);
        // Both windings: a flat ring is seen from both sides as you fly through.
        mesh.indices.push_back(current);
        mesh.indices.push_back(current + 1);
        mesh.indices.push_back(next + 1);
        mesh.indices.push_back(current);
        mesh.indices.push_back(next + 1);
        mesh.indices.push_back(next);

        mesh.indices.push_back(current);
        mesh.indices.push_back(next);
        mesh.indices.push_back(next + 1);
        mesh.indices.push_back(current);
        mesh.indices.push_back(next + 1);
        mesh.indices.push_back(current + 1);
    }
    return mesh;
}

MeshData make_sphere(float radius, Vec3 color, int segments, int rings) {
    MeshData mesh;
    if (segments < 3) segments = 3;
    if (rings < 2) rings = 2;

    // Poles are duplicated per column rather than shared. A shared pole vertex
    // needs one normal for many triangles, which pinches the shading; the extra
    // vertices are free at these counts.
    for (int r = 0; r <= rings; ++r) {
        const float v = core::PI * float(r) / float(rings);
        const float sin_v = std::sin(v), cos_v = std::cos(v);
        for (int s = 0; s <= segments; ++s) {
            const float u = core::TWO_PI * float(s) / float(segments);
            const Vec3 normal{sin_v * std::cos(u), cos_v, sin_v * std::sin(u)};

            MeshVertex vertex;
            vertex.position = normal * radius;
            vertex.normal = normal;
            vertex.color = color;
            mesh.vertices.push_back(vertex);
        }
    }

    const int stride = segments + 1;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const uint32_t a = uint32_t(r * stride + s);
            const uint32_t b = uint32_t(a + uint32_t(stride));
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b + 1);
        }
    }
    return mesh;
}

}  // namespace gfx
