#include "game/dragon_proxy.h"

using core::Vec3;

namespace game {
namespace {

// Local space matches the engine convention: forward is -Z, up is +Y.
constexpr Vec3 BODY_COLOR{0.30f, 0.20f, 0.26f};
constexpr Vec3 BELLY_COLOR{0.46f, 0.38f, 0.30f};
constexpr Vec3 HEAD_COLOR{0.62f, 0.30f, 0.22f};
constexpr Vec3 WING_COLOR{0.34f, 0.22f, 0.26f};
constexpr Vec3 WING_EDGE_COLOR{0.55f, 0.36f, 0.30f};
constexpr Vec3 TAIL_COLOR{0.24f, 0.17f, 0.22f};

uint32_t push_vertex(gfx::MeshData& mesh, Vec3 position, Vec3 color) {
    gfx::MeshVertex vertex;
    vertex.position = position;
    vertex.normal = Vec3::up();  // replaced by recompute_normals
    vertex.color = color;
    mesh.vertices.push_back(vertex);
    return uint32_t(mesh.vertices.size() - 1);
}

void push_triangle(gfx::MeshData& mesh, uint32_t a, uint32_t b, uint32_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}

void push_quad(gfx::MeshData& mesh, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    push_triangle(mesh, a, b, c);
    push_triangle(mesh, a, c, d);
}

// A ring of `segments` vertices in the XY plane at a given z.
void push_ring(gfx::MeshData& mesh, float z, float radius_x, float radius_y, int segments,
               Vec3 top_color, Vec3 bottom_color, uint32_t* out_first) {
    *out_first = uint32_t(mesh.vertices.size());
    for (int i = 0; i < segments; ++i) {
        const float angle = core::TWO_PI * float(i) / float(segments);
        const float x = std::cos(angle) * radius_x;
        const float y = std::sin(angle) * radius_y;
        // Blend belly colour into the underside so roll is readable at a glance.
        const float t = core::saturate(y / core::maxf(radius_y, 0.001f) * 0.5f + 0.5f);
        push_vertex(mesh, Vec3{x, y, z}, core::lerp(bottom_color, top_color, t));
    }
}

void connect_rings(gfx::MeshData& mesh, uint32_t first_a, uint32_t first_b, int segments) {
    for (int i = 0; i < segments; ++i) {
        const uint32_t next = uint32_t((i + 1) % segments);
        push_quad(mesh, first_a + uint32_t(i), first_b + uint32_t(i), first_b + next,
                  first_a + next);
    }
}

// Height of the wing hinge above the body centreline. The vertex shader needs
// the same value to rotate the wing about the shoulder, so it is shared.
float wing_hinge_y(const DragonProxyDims& dims) { return dims.body_radius * 0.45f; }

// One wing, mirrored by `side` (+1 right, -1 left).
//
// Three spanwise stations rather than two, so the wing can carry dihedral: the
// tip sits higher than the root. That single cue makes bank angle readable at a
// glance, which is the whole job of this placeholder.
void push_wing(gfx::MeshData& mesh, const DragonProxyDims& dims, float side) {
    const float hinge_y = wing_hinge_y(dims);
    const float mid_span = core::lerpf(dims.wing_root, dims.wing_span, 0.55f);

    struct Station {
        float span;    // |x|
        float chord;   // front-to-back extent
        float sweep;   // how far back the station sits
        float rise;    // dihedral height above the hinge
    };
    const Station stations[3] = {
        {dims.wing_root, dims.wing_chord, 0.0f, 0.0f},
        {mid_span, core::lerpf(dims.wing_chord, dims.wing_tip_chord, 0.5f),
         dims.wing_chord * 0.16f, dims.wing_span * 0.055f},
        {dims.wing_span, dims.wing_tip_chord, dims.wing_chord * 0.35f, dims.wing_span * 0.13f},
    };

    uint32_t front[3] = {};
    uint32_t back[3] = {};
    for (int i = 0; i < 3; ++i) {
        const Station& st = stations[i];
        const float x = st.span * side;
        const float y = hinge_y + st.rise;
        front[i] = push_vertex(mesh, Vec3{x, y, -st.chord * 0.5f + st.sweep}, WING_EDGE_COLOR);
        back[i] = push_vertex(mesh, Vec3{x, y, st.chord * 0.5f + st.sweep}, WING_COLOR);
    }

    // Both faces of each panel, wound oppositely, so the wing is visible from
    // above and below without relying on two-sided lighting.
    for (int i = 0; i < 2; ++i) {
        if (side > 0.0f) {
            push_quad(mesh, front[i], front[i + 1], back[i + 1], back[i]);
            push_quad(mesh, front[i], back[i], back[i + 1], front[i + 1]);
        } else {
            push_quad(mesh, front[i], back[i], back[i + 1], front[i + 1]);
            push_quad(mesh, front[i], front[i + 1], back[i + 1], back[i]);
        }
    }
}

// Vertical fin above the hips. Contributes nothing aerodynamically -- the flight
// model has no notion of it -- but it is the single clearest visual cue for roll
// and yaw, which is what makes the flight model tunable by eye.
void push_fin(gfx::MeshData& mesh, const DragonProxyDims& dims) {
    const float z0 = dims.body_length * 0.30f;
    const float z1 = dims.body_length * 0.52f;
    const float base_y = dims.body_radius * 0.7f;
    const float height = dims.body_radius * 2.4f;

    const uint32_t base_front = push_vertex(mesh, Vec3{0.0f, base_y, z0}, TAIL_COLOR);
    const uint32_t base_back = push_vertex(mesh, Vec3{0.0f, base_y, z1}, TAIL_COLOR);
    const uint32_t tip = push_vertex(mesh, Vec3{0.0f, base_y + height, z1 - 0.3f},
                                     WING_EDGE_COLOR);
    push_triangle(mesh, base_front, base_back, tip);
    push_triangle(mesh, base_front, tip, base_back);
}

}  // namespace

gfx::MeshData make_dragon_proxy(const DragonProxyDims& dims) {
    gfx::MeshData mesh;
    constexpr int SEGMENTS = 10;

    // Body: a chain of rings from chest to tail base, tapering at both ends.
    struct Section {
        float z;
        float radius_scale;
    };
    const Section sections[] = {
        {-dims.body_length * 0.50f, 0.42f},  // chest, front
        {-dims.body_length * 0.25f, 0.92f},
        {0.0f, 1.0f},                        // widest point
        {dims.body_length * 0.28f, 0.80f},
        {dims.body_length * 0.50f, 0.40f},   // hips
    };

    uint32_t rings[5] = {};
    for (int i = 0; i < 5; ++i) {
        push_ring(mesh, sections[i].z, dims.body_radius * sections[i].radius_scale,
                  dims.body_radius * sections[i].radius_scale * 0.82f, SEGMENTS, BODY_COLOR,
                  BELLY_COLOR, &rings[i]);
    }
    for (int i = 0; i < 4; ++i) connect_rings(mesh, rings[i], rings[i + 1], SEGMENTS);

    // Neck and head, forward of the chest along -Z.
    const float chest_z = -dims.body_length * 0.50f;
    uint32_t neck_ring = 0;
    push_ring(mesh, chest_z - dims.neck_length * 0.6f, dims.body_radius * 0.30f,
              dims.body_radius * 0.30f, SEGMENTS, HEAD_COLOR, HEAD_COLOR, &neck_ring);
    connect_rings(mesh, rings[0], neck_ring, SEGMENTS);

    // A single point for the snout: crude, but it makes "forward" unambiguous.
    const uint32_t snout =
        push_vertex(mesh, Vec3{0.0f, -dims.body_radius * 0.1f,
                               chest_z - dims.neck_length - dims.body_radius * 0.5f},
                    HEAD_COLOR);
    for (int i = 0; i < SEGMENTS; ++i) {
        push_triangle(mesh, neck_ring + uint32_t(i), snout,
                      neck_ring + uint32_t((i + 1) % SEGMENTS));
    }

    // Tail, tapering to a point behind the hips.
    const float hip_z = dims.body_length * 0.50f;
    uint32_t tail_ring = 0;
    push_ring(mesh, hip_z + dims.tail_length * 0.45f, dims.body_radius * 0.22f,
              dims.body_radius * 0.22f, SEGMENTS, TAIL_COLOR, TAIL_COLOR, &tail_ring);
    connect_rings(mesh, rings[4], tail_ring, SEGMENTS);
    const uint32_t tail_tip =
        push_vertex(mesh, Vec3{0.0f, 0.0f, hip_z + dims.tail_length}, TAIL_COLOR);
    for (int i = 0; i < SEGMENTS; ++i) {
        push_triangle(mesh, tail_ring + uint32_t(i), tail_ring + uint32_t((i + 1) % SEGMENTS),
                      tail_tip);
    }

    push_wing(mesh, dims, 1.0f);
    push_wing(mesh, dims, -1.0f);
    push_fin(mesh, dims);

    mesh.recompute_normals();
    return mesh;
}

}  // namespace game
