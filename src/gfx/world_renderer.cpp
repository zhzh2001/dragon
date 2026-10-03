#include "gfx/world_renderer.h"

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
    // No vertex buffer: the triangle is generated from vertex_id.
    // No depth interaction at all -- the sky is a background, not geometry.
    desc.depth_test = false;
    desc.depth_write = false;
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
    terrain_ = pipelines_->create(make_terrain_desc(false));
    terrain_wireframe_ = pipelines_->create(make_terrain_desc(true));
    water_ = pipelines_->create(make_water_desc());
    mesh_ = pipelines_->create(make_mesh_desc());
    skinned_ = pipelines_->create(make_skinned_desc());
    return sky_ != INVALID_PIPELINE && terrain_ != INVALID_PIPELINE && mesh_ != INVALID_PIPELINE &&
           skinned_ != INVALID_PIPELINE;
}

void WorldRenderer::shutdown(Device& device) {
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
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &scene_, sizeof(SceneUniforms));
    device.rhi().draw(pass, 3, 1, 0, 0);
}

void WorldRenderer::draw_terrain(Device& device, rhi::Pass* pass, const Mesh& mesh) {
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

    mesh.bind(device.rhi(), pass);
    device.rhi().draw_indexed(pass, mesh.index_count(), 1, 0, 0, 0);
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

void WorldRenderer::draw_skinned(Device& device, rhi::Pass* pass,
                                 const anim::SkinnedMesh& mesh, const ModelUniforms& model,
                                 const std::vector<core::Mat4>& joints,
                                 const std::vector<rhi::Texture*>& textures,
                                 rhi::Sampler* sampler) {
    rhi::Pipeline* pipeline = pipelines_->get(skinned_);
    if (!pipeline || !pass || !mesh.valid()) return;

    const SkinBlock block = make_skin_block(joints);

    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &scene_, sizeof(SceneUniforms));
    device.rhi().push_uniforms(rhi::Stage::Vertex, 2, &block, sizeof(SkinBlock));
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

        device.rhi().draw_indexed(pass, submesh.index_count, 1, submesh.index_offset, 0, 0);
    }
}

void WorldRenderer::draw_skinned_depth(Device& device, rhi::Pass* pass,
                                       const anim::SkinnedMesh& mesh,
                                       const core::Mat4& light_view_proj,
                                       const ModelUniforms& model,
                                       const std::vector<core::Mat4>& joints) {
    if (!pass || !mesh.valid() || !shadow_map_) return;
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
    device.rhi().push_uniforms(rhi::Stage::Vertex, 2, &block, sizeof(SkinBlock));
    mesh.bind(device.rhi(), pass);
    device.rhi().draw_indexed(pass, mesh.index_count(), 1, 0, 0, 0);
}

void WorldRenderer::draw_mesh_depth(Device& device, rhi::Pass* pass, const Mesh& mesh,
                                    const core::Mat4& light_view_proj, const ModelUniforms& model) {
    if (!pass || !mesh.valid() || !shadow_map_) return;
    rhi::Pipeline* pipeline = shadow_map_->mesh_pipeline();
    if (!pipeline) return;

    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &light_view_proj, sizeof(core::Mat4));
    device.rhi().push_uniforms(rhi::Stage::Vertex, 1, &model, sizeof(ModelUniforms));
    mesh.bind(device.rhi(), pass);
    device.rhi().draw_indexed(pass, mesh.index_count(), 1, 0, 0, 0);
}

}  // namespace gfx
