#include "gfx/clouds.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "core/log.h"
#include "gfx/atmosphere.h"
#include "gfx/device.h"
#include "gfx/render_tier.h"

namespace gfx {

namespace {

// The volume: 64^3 voxels, tiling in all three axes, laid out as 8 x 8
// slices of 64 x 64 in one 2D texture (the RHI has no 3D textures), each
// slice with a one-texel border copied from the opposite edge, so the
// bilinear filter wraps inside a slice. The shader does the third axis
// itself (clouds_common.hlsl, cloud_volume).
constexpr int VOXELS = 64;
constexpr int TILE = VOXELS + 2;
constexpr int TILES = 8;
constexpr int ATLAS = TILE * TILES;  // 528
constexpr uint32_t WEATHER = 512;
constexpr float WEATHER_HALF = 40000.0f;

uint32_t hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu ^ seed * 0x9e3779b9u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}
float unit(uint32_t h) { return float(h & 0xffffffu) / float(0xffffff); }
int wrap(int i, int n) { return ((i % n) + n) % n; }

// Gradient noise with `cells` lattice cells a period, -1..1.
float perlin(float x, float y, float z, int cells, uint32_t seed) {
    const float px = x * float(cells), py = y * float(cells), pz = z * float(cells);
    const int ix = int(std::floor(px)), iy = int(std::floor(py)), iz = int(std::floor(pz));
    const float fx = px - float(ix), fy = py - float(iy), fz = pz - float(iz);
    auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    auto corner = [&](int dx, int dy, int dz) {
        const uint32_t h = hash3(wrap(ix + dx, cells), wrap(iy + dy, cells), wrap(iz + dz, cells), seed);
        // One of twelve edge directions of a cube.
        static const float G[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                                       {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
        const float* g = G[h % 12u];
        return g[0] * (fx - float(dx)) + g[1] * (fy - float(dy)) + g[2] * (fz - float(dz));
    };
    const float u = fade(fx), v = fade(fy), w = fade(fz);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00 = lerp(corner(0, 0, 0), corner(1, 0, 0), u), x10 = lerp(corner(0, 1, 0), corner(1, 1, 0), u);
    const float x01 = lerp(corner(0, 0, 1), corner(1, 0, 1), u), x11 = lerp(corner(0, 1, 1), corner(1, 1, 1), u);
    return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
}

// Cellular noise with `cells` cells a period: 1 at a feature point, falling
// to 0 a cell away (inverted F1), the billows of a cumulus.
float worley(float x, float y, float z, int cells, uint32_t seed) {
    const float px = x * float(cells), py = y * float(cells), pz = z * float(cells);
    const int ix = int(std::floor(px)), iy = int(std::floor(py)), iz = int(std::floor(pz));
    float nearest = 1e9f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = ix + dx, cy = iy + dy, cz = iz + dz;
                const uint32_t h = hash3(wrap(cx, cells), wrap(cy, cells), wrap(cz, cells), seed);
                const float fx = float(cx) + unit(h) - px;
                const float fy = float(cy) + unit(h * 0x2c1b3c6du + 1u) - py;
                const float fz = float(cz) + unit(h * 0x297a2d39u + 7u) - pz;
                nearest = std::min(nearest, fx * fx + fy * fy + fz * fz);
            }
        }
    }
    return 1.0f - std::min(std::sqrt(nearest), 1.0f);
}

