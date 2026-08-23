#pragma once

#include <cstdint>
#include <vector>

#include "core/math.h"
#include "gfx/device.h"
#include "gfx/pipeline.h"

namespace gfx {

// Immediate-mode line drawing for diagnosing everything else in the engine:
// flight forces, IK chains, camera spring arms, AI steering vectors, physics
// raycasts. Call the shape functions anywhere during the frame, then upload()
// before the render pass and draw() inside it.
//
// Everything is lines. Solid debug shapes look better but read worse -- with
// wireframe you can see the geometry that matters through the shape.
class DebugDraw {
public:
    bool init(Device* device, PipelineCache* pipelines);
    void shutdown();

    // `overlay` shapes skip the depth test, so they stay visible through
    // terrain. Use it for things you must never lose track of (the dragon's
    // velocity vector, an off-screen target) and leave it off otherwise, since
    // occlusion is itself information.
    void line(core::Vec3 a, core::Vec3 b, core::Vec3 color, bool overlay = false);
    void ray(core::Vec3 origin, core::Vec3 direction, core::Vec3 color, bool overlay = false);
    void arrow(core::Vec3 from, core::Vec3 to, core::Vec3 color, bool overlay = false);
    void cross(core::Vec3 center, float size, core::Vec3 color, bool overlay = false);

    // Three orthogonal circles. Cheap stand-in for a sphere.
    void sphere(core::Vec3 center, float radius, core::Vec3 color, int segments = 24,
                bool overlay = false);
    void circle(core::Vec3 center, core::Vec3 normal, float radius, core::Vec3 color,
                int segments = 24, bool overlay = false);
    void box(core::Vec3 center, core::Vec3 half_extents, core::Quat rotation, core::Vec3 color,
             bool overlay = false);

    // Red/green/blue for right/up/forward. The fastest way to spot a
    // handedness or ordering mistake in a transform.
    void axes(core::Vec3 position, core::Quat rotation, float scale, bool overlay = false);
    void transform_axes(const core::Transform& transform, float scale, bool overlay = false);

    // Ground grid on the XZ plane, with every `major_every`-th line brightened.
    void grid(float half_extent, float spacing, core::Vec3 color, int major_every = 10);

    // Uploads this frame's vertices. Must run before Device::begin_main_pass,
    // because it issues a copy pass.
    void upload(Device& device);

    // Draws and then clears the accumulated geometry.
    void draw(Device& device, SDL_GPURenderPass* pass, const core::Mat4& view_proj);

    int line_count() const { return int((depth_tested_.size() + overlay_.size()) / 2); }

private:
    struct Vertex {
        float x, y, z;
        float r, g, b;
    };

    std::vector<Vertex>& list_for(bool overlay) { return overlay ? overlay_ : depth_tested_; }
    void push(std::vector<Vertex>& list, core::Vec3 a, core::Vec3 b, core::Vec3 color);
    bool ensure_capacity(uint32_t vertex_count);

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle depth_pipeline_ = INVALID_PIPELINE;
    PipelineHandle overlay_pipeline_ = INVALID_PIPELINE;

    std::vector<Vertex> depth_tested_;
    std::vector<Vertex> overlay_;

    // One growable vertex buffer holding both lists back to back.
    SDL_GPUBuffer* vertex_buffer_ = nullptr;
    SDL_GPUTransferBuffer* transfer_ = nullptr;
    uint32_t capacity_ = 0;
    uint32_t uploaded_depth_ = 0;
    uint32_t uploaded_overlay_ = 0;
};

}  // namespace gfx
