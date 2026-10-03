#include "gfx/foliage.h"
#include "gfx/palette.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

#include "core/log.h"
#include "core/noise.h"
#include "core/paths.h"
#include "gfx/texture.h"

#include "gfx/device.h"
#include "gfx/shadow_map.h"

using core::Vec3;

namespace gfx {

namespace {

std::vector<rhi::VertexBufferLayout> foliage_buffers() {
    std::vector<rhi::VertexBufferLayout> buffers = Mesh::buffer_descriptions();
    rhi::VertexBufferLayout instances = {};
    instances.slot = 1;
    instances.pitch = sizeof(FoliageInstance);
    instances.rate = rhi::InputRate::Instance;  // reserved by SDL; anything else fails creation
    buffers.push_back(instances);
    return buffers;
}

std::vector<rhi::VertexAttribute> foliage_attributes() {
    std::vector<rhi::VertexAttribute> attributes = Mesh::attributes();
    const uint32_t offsets[2] = {offsetof(FoliageInstance, position_scale),
                                 offsetof(FoliageInstance, params)};
    for (uint32_t i = 0; i < 2; ++i) {
        rhi::VertexAttribute attribute = {};
        attribute.location = 4 + i;  // after the mesh's position, normal, colour, uv
        attribute.buffer_slot = 1;
        attribute.format = rhi::VertexFormat::Float4;
        attribute.offset = offsets[i];
        attributes.push_back(attribute);
    }
    return attributes;
}

PipelineDesc make_foliage_desc() {
    PipelineDesc desc;
    desc.name = "foliage";
    desc.shader = "foliage";
    desc.vertex_buffers = foliage_buffers();
    desc.vertex_attributes = foliage_attributes();
    // Blades and cone skirts are seen from both sides.
    desc.cull = rhi::Cull::None;
    return desc;
}

PipelineDesc make_foliage_depth_desc(rhi::Format depth_format) {
    PipelineDesc desc;
    desc.name = "shadow_foliage";
    desc.shader = "foliage_depth";
    desc.vertex_buffers = foliage_buffers();
    desc.vertex_attributes = foliage_attributes();
    desc.no_color_target = true;
    desc.depth_format = depth_format;
    desc.depth_compare = rhi::Compare::Less;
    desc.cull = rhi::Cull::None;
    return desc;
}

// ---- mesh building blocks. Everything is rings, tubes, cones and blobs.

uint32_t ring(MeshData& out, int sides, Vec3 centre, float r, Vec3 color, float phase = 0.0f) {
    const uint32_t first = uint32_t(out.vertices.size());
    for (int i = 0; i < sides; ++i) {
        const float a = core::TWO_PI * float(i) / float(sides) + phase;
        MeshVertex v;
        v.position = centre + Vec3{std::cos(a) * r, 0.0f, std::sin(a) * r};
        v.normal = Vec3::up();
        v.color = color;
        // Cylindrical: around the trunk in u, up it in v (metres, so the bark
        // streaks keep their scale on a tall trunk and a short one alike).
        v.uv = core::Vec2{float(i) / float(sides), centre.y * 0.25f};
        out.vertices.push_back(v);
    }
    return first;
}

void tube(MeshData& out, uint32_t ring_a, uint32_t ring_b, int sides) {
    for (int i = 0; i < sides; ++i) {
        const uint32_t j = uint32_t((i + 1) % sides);
        out.indices.push_back(ring_a + uint32_t(i));
        out.indices.push_back(ring_b + uint32_t(i));
        out.indices.push_back(ring_a + j);
        out.indices.push_back(ring_a + j);
        out.indices.push_back(ring_b + uint32_t(i));
        out.indices.push_back(ring_b + j);
    }
}

void cone(MeshData& out, int sides, float base_y, float radius, float apex_y, Vec3 color) {
    const uint32_t base = ring(out, sides, Vec3{0.0f, base_y, 0.0f}, radius, palette_dim(color, 0.85f));
    MeshVertex apex;
    apex.position = Vec3{0.0f, apex_y, 0.0f};
    apex.normal = Vec3::up();
    apex.color = palette_dim(color, 1.1f);
    const uint32_t tip = uint32_t(out.vertices.size());
    out.vertices.push_back(apex);
    for (int i = 0; i < sides; ++i) {
        const uint32_t j = uint32_t((i + 1) % sides);
        out.indices.push_back(base + uint32_t(i));
        out.indices.push_back(tip);
        out.indices.push_back(base + j);
    }
}

// A lumpy sphere of stacked rings: a crown, a bush. The lumps come from a
// per-ring phase and a per-vertex radius wobble, because a perfect sphere on
// a stick is a lollipop.
void blob(MeshData& out, Vec3 centre, Vec3 radii, Vec3 color, int sides = 7, int layers = 4) {
    std::vector<uint32_t> rings;
    for (int l = 1; l < layers; ++l) {
        const float t = float(l) / float(layers);  // 0..1 bottom to top
        const float y = std::cos(core::PI * (1.0f - t));  // -1..1
        const float r = std::sqrt(core::maxf(1.0f - y * y, 0.0f));
        const uint32_t first = uint32_t(out.vertices.size());
        for (int i = 0; i < sides; ++i) {
            const float a = core::TWO_PI * float(i) / float(sides) + 0.7f * float(l);
            const float wobble = 0.85f + 0.3f * std::fabs(std::sin(a * 2.3f + float(l) * 1.9f));
            MeshVertex v;
            v.position = centre + Vec3{std::cos(a) * r * radii.x * wobble, y * radii.y,
                                       std::sin(a) * r * radii.z * wobble};
            v.normal = Vec3::up();
            v.color = palette_dim(color, 0.8f + 0.35f * t);
            out.vertices.push_back(v);
        }
        rings.push_back(first);
    }
    MeshVertex bottom, top;
    bottom.position = centre - Vec3{0.0f, radii.y, 0.0f};
    top.position = centre + Vec3{0.0f, radii.y, 0.0f};
    bottom.color = palette_dim(color, 0.7f);
    top.color = palette_dim(color, 1.15f);
    bottom.normal = top.normal = Vec3::up();
    const uint32_t bottom_index = uint32_t(out.vertices.size());
    out.vertices.push_back(bottom);
    const uint32_t top_index = uint32_t(out.vertices.size());
    out.vertices.push_back(top);
    for (int i = 0; i < sides; ++i) {
        const uint32_t j = uint32_t((i + 1) % sides);
        out.indices.push_back(bottom_index);
        out.indices.push_back(rings.front() + j);
        out.indices.push_back(rings.front() + uint32_t(i));
        out.indices.push_back(top_index);
        out.indices.push_back(rings.back() + uint32_t(i));
        out.indices.push_back(rings.back() + j);
    }
    for (size_t l = 0; l + 1 < rings.size(); ++l) tube(out, rings[l], rings[l + 1], sides);
}

// A thin square-section bar between two points: a branch.
void bar(MeshData& out, Vec3 a, Vec3 b, float radius_a, float radius_b, Vec3 color) {
    const Vec3 axis = core::normalize_or(b - a, Vec3::up());
    const Vec3 side = core::normalize_or(core::cross(axis, Vec3{0.3f, 1.0f, 0.2f}), Vec3::right());
    const Vec3 side2 = core::cross(axis, side);
    auto square = [&](Vec3 centre, float r) {
        const uint32_t first = uint32_t(out.vertices.size());
        const Vec3 offsets[4] = {side * r, side2 * r, side * -r, side2 * -r};
        for (const Vec3& o : offsets) {
            MeshVertex v;
            v.position = centre + o;
            v.normal = Vec3::up();
            v.color = color;
            out.vertices.push_back(v);
        }
        return first;
    };
    tube(out, square(a, radius_a), square(b, radius_b), 4);
}

// One leaf card: a textured quad at `centre`, facing `normal`, `width` across
// and `height` tall, with `roll` turning it about its own normal. Two
// triangles, four vertices, UV 0..1; the alpha in the card's texture cuts the
// leaves out of it. `material` names the texture (FOLIAGE_MAT_LEAF or
// FOLIAGE_MAT_NEEDLE), plus FOLIAGE_DETAIL for a card the distance LOD drops.
void card(MeshData& out, Vec3 centre, Vec3 normal, float roll, float width, float height,
          Vec3 color, int material) {
    const Vec3 n = core::normalize_or(normal, Vec3::up());
    Vec3 right = core::normalize_or(core::cross(n, std::fabs(n.y) > 0.9f ? Vec3::right() : Vec3::up()),
                                    Vec3::right());
    Vec3 up = core::cross(right, n);
    const float c = std::cos(roll), s = std::sin(roll);
    const Vec3 r2 = right * c + up * s;
    const Vec3 u2 = up * c - right * s;
    right = r2 * (width * 0.5f);
    up = u2 * (height * 0.5f);
    const uint32_t first = uint32_t(out.vertices.size());
    const Vec3 corners[4] = {centre - right - up, centre + right - up, centre + right + up,
                             centre - right + up};
    const core::Vec2 uvs[4] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
    Vec3 colour = color;
    colour.z = float(material);
    for (int i = 0; i < 4; ++i) {
        MeshVertex v;
        v.position = corners[i];
        v.normal = n;
        v.color = colour;
        v.uv = uvs[i];
        out.vertices.push_back(v);
    }
    out.indices.push_back(first);
    out.indices.push_back(first + 1);
    out.indices.push_back(first + 2);
    out.indices.push_back(first);
    out.indices.push_back(first + 2);
    out.indices.push_back(first + 3);
}

// A crown of cards scattered over an ellipsoid shell around `centre`. The
// largest `coarse` of them are the crown's impostor (always drawn); the rest
// are detail the distance LOD drops. Cards face roughly outward with a random
// tilt, so a crown reads as a mass of foliage from every side.
void card_crown(MeshData& out, Vec3 centre, Vec3 radii, int count, int coarse, float size,
                Vec3 color, int material, uint32_t seed) {
    uint32_t rng = seed ? seed : 1u;
    auto unit = [&rng]() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return float(rng & 0xffffffu) / float(0xffffff) * 2.0f - 1.0f;
    };
    for (int i = 0; i < count; ++i) {
        // A point on the shell, biased slightly upward: crowns are fuller on top.
        Vec3 d = core::normalize_or(Vec3{unit(), unit() * 0.8f + 0.25f, unit()}, Vec3::up());
        const float shell = 0.75f + 0.25f * std::fabs(unit());
        const Vec3 p = centre + Vec3{d.x * radii.x, d.y * radii.y, d.z * radii.z} * shell;
        // Facing outward, tilted, so silhouettes vary; detail cards are smaller.
        const Vec3 n = core::normalize_or(d + Vec3{unit(), unit(), unit()} * 0.45f, d);
        const bool is_coarse = i < coarse;
        const float card_size = is_coarse ? size * 1.5f : size * (0.85f + 0.3f * std::fabs(unit()));
        card(out, p, n, unit() * core::PI, card_size, card_size * 0.85f,
             palette_dim(color, 0.85f + 0.3f * std::fabs(unit())),
             material + (is_coarse ? 0 : FOLIAGE_DETAIL));
    }
}

// One leaning blade: a thin triangle from the root.
void blade(MeshData& out, float angle, float lean, float height, float width, Vec3 base_color,
           Vec3 tip_color) {
    const Vec3 dir{std::cos(angle), 0.0f, std::sin(angle)};
    const Vec3 side = core::cross(Vec3::up(), dir) * width;
    const Vec3 tip = dir * (lean * height) + Vec3::up() * height;
    const uint32_t first = uint32_t(out.vertices.size());
    MeshVertex l, r, t;
    l.position = side * -1.0f + dir * 0.05f;
    r.position = side + dir * 0.05f;
    t.position = tip;
    l.color = r.color = base_color;
    t.color = tip_color;
    l.normal = r.normal = t.normal = Vec3::up();
    out.vertices.push_back(l);
    out.vertices.push_back(r);
    out.vertices.push_back(t);
    out.indices.push_back(first);
    out.indices.push_back(first + 1);
    out.indices.push_back(first + 2);
}

void finish_grass(MeshData& mesh) {
    // Blades are lit like the ground they grow from, not like the walls of a
    // tent: normals pulled toward up.
    mesh.recompute_normals();
    for (MeshVertex& v : mesh.vertices) v.normal = core::normalize(v.normal + Vec3::up() * 2.0f);
}

}  // namespace

