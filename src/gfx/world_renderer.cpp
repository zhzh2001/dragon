#include "gfx/world_renderer.h"
#include "gfx/render_tier.h"
#include "gfx/prelit.h"
#include "gfx/primitives.h"

#include "gfx/frustum.h"

#include <algorithm>

#include <cstring>

#include "anim/skeleton.h"
#include "gfx/texture.h"

namespace gfx {
namespace {

PipelineDesc make_sky_desc() {
    PipelineDesc desc;
    desc.name = "sky";
    desc.shader = "sky";
    desc.cull = rhi::Cull::None;
    if (active_tier().fixed_function) {
        // The dome's positions (sky.hlsl, FIXED_FUNCTION), from a MeshVertex.
        rhi::VertexBufferLayout buffer = {};
        buffer.slot = 0;
        buffer.pitch = sizeof(MeshVertex);
        desc.vertex_buffers = {buffer};
        auto attribute = [](uint32_t location, uint32_t offset) {
            rhi::VertexAttribute a = {};
            a.location = location;
            a.buffer_slot = 0;
            a.format = rhi::VertexFormat::Float3;
            a.offset = offset;
            return a;
        };
        rhi::VertexAttribute uv = attribute(3, offsetof(MeshVertex, uv));
        uv.format = rhi::VertexFormat::Float2;
        desc.vertex_attributes = {attribute(0, offsetof(MeshVertex, position)), attribute(1, offsetof(MeshVertex, normal)),
                                  attribute(2, offsetof(MeshVertex, color)), uv};
    }
    // No vertex buffer: the triangle is generated from vertex_id.
    // Drawn after the opaque world, at the far plane, tested and not
    // writing: only the pixels nothing covered are shaded. Drawn first and
    // untested it shaded every pixel once more -- on an X550 at 1024x768 a
    // full screen of a 54-instruction shader, a third of the frame.
    // GreaterEqual against reversed-Z's cleared 0; the cache flips it for a
    // conventional tier.
    desc.depth_test = true;
    desc.depth_write = false;
    desc.depth_compare = rhi::Compare::GreaterEqual;
    return desc;
}

PipelineDesc make_terrain_desc(bool wireframe) {
    PipelineDesc desc;
    desc.name = wireframe ? "terrain_wireframe" : "terrain";
    desc.shader = "terrain";
    desc.vertex_buffers = Mesh::buffer_descriptions();
    desc.vertex_attributes = Mesh::attributes();
    desc.fill = wireframe ? rhi::Fill::Wireframe : rhi::Fill::Solid;
    // Terrain is a heightfield: we can see the underside from below when
    // flying through a canyon, so nothing is culled.
    desc.cull = rhi::Cull::None;
    return desc;
}

PipelineDesc make_water_desc() {
    PipelineDesc desc;
    desc.name = "water";
    desc.shader = "water";
    desc.vertex_buffers = Mesh::buffer_descriptions();
    desc.vertex_attributes = Mesh::attributes();
    desc.cull = rhi::Cull::None;
    return desc;
}

PipelineDesc make_mesh_desc() {
    PipelineDesc desc;
    desc.name = "mesh";
    desc.shader = "mesh";
    desc.vertex_buffers = Mesh::buffer_descriptions();
    desc.vertex_attributes = Mesh::attributes();
    // Wings are thin two-sided plates, and the fragment shader flips normals
    // toward the viewer, so culling would only remove needed faces.
    desc.cull = rhi::Cull::None;
    return desc;
}

PipelineDesc make_skinned_desc() {
    PipelineDesc desc;
    desc.name = "skinned";
    desc.shader = "skinned";
    desc.vertex_buffers = anim::SkinnedMesh::buffer_descriptions();
    desc.vertex_attributes = anim::SkinnedMesh::attributes();
    // Membranes are thin and two-sided, and the fragment shader flips normals
    // toward the viewer, so culling would only remove needed faces.
    desc.cull = rhi::Cull::None;
    return desc;
}

PipelineDesc make_skinned_depth_desc(rhi::Format depth_format) {
    PipelineDesc desc;
    desc.name = "shadow_skinned";
    desc.shader = "shadow_skinned";
    desc.vertex_buffers = anim::SkinnedMesh::buffer_descriptions();
    desc.vertex_attributes = anim::SkinnedMesh::attributes();
    desc.no_color_target = true;
    desc.depth_format = depth_format;
    desc.depth_compare = rhi::Compare::Less;  // the shadow pass uses [0,1] depth
    desc.cull = rhi::Cull::None;
    return desc;
}

// Skinning matrices are pushed as a fixed-size block, so short lists are padded
// with the identity. Sending a partial block would leave whatever the previous
// draw left behind in the unused slots.
struct SkinBlock {
    core::Mat4 joints[anim::MAX_JOINTS];
};

// A retro tier's block (PACKED_JOINTS, shaders/skin_common.hlsl): each joint's
// top three rows, 64 joints at most. The fourth row of a rig's affine
// transform is always 0 0 0 1.
constexpr size_t PACKED_JOINT_LIMIT = 64;
struct PackedSkinBlock {
    core::Vec4 rows[PACKED_JOINT_LIMIT * 3];
};

PackedSkinBlock pack_joints(const SkinBlock& block) {
    PackedSkinBlock packed;
    for (size_t j = 0; j < PACKED_JOINT_LIMIT; ++j) {
        const core::Mat4& m = block.joints[j];
        for (int r = 0; r < 3; ++r) {
            packed.rows[j * 3 + size_t(r)] = core::Vec4{m.col[0][r], m.col[1][r], m.col[2][r], m.col[3][r]};
        }
    }
    return packed;
}

// Pushes a skin block to vertex slot 2 in the layout the tier's shaders read.
void push_skin(Device& device, const SkinBlock& block) {
    if (active_tier().packed_joints) {
        const PackedSkinBlock packed = pack_joints(block);
        device.rhi().push_uniforms(rhi::Stage::Vertex, 2, &packed, sizeof(PackedSkinBlock));
    } else {
        device.rhi().push_uniforms(rhi::Stage::Vertex, 2, &block, sizeof(SkinBlock));
    }
}

// A batch's palette in the block's first slots (anim/skin_partition.h): what a
// D3D9 vertex shader's constants will hold, padded like the full block.
SkinBlock make_palette_block(const std::vector<core::Mat4>& joints, const std::vector<uint16_t>& palette) {
    SkinBlock block;
    for (size_t i = 0; i < size_t(anim::MAX_JOINTS); ++i) {
        block.joints[i] = i < palette.size() && palette[i] < joints.size() ? joints[palette[i]]
                                                                           : core::Mat4::identity();
    }
    return block;
}

SkinBlock make_skin_block(const std::vector<core::Mat4>& joints) {
    SkinBlock block;
    const size_t count = joints.size() < size_t(anim::MAX_JOINTS) ? joints.size()
                                                                 : size_t(anim::MAX_JOINTS);
    for (size_t i = 0; i < count; ++i) block.joints[i] = joints[i];
    for (size_t i = count; i < size_t(anim::MAX_JOINTS); ++i) {
        block.joints[i] = core::Mat4::identity();
    }
    return block;
}

}  // namespace

// The largest skinned caster's reach from its origin, in metres: a dragon's
// half wingspan, a tower's height.
constexpr float CASTER_RADIUS = 40.0f;

bool WorldRenderer::init(Device* device, PipelineCache* pipelines) {
    pipelines_ = pipelines;

    ImageData white;
    white.width = white.height = 1;
    white.rgba = {255, 255, 255, 255};
    white_ = create_texture_from_image(device->rhi(), white, "white");
    ImageData grey;
    grey.width = grey.height = 1;
    grey.rgba = {128, 128, 128, 128};
    neutral_ = create_texture_from_image(device->rhi(), grey, "neutral", false);
    white_sampler_ = create_model_sampler(device->rhi());
    sky_ = pipelines_->create(make_sky_desc());
    // The dome's colours are baked by the app once the lighting is known
    // (bake_sky_dome); it is not drawn before that.
    terrain_ = pipelines_->create(make_terrain_desc(false));
    terrain_wireframe_ = pipelines_->create(make_terrain_desc(true));
    water_ = pipelines_->create(make_water_desc());
    mesh_ = pipelines_->create(make_mesh_desc());
    skinned_ = pipelines_->create(make_skinned_desc());
    return sky_ != INVALID_PIPELINE && terrain_ != INVALID_PIPELINE && mesh_ != INVALID_PIPELINE &&
           skinned_ != INVALID_PIPELINE;
}

void WorldRenderer::bake_sky_dome(Device& device, const SceneUniforms& scene) {
    if (!active_tier().fixed_function) return;
    // The dome: few vertices (a card without T&L transforms each one on the
    // CPU every frame), lat-long uvs from make_sphere's layout (rings of
    // segments + 1 vertices, the seam duplicated).
    constexpr int SEGMENTS = 16, RINGS = 8;
    MeshData dome = make_sphere(1.0f, core::Vec3::one(), SEGMENTS, RINGS);
    for (size_t i = 0; i < dome.vertices.size(); ++i) {
        const int s = int(i % (SEGMENTS + 1)), r = int(i / (SEGMENTS + 1));
        dome.vertices[i].uv = core::Vec2{float(s) / SEGMENTS, float(r) / RINGS};
        dome.vertices[i].color = core::Vec3::one();
    }
    sky_dome_.release(device.rhi());
    sky_dome_.upload(device.rhi(), dome, "sky_dome");
    // The sky itself, baked once into a lat-long map: the texture carries
    // the gradient, the horizon haze and the sun's disc and glow. Uploaded
    // past the tier's texture cap (Minimal's 64 would smear the sun).
    ImageData map;
    map.width = 512;
    map.height = 256;
    map.rgba.resize(size_t(map.width) * map.height * 4);
    for (int y = 0; y < map.height; ++y) {
        const float v = (float(y) + 0.5f) / map.height;  // 0 at the zenith, as make_sphere's rings run
        const float polar = v * core::PI;
        for (int x = 0; x < map.width; ++x) {
            const float u = (float(x) + 0.5f) / map.width;
            const float azimuth = u * core::TWO_PI;
            // make_sphere's vertex at (u, v) sits at this direction (primitives.cpp).
            const core::Vec3 ray{std::sin(polar) * std::cos(azimuth), std::cos(polar), std::sin(polar) * std::sin(azimuth)};
            const core::Vec3 c = prelit::sky_diffuse(scene, ray);
            uint8_t* p = &map.rgba[(size_t(y) * map.width + x) * 4];
            p[0] = uint8_t(std::lround(c.x * 255.0f));
            p[1] = uint8_t(std::lround(c.y * 255.0f));
            p[2] = uint8_t(std::lround(c.z * 255.0f));
            p[3] = 255;
        }
    }
    device.rhi().destroy(sky_map_);
    TextureBudget uncapped;
    sky_map_ = upload_prepared(device.rhi(), prepare_texture(map, TextureKind::Data, uncapped, false), "sky_map");
    if (!sky_sampler_) {
        rhi::SamplerDesc sampler;
        sampler.address_u = rhi::Address::Repeat;
        sampler.address_v = rhi::Address::Clamp;
        sampler.address_w = rhi::Address::Clamp;
        sky_sampler_ = device.rhi().create_sampler(sampler);
    }
}

void WorldRenderer::shutdown(Device& device) {
    sky_dome_.release(device.rhi());
    device.rhi().destroy(sky_map_);
    device.rhi().destroy(sky_sampler_);
    sky_map_ = nullptr;
    sky_sampler_ = nullptr;
    device.rhi().destroy(white_);
    device.rhi().destroy(neutral_);
    neutral_ = nullptr;
    device.rhi().destroy(white_sampler_);
    white_ = nullptr;
    white_sampler_ = nullptr;
}

void WorldRenderer::draw_sky(Device& device, rhi::Pass* pass) {
    rhi::Pipeline* pipeline = pipelines_->get(sky_);
    if (!pipeline || !pass) return;
    device.rhi().bind_pipeline(pass, pipeline);
    if (active_tier().fixed_function && sky_dome_.valid()) {
        device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &scene_, sizeof(SceneUniforms));
        device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &scene_, sizeof(SceneUniforms));
        if (sky_map_ && sky_sampler_) {
            rhi::TextureBinding binding = {};
            binding.texture = sky_map_;
            binding.sampler = sky_sampler_;
            device.rhi().bind_fragment_textures(pass, 0, &binding, 1);
        }
        sky_dome_.bind(device.rhi(), pass);
        device.rhi().draw_indexed(pass, sky_dome_.index_count(), 1, 0, 0, 0);
        return;
    }
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &scene_, sizeof(SceneUniforms));
    device.rhi().draw(pass, 3, 1, 0, 0);
}