// RGBA8, the atlas: r the shape (Perlin-Worley: billowy masses with the
// gradient noise's connectedness), g b a cellular fBm at three rising
// frequencies, for the shape's fBm and for eroding its edges.
std::vector<uint8_t> build_volume() {
    std::vector<uint8_t> voxels(size_t(VOXELS) * VOXELS * VOXELS * 4);
    auto slab = [&voxels](int z0, int z1) {
        for (int z = z0; z < z1; ++z) {
            for (int y = 0; y < VOXELS; ++y) {
                for (int x = 0; x < VOXELS; ++x) {
                    const float fx = (float(x) + 0.5f) / VOXELS, fy = (float(y) + 0.5f) / VOXELS,
                                fz = (float(z) + 0.5f) / VOXELS;
                    float gradient = 0.0f, amp = 1.0f, total = 0.0f;
                    for (int o = 0; o < 3; ++o) {
                        gradient += amp * perlin(fx, fy, fz, 4 << o, 11u + uint32_t(o));
                        total += amp;
                        amp *= 0.5f;
                    }
                    gradient = std::clamp(gradient / total * 0.75f + 0.5f, 0.0f, 1.0f);
                    const float w4 = worley(fx, fy, fz, 4, 21u), w8 = worley(fx, fy, fz, 8, 22u);
                    const float w16 = worley(fx, fy, fz, 16, 23u), w32 = worley(fx, fy, fz, 32, 24u);
                    const float g = 0.625f * w4 + 0.25f * w8 + 0.125f * w16;
                    const float b = 0.625f * w8 + 0.25f * w16 + 0.125f * w32;
                    const float a = 0.75f * w16 + 0.25f * w32;
                    // Remap the gradient noise above the cells' floor.
                    const float shape = std::clamp((gradient - (g - 1.0f)) / (2.0f - g), 0.0f, 1.0f);
                    uint8_t* out = &voxels[((size_t(z) * VOXELS + size_t(y)) * VOXELS + size_t(x)) * 4];
                    out[0] = uint8_t(std::lround(shape * 255.0f));
                    out[1] = uint8_t(std::lround(g * 255.0f));
                    out[2] = uint8_t(std::lround(b * 255.0f));
                    out[3] = uint8_t(std::lround(a * 255.0f));
                }
            }
        }
    };
    // One thread: the XP build compiles this file too, and its toolchain's
    // threads are best left unexercised for a modern-tier table.
    slab(0, VOXELS);

    std::vector<uint8_t> atlas(size_t(ATLAS) * ATLAS * 4, 0);
    for (int z = 0; z < VOXELS; ++z) {
        const int ox = (z % TILES) * TILE, oy = (z / TILES) * TILE;
        for (int y = -1; y <= VOXELS; ++y) {
            for (int x = -1; x <= VOXELS; ++x) {
                const uint8_t* in = &voxels[((size_t(z) * VOXELS + size_t(wrap(y, VOXELS))) * VOXELS + size_t(wrap(x, VOXELS))) * 4];
                uint8_t* out = &atlas[(size_t(oy + 1 + y) * ATLAS + size_t(ox + 1 + x)) * 4];
                std::copy(in, in + 4, out);
            }
        }
    }
    return atlas;
}

PipelineDesc pass_desc(const char* name, rhi::Format format) {
    PipelineDesc desc;
    desc.name = name;
    desc.shader = name;
    desc.cull = rhi::Cull::None;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.color_format = format;
    desc.no_depth_target = true;
    return desc;
}

}  // namespace

void CloudSettings::parse(const std::string& list) {
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
        const Field fields[] = {{"extinction", &extinction}, {"shape", &base_scale}, {"detail_scale", &detail_scale},
                                {"erosion", &detail},        {"softness", &softness}, {"sun", &sun_gain},
                                {"sky", &sky_gain},          {"history", &history}};
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
        if (!found) LOG_WARN("--clouds: unknown key '%s'", key.c_str());
    }
}

