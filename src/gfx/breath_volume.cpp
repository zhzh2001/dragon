#include "gfx/breath_volume.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/log.h"
#include "gfx/camera.h"
#include "gfx/device.h"
#include "gfx/render_tier.h"

namespace gfx {

void BreathVolumeSettings::parse(const std::string& list) {
    size_t start = 0;
    while (start < list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos) end = list.size();
        const std::string item = list.substr(start, end - start);
        start = end + 1;
        const size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = item.substr(0, eq);
        const float value = float(std::atof(item.c_str() + eq + 1));
        struct Field {
            const char* key;
            float* value;
        };
        const Field fields[] = {{"mouth_radius", &mouth_radius}, {"end_radius", &end_radius}, {"mouth_forward", &mouth_forward},
                                {"extinction", &extinction},     {"noise_size", &noise_size},
                                {"erosion", &erosion},           {"flow", &flow},
                                {"core", &core},                 {"gain", &gain},
                                {"smoke", &smoke}};
        bool found = false;
        if (key == "on") {
            enabled = value != 0.0f;
            found = true;
        } else if (key == "steps") {
            steps = int(value);
            found = true;
        }
        for (const Field& f : fields) {
            if (key == f.key) {
                *f.value = value;
                found = true;
            }
        }
        if (!found) LOG_WARN("--breath: unknown key '%s'", key.c_str());
    }
}

bool BreathVolume::init(Device* device, PipelineCache* pipelines) {
    device_ = device;
    pipelines_ = pipelines;
    available_ = active_tier().tier == Tier::Modern && device->hdr() && device->depth_sampled();
    if (!available_) return true;

    PipelineDesc desc;
    desc.name = "breath_volume";
    desc.shader = "breath_volume";
    desc.cull = rhi::Cull::None;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.color_format = device->scene_hdr_format();
    desc.no_depth_target = true;
    desc.alpha_blend = true;  // light + scene x transmittance (breath_volume.hlsl)
    pipeline_ = pipelines->create(desc);

    rhi::SamplerDesc info;
    info.min_filter = info.mag_filter = rhi::Filter::Nearest;
    info.mip_mode = rhi::MipMode::Nearest;
    info.address_u = info.address_v = info.address_w = rhi::Address::Clamp;
    info.max_lod = 0.0f;
    point_ = device->rhi().create_sampler(info);
    available_ = point_ != nullptr;
    return true;
}

void BreathVolume::shutdown() {
    if (device_ && point_) device_->rhi().destroy(point_);
    point_ = nullptr;
    available_ = false;
    nodes_.clear();
}

void BreathVolume::clear() {
    nodes_.clear();
    owed_ = 0.0f;
    fed_ = fed_last_ = false;
}

void BreathVolume::feed(core::Vec3 mouth, core::Vec3 direction, core::Vec3 carry, const BreathJet& jet, float dt) {
    jet_ = jet;
    mouth_ = mouth;
    direction_ = core::normalize_or(direction, core::Vec3::forward());
    fed_ = true;

    // The same solve as a puff's (App::emit_flame): with the speed decaying
    // as e^(-kt), a node let go at this speed travels the range in its life.
    const float life = std::max(jet.life, 0.05f);
    const float drag = std::max(jet.drag, 1e-3f);
    const float speed = jet.range * drag / (1.0f - std::exp(-drag * life));

    // One node short of the array each life, so the mouth has its place.
    const float interval = life / float(kMaxNodes - 2);
    const bool restart = !fed_last_;
    owed_ += dt;
    if (restart) owed_ = interval;  // the first node of a new strand, now
    while (owed_ >= interval) {
        owed_ -= interval;
        Node node;
        node.velocity = direction_ * speed;
        node.carry = carry;
        // Let go `owed_` seconds ago: where it would be by now, so nodes are
        // evenly spaced along the jet whatever the frame's length.
        node.position = mouth_ + (node.velocity + node.carry) * owed_;
        node.age = owed_;
        nodes_.insert(nodes_.begin(), node);
    }
    // A restart lets go exactly one node above: it begins a strand, not
    // joined to whatever of the last breath is still in the air.
    if (restart && !nodes_.empty()) nodes_.front().strand_start = true;
    if (int(nodes_.size()) > kMaxNodes - 1) nodes_.resize(size_t(kMaxNodes - 1));
}

