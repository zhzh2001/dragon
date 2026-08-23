#include "gfx/world_renderer.h"

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

}  // namespace

bool WorldRenderer::init(Device* device, PipelineCache* pipelines) {
    (void)device;
    pipelines_ = pipelines;
    sky_ = pipelines_->create(make_sky_desc());
    terrain_ = pipelines_->create(make_terrain_desc(false));
    terrain_wireframe_ = pipelines_->create(make_terrain_desc(true));
    mesh_ = pipelines_->create(make_mesh_desc());
    return sky_ != INVALID_PIPELINE && terrain_ != INVALID_PIPELINE && mesh_ != INVALID_PIPELINE;
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
