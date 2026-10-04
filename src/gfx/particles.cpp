#include "gfx/particles.h"

#include <cstring>
#include "gfx/texture.h"
#include "gfx/render_tier.h"
#include <cmath>

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
    desc.shader = "particles";
    desc.additive_blend = true;
    desc.depth_test = true;
    // Light does not write depth: particles never occlude anything, including
    // each other, which is also why they need no sorting.
    desc.depth_write = false;
    desc.cull = rhi::Cull::None;

    rhi::VertexBufferLayout buffer = {};
    buffer.slot = 0;
    buffer.pitch = sizeof(Vertex);
    buffer.rate = rhi::InputRate::Vertex;
    desc.vertex_buffers = {buffer};

    auto attribute = [](uint32_t location, rhi::VertexFormat format, uint32_t offset) {
        rhi::VertexAttribute a = {};
        a.location = location;
        a.buffer_slot = 0;
        a.format = format;
        a.offset = offset;
        return a;
    };
    desc.vertex_attributes = {
        attribute(0, rhi::VertexFormat::Float3, offsetof(Vertex, position)),
        attribute(1, rhi::VertexFormat::Float4, offsetof(Vertex, color)),
        attribute(2, rhi::VertexFormat::Float2, offsetof(Vertex, corner)),
    };

    pipeline_ = pipelines->create(desc);
    if (active_tier().fixed_function) {
        constexpr int N = 64;
        ImageData disc;
        disc.width = disc.height = N;
        disc.rgba.resize(size_t(N) * N * 4);
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                const float u = (float(x) + 0.5f) / N * 2.0f - 1.0f, v = (float(y) + 0.5f) / N * 2.0f - 1.0f;
                float falloff = std::max(0.0f, 1.0f - std::sqrt(u * u + v * v));
                falloff *= falloff;
                const uint8_t value = uint8_t(std::lround(falloff * 255.0f));
                uint8_t* p = &disc.rgba[(size_t(y) * N + x) * 4];
                p[0] = p[1] = p[2] = value;
                p[3] = 255;
            }
        }
        disc_texture_ = create_texture_from_image(device->rhi(), disc, "particle_disc", false);
        rhi::SamplerDesc sampler;
        sampler.address_u = sampler.address_v = sampler.address_w = rhi::Address::Clamp;
        disc_sampler_ = device->rhi().create_sampler(sampler);
    }
    return pipeline_ != INVALID_PIPELINE;
}

void ParticleSystem::shutdown() {
    if (!device_) return;
    device_->rhi().destroy(disc_texture_);
    device_->rhi().destroy(disc_sampler_);
    disc_texture_ = nullptr;
    disc_sampler_ = nullptr;
    device_->rhi().destroy(vertex_buffer_);
    vertex_buffer_ = nullptr;
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
    rhi::Device& rhi = device_->rhi();
    rhi.destroy(vertex_buffer_);
    const uint32_t new_capacity = core::maxf(float(vertices), 1536.0f);
    vertex_buffer_ = rhi.create_buffer(rhi::BufferUsage::Vertex, new_capacity * uint32_t(sizeof(Vertex)),
                                       nullptr, "particles");
    if (!vertex_buffer_) {
        capacity_ = 0;
        return false;
    }
    capacity_ = new_capacity;
    return true;
}

void ParticleSystem::upload(Device& device, const core::Mat4& view_proj, Vec3 camera_right,
                            Vec3 camera_up) {
    uniforms_.view_proj = view_proj;
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

    const uint32_t bytes = uploaded_ * uint32_t(sizeof(Vertex));
    void* mapped = device.rhi().map_upload(vertex_buffer_, bytes);
    if (!mapped) {
        uploaded_ = 0;
        return;
    }
    std::memcpy(mapped, vertices_.data(), bytes);
    device.rhi().commit_upload(vertex_buffer_, bytes);
}

void ParticleSystem::draw(Device& device, rhi::Pass* pass) {
    if (uploaded_ == 0) return;
    rhi::Pipeline* pipeline = pipelines_->get(pipeline_);
    if (!pipeline) return;

    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &uniforms_, sizeof(Uniforms));
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &uniforms_, sizeof(Uniforms));
    if (disc_texture_ && disc_sampler_) {
        rhi::TextureBinding disc = {};
        disc.texture = disc_texture_;
        disc.sampler = disc_sampler_;
        device.rhi().bind_fragment_textures(pass, 0, &disc, 1);
    }
    rhi::BufferBinding binding = {};
    binding.buffer = vertex_buffer_;
    device.rhi().bind_vertex_buffers(pass, 0, &binding, 1);
    device.rhi().draw(pass, uploaded_, 1, 0, 0);
}

}  // namespace gfx
