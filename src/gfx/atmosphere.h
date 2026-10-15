#pragma once

// The atmosphere (docs/FIDELITY.md, F1): a physically based sky and aerial
// perspective for the modern tier, after Hillaire (EGSR 2020). The shaders
// are shaders/atmosphere_*.hlsl; this owns their tables and runs them.
//
// Each frame, before the main pass, update() renders the frame atlas -- the
// sky-view map, the aerial-perspective froxels and the sky light on nine
// normals -- from the transmittance and multiple-scattering tables, which it
// rebuilds only when a dial changes. apply() writes the dials the world
// shaders read into the scene block, and bind() puts the atlas in the slot
// each world shader reserves for it (ATMOSPHERE_SLOT). On a retro tier none
// of it exists: the shaders keep the analytic sky, and bind() does nothing.

#include <string>
#include <vector>

#include "core/math.h"
#include "gfx/pipeline.h"
#include "gfx/scene_uniforms.h"

namespace gfx {

class Device;

// Every dial, behind the Atmosphere section of the Lighting panel. The
// defaults are Earth's atmosphere, then pushed toward the target's haze
// (docs/concept/valley-target.png): a 15 km valley under the real air is
// barely blue at its far end.
struct AtmosphereSettings {
    bool enabled = true;
    // Scene units of sky radiance for a sun of illuminance 1: matches the
    // sky to the sun_intensity the surfaces are lit with.
    float sky_scale = 9.0f;
    // Kilometres of air per kilometre of world in the aerial perspective,
    // so the valley reads as far as the target's.
    float aerial_scale = 5.0f;
    // How far the froxels reach, in world metres (past it, the last slice).
    float aerial_far_m = 24000.0f;
    // The share of the old exp2 valley fog kept on top of the air.
    float valley_fog = 0.35f;
    // Gain on the sky light (the ambient term's sky half). Above 1 since the
    // terrain's shadows and occlusion (F4): what the ridges shade is lit by
    // this alone. 1.4 after the first playtest found the valley too dark.
    float sky_light = 1.4f;
    // The sun disc's radiance, in sky_scale units.
    float sun_disc = 60.0f;
    // Where the world's y = 0 sits above the sea, in km.
    float ground_altitude_km = 0.2f;
    float rayleigh = 1.0f;          // x Earth's Rayleigh scattering
    float mie = 1.2f;               // x Earth's aerosol density: the haze
    float mie_scale_height_km = 1.2f;
    float mie_g = 0.7f;             // forward scattering: the glow about the sun
    float ozone = 1.0f;
    // The planet's ground past the terrain: the land a ray below the horizon
    // ends on, and the bounce in the multiple scattering. Greenish-grey, the
    // valley's floor and slopes averaged.
    float ground_albedo = 0.3f;
    float ground_tint[3] = {0.55f, 0.62f, 0.45f};
    float multiscatter = 1.0f;

    // The cloud layer: a slab of cumulus over the valley, drawn in the sky
    // and shadowing the ground as it drifts (shaders/sky.hlsl, atmo_clouds;
    // scene_common.hlsl, cloud_shadow). Coverage 0 is a clear sky.
    float cloud_coverage = 0.42f;
    float cloud_base_m = 2400.0f;
    float cloud_thickness_m = 1400.0f;
    float cloud_density = 1.0f;

    // key=value[,key=value...] over these (--atmosphere): on, sky, aerial,
    // far, haze, haze_height, glow, rayleigh, ozone, fog, sky_light, disc,
    // altitude, albedo, ms, clouds, cloud_base, cloud_thickness,
    // cloud_density. Unknown keys are logged and skipped.
    void parse(const std::string& list);
};

// Must match AtmosphereUniforms in shaders/atmosphere_common.hlsl.
struct AtmosphereUniforms {
    core::Vec4 rayleigh;       // rgb scattering (1/km), a scale height (km)
    core::Vec4 mie;            // x scattering, y absorption (1/km), z scale height (km), w g
    core::Vec4 ozone;          // rgb absorption (1/km), a layer centre (km)
    core::Vec4 ground;         // rgb albedo, a multiple-scattering factor
    core::Vec4 radii;          // x planet, y top (km), z camera altitude (km), w km per world metre
    core::Vec4 sun_direction;  // xyz toward the sun, w aerial-perspective far distance (km)
    core::Vec4 world;          // xyz the camera in world metres, w the terrain-sun map's half extent (m)
};

class Atmosphere {
public:
    // Builds nothing on a retro tier; available() then stays false.
    bool init(Device* device, PipelineCache* pipelines);
    void shutdown();
    bool available() const { return atlas_ != nullptr; }

