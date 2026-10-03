#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "gfx/device.h"
#include "gfx/pipeline.h"

namespace gfx {

// CPU-simulated, GPU-billboarded particles, drawn additively: fire, sparks,
// smoke-lit-by-fire. Additive because these are light sources, not surfaces --
// they sum, they never occlude, and draw order stops mattering, which is what
// makes one unsorted draw call correct.
struct Particle {
    core::Vec3 position = core::Vec3::zero();
    core::Vec3 velocity = core::Vec3::zero();
    // Constant per-particle acceleration: buoyancy for flame, gravity for
    // debris. Cheap and covers everything fire needs.
    core::Vec3 acceleration = core::Vec3::zero();
    float drag = 0.0f;  // fraction of velocity lost per second
    float age = 0.0f;
    float life = 1.0f;
    float size_start = 1.0f;
    float size_end = 2.0f;
    // Colour is emitted light (additive), fading to black over life via the
    // brightness curve; a separate end colour lets fire cool as it dies.
    core::Vec3 color_start = core::Vec3::one();
    core::Vec3 color_end = core::Vec3::one();
    float brightness = 1.0f;
};

class ParticleSystem {
public:
    bool init(Device* device, PipelineCache* pipelines);
    void shutdown();

    void spawn(const Particle& particle);
    void update(float dt);
    void clear() { count_ = 0; }
    int alive() const { return count_; }

    // Build camera-facing quads and stage the upload; call before the render
    // pass, then draw() inside it.
    void upload(Device& device, const core::Mat4& view_proj, core::Vec3 camera_right,
                core::Vec3 camera_up);
    void draw(Device& device, rhi::Pass* pass);

private:
    struct Vertex {
        core::Vec3 position;
        core::Vec4 color;  // rgb emitted light, a = distance falloff strength
        core::Vec2 corner;
    };

    bool ensure_capacity(uint32_t vertices);

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle pipeline_ = INVALID_PIPELINE;

    // Fixed-capacity pool, swap-remove on death: a dogfight makes and destroys
    // hundreds of these per second and the pool must never allocate mid-frame.
    static constexpr int MAX_PARTICLES = 4096;
    std::vector<Particle> pool_;
    int count_ = 0;

    std::vector<Vertex> vertices_;
    uint32_t uploaded_ = 0;
    rhi::Buffer* vertex_buffer_ = nullptr;
    uint32_t capacity_ = 0;
    core::Mat4 view_proj_ = core::Mat4::identity();
};

}  // namespace gfx
