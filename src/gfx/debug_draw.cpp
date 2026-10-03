#include "gfx/debug_draw.h"

#include <cstring>

#include "core/log.h"

using core::Mat4;
using core::Quat;
using core::Vec3;

namespace gfx {
namespace {

PipelineDesc make_line_pipeline_desc(bool overlay) {
    PipelineDesc desc;
    desc.name = overlay ? "debug_line_overlay" : "debug_line";
    desc.shader = "debug_line";
    desc.primitive = rhi::Primitive::LineList;
    desc.cull = rhi::Cull::None;
    desc.depth_test = !overlay;
    desc.depth_write = !overlay;

    rhi::VertexBufferLayout vb = {};
    vb.slot = 0;
    vb.pitch = sizeof(float) * 6;
    vb.rate = rhi::InputRate::Vertex;
    desc.vertex_buffers.push_back(vb);

    rhi::VertexAttribute position = {};
    position.location = 0;
    position.buffer_slot = 0;
    position.format = rhi::VertexFormat::Float3;
    position.offset = 0;
    desc.vertex_attributes.push_back(position);

    rhi::VertexAttribute color = {};
    color.location = 1;
    color.buffer_slot = 0;
    color.format = rhi::VertexFormat::Float3;
    color.offset = sizeof(float) * 3;
    desc.vertex_attributes.push_back(color);

    return desc;
}

}  // namespace

bool DebugDraw::init(Device* device, PipelineCache* pipelines) {
    device_ = device;
    pipelines_ = pipelines;
    depth_pipeline_ = pipelines->create(make_line_pipeline_desc(false));
    overlay_pipeline_ = pipelines->create(make_line_pipeline_desc(true));
    depth_tested_.reserve(4096);
    overlay_.reserve(1024);
    return depth_pipeline_ != INVALID_PIPELINE && overlay_pipeline_ != INVALID_PIPELINE;
}

void DebugDraw::shutdown() {
    if (!device_) return;
    device_->rhi().destroy(vertex_buffer_);
    vertex_buffer_ = nullptr;
    capacity_ = 0;
}

void DebugDraw::push(std::vector<Vertex>& list, Vec3 a, Vec3 b, Vec3 color) {
    list.push_back({a.x, a.y, a.z, color.x, color.y, color.z});
    list.push_back({b.x, b.y, b.z, color.x, color.y, color.z});
}

void DebugDraw::line(Vec3 a, Vec3 b, Vec3 color, bool overlay) {
    push(list_for(overlay), a, b, color);
}

void DebugDraw::ray(Vec3 origin, Vec3 direction, Vec3 color, bool overlay) {
    push(list_for(overlay), origin, origin + direction, color);
}

void DebugDraw::arrow(Vec3 from, Vec3 to, Vec3 color, bool overlay) {
    std::vector<Vertex>& list = list_for(overlay);
    push(list, from, to, color);

    Vec3 shaft = to - from;
    float len = core::length(shaft);
    if (len < 1e-5f) return;
    Vec3 dir = shaft / len;

    // Any two vectors perpendicular to the shaft will do for the head.
    Vec3 side = core::cross(dir, Vec3::up());
    if (core::length_sq(side) < 1e-6f) side = core::cross(dir, Vec3::unit_x());
    side = core::normalize(side);
    Vec3 other = core::cross(dir, side);

    const float head = len * 0.15f;
    Vec3 base = to - dir * head;
    push(list, to, base + side * head * 0.5f, color);
    push(list, to, base - side * head * 0.5f, color);
    push(list, to, base + other * head * 0.5f, color);
    push(list, to, base - other * head * 0.5f, color);
}

void DebugDraw::cross(Vec3 center, float size, Vec3 color, bool overlay) {
    std::vector<Vertex>& list = list_for(overlay);
    push(list, center - Vec3::unit_x() * size, center + Vec3::unit_x() * size, color);
    push(list, center - Vec3::unit_y() * size, center + Vec3::unit_y() * size, color);
    push(list, center - Vec3::unit_z() * size, center + Vec3::unit_z() * size, color);
}

void DebugDraw::circle(Vec3 center, Vec3 normal, float radius, Vec3 color, int segments,
                       bool overlay) {
    if (segments < 3) segments = 3;
    Vec3 n = core::normalize_or(normal, Vec3::up());
    Vec3 tangent = core::cross(n, Vec3::up());
    if (core::length_sq(tangent) < 1e-6f) tangent = core::cross(n, Vec3::unit_x());
    tangent = core::normalize(tangent);
    Vec3 bitangent = core::cross(n, tangent);

    std::vector<Vertex>& list = list_for(overlay);
    Vec3 previous = center + tangent * radius;
    for (int i = 1; i <= segments; ++i) {
        float angle = core::TWO_PI * float(i) / float(segments);
        Vec3 point = center + (tangent * std::cos(angle) + bitangent * std::sin(angle)) * radius;
        push(list, previous, point, color);
        previous = point;
    }
}

void DebugDraw::sphere(Vec3 center, float radius, Vec3 color, int segments, bool overlay) {
    circle(center, Vec3::unit_x(), radius, color, segments, overlay);
    circle(center, Vec3::unit_y(), radius, color, segments, overlay);
    circle(center, Vec3::unit_z(), radius, color, segments, overlay);
}

void DebugDraw::box(Vec3 center, Vec3 half_extents, Quat rotation, Vec3 color, bool overlay) {
    Vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        Vec3 local{(i & 1) ? half_extents.x : -half_extents.x,
                   (i & 2) ? half_extents.y : -half_extents.y,
                   (i & 4) ? half_extents.z : -half_extents.z};
        corners[i] = center + core::rotate(rotation, local);
    }
    // Each edge connects corners differing in exactly one bit.
    static const int EDGES[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                     {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    std::vector<Vertex>& list = list_for(overlay);
    for (const auto& edge : EDGES) push(list, corners[edge[0]], corners[edge[1]], color);
}

void DebugDraw::axes(Vec3 position, Quat rotation, float scale, bool overlay) {
    std::vector<Vertex>& list = list_for(overlay);
    push(list, position, position + core::quat_right(rotation) * scale, Vec3{1.0f, 0.25f, 0.25f});
    push(list, position, position + core::quat_up(rotation) * scale, Vec3{0.3f, 1.0f, 0.3f});
    push(list, position, position + core::quat_forward(rotation) * scale, Vec3{0.35f, 0.5f, 1.0f});
}

void DebugDraw::transform_axes(const core::Transform& transform, float scale, bool overlay) {
    axes(transform.position, transform.rotation, scale, overlay);
}

void DebugDraw::grid(float half_extent, float spacing, Vec3 color, int major_every) {
    if (spacing <= 0.0f) return;
    const int lines = int(half_extent / spacing);
    const Vec3 major = color * 2.0f;

    for (int i = -lines; i <= lines; ++i) {
        float offset = float(i) * spacing;
        bool is_major = (major_every > 0) && (i % major_every == 0);
        Vec3 c = is_major ? major : color;
        push(depth_tested_, Vec3{offset, 0, -half_extent}, Vec3{offset, 0, half_extent}, c);
        push(depth_tested_, Vec3{-half_extent, 0, offset}, Vec3{half_extent, 0, offset}, c);
    }

    // Axis lines through the origin, so world orientation is never ambiguous.
    push(depth_tested_, Vec3{-half_extent, 0, 0}, Vec3{half_extent, 0, 0}, Vec3{0.7f, 0.2f, 0.2f});
    push(depth_tested_, Vec3{0, 0, -half_extent}, Vec3{0, 0, half_extent}, Vec3{0.25f, 0.4f, 0.8f});
}

bool DebugDraw::ensure_capacity(uint32_t vertex_count) {
    if (vertex_count <= capacity_) return true;

    // Grow in powers of two so a busy frame does not reallocate repeatedly.
    uint32_t new_capacity = capacity_ > 0 ? capacity_ : 4096;
    while (new_capacity < vertex_count) new_capacity *= 2;

    rhi::Device& rhi = device_->rhi();
    rhi.destroy(vertex_buffer_);
    capacity_ = 0;
    vertex_buffer_ = rhi.create_buffer(rhi::BufferUsage::Vertex, new_capacity * uint32_t(sizeof(Vertex)),
                                       nullptr, "debug_lines");
    if (!vertex_buffer_) return false;

    capacity_ = new_capacity;
    LOG_INFO("debug line capacity -> %u vertices", capacity_);
    return true;
}

void DebugDraw::upload(Device& device) {
    uploaded_depth_ = uint32_t(depth_tested_.size());
    uploaded_overlay_ = uint32_t(overlay_.size());
    const uint32_t total = uploaded_depth_ + uploaded_overlay_;
    if (total == 0) return;
    if (!ensure_capacity(total)) {
        uploaded_depth_ = uploaded_overlay_ = 0;
        return;
    }

    const uint32_t bytes = total * uint32_t(sizeof(Vertex));
    void* mapped = device.rhi().map_upload(vertex_buffer_, bytes);
    if (!mapped) {
        uploaded_depth_ = uploaded_overlay_ = 0;
        return;
    }
    Vertex* out = static_cast<Vertex*>(mapped);
    if (uploaded_depth_ > 0)
        std::memcpy(out, depth_tested_.data(), uploaded_depth_ * sizeof(Vertex));
    if (uploaded_overlay_ > 0)
        std::memcpy(out + uploaded_depth_, overlay_.data(), uploaded_overlay_ * sizeof(Vertex));
    device.rhi().commit_upload(vertex_buffer_, bytes);
}

void DebugDraw::draw(Device& device, rhi::Pass* pass, const Mat4& view_proj) {
    // Cleared unconditionally: a frame that fails to draw must not leak its
    // geometry into the next one.
    struct Clear {
        std::vector<Vertex>& a;
        std::vector<Vertex>& b;
        ~Clear() {
            a.clear();
            b.clear();
        }
    } clear{depth_tested_, overlay_};

    if (!pass || !vertex_buffer_) return;
    if (uploaded_depth_ == 0 && uploaded_overlay_ == 0) return;

    // Resolved every frame rather than cached, so a hot-reloaded shader is
    // picked up automatically. Either may be null if its shader is broken.
    rhi::Pipeline* depth_pipeline = pipelines_->get(depth_pipeline_);
    rhi::Pipeline* overlay_pipeline = pipelines_->get(overlay_pipeline_);

    rhi::BufferBinding binding = {};
    binding.buffer = vertex_buffer_;
    binding.offset = 0;
    device.rhi().bind_vertex_buffers(pass, 0, &binding, 1);
    device.rhi().push_uniforms(rhi::Stage::Vertex, 0, &view_proj, sizeof(Mat4));

    if (depth_pipeline && uploaded_depth_ > 0) {
        device.rhi().bind_pipeline(pass, depth_pipeline);
        device.rhi().draw(pass, uploaded_depth_, 1, 0, 0);
    }
    if (overlay_pipeline && uploaded_overlay_ > 0) {
        device.rhi().bind_pipeline(pass, overlay_pipeline);
        // Overlay vertices live directly after the depth-tested ones.
        device.rhi().draw(pass, uploaded_overlay_, 1, uploaded_depth_, 0);
    }
}

}  // namespace gfx