float Foliage::tree_height(TreeKind kind) {
    switch (kind) {
        case TreeKind::Spruce: return 14.0f;
        case TreeKind::Pine: return 11.0f;
        case TreeKind::Broadleaf: return 10.0f;
        case TreeKind::Dead: return 9.0f;
        default: return 10.0f;
    }
}

float Foliage::grass_height(GrassKind kind) {
    switch (kind) {
        case GrassKind::Tuft: return 1.5f;
        case GrassKind::Reed: return 2.6f;
        case GrassKind::Bush: return 2.0f;
        default: return 1.5f;
    }
}

// Plant meshes carry no colour: each vertex names a palette entry and a
// brightness (see gfx/palette.h), and the foliage shader resolves it against
// the live table. So the greens below are choices of ENTRY, and the numbers
// are shading within a crown -- which parts of it are darker.
// Vegetation v2: crowns are CARDS -- textured quads whose alpha cuts leaves
// or needles out of them -- not the solid blobs and cones of v1. A blob is a
// single facet of green per triangle, which is why the forest read as pastel
// polygon blobs against a scanned dragon; a card carries the mid-frequency
// detail of real leaves at the cost of two triangles. Each crown is a scatter
// of cards over a shell: a few large COARSE ones, always drawn, that are the
// tree's impostor from a distance, and many smaller DETAIL ones the LOD drops
// past `lod_distance`. Trunks and branches are bark, streaked in the shader.
MeshData make_tree_mesh(TreeKind kind) {
    MeshData mesh;
    const Vec3 bark = palette_vertex(PaletteEntry::Bark, 1.0f, FOLIAGE_MAT_BARK);
    const int sides = 7;
    switch (kind) {
        case TreeKind::Spruce: {
            // A tall spruce: a short trunk and a cone of needle sprays,
            // widest low down, drooping like skirts.
            const Vec3 needles = palette_vertex(PaletteEntry::Spruce);
            tube(mesh, ring(mesh, sides, Vec3::zero(), 0.45f, bark),
                 ring(mesh, sides, Vec3{0.0f, 5.0f, 0.0f}, 0.22f, bark), sides);
            // Coarse: three tall cards crossed through the axis -- the cone
            // silhouette from any side.
            for (int i = 0; i < 3; ++i) {
                const float a = core::PI * float(i) / 3.0f;
                card(mesh, Vec3{0.0f, 8.4f, 0.0f}, Vec3{std::cos(a), 0.0f, std::sin(a)}, 0.0f, 5.6f,
                     11.5f, palette_dim(needles, 0.95f), FOLIAGE_MAT_NEEDLE);
            }
            // Detail: sprays in layers, tilted downward and outward.
            uint32_t rng = 17u;
            auto unit = [&rng]() {
                rng ^= rng << 13;
                rng ^= rng >> 17;
                rng ^= rng << 5;
                return float(rng & 0xffffffu) / float(0xffffff) * 2.0f - 1.0f;
            };
            for (int layer = 0; layer < 6; ++layer) {
                const float y = 3.0f + 1.8f * float(layer);
                const float radius = 3.1f * (1.0f - float(layer) / 6.5f);
                const int count = 7 - layer / 2;
                for (int i = 0; i < count; ++i) {
                    const float a = core::TWO_PI * float(i) / float(count) + 0.5f * float(layer) + 0.2f * unit();
                    const Vec3 out_dir{std::cos(a), 0.0f, std::sin(a)};
                    const Vec3 p = out_dir * (radius * 0.55f) + Vec3{0.0f, y, 0.0f};
                    // Facing out and DOWN: a spruce skirt hangs.
                    const Vec3 n = core::normalize_or(out_dir + Vec3{0.0f, -0.55f, 0.0f}, out_dir);
                    card(mesh, p, n, unit() * 0.4f, radius * 1.5f, radius * 1.1f,
                         palette_dim(needles, 0.85f + 0.3f * std::fabs(unit())),
                         FOLIAGE_MAT_NEEDLE + FOLIAGE_DETAIL);
                }
            }
            break;
        }
        case TreeKind::Pine: {
            // A mountain pine: long bare trunk, one broad flat crown of sprays.
            const Vec3 needles = palette_vertex(PaletteEntry::Pine);
            tube(mesh, ring(mesh, sides, Vec3::zero(), 0.5f, bark),
                 ring(mesh, sides, Vec3{0.0f, 7.0f, 0.0f}, 0.28f, bark), sides);
            bar(mesh, Vec3{0.0f, 6.2f, 0.0f}, Vec3{2.6f, 7.6f, 0.8f}, 0.2f, 0.08f, bark);
            bar(mesh, Vec3{0.0f, 6.6f, 0.0f}, Vec3{-2.2f, 8.0f, -1.4f}, 0.2f, 0.08f, bark);
            card_crown(mesh, Vec3{0.0f, 8.3f, 0.0f}, Vec3{3.7f, 1.5f, 3.5f}, 22, 5, 3.0f, needles,
                       FOLIAGE_MAT_NEEDLE, 23u);
            break;
        }
        case TreeKind::Broadleaf: {
            // A valley-floor broadleaf: trunk, a fork, and a crown of leaf
            // clusters, wider than tall.
            const Vec3 leaves = palette_vertex(PaletteEntry::Broadleaf);
            tube(mesh, ring(mesh, sides, Vec3::zero(), 0.5f, bark),
                 ring(mesh, sides, Vec3{0.0f, 4.2f, 0.0f}, 0.35f, bark), sides);
            bar(mesh, Vec3{0.0f, 3.8f, 0.0f}, Vec3{1.9f, 6.0f, 0.6f}, 0.28f, 0.14f, bark);
            bar(mesh, Vec3{0.0f, 3.8f, 0.0f}, Vec3{-1.5f, 5.9f, -1.2f}, 0.28f, 0.14f, bark);
            bar(mesh, Vec3{0.0f, 4.6f, 0.0f}, Vec3{0.4f, 7.4f, 1.4f}, 0.22f, 0.10f, bark);
            card_crown(mesh, Vec3{0.0f, 7.0f, 0.0f}, Vec3{3.2f, 2.3f, 3.0f}, 34, 7, 3.0f, leaves,
                       FOLIAGE_MAT_LEAF, 41u);
            break;
        }
        case TreeKind::Dead:
        default: {
            // A dead snag near the treeline: a leaning trunk and bare branches,
            // bleached.
            const Vec3 grey = palette_vertex(PaletteEntry::Deadwood);
            tube(mesh, ring(mesh, sides, Vec3::zero(), 0.45f, palette_dim(grey, 0.8f)),
                 ring(mesh, sides, Vec3{0.4f, 5.5f, 0.2f}, 0.22f, grey), sides);
            tube(mesh, ring(mesh, sides, Vec3{0.4f, 5.5f, 0.2f}, 0.22f, grey),
                 ring(mesh, sides, Vec3{0.9f, 9.0f, 0.5f}, 0.08f, palette_dim(grey, 1.1f)), sides);
            bar(mesh, Vec3{0.2f, 3.6f, 0.1f}, Vec3{-2.4f, 5.6f, 0.8f}, 0.16f, 0.05f, grey);
            bar(mesh, Vec3{0.5f, 5.0f, 0.2f}, Vec3{2.6f, 7.4f, -1.0f}, 0.14f, 0.05f, grey);
            bar(mesh, Vec3{0.6f, 6.8f, 0.3f}, Vec3{-1.2f, 8.8f, -1.6f}, 0.10f, 0.04f, grey);
            break;
        }
    }
    mesh.recompute_normals();
    return mesh;
}