// Draws `mesh` whole, or only the chunks inside `view_proj`'s frustum. With
// `eye`, nearest first, so a hill's pixels hide the ones behind it before
// they are shaded (early depth rejection), which a fill-bound SM2 card feels.
void draw_chunks(rhi::Device& rhi, rhi::Pass* pass, const Mesh& mesh, const std::vector<MeshChunk>* chunks,
                 const core::Mat4& view_proj, const core::Vec3* eye = nullptr) {
    if (!chunks || chunks->empty()) {
        rhi.draw_indexed(pass, mesh.index_count(), 1, 0, 0, 0);
        return;
    }
    const Frustum frustum(view_proj);
    std::vector<std::pair<float, const MeshChunk*>> visible;
    visible.reserve(chunks->size());
    for (const MeshChunk& chunk : *chunks) {
        if (frustum.sees(chunk.centre, chunk.radius)) {
            visible.push_back({eye ? core::distance(*eye, chunk.centre) : 0.0f, &chunk});
        }
    }
    if (eye) std::sort(visible.begin(), visible.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& v : visible) rhi.draw_indexed(pass, v.second->index_count, 1, v.second->first_index, 0, 0);
}

void WorldRenderer::draw_terrain(Device& device, rhi::Pass* pass, const Mesh& mesh,
                                 const std::vector<MeshChunk>* chunks) {
    rhi::Pipeline* pipeline =
        pipelines_->get(wireframe ? terrain_wireframe_ : terrain_);
    if (!pipeline || !pass || !mesh.valid()) return;

    device.rhi().bind_pipeline(pass, pipeline);
    // Both stages read the same block, so it is pushed to both slots.
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &scene_, sizeof(SceneUniforms));
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &scene_, sizeof(SceneUniforms));

    if (shadow_map_ && shadow_map_->texture()) {
        rhi::TextureBinding binding = {};
        binding.texture = shadow_map_->texture();
        binding.sampler = shadow_map_->sampler();
        device.rhi().bind_fragment_textures(pass, 0, &binding, 1);
    }
    {
        rhi::TextureBinding detail = {};
        detail.texture = terrain_detail_ ? terrain_detail_ : neutral_;
        detail.sampler = white_sampler_;
        if (detail.texture && detail.sampler) device.rhi().bind_fragment_textures(pass, 1, &detail, 1);
    }
    if (noise_lattice_ && noise_lattice_->valid()) {
        const rhi::TextureBinding lattice = noise_lattice_->binding();
        device.rhi().bind_fragment_textures(pass, 2, &lattice, 1);
    }

    mesh.bind(device.rhi(), pass);
    // Chunked, the terrain behind the camera costs nothing.
    const core::Vec3 eye = scene_.camera_position.xyz();
    draw_chunks(device.rhi(), pass, mesh, chunks, scene_.view_proj, &eye);
}