bool Clouds::init(Device* device, PipelineCache* pipelines) {
    device_ = device;
    pipelines_ = pipelines;
    available_ = active_tier().tier == Tier::Modern && device->hdr() && device->depth_sampled();
    if (!available_) return true;
    weather_pipeline_ = pipelines->create(pass_desc("clouds_weather", rhi::Format::RGBA8));
    march_pipeline_ = pipelines->create(pass_desc("clouds_march", rhi::Format::RGBA16F));
    resolve_pipeline_ = pipelines->create(pass_desc("clouds_resolve", rhi::Format::RGBA16F));
    PipelineDesc apply = pass_desc("clouds_apply", device->scene_hdr_format());
    apply.alpha_blend = true;  // light + scene x transmittance (clouds_apply.hlsl)
    apply_pipeline_ = pipelines->create(apply);

    rhi::Device& rhi = device->rhi();
    rhi::SamplerDesc info;
    info.min_filter = info.mag_filter = rhi::Filter::Nearest;
    info.mip_mode = rhi::MipMode::Nearest;
    info.address_u = info.address_v = info.address_w = rhi::Address::Clamp;
    info.max_lod = 0.0f;
    point_ = rhi.create_sampler(info);
    info.min_filter = info.mag_filter = rhi::Filter::Linear;
    linear_ = rhi.create_sampler(info);
    // The volume's atlas: linear within a slice, its borders do the wrap.
    wrap_ = rhi.create_sampler(info);

    const auto start = std::chrono::steady_clock::now();
    const std::vector<uint8_t> atlas = build_volume();
    rhi::TextureDesc desc;
    desc.width = desc.height = ATLAS;
    desc.format = rhi::Format::RGBA8;
    desc.usage = rhi::TEXTURE_SAMPLED;
    noise_ = rhi.create_texture(desc, "cloud volume");
    if (noise_) rhi.upload_texture(noise_, atlas.data(), ATLAS, ATLAS, false);
    desc.width = desc.height = WEATHER;
    desc.usage = rhi::TEXTURE_SAMPLED | rhi::TEXTURE_COLOR_TARGET;
    weather_ = rhi.create_texture(desc, "cloud weather");
    LOG_INFO("clouds: %dx%dx%d volume in %.0f ms", VOXELS, VOXELS, VOXELS, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    available_ = point_ && linear_ && wrap_ && noise_ && weather_;
    return true;
}

void Clouds::shutdown() {
    if (!device_) return;
    rhi::Device& rhi = device_->rhi();
    rhi.destroy(raw_);
    rhi.destroy(history_[0]);
    rhi.destroy(history_[1]);
    rhi.destroy(noise_);
    rhi.destroy(weather_);
    rhi.destroy(point_);
    rhi.destroy(linear_);
    rhi.destroy(wrap_);
    raw_ = history_[0] = history_[1] = noise_ = weather_ = nullptr;
    point_ = linear_ = wrap_ = nullptr;
    width_ = height_ = 0;
    valid_ = false;
    have_prev_ = false;
}

bool Clouds::ensure_targets(Device& device, uint32_t width, uint32_t height) {
    if (raw_ && width_ == width && height_ == height) return true;
    rhi::Device& rhi = device.rhi();
    rhi.destroy(raw_);
    rhi.destroy(history_[0]);
    rhi.destroy(history_[1]);
    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = rhi::Format::RGBA16F;
    desc.usage = rhi::TEXTURE_SAMPLED | rhi::TEXTURE_COLOR_TARGET;
    raw_ = rhi.create_texture(desc, "clouds");
    history_[0] = rhi.create_texture(desc, "clouds_history_a");
    history_[1] = rhi.create_texture(desc, "clouds_history_b");
    width_ = width;
    height_ = height;
    valid_ = false;
    return raw_ && history_[0] && history_[1];
}

void Clouds::render(Device& device, const Camera& camera, float aspect, const SceneUniforms& scene,
                    const Atmosphere& atmosphere, const CloudSettings& s) {
    if (!active(scene, s)) {
        valid_ = false;
        return;
    }
    rhi::Pipeline* weather = pipelines_->get(weather_pipeline_);
    rhi::Pipeline* march = pipelines_->get(march_pipeline_);
    rhi::Pipeline* resolve = pipelines_->get(resolve_pipeline_);
    const uint32_t w = std::max(1u, device.world_width() / 2), h = std::max(1u, device.world_height() / 2);
    if (!weather || !march || !resolve || !device.depth() || !ensure_targets(device, w, h)) return;
    rhi::Device& rhi = device.rhi();

    // This frame's camera, camera-relative and unjittered, against last
    // frame's: a cut starts the history afresh.
    const core::Mat4 view_proj = camera.projection(aspect) * core::view_from_transform(core::Vec3{}, camera.rotation);
    const core::Vec3 forward = camera.forward();
    const bool cut = !have_prev_ || core::distance(camera.position, prev_camera_) > 300.0f ||
                     core::dot(forward, prev_forward_) < 0.9f;
    if (cut) valid_ = false;

    Uniforms u;
    u.prev_view_proj = have_prev_ ? prev_view_proj_ : view_proj;
    const core::Vec3 delta = have_prev_ ? camera.position - prev_camera_ : core::Vec3{};
    u.camera_delta = core::Vec4{delta.x, delta.y, delta.z, valid_ ? s.history : 0.0f};
    u.params = core::Vec4{camera.z_near, float(frame_++ % 1024u), float(std::clamp(s.steps, 8, 128)), s.extinction};
    u.texel = core::Vec4{1.0f / float(w), 1.0f / float(h), 1.0f / float(device.world_width()),
                         1.0f / float(device.world_height())};
    // The weather map follows the camera in whole texels, so its samples do
    // not crawl as it moves.
    const float texel = 2.0f * WEATHER_HALF / float(WEATHER);
    weather_centre_ = core::Vec2{std::round(camera.position.x / texel) * texel, std::round(camera.position.z / texel) * texel};
    u.weather = core::Vec4{weather_centre_.x, weather_centre_.y, WEATHER_HALF, texel};
    u.shape = core::Vec4{s.base_scale, s.detail_scale, s.detail, s.softness};
    u.light = core::Vec4{s.sun_gain, s.sky_gain, 0.0f, 0.0f};
    last_ = u;

    prev_view_proj_ = view_proj;
    prev_camera_ = camera.position;
    prev_forward_ = forward;
    have_prev_ = true;

    auto push = [&](rhi::Pass*) {
        rhi.push_uniforms(rhi::Stage::Fragment, 0, &scene, sizeof(SceneUniforms));
        rhi.push_uniforms(rhi::Stage::Fragment, 1, &u, sizeof(u));
    };
    {
        rhi::Pass* pass = device.begin_color_pass(weather_, false);
        rhi.bind_pipeline(pass, weather);
        push(pass);
        rhi.draw(pass, 3, 1, 0, 0);
        device.end_pass(pass);
    }
    {
        rhi::Pass* pass = device.begin_color_pass(raw_, false);
        rhi.bind_pipeline(pass, march);
        push(pass);
        rhi::TextureBinding depth = {};
        depth.texture = device.depth();
        depth.sampler = point_;
        rhi.bind_fragment_textures(pass, 0, &depth, 1);
        atmosphere.bind(rhi, pass, 1, false);
        rhi::TextureBinding maps[2] = {};
        maps[0].texture = noise_;
        maps[0].sampler = wrap_;
        maps[1].texture = weather_;
        maps[1].sampler = linear_;
        rhi.bind_fragment_textures(pass, 2, maps, 2);
        rhi.draw(pass, 3, 1, 0, 0);
        device.end_pass(pass);
    }
    {
        rhi::Pass* pass = device.begin_color_pass(history_[write_], false);
        rhi.bind_pipeline(pass, resolve);
        push(pass);
        rhi::TextureBinding inputs[2] = {};
        inputs[0].texture = raw_;
        inputs[0].sampler = point_;
        inputs[1].texture = valid_ ? history_[1 - write_] : raw_;
        inputs[1].sampler = linear_;
        rhi.bind_fragment_textures(pass, 0, inputs, 2);
        rhi.draw(pass, 3, 1, 0, 0);
        device.end_pass(pass);
    }
    valid_ = true;
    write_ = 1 - write_;
}

void Clouds::apply(Device& device, rhi::Pass* pass, const Camera& camera, const SceneUniforms& scene) {
    (void)camera;
    rhi::Pipeline* apply = pipelines_->get(apply_pipeline_);
    if (!pass || !apply || !valid_) return;
    rhi::Device& rhi = device.rhi();
    rhi.bind_pipeline(pass, apply);
    rhi.push_uniforms(rhi::Stage::Fragment, 0, &scene, sizeof(SceneUniforms));
    rhi.push_uniforms(rhi::Stage::Fragment, 1, &last_, sizeof(last_));
    rhi::TextureBinding inputs[2] = {};
    inputs[0].texture = history_[1 - write_];  // the one just resolved
    inputs[0].sampler = linear_;
    inputs[1].texture = device.depth();
    inputs[1].sampler = point_;
    rhi.bind_fragment_textures(pass, 0, inputs, 2);
    rhi.draw(pass, 3, 1, 0, 0);
}

}  // namespace gfx