MeshData make_grass_mesh(GrassKind kind) {
    // No texture, no alpha: a few hundred thousand thin triangles are cheaper
    // than one alpha-tested billboard pass, and from a dragon they read the
    // same.
    MeshData mesh;
    switch (kind) {
        case GrassKind::Tuft: {
            // Eight blades to about a metre and a half, darker at the root.
            const Vec3 base = palette_vertex(PaletteEntry::GrassBlade);
            const Vec3 tip = palette_dim(base, 1.7f);
            for (int b = 0; b < 8; ++b) {
                const float a = core::TWO_PI * float(b) / 8.0f + 0.35f * float(b % 3);
                const float height = 1.1f + 0.45f * float((b * 5) % 4) / 3.0f;
                blade(mesh, a, 0.4f, height, 0.07f, base, tip);
            }
            break;
        }
        case GrassKind::Reed: {
            // Tall thin reeds for the waterside, pale and straight.
            const Vec3 base = palette_vertex(PaletteEntry::Reed);
            const Vec3 tip = palette_dim(base, 1.8f);
            for (int b = 0; b < 6; ++b) {
                const float a = core::TWO_PI * float(b) / 6.0f + 0.5f * float(b % 2);
                const float height = 2.1f + 0.6f * float((b * 3) % 3) / 2.0f;
                blade(mesh, a, 0.12f, height, 0.045f, base, tip);
            }
            break;
        }
        case GrassKind::Bush:
        default: {
            // A low shrub: two lumps of darker leaf.
            const Vec3 leaf = palette_vertex(PaletteEntry::Bush);
            blob(mesh, Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.3f, 1.0f, 1.2f}, leaf, 6, 3);
            blob(mesh, Vec3{0.9f, 0.7f, 0.5f}, Vec3{0.9f, 0.7f, 0.8f}, palette_dim(leaf, 1.1f), 6, 3);
            break;
        }
    }
    finish_grass(mesh);
    return mesh;
}

