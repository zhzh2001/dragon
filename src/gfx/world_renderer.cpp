#include "gfx/world_renderer.h"

#include <cstring>

#include "anim/skeleton.h"

namespace gfx {
namespace {

PipelineDesc make_sky_desc() {
    PipelineDesc desc;
    desc.name = "sky";
    desc.shader_path = "sky.msl";
    desc.vs_uniform_buffers = 0;
    desc.fs_uniform_buffers = 1;
    desc.cull = SDL_GPU_CULLMODE_NONE;
    // No vertex buffer: the triangle is generated from vertex_id.
    // No depth interaction at all -- the sky is a background, not geometry.
    desc.depth_test = false;
    desc.depth_write = false;
    return desc;
}

PipelineDesc make_terrain_desc(bool wireframe) {
    PipelineDesc desc;
    desc.name = wireframe ? "terrain_wireframe" : "terrain";
    desc.shader_path = "terrain.msl";
    desc.vs_uniform_buffers = 1;
    desc.fs_uniform_buffers = 1;
    desc.vertex_buffers = Mesh::buffer_descriptions();
    desc.vertex_attributes = Mesh::attributes();
    desc.fs_samplers = 1;  // the shadow map
    desc.fill = wireframe ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    // Terrain is a heightfield: we can see the underside from below when
    // flying through a canyon, so nothing is culled.
    desc.cull = SDL_GPU_CULLMODE_NONE;
    return desc;
}

PipelineDesc make_mesh_desc() {
    PipelineDesc desc;
    desc.name = "mesh";
    desc.shader_path = "mesh.msl";
    desc.vs_uniform_buffers = 2;  // 0 scene, 1 model
    desc.fs_uniform_buffers = 1;
    desc.fs_samplers = 1;  // the shadow map
    desc.vertex_buffers = Mesh::buffer_descriptions();
    desc.vertex_attributes = Mesh::attributes();
    // Wings are thin two-sided plates, and the fragment shader flips normals
    // toward the viewer, so culling would only remove needed faces.
    desc.cull = SDL_GPU_CULLMODE_NONE;
    return desc;
}

PipelineDesc make_skinned_desc() {
    PipelineDesc desc;
    desc.name = "skinned";
    desc.shader_path = "skinned.msl";
    desc.vs_uniform_buffers = 3;  // 0 scene, 1 model, 2 skinning matrices
    desc.fs_uniform_buffers = 1;
    desc.fs_samplers = 1;
    desc.vertex_buffers = anim::SkinnedMesh::buffer_descriptions();
    desc.vertex_attributes = anim::SkinnedMesh::attributes();
    // Membranes are thin and two-sided, and the fragment shader flips normals
    // toward the viewer, so culling would only remove needed faces.
    desc.cull = SDL_GPU_CULLMODE_NONE;
    return desc;
}

PipelineDesc make_skinned_depth_desc(SDL_GPUTextureFormat depth_format) {
    PipelineDesc desc;
    desc.name = "shadow_skinned";
    desc.shader_path = "shadow_skinned.msl";
    desc.vs_uniform_buffers = 3;
    desc.vertex_buffers = anim::SkinnedMesh::buffer_descriptions();
    desc.vertex_attributes = anim::SkinnedMesh::attributes();
    desc.no_color_target = true;
    desc.depth_format = depth_format;
    desc.depth_compare = SDL_GPU_COMPAREOP_LESS;  // the shadow pass uses [0,1] depth
    desc.cull = SDL_GPU_CULLMODE_NONE;
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
    (void)device;
    pipelines_ = pipelines;
    sky_ = pipelines_->create(make_sky_desc());
    terrain_ = pipelines_->create(make_terrain_desc(false));
    terrain_wireframe_ = pipelines_->create(make_terrain_desc(true));
    mesh_ = pipelines_->create(make_mesh_desc());
    skinned_ = pipelines_->create(make_skinned_desc());
    return sky_ != INVALID_PIPELINE && terrain_ != INVALID_PIPELINE && mesh_ != INVALID_PIPELINE &&
           skinned_ != INVALID_PIPELINE;
}

void WorldRenderer::draw_sky(Device& device, SDL_GPURenderPass* pass) {
    SDL_GPUGraphicsPipeline* pipeline = pipelines_->get(sky_);
    if (!pipeline || !pass) return;
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_PushGPUFragmentUniformData(device.cmd(), 0, &scene_, sizeof(SceneUniforms));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
}

void WorldRenderer::draw_terrain(Device& device, SDL_GPURenderPass* pass, const Mesh& mesh) {
    SDL_GPUGraphicsPipeline* pipeline =
        pipelines_->get(wireframe ? terrain_wireframe_ : terrain_);
    if (!pipeline || !pass || !mesh.valid()) return;

    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    // Both stages read the same block, so it is pushed to both slots.
    SDL_PushGPUVertexUniformData(device.cmd(), 0, &scene_, sizeof(SceneUniforms));
    SDL_PushGPUFragmentUniformData(device.cmd(), 0, &scene_, sizeof(SceneUniforms));

    if (shadow_map_ && shadow_map_->texture()) {
        SDL_GPUTextureSamplerBinding binding = {};
        binding.texture = shadow_map_->texture();
        binding.sampler = shadow_map_->sampler();
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    }

    mesh.bind(pass);
    SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count(), 1, 0, 0, 0);
}

void WorldRenderer::draw_mesh(Device& device, SDL_GPURenderPass* pass, const Mesh& mesh,
                              const ModelUniforms& model) {
    SDL_GPUGraphicsPipeline* pipeline = pipelines_->get(mesh_);
    if (!pipeline || !pass || !mesh.valid()) return;

    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_PushGPUVertexUniformData(device.cmd(), 0, &scene_, sizeof(SceneUniforms));
    SDL_PushGPUVertexUniformData(device.cmd(), 1, &model, sizeof(ModelUniforms));
    SDL_PushGPUFragmentUniformData(device.cmd(), 0, &scene_, sizeof(SceneUniforms));

    if (shadow_map_ && shadow_map_->texture()) {
        SDL_GPUTextureSamplerBinding binding = {};
        binding.texture = shadow_map_->texture();
        binding.sampler = shadow_map_->sampler();
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    }

    mesh.bind(pass);
    SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count(), 1, 0, 0, 0);
}