void BreathVolume::update(float dt) {
    const float decay = std::exp(-jet_.drag * dt);
    for (Node& node : nodes_) {
        node.velocity = node.velocity * decay;
        node.velocity.y += jet_.buoyancy * dt;
        node.position = node.position + (node.velocity + node.carry) * dt;
        node.age += dt;
    }
    const float life = std::max(jet_.life, 0.05f);
    while (!nodes_.empty() && nodes_.back().age >= life) nodes_.pop_back();
    fed_last_ = fed_;
    fed_ = false;
}

void BreathVolume::draw(Device& device, rhi::Pass* pass, const Camera& camera, const SceneUniforms& scene,
                        const BreathVolumeSettings& settings, float time_seconds) {
    rhi::Pipeline* pipeline = pipelines_ ? pipelines_->get(pipeline_) : nullptr;
    if (!pass || !pipeline || !available_ || !device.depth()) return;

    const float life = std::max(jet_.life, 0.05f);
    const float drag = std::max(jet_.drag, 1e-3f);
    // As wide at the tip as the puffs spread: their own size and their
    // scatter over the range. On size alone the jet was a lance two degrees
    // across, hidden behind the head from the chase camera.
    const float end_radius =
        std::max((jet_.size_end + jet_.range * jet_.spread * 0.6f) * settings.end_radius, settings.mouth_radius);
    auto radius_at = [&](float age) {
        // A cone: the radius grows with the distance travelled, which under
        // drag is 1 - e^(-kt) of the way.
        const float gone = (1.0f - std::exp(-drag * age)) / (1.0f - std::exp(-drag * life));
        return core::lerpf(settings.mouth_radius, end_radius, std::pow(core::saturate(gone), 0.9f));
    };

    Uniforms u = {};
    int count = 0;
    // The mouth is the youngest node while the breath is held. `fed_last_`
    // is this frame's feed: update() has run since.
    if (fed_last_) {
        u.nodes[count] = core::Vec4{mouth_.x, mouth_.y, mouth_.z, settings.mouth_radius};
        // Joined to the newest node let go, which is of this strand.
        u.info[count] = core::Vec4{0.0f, nodes_.empty() ? 0.0f : 1.0f, 0.0f, 0.0f};
        ++count;
    }
    for (size_t i = 0; i < nodes_.size() && count < kMaxNodes; ++i) {
        const Node& n = nodes_[i];
        const float age01 = core::saturate(n.age / life);
        u.nodes[count] = core::Vec4{n.position.x, n.position.y, n.position.z, radius_at(std::min(n.age, life))};
        // Joined to the next, older node unless this one began a strand.
        const bool joined = i + 1 < nodes_.size() && !n.strand_start;
        u.info[count] = core::Vec4{age01, joined ? 1.0f : 0.0f, 0.0f, 0.0f};
        ++count;
    }
    if (count < 2) return;

    const float speed = jet_.range * drag / (1.0f - std::exp(-drag * life));
    u.params = core::Vec4{camera.z_near, time_seconds, float(count), float(std::clamp(settings.steps, 8, 96))};
    u.shape = core::Vec4{settings.extinction * jet_.cover, 1.0f / std::max(settings.noise_size, 0.1f),
                         settings.erosion, settings.flow * speed};
    u.colour = core::Vec4{settings.core, settings.gain, core::saturate(settings.smoke), jet_.brightness};
    u.hot = core::Vec4{jet_.hot.x, jet_.hot.y, jet_.hot.z, 0.0f};
    u.cool = core::Vec4{jet_.cool.x, jet_.cool.y, jet_.cool.z, 0.0f};
    u.axis = core::Vec4{direction_.x, direction_.y, direction_.z, 0.0f};
    submit(device, pass, scene, u, count, settings);
}

