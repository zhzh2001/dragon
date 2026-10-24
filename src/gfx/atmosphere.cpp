#include "gfx/atmosphere.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "gfx/device.h"
#include "gfx/render_tier.h"

namespace gfx {

namespace {

constexpr float PLANET_RADIUS_KM = 6360.0f;
constexpr float ATMOSPHERE_TOP_KM = 6460.0f;  // shaders/sky.hlsl assumes planet + 100

// Earth's, per km (Hillaire 2020, table 1 and his sample's defaults).
constexpr float RAYLEIGH[3] = {5.802e-3f, 13.558e-3f, 33.1e-3f};
constexpr float RAYLEIGH_SCALE_HEIGHT_KM = 8.0f;
constexpr float MIE_SCATTERING = 3.996e-3f;
constexpr float MIE_ABSORPTION = 0.444e-3f;
constexpr float OZONE[3] = {0.650e-3f, 1.881e-3f, 0.085e-3f};
constexpr float OZONE_CENTRE_KM = 25.0f;

PipelineDesc table_desc(const char* name) {
    PipelineDesc desc;
    desc.name = name;
    desc.shader = name;
    desc.cull = rhi::Cull::None;
    desc.depth_test = false;
    desc.depth_write = false;
    desc.color_format = rhi::Format::RGBA16F;
    desc.no_depth_target = true;
    return desc;
}

rhi::Texture* make_target(rhi::Device& rhi, uint32_t w, uint32_t h, const char* name) {
    rhi::TextureDesc desc;
    desc.width = w;
    desc.height = h;
    desc.format = rhi::Format::RGBA16F;
    desc.usage = rhi::TEXTURE_SAMPLED | rhi::TEXTURE_COLOR_TARGET;
    return rhi.create_texture(desc, name);
}

// IEEE half from float, rounding to nearest; heights and distances only,
// so no NaN handling beyond what falls out.
uint16_t to_half(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int exponent = int((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent <= 0) return uint16_t(sign);  // tiny: zero
    if (exponent >= 31) return uint16_t(sign | 0x7BFFu);  // huge: the largest finite
    mantissa += 0x1000u;  // round
    if (mantissa & 0x800000u) {
        mantissa = 0;
        if (exponent + 1 >= 31) return uint16_t(sign | 0x7BFFu);
        return uint16_t(sign | (uint32_t(exponent + 1) << 10));
    }
    return uint16_t(sign | (uint32_t(exponent) << 10) | (mantissa >> 13));
}

struct TerrainSunUniforms {
    core::Vec4 sun;     // xy the sun's horizontal direction, z tan(elevation), w the extent's half
    core::Vec4 height;  // x the height texture's size
};

constexpr uint32_t TERRAIN_SUN_SIZE = 2048;

// Whether the inputs of the static tables differ (not the camera or the sun).
// The world block (the camera, the map's extent) is per frame and shapes no
// table.
bool tables_differ(const AtmosphereUniforms& a, const AtmosphereUniforms& b) {
    return std::memcmp(&a.rayleigh, &b.rayleigh, sizeof(core::Vec4) * 4) != 0 || a.radii.x != b.radii.x ||
           a.radii.y != b.radii.y;
}

}  // namespace

void AtmosphereSettings::parse(const std::string& list) {
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
        const Field fields[] = {{"sky", &sky_scale},       {"aerial", &aerial_scale},
                                {"far", &aerial_far_m},     {"haze", &mie},
                                {"haze_height", &mie_scale_height_km}, {"glow", &mie_g},
                                {"rayleigh", &rayleigh},    {"ozone", &ozone},
                                {"fog", &valley_fog},       {"sky_light", &sky_light},
                                {"disc", &sun_disc},        {"altitude", &ground_altitude_km},
                                {"albedo", &ground_albedo}, {"ms", &multiscatter},
                                {"clouds", &cloud_coverage}, {"cloud_base", &cloud_base_m},
                                {"cloud_thickness", &cloud_thickness_m}, {"cloud_density", &cloud_density},
                                {"mist", &mist_density},    {"mist_height", &mist_height_m},
                                {"mist_patches", &mist_patches}};
        bool found = false;
        if (key == "on") {
            enabled = value != 0.0f;
            found = true;
        }
        for (const Field& f : fields) {
            if (key == f.key) {
                *f.value = value;
                found = true;
            }
        }
        if (!found) LOG_WARN("--atmosphere: unknown key '%s'", key.c_str());
    }
}

bool Atmosphere::init(Device* device, PipelineCache* pipelines) {
    device_ = device;
    pipelines_ = pipelines;
    if (active_tier().tier != Tier::Modern) return true;
    rhi::Device& rhi = device->rhi();
    transmittance_ = make_target(rhi, 256, 64, "atmosphere transmittance");
    multiscatter_ = make_target(rhi, 32, 32, "atmosphere multiscatter");
    atlas_ = make_target(rhi, 256, 232, "atmosphere atlas");
    rhi::SamplerDesc info;
    info.min_filter = rhi::Filter::Linear;
    info.mag_filter = rhi::Filter::Linear;
    info.mip_mode = rhi::MipMode::Nearest;
    info.address_u = info.address_v = info.address_w = rhi::Address::Clamp;
    info.max_lod = 0.0f;
    sampler_ = rhi.create_sampler(info);
    transmittance_pipeline_ = pipelines->create(table_desc("atmosphere_transmittance"));
    multiscatter_pipeline_ = pipelines->create(table_desc("atmosphere_multiscatter"));
    frame_pipeline_ = pipelines->create(table_desc("atmosphere_frame"));
    terrain_sun_pipeline_ = pipelines->create(table_desc("terrain_sun"));
    terrain_sun_ = make_target(rhi, TERRAIN_SUN_SIZE, TERRAIN_SUN_SIZE, "terrain sun");
    // No terrain yet: one texel at sea level, which shades nothing.
    set_heightfield(std::vector<float>(1, 0.0f), 1, 1.0f);
    if (!transmittance_ || !multiscatter_ || !atlas_ || !sampler_ || !terrain_sun_ || !heights_) {
        LOG_ERROR("atmosphere: could not create its tables; the analytic sky stays");
        shutdown();
        return false;
    }
    return true;
}

void Atmosphere::shutdown() {
    if (!device_) return;
    rhi::Device& rhi = device_->rhi();
    rhi.destroy(transmittance_);
    rhi.destroy(multiscatter_);
    rhi.destroy(atlas_);
    rhi.destroy(sampler_);
    rhi.destroy(heights_);
    rhi.destroy(terrain_sun_);
    heights_ = terrain_sun_ = nullptr;
    transmittance_ = multiscatter_ = atlas_ = nullptr;
    sampler_ = nullptr;
    tables_valid_ = false;
}

AtmosphereUniforms Atmosphere::uniforms_for(const SceneUniforms& scene, const AtmosphereSettings& s) const {
    AtmosphereUniforms u;
    u.rayleigh = core::Vec4{RAYLEIGH[0] * s.rayleigh, RAYLEIGH[1] * s.rayleigh, RAYLEIGH[2] * s.rayleigh,
                            RAYLEIGH_SCALE_HEIGHT_KM};
    u.mie = core::Vec4{MIE_SCATTERING * s.mie, MIE_ABSORPTION * s.mie, core::maxf(s.mie_scale_height_km, 0.05f),
                       s.mie_g};
    u.ozone = core::Vec4{OZONE[0] * s.ozone, OZONE[1] * s.ozone, OZONE[2] * s.ozone, OZONE_CENTRE_KM};
    u.ground = core::Vec4{s.ground_albedo * s.ground_tint[0], s.ground_albedo * s.ground_tint[1],
                          s.ground_albedo * s.ground_tint[2], s.multiscatter};
    const float altitude_km = core::maxf(s.ground_altitude_km + scene.camera_position.y * 0.001f, 0.001f);
    const float km_per_metre = 0.001f * s.aerial_scale;
    u.radii = core::Vec4{PLANET_RADIUS_KM, ATMOSPHERE_TOP_KM, altitude_km, km_per_metre};
    u.sun_direction = core::Vec4{scene.sun.x, scene.sun.y, scene.sun.z, s.aerial_far_m * km_per_metre};
    u.world = core::Vec4{scene.camera_position.x, scene.camera_position.y, scene.camera_position.z, heights_half_};
    return u;
}

void Atmosphere::apply(SceneUniforms& scene, const AtmosphereSettings& s) const {
    if (!available() || !s.enabled) {
        scene.atmosphere = core::Vec4{0.0f, 0.0f, PLANET_RADIUS_KM, 0.0f};
        scene.atmosphere2 = core::Vec4{1.0f, 0.0f, PLANET_RADIUS_KM, 0.0f};
        scene.clouds = core::Vec4{0.0f, s.cloud_base_m, s.cloud_thickness_m, s.cloud_density};
        return;
    }
    scene.clouds = core::Vec4{s.cloud_coverage, s.cloud_base_m, core::maxf(s.cloud_thickness_m, 10.0f), s.cloud_density};
    const AtmosphereUniforms u = uniforms_for(scene, s);
    scene.atmosphere = core::Vec4{s.sky_scale, u.radii.w, PLANET_RADIUS_KM + u.radii.z, s.valley_fog};
    scene.atmosphere2 = core::Vec4{u.sun_direction.w, s.sky_light, PLANET_RADIUS_KM, s.sun_disc};
}

void Atmosphere::update(Device& device, const SceneUniforms& scene, const AtmosphereSettings& s) {
    if (!available() || !s.enabled) return;
    rhi::Pipeline* transmittance = pipelines_->get(transmittance_pipeline_);
    rhi::Pipeline* multiscatter = pipelines_->get(multiscatter_pipeline_);
    rhi::Pipeline* frame = pipelines_->get(frame_pipeline_);
    if (!transmittance || !multiscatter || !frame) return;
    rhi::Device& rhi = device.rhi();
    const AtmosphereUniforms u = uniforms_for(scene, s);

    auto bind = [&](rhi::Pass* pass, rhi::Texture* texture, uint32_t slot) {
        rhi::TextureBinding binding = {};
        binding.texture = texture;
        binding.sampler = sampler_;
        rhi.bind_fragment_textures(pass, slot, &binding, 1);
    };

    // A hot reload rebuilds a pipeline object; the tables then follow it.
    if (!tables_valid_ || tables_differ(u, built_) || transmittance != last_transmittance_ ||
        multiscatter != last_multiscatter_) {
        {
            rhi::Pass* pass = device.begin_color_pass(transmittance_, true);
            rhi.bind_pipeline(pass, transmittance);
            rhi.push_uniforms(rhi::Stage::Fragment, 0, &u, sizeof(u));
            rhi.draw(pass, 3, 1, 0, 0);
            device.end_pass(pass);
        }
        {
            rhi::Pass* pass = device.begin_color_pass(multiscatter_, true);
            rhi.bind_pipeline(pass, multiscatter);
            rhi.push_uniforms(rhi::Stage::Fragment, 0, &u, sizeof(u));
            bind(pass, transmittance_, 0);
            rhi.draw(pass, 3, 1, 0, 0);
            device.end_pass(pass);
        }
        built_ = u;
        tables_valid_ = true;
        last_transmittance_ = transmittance;
        last_multiscatter_ = multiscatter;
    }

    // The terrain's shadow heights, when the sun or the terrain moved.
    rhi::Pipeline* terrain_sun = pipelines_->get(terrain_sun_pipeline_);
    const core::Vec3 sun = scene.sun.xyz();
    if (terrain_sun && heights_ &&
        (terrain_sun_dirty_ || core::length(sun - terrain_sun_for_) > 1e-5f || terrain_sun != last_terrain_sun_)) {
        const float horizontal = core::maxf(std::sqrt(sun.x * sun.x + sun.z * sun.z), 1e-4f);
        TerrainSunUniforms t;
        t.sun = core::Vec4{sun.x / horizontal, sun.z / horizontal, sun.y / horizontal, heights_half_};
        t.height = core::Vec4{float(heights_size_), 0.0f, 0.0f, 0.0f};
        rhi::Pass* pass = device.begin_color_pass(terrain_sun_, true);
        rhi.bind_pipeline(pass, terrain_sun);
        rhi.push_uniforms(rhi::Stage::Fragment, 0, &t, sizeof(t));
        bind(pass, heights_, 0);
        rhi.draw(pass, 3, 1, 0, 0);
        device.end_pass(pass);
        terrain_sun_dirty_ = false;
        terrain_sun_for_ = sun;
        last_terrain_sun_ = terrain_sun;
    }

    rhi::Pass* pass = device.begin_color_pass(atlas_, true);
    rhi.bind_pipeline(pass, frame);
    rhi.push_uniforms(rhi::Stage::Fragment, 0, &scene, sizeof(scene));
    rhi.push_uniforms(rhi::Stage::Fragment, 1, &u, sizeof(u));
    bind(pass, transmittance_, 0);
    bind(pass, multiscatter_, 1);
    bind(pass, terrain_sun_, 2);  // the shadows in the air
    rhi.draw(pass, 3, 1, 0, 0);
    device.end_pass(pass);
}

void Atmosphere::set_heightfield(const std::vector<float>& heights, int n, float half) {
    if (!device_ || active_tier().tier != Tier::Modern || n < 1 || heights.size() != size_t(n) * size_t(n)) return;
    rhi::Device& rhi = device_->rhi();
    if (!heights_ || heights_size_ != uint32_t(n)) {
        rhi.destroy(heights_);
        rhi::TextureDesc desc;
        desc.width = desc.height = uint32_t(n);
        desc.format = rhi::Format::RGBA16F;
        desc.usage = rhi::TEXTURE_SAMPLED;
        heights_ = rhi.create_texture(desc, "terrain heights");
        heights_size_ = uint32_t(n);
    }
    if (!heights_) return;
    height_texels_.assign(heights.size() * 4, 0);
    for (size_t i = 0; i < heights.size(); ++i) height_texels_[i * 4] = to_half(heights[i]);
    rhi.upload_texture(heights_, height_texels_.data(), uint32_t(n), uint32_t(n), false);
    heights_half_ = half;
    terrain_sun_dirty_ = true;
}

void Atmosphere::set_canopy(const std::vector<float>& cover, const std::vector<float>& top, int n) {
    if (!device_ || !heights_ || heights_size_ != uint32_t(n) || cover.size() != size_t(n) * size_t(n) ||
        top.size() != cover.size() || height_texels_.size() != cover.size() * 4) {
        return;
    }
    for (size_t i = 0; i < cover.size(); ++i) {
        height_texels_[i * 4 + 1] = to_half(cover[i]);
        height_texels_[i * 4 + 2] = to_half(top[i]);
    }
    device_->rhi().upload_texture(heights_, height_texels_.data(), uint32_t(n), uint32_t(n), false);
}

void Atmosphere::bind_heights(rhi::Device& rhi, rhi::Pass* pass, uint32_t slot) const {
    if (!available() || !heights_) return;
    rhi::TextureBinding binding = {};
    binding.texture = heights_;
    binding.sampler = sampler_;
    rhi.bind_fragment_textures(pass, slot, &binding, 1);
}

void Atmosphere::bind(rhi::Device& rhi, rhi::Pass* pass, uint32_t slot, bool terrain_sun) const {
    if (!available()) return;
    rhi::TextureBinding bindings[2] = {};
    bindings[0].texture = atlas_;
    bindings[0].sampler = sampler_;
    bindings[1].texture = terrain_sun_;
    bindings[1].sampler = sampler_;
    rhi.bind_fragment_textures(pass, slot, bindings, terrain_sun ? 2 : 1);
}

void Atmosphere::bind_transmittance(rhi::Device& rhi, rhi::Pass* pass, uint32_t slot) const {
    if (!available()) return;
    rhi::TextureBinding binding = {};
    binding.texture = transmittance_;
    binding.sampler = sampler_;
    rhi.bind_fragment_textures(pass, slot, &binding, 1);
}

}  // namespace gfx