void WorldRenderer::draw_water(Device& device, rhi::Pass* pass, const Mesh& mesh) {
    rhi::Pipeline* pipeline = pipelines_->get(water_);
    if (!pipeline || !pass || !mesh.valid()) return;
    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &scene_, sizeof(SceneUniforms));
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &scene_, sizeof(SceneUniforms));
    mesh.bind(device.rhi(), pass);
    device.rhi().draw_indexed(pass, mesh.index_count(), 1, 0, 0, 0);
}

void WorldRenderer::draw_mesh(Device& device, rhi::Pass* pass, const Mesh& mesh,
                              const ModelUniforms& model) {
    rhi::Pipeline* pipeline = pipelines_->get(mesh_);
    if (!pipeline || !pass || !mesh.valid()) return;
    if (!in_view(model, mesh.bound_centre(), mesh.bound_radius(), 1.0f)) return;

    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &scene_, sizeof(SceneUniforms));
    device.rhi().push_uniforms(rhi::Stage::Vertex, 1, &model, sizeof(ModelUniforms));
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &scene_, sizeof(SceneUniforms));

    if (shadow_map_ && shadow_map_->texture()) {
        rhi::TextureBinding binding = {};
        binding.texture = shadow_map_->texture();
        binding.sampler = shadow_map_->sampler();
        device.rhi().bind_fragment_textures(pass, 0, &binding, 1);
    }

    mesh.bind(device.rhi(), pass);
    device.rhi().draw_indexed(pass, mesh.index_count(), 1, 0, 0, 0);
}

