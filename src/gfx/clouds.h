#pragma once

// Volumetric clouds (docs/FIDELITY.md, F6), on the modern tier: the cumulus
// slab the sky used to paint (sky.hlsl, atmo_clouds), now marched through
// as a volume after the main pass, so clouds stand in front of mountains,
// the dragon flies into them and out, and from above they are a floor.
//
// Each frame, between the opaque world and the particles:
//
//   1. the weather map: the layer's coverage over 80 km about the camera,
//      one fetch for the march instead of four octaves of noise
//      (shaders/clouds_weather.hlsl);
//   2. the march, at half resolution: the view ray through the slab to the
//      nearest surface, each step lit by a short march toward the sun, from
//      a tiling Perlin-Worley volume built here at init
//      (shaders/clouds_march.hlsl);
//   3. the resolve: the march blended into its history, reprojected through
//      where the ray enters the slab, so a few dozen dithered steps
//      converge (shaders/clouds_resolve.hlsl);
//   4. the apply: the result over the scene target at full resolution,
//      except where a surface stands nearer than the slab
//      (shaders/clouds_apply.hlsl).

#include <string>

#include "core/math.h"
#include "gfx/camera.h"
#include "gfx/pipeline.h"
#include "gfx/scene_uniforms.h"

namespace gfx {

class Device;
class Atmosphere;

struct CloudSettings {
    bool enabled = true;
    int steps = 48;              // along the view ray, at most
    float extinction = 0.06f;    // per metre at density 1
    float base_scale = 5500.0f;  // metres a period of the shape noise
    float detail_scale = 650.0f; // metres a period of the erosion noise
    float detail = 0.5f;         // how far the erosion eats the edges
    float softness = 0.3f;       // the coverage's ramp, 0..1
    float history = 0.9f;        // the history's weight each frame
    float sun_gain = 1.0f;
    float sky_gain = 1.0f;

    // `--clouds key=value,...`: on, steps, extinction, shape, detail_scale,
    // erosion, softness, sun, sky, history.
    void parse(const std::string& list);
};

class Clouds {
public:
    bool init(Device* device, PipelineCache* pipelines);
    void shutdown();
    bool available() const { return available_; }
    bool active(const SceneUniforms& scene, const CloudSettings& settings) const {
        return available_ && settings.enabled && scene.clouds.x > 0.0f;
    }

    // Steps 1 to 3, outside any pass, after the main pass's opaque draws;
    // then apply() inside a pass on the scene target.
    void render(Device& device, const Camera& camera, float aspect, const SceneUniforms& scene,
                const Atmosphere& atmosphere, const CloudSettings& settings);
    void apply(Device& device, rhi::Pass* pass, const Camera& camera, const SceneUniforms& scene);

private:
    struct Uniforms {
        core::Mat4 prev_view_proj;  // camera-relative, last frame's
        core::Vec4 camera_delta;    // xyz this camera minus last frame's, w the history's weight (0: reset)
        core::Vec4 params;          // x the near plane, y frame index, z steps, w extinction
        core::Vec4 texel;           // xy one half-resolution texel, zw one full-resolution texel (uv)
        core::Vec4 weather;         // xy the weather map's centre (world xz), z its half extent, w its texel (m)
        core::Vec4 shape;           // x base scale, y detail scale, z detail, w softness (see CloudSettings)
        core::Vec4 light;           // x sun gain, y sky gain
    };
    bool ensure_targets(Device& device, uint32_t width, uint32_t height);
    Uniforms uniforms_for(const Camera& camera, const CloudSettings& settings) const;

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle weather_pipeline_ = INVALID_PIPELINE;
    PipelineHandle march_pipeline_ = INVALID_PIPELINE;
    PipelineHandle resolve_pipeline_ = INVALID_PIPELINE;
    PipelineHandle apply_pipeline_ = INVALID_PIPELINE;
    rhi::Sampler* point_ = nullptr;
    rhi::Sampler* linear_ = nullptr;
    rhi::Sampler* wrap_ = nullptr;
    rhi::Texture* noise_ = nullptr;    // the tiling volume, slices side by side (see clouds.cpp)
    rhi::Texture* weather_ = nullptr;  // r the coverage
    rhi::Texture* raw_ = nullptr;      // this frame's march: rgb light, a transmittance
    rhi::Texture* history_[2] = {nullptr, nullptr};
    uint32_t width_ = 0, height_ = 0;
    int write_ = 0;
    bool valid_ = false;
    bool available_ = false;
    uint32_t frame_ = 0;
    core::Vec2 weather_centre_ = {};
    core::Mat4 prev_view_proj_ = core::Mat4::identity();
    core::Vec3 prev_camera_ = {};
    core::Vec3 prev_forward_ = {};
    bool have_prev_ = false;
    Uniforms last_ = {};
};

}  // namespace gfx
