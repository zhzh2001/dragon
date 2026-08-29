#include "gfx/particles.h"

#include <cstring>

using core::Vec2;
using core::Vec3;
using core::Vec4;

namespace gfx {

bool ParticleSystem::init(Device* device, PipelineCache* pipelines) {
    device_ = device;
    pipelines_ = pipelines;
    pool_.resize(MAX_PARTICLES);
    vertices_.reserve(MAX_PARTICLES * 6);

    PipelineDesc desc;
    desc.name = "particles";
    desc.shader_path = "particles.msl";
    desc.vs_uniform_buffers = 1;
    desc.additive_blend = true;
    desc.depth_test = true;
    // Light does not write depth: particles never occlude anything, including
    // each other, which is also why they need no sorting.
    desc.depth_write = false;
    desc.cull = SDL_GPU_CULLMODE_NONE;

    SDL_GPUVertexBufferDescription buffer = {};
    buffer.slot = 0;
    buffer.pitch = sizeof(Vertex);
    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    desc.vertex_buffers = {buffer};

    auto attribute = [](uint32_t location, SDL_GPUVertexElementFormat format, uint32_t offset) {
        SDL_GPUVertexAttribute a = {};
        a.location = location;
        a.buffer_slot = 0;
        a.format = format;
        a.offset = offset;
        return a;
    };
    desc.vertex_attributes = {
        attribute(0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(Vertex, position)),
        attribute(1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, color)),
        attribute(2, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, corner)),
    };

    pipeline_ = pipelines->create(desc);
    return pipeline_ != INVALID_PIPELINE;
}

void ParticleSystem::shutdown() {
    if (!device_) return;
    SDL_GPUDevice* gpu = device_->gpu();
    if (vertex_buffer_) SDL_ReleaseGPUBuffer(gpu, vertex_buffer_);
    if (transfer_) SDL_ReleaseGPUTransferBuffer(gpu, transfer_);
    vertex_buffer_ = nullptr;
    transfer_ = nullptr;
    capacity_ = 0;
}

void ParticleSystem::spawn(const Particle& particle) {
    // Full pool: replace the oldest-ish slot rather than dropping the new one.
    // New particles are the bright, close, just-happened ones -- the exact ones
    // whose absence the player would notice.
    if (count_ >= MAX_PARTICLES) {
        int oldest = 0;
        float best = -1.0f;
        // Sampling a handful is plenty; a full scan per spawn would be O(n^2)
        // in a saturated fight.
        for (int i = 0; i < 8; ++i) {
            const int candidate = (i * 517) % MAX_PARTICLES;
            const float fraction = pool_[candidate].age / pool_[candidate].life;
            if (fraction > best) {
                best = fraction;
                oldest = candidate;
            }
        }
        pool_[oldest] = particle;
        return;
    }
    pool_[count_++] = particle;
}

void ParticleSystem::update(float dt) {
    for (int i = 0; i < count_;) {
        Particle& p = pool_[i];
        p.age += dt;
        if (p.age >= p.life) {
            pool_[i] = pool_[--count_];  // swap-remove; order is irrelevant
            continue;
        }
        p.velocity += p.acceleration * dt;
        p.velocity *= core::maxf(1.0f - p.drag * dt, 0.0f);
        p.position += p.velocity * dt;
        ++i;
    }
}

bool ParticleSystem::ensure_capacity(uint32_t vertices) {
    if (vertices <= capacity_) return true;
    SDL_GPUDevice* gpu = device_->gpu();
    if (vertex_buffer_) SDL_ReleaseGPUBuffer(gpu, vertex_buffer_);
    if (transfer_) SDL_ReleaseGPUTransferBuffer(gpu, transfer_);

    const uint32_t new_capacity = core::maxf(float(vertices), 1536.0f);
    const uint32_t bytes = new_capacity * uint32_t(sizeof(Vertex));

    SDL_GPUBufferCreateInfo buffer_info = {};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = bytes;
    vertex_buffer_ = SDL_CreateGPUBuffer(gpu, &buffer_info);
    if (!vertex_buffer_) return false;
    SDL_SetGPUBufferName(gpu, vertex_buffer_, "particles");

    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = bytes;
    transfer_ = SDL_CreateGPUTransferBuffer(gpu, &transfer_info);
    if (!transfer_) {
        SDL_ReleaseGPUBuffer(gpu, vertex_buffer_);
        vertex_buffer_ = nullptr;
        return false;
    }
    capacity_ = new_capacity;
    return true;
}

void ParticleSystem::upload(Device& device, const core::Mat4& view_proj, Vec3 camera_right,
                            Vec3 camera_up) {
    view_proj_ = view_proj;
    vertices_.clear();
    for (int i = 0; i < count_; ++i) {
        const Particle& p = pool_[i];
        const float t = p.age / p.life;
        const float size = core::lerpf(p.size_start, p.size_end, t);
        // Bright fast, gone smoothly: quadratic fade reads as cooling.
        const float fade = (1.0f - t) * (1.0f - t);
        const Vec3 color = core::lerp(p.color_start, p.color_end, t) * (p.brightness * fade);
        if (color.x + color.y + color.z < 0.01f) continue;

        const Vec3 right = camera_right * size;
        const Vec3 up = camera_up * size;
        const Vec4 packed{color, 1.0f};
        const Vertex corners[4] = {
            {p.position - right - up, packed, Vec2{-1.0f, -1.0f}},
            {p.position + right - up, packed, Vec2{1.0f, -1.0f}},
            {p.position + right + up, packed, Vec2{1.0f, 1.0f}},
            {p.position - right + up, packed, Vec2{-1.0f, 1.0f}},
        };
        vertices_.push_back(corners[0]);
        vertices_.push_back(corners[1]);
        vertices_.push_back(corners[2]);
        vertices_.push_back(corners[0]);
        vertices_.push_back(corners[2]);
        vertices_.push_back(corners[3]);
    }

    uploaded_ = uint32_t(vertices_.size());
    if (uploaded_ == 0) return;
    if (!ensure_capacity(uploaded_)) {
        uploaded_ = 0;
        return;
    }

    SDL_GPUDevice* gpu = device.gpu();
    void* mapped = SDL_MapGPUTransferBuffer(gpu, transfer_, true);
    if (!mapped) {
        uploaded_ = 0;
        return;
    }
    std::memcpy(mapped, vertices_.data(), uploaded_ * sizeof(Vertex));
    SDL_UnmapGPUTransferBuffer(gpu, transfer_);

    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(device.cmd());
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = transfer_;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = vertex_buffer_;
    dst.size = uploaded_ * uint32_t(sizeof(Vertex));
    SDL_UploadToGPUBuffer(pass, &src, &dst, true);
    SDL_EndGPUCopyPass(pass);
}

void ParticleSystem::draw(Device& device, SDL_GPURenderPass* pass) {
    if (uploaded_ == 0) return;
    SDL_GPUGraphicsPipeline* pipeline = pipelines_->get(pipeline_);
    if (!pipeline) return;

    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_PushGPUVertexUniformData(device.cmd(), 0, &view_proj_, sizeof(core::Mat4));
    SDL_GPUBufferBinding binding = {};
    binding.buffer = vertex_buffer_;
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    SDL_DrawGPUPrimitives(pass, uploaded_, 1, 0, 0);
}

}  // namespace gfx
