#include "gfx/foliage.h"

#include <cmath>
#include <cstring>

#include "gfx/buffer.h"
#include "gfx/device.h"
#include "gfx/shadow_map.h"

using core::Vec3;

namespace gfx {

namespace {

std::vector<SDL_GPUVertexBufferDescription> foliage_buffers() {
    std::vector<SDL_GPUVertexBufferDescription> buffers = Mesh::buffer_descriptions();
    SDL_GPUVertexBufferDescription instances = {};
    instances.slot = 1;
    instances.pitch = sizeof(FoliageInstance);
    instances.input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
    instances.instance_step_rate = 0;  // reserved by SDL; anything else fails creation
    buffers.push_back(instances);
    return buffers;
}

std::vector<SDL_GPUVertexAttribute> foliage_attributes() {
    std::vector<SDL_GPUVertexAttribute> attributes = Mesh::attributes();
    const uint32_t offsets[2] = {offsetof(FoliageInstance, position_scale),
                                 offsetof(FoliageInstance, params)};
    for (uint32_t i = 0; i < 2; ++i) {
        SDL_GPUVertexAttribute attribute = {};
        attribute.location = 3 + i;
        attribute.buffer_slot = 1;
        attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attribute.offset = offsets[i];
        attributes.push_back(attribute);
    }
    return attributes;
}

PipelineDesc make_foliage_desc() {
    PipelineDesc desc;
    desc.name = "foliage";
    desc.shader_path = "foliage.msl";
    desc.vs_uniform_buffers = 2;  // 0 scene, 1 params
    desc.fs_uniform_buffers = 1;
    desc.fs_samplers = 1;  // the shadow map
    desc.vertex_buffers = foliage_buffers();
    desc.vertex_attributes = foliage_attributes();
    // Blades and cone skirts are seen from both sides.
    desc.cull = SDL_GPU_CULLMODE_NONE;
    return desc;
}

PipelineDesc make_foliage_depth_desc(SDL_GPUTextureFormat depth_format) {
    PipelineDesc desc;
    desc.name = "shadow_foliage";
    desc.shader_path = "foliage_depth.msl";
    desc.vs_uniform_buffers = 2;  // 0 light view-proj, 1 params
    desc.vertex_buffers = foliage_buffers();
    desc.vertex_attributes = foliage_attributes();
    desc.no_color_target = true;
    desc.depth_format = depth_format;
    desc.depth_compare = SDL_GPU_COMPAREOP_LESS;
    desc.cull = SDL_GPU_CULLMODE_NONE;
    return desc;
}

// A ring of `sides` vertices at height y and radius r, appended to `out`.
// Returns the index of the first.
uint32_t ring(MeshData& out, int sides, float y, float r, Vec3 color) {
    const uint32_t first = uint32_t(out.vertices.size());
    for (int i = 0; i < sides; ++i) {
        const float a = core::TWO_PI * float(i) / float(sides);
        MeshVertex v;
        v.position = Vec3{std::cos(a) * r, y, std::sin(a) * r};
        v.normal = Vec3::up();
        v.color = color;
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
    const uint32_t base = ring(out, sides, base_y, radius, color * 0.85f);
    MeshVertex apex;
    apex.position = Vec3{0.0f, apex_y, 0.0f};
    apex.normal = Vec3::up();
    apex.color = color * 1.1f;
    const uint32_t tip = uint32_t(out.vertices.size());
    out.vertices.push_back(apex);
    for (int i = 0; i < sides; ++i) {
        const uint32_t j = uint32_t((i + 1) % sides);
        out.indices.push_back(base + uint32_t(i));
        out.indices.push_back(tip);
        out.indices.push_back(base + j);
    }
}

}  // namespace

MeshData make_conifer_mesh() {
    // Thirteen metres at scale 1: a big spruce, on a dragon with a 19 m span.
    // Three overlapping skirts read as a conifer from any distance the game
    // shows them at; a real tree's detail would be wasted on a fly-by.
    MeshData mesh;
    const Vec3 bark{0.26f, 0.17f, 0.10f};
    const Vec3 needles{0.10f, 0.22f, 0.09f};
    const int sides = 7;
    const uint32_t root = ring(mesh, sides, 0.0f, 0.42f, bark);
    const uint32_t collar = ring(mesh, sides, 4.0f, 0.28f, bark);
    tube(mesh, root, collar, sides);
    cone(mesh, sides, 2.4f, 3.3f, 7.8f, needles * 0.9f);
    cone(mesh, sides, 5.6f, 2.6f, 10.6f, needles);
    cone(mesh, sides, 8.6f, 1.8f, 13.0f, needles * 1.15f);
    mesh.recompute_normals();
    return mesh;
}

MeshData make_grass_tuft_mesh() {
    // Six blades leaning outward from a common root, each a thin triangle. No
    // texture, no alpha: a few hundred thousand of these are cheaper than one
    // alpha-tested billboard pass, and from a dragon they read the same.
    MeshData mesh;
    const Vec3 base_color{0.18f, 0.32f, 0.09f};
    const Vec3 tip_color{0.38f, 0.52f, 0.16f};
    const int blades = 6;
    for (int b = 0; b < blades; ++b) {
        const float a = core::TWO_PI * float(b) / float(blades) + 0.4f * float(b % 2);
        const Vec3 out{std::cos(a), 0.0f, std::sin(a)};
        const Vec3 side = core::cross(Vec3::up(), out) * 0.06f;
        const float height = 0.7f + 0.25f * float((b * 7) % 3) / 2.0f;
        const Vec3 tip = out * (0.35f * height) + Vec3::up() * height;
        const uint32_t first = uint32_t(mesh.vertices.size());
        MeshVertex l, r, t;
        l.position = side * -1.0f + out * 0.05f;
        r.position = side + out * 0.05f;
        t.position = tip;
        l.color = r.color = base_color;
        t.color = tip_color;
        l.normal = r.normal = t.normal = Vec3::up();
        mesh.vertices.push_back(l);
        mesh.vertices.push_back(r);
        mesh.vertices.push_back(t);
        mesh.indices.push_back(first);
        mesh.indices.push_back(first + 1);
        mesh.indices.push_back(first + 2);
    }
    // Blades are lit like the ground they grow from, not like the walls of a
    // tent: normals pulled toward up.
    mesh.recompute_normals();
    for (MeshVertex& v : mesh.vertices) v.normal = core::normalize(v.normal + Vec3::up() * 2.0f);
    return mesh;
}

bool Foliage::init(Device* device, PipelineCache* pipelines, ShadowMap* shadow_map) {
    device_ = device;
    pipelines_ = pipelines;
    shadow_map_ = shadow_map;
    pipeline_ = pipelines_->create(make_foliage_desc());
    depth_pipeline_ = pipelines_->create(make_foliage_depth_desc(shadow_map->format()));
    if (!conifer_.upload(device->gpu(), make_conifer_mesh(), "conifer")) return false;
    if (!tuft_.upload(device->gpu(), make_grass_tuft_mesh(), "grass_tuft")) return false;
    return pipeline_ != INVALID_PIPELINE;
}

void Foliage::shutdown(Device& device) {
    SDL_GPUDevice* gpu = device.gpu();
    conifer_.release(gpu);
    tuft_.release(gpu);
    if (trees_) SDL_ReleaseGPUBuffer(gpu, trees_);
    if (grass_) SDL_ReleaseGPUBuffer(gpu, grass_);
    if (grass_transfer_) SDL_ReleaseGPUTransferBuffer(gpu, grass_transfer_);
    trees_ = grass_ = nullptr;
    grass_transfer_ = nullptr;
}

void Foliage::set_trees(Device& device, const std::vector<FoliageInstance>& trees) {
    if (trees_) SDL_ReleaseGPUBuffer(device.gpu(), trees_);
    trees_ = nullptr;
    tree_count_ = 0;
    if (trees.empty()) return;
    trees_ = create_buffer_with_data(device.gpu(), trees.data(),
                                     uint32_t(trees.size() * sizeof(FoliageInstance)),
                                     SDL_GPU_BUFFERUSAGE_VERTEX, "trees");
    if (trees_) tree_count_ = uint32_t(trees.size());
}

bool Foliage::ensure_grass_capacity(uint32_t count) {
    if (count <= grass_capacity_) return true;
    SDL_GPUDevice* gpu = device_->gpu();
    if (grass_) SDL_ReleaseGPUBuffer(gpu, grass_);
    if (grass_transfer_) SDL_ReleaseGPUTransferBuffer(gpu, grass_transfer_);
    const uint32_t capacity = count < 4096u ? 4096u : count + count / 4u;
    const uint32_t bytes = capacity * uint32_t(sizeof(FoliageInstance));

    SDL_GPUBufferCreateInfo buffer_info = {};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = bytes;
    grass_ = SDL_CreateGPUBuffer(gpu, &buffer_info);
    if (!grass_) return false;
    SDL_SetGPUBufferName(gpu, grass_, "grass");

    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = bytes;
    grass_transfer_ = SDL_CreateGPUTransferBuffer(gpu, &transfer_info);
    if (!grass_transfer_) {
        SDL_ReleaseGPUBuffer(gpu, grass_);
        grass_ = nullptr;
        return false;
    }
    grass_capacity_ = capacity;
    return true;
}

void Foliage::upload_grass(Device& device, const std::vector<FoliageInstance>& grass) {
    grass_uploaded_ = 0;
    if (grass.empty() || !ensure_grass_capacity(uint32_t(grass.size()))) return;
    SDL_GPUDevice* gpu = device.gpu();
    void* mapped = SDL_MapGPUTransferBuffer(gpu, grass_transfer_, true);
    if (!mapped) return;
    std::memcpy(mapped, grass.data(), grass.size() * sizeof(FoliageInstance));
    SDL_UnmapGPUTransferBuffer(gpu, grass_transfer_);

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(device.cmd());
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = grass_transfer_;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = grass_;
    dst.size = uint32_t(grass.size() * sizeof(FoliageInstance));
    SDL_UploadToGPUBuffer(pass, &src, &dst, true);
    SDL_EndGPUCopyPass(pass);
    grass_uploaded_ = uint32_t(grass.size());
}

void Foliage::draw(Device& device, SDL_GPURenderPass* pass, const SceneUniforms& scene,
                   const Mesh& mesh, SDL_GPUBuffer* instances, uint32_t count, float fade_start,
                   float fade_end, float height) {
    if (!pass || !instances || count == 0 || !mesh.valid()) return;
    SDL_GPUGraphicsPipeline* pipeline = pipelines_->get(pipeline_);
    if (!pipeline) return;

    Params params;
    params.wind_time_fade = core::Vec4{wind, scene.view_params.z, fade_start, fade_end};
    params.extra = core::Vec4{height, 0.0f, 0.0f, 0.0f};

    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_PushGPUVertexUniformData(device.cmd(), 0, &scene, sizeof(SceneUniforms));
    SDL_PushGPUVertexUniformData(device.cmd(), 1, &params, sizeof(Params));
    SDL_PushGPUFragmentUniformData(device.cmd(), 0, &scene, sizeof(SceneUniforms));
    if (shadow_map_ && shadow_map_->texture()) {
        SDL_GPUTextureSamplerBinding binding = {};
        binding.texture = shadow_map_->texture();
        binding.sampler = shadow_map_->sampler();
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
    }
    mesh.bind(pass);
    SDL_GPUBufferBinding instance_binding = {};
    instance_binding.buffer = instances;
    SDL_BindGPUVertexBuffers(pass, 1, &instance_binding, 1);
    SDL_DrawGPUIndexedPrimitives(pass, mesh.index_count(), count, 0, 0, 0);
}

void Foliage::draw_trees(Device& device, SDL_GPURenderPass* pass, const SceneUniforms& scene) {
    // No fade: a forest that thins out with distance is a forest that pops.
    draw(device, pass, scene, conifer_, trees_, tree_count_, 1e8f, 2e8f, tree_height);
}

void Foliage::draw_grass(Device& device, SDL_GPURenderPass* pass, const SceneUniforms& scene) {
    draw(device, pass, scene, tuft_, grass_, grass_uploaded_, grass_fade_start, grass_fade_end,
         tuft_height);
}

void Foliage::draw_trees_depth(Device& device, SDL_GPURenderPass* pass,
                               const core::Mat4& light_view_proj, float time) {
    if (!pass || !trees_ || tree_count_ == 0 || !conifer_.valid()) return;
    SDL_GPUGraphicsPipeline* pipeline = pipelines_->get(depth_pipeline_);
    if (!pipeline) return;
    Params params;
    params.wind_time_fade = core::Vec4{wind, time, 1e8f, 2e8f};
    params.extra = core::Vec4{tree_height, 0.0f, 0.0f, 0.0f};
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_PushGPUVertexUniformData(device.cmd(), 0, &light_view_proj, sizeof(core::Mat4));
    SDL_PushGPUVertexUniformData(device.cmd(), 1, &params, sizeof(Params));
    conifer_.bind(pass);
    SDL_GPUBufferBinding instance_binding = {};
    instance_binding.buffer = trees_;
    SDL_BindGPUVertexBuffers(pass, 1, &instance_binding, 1);
    SDL_DrawGPUIndexedPrimitives(pass, conifer_.index_count(), tree_count_, 0, 0, 0);
}

}  // namespace gfx