bool Foliage::init(Device* device, PipelineCache* pipelines, ShadowMap* shadow_map) {
    device_ = device;
    pipelines_ = pipelines;
    shadow_map_ = shadow_map;
    pipeline_ = pipelines_->create(make_foliage_desc());
    depth_pipeline_ = pipelines_->create(make_foliage_depth_desc(shadow_map->format()));

    // The two card textures: grey detail maps with the leaves cut out in
    // alpha, rendered in Blender (tools: the concept-art skill's notes) and
    // coloured by the palette in the shader. Missing files are not fatal --
    // the shader falls back to a solid card -- but they are logged.
    const auto load = [&](const char* path, const char* name) -> rhi::Texture* {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            LOG_WARN("foliage: no %s at %s; cards will be solid", name, path);
            return nullptr;
        }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        const ImageData image = decode_image(bytes.data(), bytes.size());
        return create_texture_from_image(device->rhi(), image, name, false);
    };
    leaf_texture_ = load(core::paths::asset("textures/leaf_cluster.png").c_str(), "leaf_cluster");
    needle_texture_ = load(core::paths::asset("textures/needle_spray.png").c_str(), "needle_spray");
    card_sampler_ = create_model_sampler(device->rhi());
    static const char* tree_names[TREE_KINDS] = {"spruce", "pine", "broadleaf", "dead_tree"};
    static const char* grass_names[GRASS_KINDS] = {"grass_tuft", "reed", "bush"};
    for (int k = 0; k < TREE_KINDS; ++k) {
        if (!trees_[k].mesh.upload(device->rhi(), make_tree_mesh(TreeKind(k)), tree_names[k])) {
            return false;
        }
    }
    for (int k = 0; k < GRASS_KINDS; ++k) {
        if (!grass_[k].mesh.upload(device->rhi(), make_grass_mesh(GrassKind(k)), grass_names[k])) {
            return false;
        }
    }
    return pipeline_ != INVALID_PIPELINE;
}