void BreathVolume::submit(Device& device, rhi::Pass* pass, const SceneUniforms& scene, Uniforms& u, int count,
                          const BreathVolumeSettings& settings) {
    rhi::Pipeline* pipeline = pipelines_ ? pipelines_->get(pipeline_) : nullptr;
    if (!pipeline || count < 2) return;
    u.params.z = float(count);
    core::Vec3 lo = core::Vec3{u.nodes[0].x, u.nodes[0].y, u.nodes[0].z};
    core::Vec3 hi = lo;
    float widest = 0.0f;
    for (int i = 0; i < count; ++i) {
        lo = core::Vec3{std::min(lo.x, u.nodes[i].x), std::min(lo.y, u.nodes[i].y), std::min(lo.z, u.nodes[i].z)};
        hi = core::Vec3{std::max(hi.x, u.nodes[i].x), std::max(hi.y, u.nodes[i].y), std::max(hi.z, u.nodes[i].z)};
        widest = std::max(widest, u.nodes[i].w);
    }
    const core::Vec3 centre = (lo + hi) * 0.5f;
    // The noise can push the edge out by 0.45 radii at most (breath_volume.hlsl).
    const float reach = core::length(hi - lo) * 0.5f + widest * (1.0f + std::clamp(settings.erosion, 0.0f, 0.45f));

    u.bounds = core::Vec4{centre.x, centre.y, centre.z, reach};
    // A capsule from the youngest node to the oldest, as wide as the node
    // that stands furthest off that line needs. From the chase camera the
    // eye is inside the sphere and every pixel would march; the capsule is
    // a few degrees of the screen.
    {
        const core::Vec3 a{u.nodes[0].x, u.nodes[0].y, u.nodes[0].z};
        const core::Vec3 b{u.nodes[count - 1].x, u.nodes[count - 1].y, u.nodes[count - 1].z};
        const core::Vec3 ab = b - a;
        const float len2 = std::max(core::dot(ab, ab), 1e-6f);
        const float swell = 1.0f + std::clamp(settings.erosion, 0.0f, 0.45f);
        float hull = 0.0f;
        for (int i = 0; i < count; ++i) {
            const core::Vec3 p{u.nodes[i].x, u.nodes[i].y, u.nodes[i].z};
            const float t = core::saturate(core::dot(p - a, ab) / len2);
            hull = std::max(hull, core::length(p - (a + ab * t)) + u.nodes[i].w * swell);
        }
        u.hull_a = core::Vec4{a.x, a.y, a.z, hull};
        u.hull_b = core::Vec4{b.x, b.y, b.z, 0.0f};
    }

    rhi::Device& rhi = device.rhi();
    rhi.bind_pipeline(pass, pipeline);
    rhi.push_uniforms(rhi::Stage::Fragment, 0, &scene, sizeof(SceneUniforms));
    rhi.push_uniforms(rhi::Stage::Fragment, 1, &u, sizeof(u));
    rhi::TextureBinding depth = {};
    depth.texture = device.depth();
    depth.sampler = point_;
    rhi.bind_fragment_textures(pass, 0, &depth, 1);
    rhi.draw(pass, 3, 1, 0, 0);
}

void BreathVolume::draw_comet(Device& device, rhi::Pass* pass, const Camera& camera, const SceneUniforms& scene,
                              const BreathVolumeSettings& settings, const Comet& comet, float time_seconds) {
    if (!pass || !available_ || !device.depth()) return;
    const float speed = core::length(comet.velocity);
    const core::Vec3 back = speed > 1e-3f ? comet.velocity * (-1.0f / speed) : core::Vec3{0.0f, 0.0f, 1.0f};
    // Six nodes: the head a little ahead of the round's centre, then the
    // tail thinning over eight radii. Age runs head to tail, which is what
    // makes the head pale and whole and the tail orange and torn.
    constexpr int kNodes = 6;
    Uniforms u = {};
    for (int i = 0; i < kNodes; ++i) {
        const float along = float(i) / float(kNodes - 1);
        const core::Vec3 p = comet.position + back * (comet.radius * (8.0f * along - 0.3f));
        u.nodes[i] = core::Vec4{p.x, p.y, p.z, comet.radius * core::lerpf(1.0f, 0.3f, along)};
        u.info[i] = core::Vec4{along * 0.97f, i + 1 < kNodes ? 1.0f : 0.0f, 0.0f, 0.0f};
    }
    u.params = core::Vec4{camera.z_near, time_seconds, float(kNodes), float(std::clamp(settings.steps / 2, 8, 48))};
    u.shape = core::Vec4{settings.extinction, 1.0f / std::max(settings.noise_size, 0.1f), settings.erosion * 0.8f,
                         std::max(speed * 0.2f, 12.0f)};
    u.colour = core::Vec4{settings.core, settings.gain, core::saturate(settings.smoke) * 0.5f, comet.brightness};
    u.hot = core::Vec4{comet.hot.x, comet.hot.y, comet.hot.z, 0.0f};
    u.cool = core::Vec4{comet.cool.x, comet.cool.y, comet.cool.z, 0.0f};
    u.axis = core::Vec4{back.x, back.y, back.z, 0.0f};
    submit(device, pass, scene, u, kNodes, settings);
}

}  // namespace gfx
