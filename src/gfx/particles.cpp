#include "gfx/particles.h"

#include <algorithm>
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
    // The shape (seed, age, kind) on the shader tiers; fixed function's
    // vertex layout stays as R5 measured it.
    if (!active_tier().fixed_function) {
        desc.vertex_attributes.push_back(attribute(3, rhi::VertexFormat::Float4, offsetof(Vertex, shape)));
    }

    // On the shader tiers light is premultiplied: a disc's alpha is 0, so it
    // adds as before; a flame's is its body, so it also hides some of the
    // ground behind it and keeps its orange over a sunlit slope. Fixed
    // function's stage writes alpha 1 and stays additive.
    desc.premultiplied_blend = !active_tier().fixed_function;
    pipeline_ = pipelines->create(desc);
    desc.premultiplied_blend = false;
    if (!active_tier().fixed_function) {
        // Smoke is a surface, not light: blended over what is behind it, still
        // writing no depth, before the additive light so fire glows over it.
        PipelineDesc smoke = desc;
        smoke.name = "particles_smoke";
        smoke.additive_blend = false;
        smoke.alpha_blend = true;
        smoke_pipeline_ = pipelines->create(smoke);
    }
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

void ParticleSystem::spawn(const Particle& source) {
    Particle particle = source;
    if (density_ < 1.0f) {
        density_owed_ += density_;
        if (density_owed_ < 1.0f) return;
        density_owed_ -= 1.0f;
        const float grow = 1.0f / std::sqrt(core::maxf(density_, 0.25f));
        particle.size_start *= grow;
        particle.size_end *= grow;
    }
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
    const bool smoke_drawn = smoke_pipeline_ != INVALID_PIPELINE;
    // Back to front: flame and smoke cover what is behind them, so order
    // matters now (a disc's alpha is 0 and adds in any order). A few hundred
    // puffs, sorted by depth along the view.
    const Vec3 camera_forward = core::cross(camera_up, camera_right);
    order_.resize(size_t(count_));
    for (int i = 0; i < count_; ++i) order_[size_t(i)] = i;
    std::sort(order_.begin(), order_.end(), [&](int a, int b) {
        return core::dot(pool_[a].position, camera_forward) > core::dot(pool_[b].position, camera_forward);
    });
    // Smoke first, then light: two draws from one buffer.
    for (int pass = 0; pass < 2; ++pass) {
        const bool smoke_pass = pass == 0;
        if (smoke_pass && !smoke_drawn) continue;
        for (const int i : order_) {
            const Particle& p = pool_[i];
            if ((p.kind == ParticleKind::Smoke) != smoke_pass) continue;
            const float t = p.age / p.life;
            const float size = core::lerpf(p.size_start, p.size_end, t);
            Vec4 packed;
            if (smoke_pass) {
                // In after the flame it follows, out slowly as it spreads.
                const float in = p.fade_in > 0.0f ? core::smoothstep(p.fade_in * 0.7f, p.fade_in, t) : 1.0f;
                const float opacity = p.brightness * in * (1.0f - t);
                if (opacity < 0.005f) continue;
                packed = Vec4{core::lerp(p.color_start, p.color_end, t), opacity};
            } else {
                // Bright fast, gone smoothly: quadratic fade reads as cooling.
                const float fade = (1.0f - t) * (1.0f - t);
                const Vec3 color = core::lerp(p.color_start, p.color_end, t) * (p.brightness * fade);
                if (color.x + color.y + color.z < 0.01f) continue;
                packed = Vec4{color, 1.0f};
            }
            Vec3 right = camera_right * size;
            Vec3 up = camera_up * size;
            if (p.kind == ParticleKind::Flame) {
                // Stretched along its motion across the screen: a fast young
                // puff is a streak, so the breath leaves the jaws as a jet and
                // slows into billows, rather than a row of balls.
                const Vec3 across = p.velocity - camera_forward * core::dot(p.velocity, camera_forward);
                const float speed = core::length(across);
                if (speed > 1.0f) {
                    const Vec3 along = across / speed;
                    const Vec3 side = core::normalize_or(core::cross(camera_forward, along), camera_right);
                    const float stretch = 1.0f + core::minf(speed * 0.03f, 1.5f);
                    up = along * (size * stretch);
                    right = side * size;
                }
            }
            const Vec4 shape{p.seed, t, float(int(p.kind)), 0.0f};
            const Vertex corners[4] = {
                {p.position - right - up, packed, Vec2{-1.0f, -1.0f}, shape},
                {p.position + right - up, packed, Vec2{1.0f, -1.0f}, shape},
                {p.position + right + up, packed, Vec2{1.0f, 1.0f}, shape},
                {p.position - right + up, packed, Vec2{-1.0f, 1.0f}, shape},
            };
            vertices_.push_back(corners[0]);
            vertices_.push_back(corners[1]);
            vertices_.push_back(corners[2]);
            vertices_.push_back(corners[0]);
            vertices_.push_back(corners[2]);
            vertices_.push_back(corners[3]);
        }
        if (smoke_pass) smoke_vertices_ = uint32_t(vertices_.size());
    }
    if (!smoke_drawn) smoke_vertices_ = 0;

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

    rhi::BufferBinding binding = {};
    binding.buffer = vertex_buffer_;
    rhi::Pipeline* smoke = smoke_vertices_ > 0 ? pipelines_->get(smoke_pipeline_) : nullptr;
    if (smoke) {
        device.rhi().bind_pipeline(pass, smoke);
        device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &uniforms_, sizeof(Uniforms));
        device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &uniforms_, sizeof(Uniforms));
        device.rhi().bind_vertex_buffers(pass, 0, &binding, 1);
        device.rhi().draw(pass, smoke_vertices_, 1, 0, 0);
    }
    const uint32_t light_first = smoke ? smoke_vertices_ : 0;
    if (uploaded_ <= light_first) return;
    device.rhi().bind_pipeline(pass, pipeline);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &uniforms_, sizeof(Uniforms));
    device.rhi().push_uniforms(rhi::Stage::Fragment, 0, &uniforms_, sizeof(Uniforms));
    if (disc_texture_ && disc_sampler_) {
        rhi::TextureBinding disc = {};
        disc.texture = disc_texture_;
        disc.sampler = disc_sampler_;
        device.rhi().bind_fragment_textures(pass, 0, &disc, 1);
    }
    device.rhi().bind_vertex_buffers(pass, 0, &binding, 1);
    device.rhi().draw(pass, uploaded_ - light_first, 1, light_first, 0);
}

}  // namespace gfx