void Foliage::shutdown(Device& device) {
    device.rhi().destroy(leaf_texture_);
    device.rhi().destroy(needle_texture_);
    device.rhi().destroy(card_sampler_);
    leaf_texture_ = needle_texture_ = nullptr;
    card_sampler_ = nullptr;
    rhi::Device& rhi = device.rhi();
    for (StaticSet& set : trees_) {
        set.mesh.release(rhi);
        rhi.destroy(set.instances);
        set.instances = nullptr;
    }
    for (StaticSet& set : rocks_) {
        set.mesh.release(rhi);
        rhi.destroy(set.instances);
        set.instances = nullptr;
    }
    for (StreamSet& set : grass_) {
        set.mesh.release(rhi);
        rhi.destroy(set.instances);
        set.instances = nullptr;
    }
}

void Foliage::set_trees(Device& device, TreeKind kind, const std::vector<FoliageInstance>& trees) {
    fill_static(device, trees_[int(kind)], trees, tree_height(kind), "trees");
}

void Foliage::set_rock_mesh(Device& device, int kind, const MeshData& mesh, float height) {
    if (kind < 0 || kind >= ROCK_KINDS) return;
    rocks_[kind].mesh.release(device.rhi());
    rocks_[kind].mesh.upload(device.rhi(), mesh, "rock");
    rock_height_[kind] = height;
}

void Foliage::set_rocks(Device& device, int kind, const std::vector<FoliageInstance>& rocks) {
    if (kind < 0 || kind >= ROCK_KINDS) return;
    fill_static(device, rocks_[kind], rocks, core::maxf(rock_height_[kind], 1.0f), "rocks");
}

uint32_t Foliage::rock_count() const {
    uint32_t total = 0;
    for (const StaticSet& set : rocks_) total += set.count;
    return total;
}

void Foliage::fill_static(Device& device, StaticSet& set, const std::vector<FoliageInstance>& items,
                          float height, const char* name) {
    device.rhi().destroy(set.instances);
    set.instances = nullptr;
    set.count = 0;
    set.cells.clear();
    if (items.empty()) return;

    // Sort into ground cells so each cell is one contiguous instance range.
    auto cell_key = [](const FoliageInstance& t) {
        const int cx = int(std::floor(t.position_scale.x / CELL_SIZE));
        const int cz = int(std::floor(t.position_scale.z / CELL_SIZE));
        return (int64_t(cz) << 32) ^ int64_t(uint32_t(cx));
    };
    std::vector<FoliageInstance> sorted = items;
    std::stable_sort(sorted.begin(), sorted.end(), [&](const FoliageInstance& a,
                                                        const FoliageInstance& b) {
        return cell_key(a) < cell_key(b);
    });
    size_t begin = 0;
    while (begin < sorted.size()) {
        size_t end = begin + 1;
        while (end < sorted.size() && cell_key(sorted[end]) == cell_key(sorted[begin])) ++end;
        Cell cell;
        core::Vec3 lo = sorted[begin].position_scale.xyz(), hi = lo;
        float biggest = 1.0f;
        for (size_t i = begin; i < end; ++i) {
            const core::Vec3 p = sorted[i].position_scale.xyz();
            lo = core::Vec3{core::minf(lo.x, p.x), core::minf(lo.y, p.y), core::minf(lo.z, p.z)};
            hi = core::Vec3{core::maxf(hi.x, p.x), core::maxf(hi.y, p.y), core::maxf(hi.z, p.z)};
            biggest = core::maxf(biggest, sorted[i].position_scale.w);
        }
        // The sphere covers the bases plus the tallest one's crown and sway.
        hi.y += height * 1.8f * biggest;
        cell.centre = (lo + hi) * 0.5f;
        cell.radius = core::length(hi - lo) * 0.5f + height * 0.5f * biggest;
        cell.first = uint32_t(begin);
        cell.count = uint32_t(end - begin);
        set.cells.push_back(cell);
        begin = end;
    }

    set.instances = device.rhi().create_buffer(rhi::BufferUsage::Vertex,
                                               uint32_t(sorted.size() * sizeof(FoliageInstance)),
                                               sorted.data(), name);
    if (set.instances) set.count = uint32_t(sorted.size());
}