    // The dials the world shaders read (scene.atmosphere, atmosphere2). With
    // the atmosphere off or unavailable, x is 0 and the shaders fall back to
    // the analytic sky.
    void apply(SceneUniforms& scene, const AtmosphereSettings& settings) const;
    // Outside any pass, before the main pass: the tables, as needed, then
    // the frame atlas for `scene` (after apply()).
    void update(Device& device, const SceneUniforms& scene, const AtmosphereSettings& settings);

    // The terrain as the sun sees it (F4, shaders/terrain_sun.hlsl): `n` x
    // `n` heights in metres, row-major from (-half, -half), over the
    // terrain's whole extent. Marched toward the sun into a map of shadow
    // heights on the next update(), and again whenever the sun moves.
    void set_heightfield(const std::vector<float>& heights, int n, float half);
    // The forest over that heightfield, on the same grid: each texel's
    // crown cover (0..1) and the height of its tops above the ground (m).
    // Into the heights texture's g and b, for the river to reflect
    // (water.hlsl); the shadow march does not read them.
    void set_canopy(const std::vector<float>& cover, const std::vector<float>& top, int n);

    // The atlas into fragment slot `slot`, and with `terrain_sun` the
    // terrain's shadow heights into `slot + 1`; nothing on a retro tier.
    void bind(rhi::Device& rhi, rhi::Pass* pass, uint32_t slot, bool terrain_sun = true) const;
    // The heights texture itself (r the ground, g the canopy's cover, b its
    // top), for the river's reflection.
    void bind_heights(rhi::Device& rhi, rhi::Pass* pass, uint32_t slot) const;
    // The transmittance table, for the sky's sun disc.
    void bind_transmittance(rhi::Device& rhi, rhi::Pass* pass, uint32_t slot) const;

private:
    AtmosphereUniforms uniforms_for(const SceneUniforms& scene, const AtmosphereSettings& settings) const;

    Device* device_ = nullptr;
    PipelineCache* pipelines_ = nullptr;
    PipelineHandle transmittance_pipeline_ = INVALID_PIPELINE;
    PipelineHandle multiscatter_pipeline_ = INVALID_PIPELINE;
    PipelineHandle frame_pipeline_ = INVALID_PIPELINE;
    PipelineHandle terrain_sun_pipeline_ = INVALID_PIPELINE;
    rhi::Texture* heights_ = nullptr;      // RGBA16F: height, canopy cover, canopy top
    std::vector<uint16_t> height_texels_;  // what it holds, so the canopy can be added after
    rhi::Texture* terrain_sun_ = nullptr;  // RGBA16F: shadow top, ridge distance
    uint32_t heights_size_ = 1;
    float heights_half_ = 1.0f;
    bool terrain_sun_dirty_ = true;
    core::Vec3 terrain_sun_for_ = {};
    const rhi::Pipeline* last_terrain_sun_ = nullptr;
    rhi::Texture* transmittance_ = nullptr;
    rhi::Texture* multiscatter_ = nullptr;
    rhi::Texture* atlas_ = nullptr;
    rhi::Sampler* sampler_ = nullptr;
    // What the static tables were last built from; a change rebuilds them.
    AtmosphereUniforms built_ = {};
    bool tables_valid_ = false;
    const rhi::Pipeline* last_transmittance_ = nullptr;
    const rhi::Pipeline* last_multiscatter_ = nullptr;
};

}  // namespace gfx
