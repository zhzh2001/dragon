#include "gfx/foliage.h"
#include "gfx/palette.h"

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

// ---- mesh building blocks. Everything is rings, tubes, cones and blobs.

uint32_t ring(MeshData& out, int sides, Vec3 centre, float r, Vec3 color, float phase = 0.0f) {
    const uint32_t first = uint32_t(out.vertices.size());
    for (int i = 0; i < sides; ++i) {
        const float a = core::TWO_PI * float(i) / float(sides) + phase;
        MeshVertex v;
        v.position = centre + Vec3{std::cos(a) * r, 0.0f, std::sin(a) * r};
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
MeshData make_tree_mesh(TreeKind kind) {
    MeshData mesh;
    const Vec3 bark = palette_vertex(PaletteEntry::Bark);
    const int sides = 7;
    switch (kind) {
        case TreeKind::Spruce: {
            // A tall spruce: three overlapping skirts on a short trunk.
            const Vec3 needles = palette_vertex(PaletteEntry::Spruce);
            tube(mesh, ring(mesh, sides, Vec3::zero(), 0.45f, bark),
                 ring(mesh, sides, Vec3{0.0f, 4.0f, 0.0f}, 0.28f, bark), sides);
            cone(mesh, sides, 2.4f, 3.4f, 8.2f, palette_dim(needles, 0.9f));
            cone(mesh, sides, 5.8f, 2.6f, 11.2f, needles);
            cone(mesh, sides, 9.0f, 1.7f, 14.0f, palette_dim(needles, 1.15f));
            break;
        }
        case TreeKind::Pine: {
            // A mountain pine: long bare trunk, one broad flat crown and a cap.
            const Vec3 needles = palette_vertex(PaletteEntry::Pine);
            tube(mesh, ring(mesh, sides, Vec3::zero(), 0.5f, bark),
                 ring(mesh, sides, Vec3{0.0f, 6.5f, 0.0f}, 0.3f, bark), sides);
            cone(mesh, sides, 6.0f, 3.8f, 9.4f, palette_dim(needles, 0.9f));
            cone(mesh, sides, 8.4f, 2.3f, 11.0f, palette_dim(needles, 1.1f));
            break;
        }
        case TreeKind::Broadleaf: {
            // A valley-floor broadleaf: trunk, a fork, and four lumpy crowns of
            // a lighter, warmer green. Wider than tall, in lumps: a round
            // bright ball on a stick is a lollipop.
            const Vec3 leaves = palette_vertex(PaletteEntry::Broadleaf);
            tube(mesh, ring(mesh, sides, Vec3::zero(), 0.5f, bark),
                 ring(mesh, sides, Vec3{0.0f, 4.2f, 0.0f}, 0.35f, bark), sides);
            bar(mesh, Vec3{0.0f, 3.8f, 0.0f}, Vec3{1.9f, 6.0f, 0.6f}, 0.28f, 0.14f, bark);
            bar(mesh, Vec3{0.0f, 3.8f, 0.0f}, Vec3{-1.5f, 5.9f, -1.2f}, 0.28f, 0.14f, bark);
            bar(mesh, Vec3{0.0f, 4.6f, 0.0f}, Vec3{0.4f, 7.4f, 1.4f}, 0.22f, 0.10f, bark);
            blob(mesh, Vec3{0.0f, 7.0f, 0.0f}, Vec3{3.4f, 2.3f, 3.2f}, leaves);
            blob(mesh, Vec3{2.2f, 6.3f, 0.9f}, Vec3{2.4f, 1.7f, 2.3f}, palette_dim(leaves, 0.9f));
            blob(mesh, Vec3{-1.9f, 6.2f, -1.4f}, Vec3{2.3f, 1.6f, 2.2f}, palette_dim(leaves, 1.08f));
            blob(mesh, Vec3{0.5f, 8.6f, 1.2f}, Vec3{1.8f, 1.5f, 1.8f}, palette_dim(leaves, 1.15f));
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
    static const char* tree_names[TREE_KINDS] = {"spruce", "pine", "broadleaf", "dead_tree"};
    static const char* grass_names[GRASS_KINDS] = {"grass_tuft", "reed", "bush"};
    for (int k = 0; k < TREE_KINDS; ++k) {
        if (!trees_[k].mesh.upload(device->gpu(), make_tree_mesh(TreeKind(k)), tree_names[k])) {
            return false;
        }
    }
    for (int k = 0; k < GRASS_KINDS; ++k) {
        if (!grass_[k].mesh.upload(device->gpu(), make_grass_mesh(GrassKind(k)), grass_names[k])) {
            return false;
        }
    }
    return pipeline_ != INVALID_PIPELINE;
}

void Foliage::shutdown(Device& device) {
    SDL_GPUDevice* gpu = device.gpu();
    for (StaticSet& set : trees_) {
        set.mesh.release(gpu);
        if (set.instances) SDL_ReleaseGPUBuffer(gpu, set.instances);
        set.instances = nullptr;
    }
    for (StreamSet& set : grass_) {
        set.mesh.release(gpu);
        if (set.instances) SDL_ReleaseGPUBuffer(gpu, set.instances);
        if (set.transfer) SDL_ReleaseGPUTransferBuffer(gpu, set.transfer);
        set.instances = nullptr;
        set.transfer = nullptr;
    }
}

void Foliage::set_trees(Device& device, TreeKind kind, const std::vector<FoliageInstance>& trees) {
    StaticSet& set = trees_[int(kind)];
    if (set.instances) SDL_ReleaseGPUBuffer(device.gpu(), set.instances);
    set.instances = nullptr;
    set.count = 0;
    if (trees.empty()) return;
    set.instances = create_buffer_with_data(device.gpu(), trees.data(),
                                            uint32_t(trees.size() * sizeof(FoliageInstance)),
                                            SDL_GPU_BUFFERUSAGE_VERTEX, "trees");
    if (set.instances) set.count = uint32_t(trees.size());
}

bool Foliage::ensure_capacity(StreamSet& set, uint32_t count, const char* name) {
    if (count <= set.capacity) return true;
    SDL_GPUDevice* gpu = device_->gpu();
    if (set.instances) SDL_ReleaseGPUBuffer(gpu, set.instances);
    if (set.transfer) SDL_ReleaseGPUTransferBuffer(gpu, set.transfer);
    const uint32_t capacity = count < 2048u ? 2048u : count + count / 4u;
    const uint32_t bytes = capacity * uint32_t(sizeof(FoliageInstance));

    SDL_GPUBufferCreateInfo buffer_info = {};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = bytes;
    set.instances = SDL_CreateGPUBuffer(gpu, &buffer_info);
    if (!set.instances) return false;
    SDL_SetGPUBufferName(gpu, set.instances, name);

    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = bytes;
    set.transfer = SDL_CreateGPUTransferBuffer(gpu, &transfer_info);
    if (!set.transfer) {
        SDL_ReleaseGPUBuffer(gpu, set.instances);
        set.instances = nullptr;
        return false;
    }
    set.capacity = capacity;
    return true;
}

void Foliage::upload_grass(Device& device, GrassKind kind,
                           const std::vector<FoliageInstance>& grass) {
    StreamSet& set = grass_[int(kind)];
    set.uploaded = 0;
    if (grass.empty() || !ensure_capacity(set, uint32_t(grass.size()), "grass")) return;
    SDL_GPUDevice* gpu = device.gpu();
    void* mapped = SDL_MapGPUTransferBuffer(gpu, set.transfer, true);
    if (!mapped) return;
    std::memcpy(mapped, grass.data(), grass.size() * sizeof(FoliageInstance));
    SDL_UnmapGPUTransferBuffer(gpu, set.transfer);

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(device.cmd());
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = set.transfer;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = set.instances;
    dst.size = uint32_t(grass.size() * sizeof(FoliageInstance));
    SDL_UploadToGPUBuffer(pass, &src, &dst, true);
    SDL_EndGPUCopyPass(pass);
    set.uploaded = uint32_t(grass.size());
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
    for (int k = 0; k < TREE_KINDS; ++k) {
        draw(device, pass, scene, trees_[k].mesh, trees_[k].instances, trees_[k].count, 1e8f,
             2e8f, tree_height(TreeKind(k)));
    }
}

void Foliage::draw_grass(Device& device, SDL_GPURenderPass* pass, const SceneUniforms& scene) {
    for (int k = 0; k < GRASS_KINDS; ++k) {
        draw(device, pass, scene, grass_[k].mesh, grass_[k].instances, grass_[k].uploaded,
             grass_fade_start, grass_fade_end, grass_height(GrassKind(k)));
    }
}

void Foliage::draw_trees_depth(Device& device, SDL_GPURenderPass* pass,
                               const core::Mat4& light_view_proj, float time) {
    if (!pass) return;
    SDL_GPUGraphicsPipeline* pipeline = pipelines_->get(depth_pipeline_);
    if (!pipeline) return;
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_PushGPUVertexUniformData(device.cmd(), 0, &light_view_proj, sizeof(core::Mat4));
    for (int k = 0; k < TREE_KINDS; ++k) {
        const StaticSet& set = trees_[k];
        if (!set.instances || set.count == 0 || !set.mesh.valid()) continue;
        Params params;
        params.wind_time_fade = core::Vec4{wind, time, 1e8f, 2e8f};
        params.extra = core::Vec4{tree_height(TreeKind(k)), 0.0f, 0.0f, 0.0f};
        SDL_PushGPUVertexUniformData(device.cmd(), 1, &params, sizeof(Params));
        set.mesh.bind(pass);
        SDL_GPUBufferBinding instance_binding = {};
        instance_binding.buffer = set.instances;
        SDL_BindGPUVertexBuffers(pass, 1, &instance_binding, 1);
        SDL_DrawGPUIndexedPrimitives(pass, set.mesh.index_count(), set.count, 0, 0, 0);
    }
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

}  // namespace gfx