namespace {

// The six clip planes of a [0, 1] clip-depth projection (Gribb/Hartmann),
// each normalised, as (normal, d) with inside being dot(n, p) + d >= 0. Row i
// of the column-major matrix is col[c][i]. Valid for the reversed-Z main
// projection and the conventional shadow ortho alike: both keep z in [0, w].
struct Frustum {
    core::Vec4 planes[6];
    int count = 0;

    explicit Frustum(const core::Mat4& m) {
        auto row = [&](int i) {
            return core::Vec4{m.col[0][i], m.col[1][i], m.col[2][i], m.col[3][i]};
        };
        const core::Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        auto add = [&](core::Vec4 p) {
            const float len = core::length(p.xyz());
            if (len < 1e-6f) return;
            planes[count++] = core::Vec4{p.x / len, p.y / len, p.z / len, p.w / len};
        };
        add(core::Vec4{r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w});  // left
        add(core::Vec4{r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w});  // right
        add(core::Vec4{r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w});  // bottom
        add(core::Vec4{r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w});  // top
        add(r2);                                                                // z >= 0
        add(core::Vec4{r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w});  // z <= w
    }

    bool sees(core::Vec3 centre, float radius) const {
        for (int i = 0; i < count; ++i) {
            const core::Vec4& p = planes[i];
            if (p.x * centre.x + p.y * centre.y + p.z * centre.z + p.w < -radius) return false;
        }
        return true;
    }
};

}  // namespace

bool Foliage::ensure_capacity(StreamSet& set, uint32_t count, const char* name) {
    if (count <= set.capacity) return true;
    rhi::Device& rhi = device_->rhi();
    rhi.destroy(set.instances);
    set.capacity = 0;
    const uint32_t capacity = count < 2048u ? 2048u : count + count / 4u;
    set.instances = rhi.create_buffer(rhi::BufferUsage::Vertex, capacity * uint32_t(sizeof(FoliageInstance)),
                                      nullptr, name);
    if (!set.instances) return false;
    set.capacity = capacity;
    return true;
}

void Foliage::upload_grass(Device& device, GrassKind kind,
                           const std::vector<FoliageInstance>& grass) {
    StreamSet& set = grass_[int(kind)];
    set.uploaded = 0;
    if (grass.empty() || !ensure_capacity(set, uint32_t(grass.size()), "grass")) return;
    const uint32_t bytes = uint32_t(grass.size() * sizeof(FoliageInstance));
    void* mapped = device.rhi().map_upload(set.instances, bytes);
    if (!mapped) return;
    std::memcpy(mapped, grass.data(), bytes);
    device.rhi().commit_upload(set.instances, bytes);
    set.uploaded = uint32_t(grass.size());
}

void Foliage::draw(Device& device, rhi::Pass* pass, const SceneUniforms& scene,
                   const Mesh& mesh, rhi::Buffer* instances, uint32_t count, float fade_start,
                   float fade_end, float height) {
    if (!pass || !instances || count == 0 || !mesh.valid()) return;
    rhi::Pipeline* pipeline = pipelines_->get(pipeline_);
    if (!pipeline) return;

    Params params;
    params.wind_time_fade = core::Vec4{wind, scene.view_params.z, fade_start, fade_end};
    params.extra = core::Vec4{height, lod_distance, 0.0f, 0.0f};

    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &scene, sizeof(SceneUniforms));
    device.rhi().push_uniforms(rhi::Stage::Vertex, 1, &params, sizeof(Params));
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &scene, sizeof(SceneUniforms));
    device.rhi().push_uniforms(rhi::Stage::Fragment, 1, &params, sizeof(Params));
    if (shadow_map_ && shadow_map_->texture()) {
        rhi::TextureBinding binding = {};
        binding.texture = shadow_map_->texture();
        binding.sampler = shadow_map_->sampler();
        device.rhi().bind_fragment_textures(pass, 0, &binding, 1);
    }
    bind_cards(device, pass, 1);
    mesh.bind(device.rhi(), pass);
    rhi::BufferBinding instance_binding = {};
    instance_binding.buffer = instances;
    device.rhi().bind_vertex_buffers(pass, 1, &instance_binding, 1);
    device.rhi().draw_indexed(pass, mesh.index_count(), count, 0, 0, 0);
}

