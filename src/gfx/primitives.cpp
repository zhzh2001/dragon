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

namespace {

// One flat triangle: its own three vertices, the face's normal on each.
void add_face(MeshData& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 color) {
    const Vec3 normal = core::normalize_or(core::cross(b - a, c - a), Vec3::up());
    const uint32_t first = uint32_t(mesh.vertices.size());
    for (const Vec3& p : {a, b, c}) {
        MeshVertex vertex;
        vertex.position = p;
        vertex.normal = normal;
        vertex.color = color;
        mesh.vertices.push_back(vertex);
    }
    mesh.indices.push_back(first);
    mesh.indices.push_back(first + 1);
    mesh.indices.push_back(first + 2);
}

}  // namespace

MeshData make_crystal(Vec3 color) {
    MeshData mesh;
    constexpr int kSides = 6;
    const float front = -0.35f;  // where the long point begins
    const float back = 0.72f;    // and the short one
    const Vec3 tip_front{0.0f, 0.0f, -1.0f};
    const Vec3 tip_back{0.0f, 0.0f, 1.0f};
    for (int i = 0; i < kSides; ++i) {
        const float a0 = core::TWO_PI * float(i) / float(kSides);
        const float a1 = core::TWO_PI * float(i + 1) / float(kSides);
        // Not a regular hexagon: a crystal's flats differ.
        const float r0 = i % 2 == 0 ? 1.0f : 0.82f;
        const float r1 = (i + 1) % 2 == 0 ? 1.0f : 0.82f;
        const Vec3 f0{std::cos(a0) * r0, std::sin(a0) * r0, front};
        const Vec3 f1{std::cos(a1) * r1, std::sin(a1) * r1, front};
        const Vec3 b0{std::cos(a0) * r0 * 0.9f, std::sin(a0) * r0 * 0.9f, back};
        const Vec3 b1{std::cos(a1) * r1 * 0.9f, std::sin(a1) * r1 * 0.9f, back};
        add_face(mesh, tip_front, f1, f0, color);
        add_face(mesh, f0, f1, b1, color);
        add_face(mesh, f0, b1, b0, color);
        add_face(mesh, tip_back, b0, b1, color);
    }
    return mesh;
}

MeshData make_tube(int along, int round) {
    MeshData mesh;
    if (along < 1) along = 1;
    if (round < 3) round = 3;
    for (int i = 0; i <= along; ++i) {
        for (int j = 0; j <= round; ++j) {
            const float angle = core::TWO_PI * float(j) / float(round);
            MeshVertex vertex;
            vertex.position = Vec3{std::cos(angle), std::sin(angle), float(i) / float(along)};
            vertex.normal = Vec3{std::cos(angle), std::sin(angle), 0.0f};
            vertex.color = Vec3::one();
            mesh.vertices.push_back(vertex);
        }
    }
    const int stride = round + 1;
    for (int i = 0; i < along; ++i) {
        for (int j = 0; j < round; ++j) {
            const uint32_t a = uint32_t(i * stride + j);
            const uint32_t b = a + uint32_t(stride);
            mesh.indices.push_back(a);
            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b);
            mesh.indices.push_back(a + 1);
            mesh.indices.push_back(b + 1);
            mesh.indices.push_back(b);
        }
    }
    return mesh;
}

MeshData make_boulder(Vec3 color, unsigned seed) {
    constexpr int kSegments = 9;
    constexpr int kRings = 6;
    // A hash of the grid place and the seed, 0..1.
    auto bump = [seed](int s, int r) {
        unsigned h = unsigned(s) * 374761393u + unsigned(r) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return float((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
    };
    auto point = [&](int s, int r) {
        s %= kSegments;
        const float v = core::PI * float(r) / float(kRings);
        const float u = core::TWO_PI * float(s) / float(kSegments);
        const Vec3 on{std::sin(v) * std::cos(u), std::cos(v), std::sin(v) * std::sin(u)};
        // The poles are one point each, whatever the column.
        const float radius = 0.72f + 0.42f * bump(r == 0 || r == kRings ? 0 : s, r);
        return on * radius;
    };
    MeshData mesh;
    for (int r = 0; r < kRings; ++r) {
        for (int s = 0; s < kSegments; ++s) {
            const Vec3 a = point(s, r), b = point(s, r + 1), c = point(s + 1, r), d = point(s + 1, r + 1);
            // A shade a face: rock is not one grey.
            const Vec3 shade = color * (0.8f + 0.4f * bump(s + 31, r + 17));
            if (r > 0) add_face(mesh, a, c, b, shade);
            if (r < kRings - 1) add_face(mesh, c, d, b, shade);
        }
    }
    return mesh;
}

}  // namespace gfx
