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
// What a particle is drawn as (particles.hlsl). The shader tiers alone tell
// them apart; fixed function draws every light as the disc and no smoke.
enum class ParticleKind : uint8_t {
    Disc = 0,   // a soft glowing disc: embers, sparks, flashes
    Flame = 1,  // a turbulent tongue that frays as it ages: a breath's puffs
    Smoke = 2,  // not light: alpha-blended, drawn before the light over it
};

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
    ParticleKind kind = ParticleKind::Disc;
    // 0..1, a puff's own noise and turn, so no two look alike.
    float seed = 0.0f;
    // Smoke: the share of its life over which it fades in, so a puff
    // launched with the flame shows only past where the flame dies.
    float fade_in = 0.0f;
};

class ParticleSystem {
public:
    bool init(Device* device, PipelineCache* pipelines);
    void shutdown();

    void spawn(const Particle& particle);
    // The Particles setting (ContentBudget::particle_density): below 1, that
    // fraction of spawns is kept -- every other one at Lowest, counted, not
    // drawn at random, so a seed builds the same effects everywhere -- each
    // a little larger so a plume still reads as one.
    void set_density(float density) { density_ = density; }
    void update(float dt);
    void clear() { count_ = 0; }
    int alive() const { return count_; }

    // Build camera-facing quads and stage the upload; call before the render
    // pass, then draw() inside it.
    // The in-shader grade an LDR tier finishes additive light with (see
    // SceneUniforms::output_grade).
    void set_output_grade(core::Vec4 grade) { uniforms_.output_grade = grade; }
    void upload(Device& device, const core::Mat4& view_proj, core::Vec3 camera_right,
                core::Vec3 camera_up);
    void draw(Device& device, rhi::Pass* pass);

private:
    struct Vertex {
        core::Vec3 position;
        core::Vec4 color;  // rgb emitted light (smoke: its colour), a smoke's opacity
        core::Vec2 corner;
        core::Vec4 shape;  // x seed, y age over life, z kind; the shader tiers' alone
    };

    bool ensure_capacity(uint32_t vertices);

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle pipeline_ = INVALID_PIPELINE;
    PipelineHandle smoke_pipeline_ = INVALID_PIPELINE;  // the shader tiers'
    uint32_t smoke_vertices_ = 0;  // the first ones uploaded, drawn first

    // Fixed-capacity pool, swap-remove on death: a dogfight makes and destroys
    // hundreds of these per second and the pool must never allocate mid-frame.
    static constexpr int MAX_PARTICLES = 4096;
    std::vector<Particle> pool_;
    int count_ = 0;
    float density_ = 1.0f;
    float density_owed_ = 0.0f;

    std::vector<Vertex> vertices_;
    std::vector<int> order_;  // the pool's indices, back to front
    uint32_t uploaded_ = 0;
    rhi::Buffer* vertex_buffer_ = nullptr;
    uint32_t capacity_ = 0;
    // Must match ParticleUniforms in shaders/particles.hlsl.
    struct Uniforms {
        core::Mat4 view_proj = core::Mat4::identity();
        core::Vec4 output_grade = core::Vec4{1.0f, 1.0f, 1.0f, 0.0f};  // as SceneUniforms'
    };
    Uniforms uniforms_;
    // The fixed-function tier's disc (particles.hlsl, FIXED_FUNCTION): the
    // (1 - r)^2 falloff the pixel stage computes, baked into a texture a
    // stage can modulate by.
    rhi::Texture* disc_texture_ = nullptr;
    rhi::Sampler* disc_sampler_ = nullptr;
};

}  // namespace gfx
