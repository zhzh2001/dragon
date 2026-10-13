#include "gfx/atmosphere.h"

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

// Whether the inputs of the static tables differ (not the camera or the sun).
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
                                {"albedo", &ground_albedo}, {"ms", &multiscatter}};
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
    if (!transmittance_ || !multiscatter_ || !atlas_ || !sampler_) {
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
    return u;
}

void Atmosphere::apply(SceneUniforms& scene, const AtmosphereSettings& s) const {
    if (!available() || !s.enabled) {
        scene.atmosphere = core::Vec4{0.0f, 0.0f, PLANET_RADIUS_KM, 0.0f};
        scene.atmosphere2 = core::Vec4{1.0f, 0.0f, PLANET_RADIUS_KM, 0.0f};
        return;
    }
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

    rhi::Pass* pass = device.begin_color_pass(atlas_, true);
    rhi.bind_pipeline(pass, frame);
    rhi.push_uniforms(rhi::Stage::Fragment, 0, &scene, sizeof(scene));
    rhi.push_uniforms(rhi::Stage::Fragment, 1, &u, sizeof(u));
    bind(pass, transmittance_, 0);
    bind(pass, multiscatter_, 1);
    rhi.draw(pass, 3, 1, 0, 0);
    device.end_pass(pass);
}

void Atmosphere::bind(rhi::Device& rhi, rhi::Pass* pass, uint32_t slot) const {
    if (!available()) return;
    rhi::TextureBinding binding = {};
    binding.texture = atlas_;
    binding.sampler = sampler_;
    rhi.bind_fragment_textures(pass, slot, &binding, 1);
}

void Atmosphere::bind_transmittance(rhi::Device& rhi, rhi::Pass* pass, uint32_t slot) const {
    if (!available()) return;
    rhi::TextureBinding binding = {};
    binding.texture = transmittance_;
    binding.sampler = sampler_;
    rhi.bind_fragment_textures(pass, slot, &binding, 1);
}

}  // namespace gfx