void WorldRenderer::draw_skinned(Device& device, SDL_GPURenderPass* pass,
                                 const anim::SkinnedMesh& mesh, const ModelUniforms& model,
                                 const std::vector<core::Mat4>& joints) {
    SDL_GPUGraphicsPipeline* pipeline = pipelines_->get(skinned_);
    if (!pipeline || !pass || !mesh.valid()) return;

    const SkinBlock block = make_skin_block(joints);

    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_PushGPUVertexUniformData(device.cmd(), 0, &scene_, sizeof(SceneUniforms));
    SDL_PushGPUVertexUniformData(device.cmd(), 1, &model, sizeof(ModelUniforms));
    SDL_PushGPUVertexUniformData(device.cmd(), 2, &block, sizeof(SkinBlock));
    SDL_PushGPUFragmentUniformData(device.cmd(), 0, &scene_, sizeof(SceneUniforms));

    if (shadow_map_ && shadow_map_->texture()) {
        SDL_GPUTextureSamplerBinding binding = {};
        binding.texture = shadow_map_->texture();
        binding.sampler = shadow_map_->sampler();
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    }

    mesh.bind(pass);
    SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count(), 1, 0, 0, 0);
}

void WorldRenderer::draw_skinned_depth(Device& device, SDL_GPURenderPass* pass,
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
    SDL_GPUGraphicsPipeline* pipeline = pipelines_->get(skinned_depth_);
    if (!pipeline) return;

    const SkinBlock block = make_skin_block(joints);
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_PushGPUVertexUniformData(device.cmd(), 0, &light_view_proj, sizeof(core::Mat4));
    SDL_PushGPUVertexUniformData(device.cmd(), 1, &model, sizeof(ModelUniforms));
    SDL_PushGPUVertexUniformData(device.cmd(), 2, &block, sizeof(SkinBlock));
    mesh.bind(pass);
    SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count(), 1, 0, 0, 0);
}

void WorldRenderer::draw_mesh_depth(Device& device, SDL_GPURenderPass* pass, const Mesh& mesh,
                                    const core::Mat4& light_view_proj, const ModelUniforms& model) {
    if (!pass || !mesh.valid() || !shadow_map_) return;
    SDL_GPUGraphicsPipeline* pipeline = shadow_map_->mesh_pipeline();
    if (!pipeline) return;

    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_PushGPUVertexUniformData(device.cmd(), 0, &light_view_proj, sizeof(core::Mat4));
    SDL_PushGPUVertexUniformData(device.cmd(), 1, &model, sizeof(ModelUniforms));
    mesh.bind(pass);
    SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count(), 1, 0, 0, 0);
}

}  // namespace gfx