void Foliage::draw_trees(Device& device, rhi::Pass* pass, const SceneUniforms& scene) {
    // No fade: a forest that thins out with distance is a forest that pops.
    // One instanced draw per ground cell the frustum and the distance admit.
    trees_drawn_ = 0;
    if (!pass) return;
    rhi::Pipeline* pipeline = pipelines_->get(pipeline_);
    if (!pipeline) return;
    const Frustum frustum(scene.view_proj);
    const core::Vec3 eye = scene.camera_position.xyz();
    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &scene, sizeof(SceneUniforms));
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &scene, sizeof(SceneUniforms));
    if (shadow_map_ && shadow_map_->texture()) {
        rhi::TextureBinding binding = {};
        binding.texture = shadow_map_->texture();
        binding.sampler = shadow_map_->sampler();
        device.rhi().bind_fragment_textures(pass, 0, &binding, 1);
    }
    bind_cards(device, pass, 1);
    for (int k = 0; k < TREE_KINDS; ++k) {
        const StaticSet& set = trees_[k];
        if (!set.instances || set.count == 0 || !set.mesh.valid()) continue;
        Params params;
        params.wind_time_fade = core::Vec4{wind, scene.view_params.z, 1e8f, 2e8f};
        params.extra = core::Vec4{tree_height(TreeKind(k)), lod_distance, 0.0f, 0.0f};
        device.rhi().push_uniforms(rhi::Stage::Vertex, 1, &params, sizeof(Params));
        device.rhi().push_uniforms(rhi::Stage::Fragment, 1, &params, sizeof(Params));
        set.mesh.bind(device.rhi(), pass);
        rhi::BufferBinding instance_binding = {};
        instance_binding.buffer = set.instances;
        device.rhi().bind_vertex_buffers(pass, 1, &instance_binding, 1);
        for (const Cell& cell : set.cells) {
            if (core::distance(eye, cell.centre) - cell.radius > tree_draw_distance) continue;
            if (!frustum.sees(cell.centre, cell.radius)) continue;
            device.rhi().draw_indexed(pass, set.mesh.index_count(), cell.count, 0, 0,
                                         cell.first);
            trees_drawn_ += cell.count;
        }
    }
    // The rocks: the same shader, no sway (a nominal height no rock reaches
    // makes the sway's height fraction nothing), no card LOD.
    for (int k = 0; k < ROCK_KINDS; ++k) {
        const StaticSet& set = rocks_[k];
        if (!set.instances || set.count == 0 || !set.mesh.valid()) continue;
        Params params;
        params.wind_time_fade = core::Vec4{0.0f, scene.view_params.z, 1e8f, 2e8f};
        params.extra = core::Vec4{1e6f, 1e8f, 0.0f, 0.0f};
        device.rhi().push_uniforms(rhi::Stage::Vertex, 1, &params, sizeof(Params));
        device.rhi().push_uniforms(rhi::Stage::Fragment, 1, &params, sizeof(Params));
        set.mesh.bind(device.rhi(), pass);
        rhi::BufferBinding instance_binding = {};
        instance_binding.buffer = set.instances;
        device.rhi().bind_vertex_buffers(pass, 1, &instance_binding, 1);
        const bool big = k == 1 || k == 3 || k == 5;
        const float reach = big ? big_rock_draw_distance : rock_draw_distance;
        for (const Cell& cell : set.cells) {
            if (core::distance(eye, cell.centre) - cell.radius > reach) continue;
            if (!frustum.sees(cell.centre, cell.radius)) continue;
            device.rhi().draw_indexed(pass, set.mesh.index_count(), cell.count, 0, 0, cell.first);
        }
    }
}

void Foliage::draw_grass(Device& device, rhi::Pass* pass, const SceneUniforms& scene) {
    for (int k = 0; k < GRASS_KINDS; ++k) {
        draw(device, pass, scene, grass_[k].mesh, grass_[k].instances, grass_[k].uploaded,
             grass_fade_start, grass_fade_end, grass_height(GrassKind(k)));
    }
}

void Foliage::draw_trees_depth(Device& device, rhi::Pass* pass,
                               const core::Mat4& light_view_proj, float time, core::Vec3 eye) {
    if (!pass) return;
    rhi::Pipeline* pipeline = pipelines_->get(depth_pipeline_);
    if (!pipeline) return;
    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &light_view_proj, sizeof(core::Mat4));
    bind_cards(device, pass, 0);
    for (int k = 0; k < TREE_KINDS; ++k) {
        const StaticSet& set = trees_[k];
        if (!set.instances || set.count == 0 || !set.mesh.valid()) continue;
        Params params;
        params.wind_time_fade = core::Vec4{wind, time, 1e8f, 2e8f};
        params.extra = core::Vec4{tree_height(TreeKind(k)), 0.0f, 0.0f, 0.0f};
        device.rhi().push_uniforms(rhi::Stage::Vertex, 1, &params, sizeof(Params));
        set.mesh.bind(device.rhi(), pass);
        rhi::BufferBinding instance_binding = {};
        instance_binding.buffer = set.instances;
        device.rhi().bind_vertex_buffers(pass, 1, &instance_binding, 1);
        // The light's box is the shadow map's extent about the camera; a
        // cell outside it cannot shadow anything that is drawn.
        const Frustum light(light_view_proj);
        for (const Cell& cell : set.cells) {
            if (!light.sees(cell.centre, cell.radius)) continue;
            device.rhi().draw_indexed(pass, set.mesh.index_count(), cell.count, 0, 0,
                                         cell.first);
        }
    }
    for (int k = 0; k < ROCK_KINDS; ++k) {
        const StaticSet& set = rocks_[k];
        if (!set.instances || set.count == 0 || !set.mesh.valid()) continue;
        Params params;
        params.wind_time_fade = core::Vec4{0.0f, time, 1e8f, 2e8f};
        params.extra = core::Vec4{1e6f, 0.0f, 0.0f, 0.0f};
        device.rhi().push_uniforms(rhi::Stage::Vertex, 1, &params, sizeof(Params));
        set.mesh.bind(device.rhi(), pass);
        rhi::BufferBinding instance_binding = {};
        instance_binding.buffer = set.instances;
        device.rhi().bind_vertex_buffers(pass, 1, &instance_binding, 1);
        const Frustum light(light_view_proj);
        for (const Cell& cell : set.cells) {
            if (core::distance(eye, cell.centre) - cell.radius > rock_shadow_distance) continue;
            if (!light.sees(cell.centre, cell.radius)) continue;
            device.rhi().draw_indexed(pass, set.mesh.index_count(), cell.count, 0, 0, cell.first);
        }
    }
}