// Whether a model's bounding sphere (in its own space, scaled by `pad`) is
// in the view. A draw the camera cannot see still costs its vertices: on a
// card without T&L every one is transformed on the CPU and sent across the
// bus, and on the G41's PCI M6 that traffic was most of the frame -- a herd
// of grazers behind the camera, skinned and sent each frame.
bool WorldRenderer::in_view(const ModelUniforms& model, core::Vec3 centre, float radius, float pad) const {
    if (radius <= 0.0f) return true;
    const core::Mat4& m = model.model;
    const core::Vec4 c = m * core::Vec4{centre.x, centre.y, centre.z, 1.0f};
    const float scale = std::sqrt(core::maxf(core::maxf(core::length_sq(m.col[0].xyz()), core::length_sq(m.col[1].xyz())),
                                             core::length_sq(m.col[2].xyz())));
    const float r = radius * scale * pad;
    if (!Frustum(scene_.view_proj).sees(c.xyz(), r)) return false;
    if (min_draw_pixels_ > 0.0f) {
        // Its radius in pixels: r / (distance tan(fov/2)) of half the height.
        const float d = core::distance(c.xyz(), scene_.camera_position.xyz());
        if (d > r && r / (d * scene_.view_params.x) * 0.5f * view_height_ < min_draw_pixels_) return false;
    }
    return true;
}