void Foliage::bind_cards(Device& device, rhi::Pass* pass, uint32_t first) const {
    // A missing texture binds the other one, so the slot is never empty; the
    // shader's alpha test then cuts the wrong shape, which is visible and
    // logged at load rather than a validation crash.
    rhi::Texture* leaf = leaf_texture_ ? leaf_texture_ : needle_texture_;
    rhi::Texture* needle = needle_texture_ ? needle_texture_ : leaf_texture_;
    if (!leaf || !card_sampler_) return;
    rhi::TextureBinding bindings[2] = {};
    bindings[0].texture = leaf;
    bindings[0].sampler = card_sampler_;
    bindings[1].texture = needle;
    bindings[1].sampler = card_sampler_;
    device.rhi().bind_fragment_textures(pass, first, bindings, 2);
}

uint32_t Foliage::tree_count() const {
    uint32_t total = 0;
    for (const StaticSet& set : trees_) total += set.count;
    return total;
}

uint32_t Foliage::grass_count() const {
    uint32_t total = 0;
    for (const StreamSet& set : grass_) total += set.uploaded;
    return total;
}

core::Vec3 rock_size(int kind) {
    static const core::Vec3 sizes[ROCK_KINDS] = {
        {4.0f, 3.0f, 4.0f}, {8.0f, 5.0f, 7.0f}, {9.0f, 1.8f, 6.0f},
        {4.0f, 10.0f, 4.0f}, {7.0f, 2.5f, 7.0f}, {16.0f, 8.0f, 12.0f}};
    return sizes[kind >= 0 && kind < ROCK_KINDS ? kind : 0];
}

MeshData make_rock_mesh(int kind) {
    // An octahedron subdivided three times onto the unit sphere, then pushed
    // about by noise: lumpy, faceted, and cheap (512 triangles).
    std::vector<core::Vec3> points = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    std::vector<uint32_t> tris = {0, 2, 4, 4, 2, 1, 1, 2, 5, 5, 2, 0, 4, 3, 0, 1, 3, 4, 5, 3, 1, 0, 3, 5};
    for (int level = 0; level < 3; ++level) {
        std::vector<uint32_t> next;
        auto mid = [&](uint32_t a, uint32_t b) {
            points.push_back(core::normalize((points[a] + points[b]) * 0.5f));
            return uint32_t(points.size() - 1);
        };
        for (size_t i = 0; i < tris.size(); i += 3) {
            const uint32_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
            const uint32_t ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
            next.insert(next.end(), {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
        }
        tris = std::move(next);
    }
    const core::Noise noise(uint32_t(kind) * 7919u + 13u);
    const core::Vec3 size = rock_size(kind);
    MeshData mesh;
    for (const core::Vec3& p : points) {
        const float bump = noise.fbm(p.x * 1.7f + 3.0f, p.z * 1.7f + p.y * 1.3f, 4);
        const float r = 1.0f + 0.35f * bump;
        core::Vec3 q = p * r;
        // Flatten the base, and squash to the kind's proportions.
        if (q.y < -0.3f) q.y = -0.3f + (q.y + 0.3f) * 0.25f;
        MeshVertex v;
        v.position = core::Vec3{q.x * size.x * 0.5f, (q.y + 0.35f) * size.y * 0.75f - 0.6f, q.z * size.z * 0.5f};
        const float brightness = 0.75f + 0.5f * bump + 0.2f * core::saturate(p.y);
        v.color = palette_vertex(PaletteEntry::Rock, core::clampf(brightness, 0.4f, 1.4f), FOLIAGE_MAT_PLAIN);
        mesh.vertices.push_back(v);
    }
    // Faceted: every triangle its own vertices, so the lighting reads edges.
    MeshData faceted;
    for (size_t i = 0; i < tris.size(); ++i) {
        faceted.vertices.push_back(mesh.vertices[tris[i]]);
        faceted.indices.push_back(uint32_t(i));
    }
    faceted.recompute_normals();
    return faceted;
}

MeshData encode_rock_mesh(const MeshData& source) {
    MeshData out = source;
    for (MeshVertex& v : out.vertices) {
        // The file's greys run about 0.35..0.75; the palette's rock entry is
        // the mean, so 0.55 maps to 1.
        const float grey = (v.color.x + v.color.y + v.color.z) / 3.0f;
        // A touch lifted (1.1): at 1.35 their sun-facing tops read as pale
        // polka dots on a slope in its own shade; at 1 a shade dark on a sunlit face.
        v.color = palette_vertex(PaletteEntry::Rock, core::clampf(1.1f * grey / 0.55f, 0.3f, 2.0f), FOLIAGE_MAT_PLAIN);
    }
    return out;
}

}  // namespace gfx