// The camera's distance to a model's origin, which picks its LOD.
float WorldRenderer::camera_distance(const ModelUniforms& model) const {
    const core::Vec3 origin{model.model.col[3].x, model.model.col[3].y, model.model.col[3].z};
    return core::distance(origin, scene_.camera_position.xyz());
}

void WorldRenderer::draw_skinned(Device& device, rhi::Pass* pass,
                                 const anim::SkinnedMesh& full_mesh, const ModelUniforms& model,
                                 const std::vector<core::Mat4>& joints,
                                 const std::vector<rhi::Texture*>& textures,
                                 rhi::Sampler* sampler) {
    const anim::SkinnedMesh& mesh = full_mesh.for_distance(camera_distance(model));
    rhi::Pipeline* pipeline = pipelines_->get(skinned_);
    if (!pipeline || !pass || !mesh.valid()) return;
    // Padded: a pose reaches past the bind pose's sphere (a wing, a tail).
    if (!in_view(model, full_mesh.bound_centre(), full_mesh.bound_radius(), 1.5f)) return;

    const SkinBlock block = make_skin_block(joints);

    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &scene_, sizeof(SceneUniforms));
    push_skin(device, block);
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &scene_, sizeof(SceneUniforms));
    mesh.bind(device.rhi(), pass);

    rhi::Texture* shadow_texture = shadow_map_ ? shadow_map_->texture() : nullptr;
    rhi::Sampler* shadow_sampler = shadow_map_ ? shadow_map_->sampler() : nullptr;

    for (const anim::SkinnedSubmesh& submesh : mesh.submeshes()) {
        if (submesh.index_count == 0) continue;

        auto lookup = [&textures](int slot) -> rhi::Texture* {
            if (slot < 0 || size_t(slot) >= textures.size()) return nullptr;
            return textures[size_t(slot)];
        };
        rhi::Texture* base_colour =
            material_toggles_.base_colour ? lookup(submesh.base_color_texture) : nullptr;
        rhi::Texture* normal_map =
            material_toggles_.normal_map ? lookup(submesh.normal_texture) : nullptr;
        rhi::Texture* orm_map =
            material_toggles_.orm_map ? lookup(submesh.orm_texture) : nullptr;

        ModelUniforms submesh_model = model;
        submesh_model.material.x = base_colour && sampler ? 1.0f : 0.0f;
        submesh_model.material.y = normal_map && sampler ? 1.0f : 0.0f;
        submesh_model.material.z = orm_map && sampler ? 1.0f : 0.0f;
        device.rhi().push_uniforms(rhi::Stage::Vertex, 1, &submesh_model, sizeof(ModelUniforms));
        device.rhi().push_uniforms(rhi::Stage::Fragment, 1, &submesh_model, sizeof(ModelUniforms));

        // Every slot the pipeline declares has to be bound whether or not the
        // submesh has that map, so a missing one gets the 1x1 white texture and
        // is switched off by the material flags above.
        rhi::TextureBinding bindings[4] = {};
        bindings[0].texture = shadow_texture;
        bindings[0].sampler = shadow_sampler;
        auto bind_map = [&](int index, rhi::Texture* texture) {
            bindings[index].texture = texture ? texture : white_;
            bindings[index].sampler = texture && sampler ? sampler : white_sampler_;
        };
        bind_map(1, base_colour);
        bind_map(2, normal_map);
        bind_map(3, orm_map);
        if (bindings[0].texture && bindings[1].texture) {
            device.rhi().bind_fragment_textures(pass, 0, bindings, 4);
        }

        if (!submesh.palette.empty()) {
            const SkinBlock palette = make_palette_block(joints, submesh.palette);
            push_skin(device, palette);
        }
        device.rhi().draw_indexed(pass, submesh.index_count, 1, submesh.index_offset, 0, 0);
    }
}

void WorldRenderer::draw_skinned_depth(Device& device, rhi::Pass* pass,
                                       const anim::SkinnedMesh& full_mesh,
                                       const core::Mat4& light_view_proj,
                                       const ModelUniforms& model,
                                       const std::vector<core::Mat4>& joints) {
    const anim::SkinnedMesh& mesh = full_mesh.for_distance(camera_distance(model));
    if (!pass || !mesh.valid() || !shadow_map_) return;
    // A caster outside the map's square casts nothing into it: the light's
    // projection runs along the sun, so where a caster's origin lands in it
    // is where all of its shadow does, give or take its size. A rival two
    // kilometres off was a full skinned draw into a map covering 512 m.
    {
        const core::Vec4 c = light_view_proj * core::Vec4{model.model.col[3].x, model.model.col[3].y,
                                                          model.model.col[3].z, 1.0f};
        const float reach = 1.0f + CASTER_RADIUS / core::maxf(shadow_map_->extent, 1.0f);
        if (std::fabs(c.x) > reach * c.w || std::fabs(c.y) > reach * c.w) return;
    }
    // Created lazily: it needs the shadow map's depth format, which is not known
    // until the shadow map itself has initialized.
    if (skinned_depth_ == INVALID_PIPELINE) {
        skinned_depth_ = pipelines_->create(make_skinned_depth_desc(shadow_map_->format()));
    }
    rhi::Pipeline* pipeline = pipelines_->get(skinned_depth_);
    if (!pipeline) return;

    const SkinBlock block = make_skin_block(joints);
    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &light_view_proj, sizeof(core::Mat4));
    device.rhi().push_uniforms(rhi::Stage::Vertex, 1, &model, sizeof(ModelUniforms));
    push_skin(device, block);
    mesh.bind(device.rhi(), pass);
    const bool split = !mesh.submeshes().empty() && !mesh.submeshes().front().palette.empty();
    if (!split) {
        device.rhi().draw_indexed(pass, mesh.index_count(), 1, 0, 0, 0);
        return;
    }
    // A palette-split mesh draws batch by batch, each with its own joints.
    for (const anim::SkinnedSubmesh& submesh : mesh.submeshes()) {
        if (submesh.index_count == 0) continue;
        const SkinBlock palette = make_palette_block(joints, submesh.palette);
        push_skin(device, palette);
        device.rhi().draw_indexed(pass, submesh.index_count, 1, submesh.index_offset, 0, 0);
    }
}

void WorldRenderer::draw_mesh_depth(Device& device, rhi::Pass* pass, const Mesh& mesh,
                                    const core::Mat4& light_view_proj, const ModelUniforms& model,
                                    const std::vector<MeshChunk>* chunks) {
    if (!pass || !mesh.valid() || !shadow_map_) return;
    rhi::Pipeline* pipeline = shadow_map_->mesh_pipeline();
    if (!pipeline) return;

    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &light_view_proj, sizeof(core::Mat4));
    device.rhi().push_uniforms(rhi::Stage::Vertex, 1, &model, sizeof(ModelUniforms));
    mesh.bind(device.rhi(), pass);
    // Chunked (the terrain), only what the light's box covers: the map spans
    // a few hundred metres of a five-kilometre valley. Chunks are in world
    // space, so this assumes the model transform is the identity.
    draw_chunks(device.rhi(), pass, mesh, chunks, light_view_proj);
}

}  // namespace gfx
