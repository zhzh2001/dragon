#pragma once

#include "core/math.h"
#include "gfx/camera.h"
#include "gfx/palette.h"

namespace gfx {

// Per-frame scene constants, shared by every world shader.
//
// Must match `SceneUniforms` in shaders/scene_common.hlsl exactly. Every vec3 is
// stored as a Vec4: HLSL's constant-buffer packing, MSL's float3 alignment and
// std140 all agree on a float4, so using float4 throughout removes any doubt
// about where a field lands on any backend, at the cost of a few wasted floats.
struct SceneUniforms {
    core::Mat4 view_proj;
    core::Mat4 light_view_proj;

    core::Vec4 camera_position;
    core::Vec4 camera_right;
    core::Vec4 camera_up;
    core::Vec4 camera_forward;

    core::Vec4 sun;         // .xyz surface -> sun (normalized), .w intensity
    core::Vec4 sun_color;   // .rgb
    core::Vec4 sky_zenith;  // .rgb
    core::Vec4 sky_horizon; // .rgb
    core::Vec4 fog_color;   // .rgb, .a density (1/metres)

    core::Vec4 view_params;     // x tan(fov/2), y aspect, z time, w terrain half extent
    core::Vec4 terrain_params;  // x water level, y snow line, z rock slope, w ambient
    core::Vec4 shadow_params;   // x texel size, y depth bias, z strength, w enabled
    core::Vec4 light_params;    // x sun wrap, y foliage translucency, z ground bounce
    core::Vec4 palette[PALETTE_COUNT];
    // The grade a tier without HDR applies in the world shaders themselves
    // (LDR_OUTPUT): x exposure, y contrast, z saturation, w hue preservation.
    // Unread when the post stack grades.
    core::Vec4 output_grade = core::Vec4{1.0f, 1.0f, 1.0f, 0.0f};
    // x start, y end of a linear fixed-function fog (the View distance
    // setting); y 0 keeps the exp2 fog by fog_color.a. Read by ff_fog only.
    core::Vec4 fog_range = core::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    // The atmosphere (gfx/atmosphere.h, Atmosphere::apply), read by the
    // modern tier's world shaders: x sky luminance scale (0: the analytic
    // sky), y km of air per world metre, z the camera's radius (km), w the
    // valley fog's share; atmosphere2 x the aerial perspective's far
    // distance (km), y sky-light gain, z the planet's radius (km), w the sun
    // disc. Unread on a retro tier.
    core::Vec4 atmosphere = core::Vec4{0.0f, 0.0f, 6360.0f, 0.0f};
    core::Vec4 atmosphere2 = core::Vec4{1.0f, 0.0f, 6360.0f, 0.0f};
    // The cloud layer (modern tier): x coverage (0: none), y base and z
    // thickness in metres, w density.
    core::Vec4 clouds = core::Vec4{0.0f, 1500.0f, 1300.0f, 1.0f};
    // The shadow cascades (gfx/shadow_map.h, modern tier): each one's light
    // matrix, and x their count (0: the single light_view_proj above),
    // y z w each one's texel in metres.
    core::Mat4 cascade_view_proj[3];
    core::Vec4 cascade_params = core::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    // The volumetric clouds (gfx/clouds.h): x 1 when they are drawn, so the
    // sky leaves its own slab out (sky.hlsl); yzw unused.
    core::Vec4 clouds2 = core::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    // One local light (modern tier, scene_common.hlsl local_light): a
    // breath's fire on the jaws that breathe it, the wings, the ground and
    // the target. xyz where, w its reach in metres (0: none); colour rgb, w
    // the wing membranes' glow from behind (skinned.hlsl), not the breath's.
    core::Vec4 local_light = core::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    core::Vec4 local_light_color = core::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    // The valley's thermals (game/thermals.h), for the cumulus over each
    // (scene_common.hlsl, cloud_coverage): x, z the column, y its radius, w
    // its strength (m/s); thermal_count.x how many are set.
    static constexpr int MAX_THERMALS = 12;
    core::Vec4 thermals[MAX_THERMALS];
    core::Vec4 thermal_count = core::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
};

// Per-object transform plus procedural deformation parameters.
//
// Must match `ModelUniforms` in shaders/model_common.hlsl exactly.
struct ModelUniforms {
    core::Mat4 model = core::Mat4::identity();
    // rgb multiplies vertex colour, a is an unlit emissive add. Lets one mesh
    // serve many states -- the next checkpoint glows, the rest do not -- without
    // needing a material system.
    core::Vec4 tint = core::Vec4{1.0f, 1.0f, 1.0f, 0.0f};
    // Which material maps are real for this draw: x base colour, y normal,
    // z occlusion-roughness-metallic. Needed because a submesh without a given
    // map still has to bind *something* to satisfy the pipeline's sampler slot,
    // and the shader has to know to ignore it.
    core::Vec4 material = core::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    // Recolour: rgb is the hue the surface is pushed toward (keeping its own
    // luminance, so scales and shading survive), a is how far. A multiplicative
    // tint cannot recolour a dark texture -- four bot tints were four
    // indistinguishable greys -- so this replaces the hue instead.
    core::Vec4 recolour = core::Vec4{1.0f, 1.0f, 1.0f, 0.0f};
};

// Sun and sky, as tunable values rather than shader constants. Sun angle is
// authored as azimuth/elevation because that is how a person thinks about time
// of day, not as a direction vector.
struct Lighting {
    // Late afternoon: a low, warm sun. The noon-flat 26 degrees it replaced was
    // the cruellest condition for a style seam -- it removed the long shadows
    // and the warm/cool split that tie materials of different detail levels
    // together. From the south-west, so a flight up the valley (toward -Z)
    // has the light raking across from the front-left and every ridge throws
    // a shadow into the frame.
    float sun_azimuth_deg = 235.0f;
    float sun_elevation_deg = 14.0f;
    // The terrain tonemaps with Reinhard; brighter than this drives snow and
    // pale rock straight to white.
    float sun_intensity = 2.3f;
    float sun_color[3] = {1.0f, 0.80f, 0.58f};

    // LINEAR colours, not display colours. Everything goes through the shared
    // tonemap, and the sky values double as the ambient term, which was
    // always meant to be linear.
    float sky_zenith[3] = {0.020f, 0.070f, 0.280f};
    float sky_horizon[3] = {0.300f, 0.400f, 0.560f};
    float fog_color[3] = {0.300f, 0.360f, 0.470f};
    float fog_density = 0.00027f;
    // Cut from 0.55 so shadow sides go dark and cool; the warm ground bounce
    // (ground_bounce, below) is what keeps undersides from going black.
    float ambient = 0.36f;

    // The shared lighting terms every world shader now uses. `sun_wrap` is how
    // far direct light reaches past the terminator (0 is a hard Lambert
    // cutoff); `foliage_translucency` is the sun leaking through a crown lit
    // from behind; `ground_bounce` scales the warm upward ambient.
    float sun_wrap = 0.22f;
    float foliage_translucency = 0.30f;
    float ground_bounce = 1.0f;
    // How much of the sky a creature's glossy hide reflects (skinned.hlsl;
    // the modern tier's).
    float sky_reflection = 1.0f;

    Palette palette;

    core::Vec3 sun_direction() const {
        const float azimuth = core::radians(sun_azimuth_deg);
        const float elevation = core::radians(sun_elevation_deg);
        const float horizontal = std::cos(elevation);
        return core::normalize(core::Vec3{horizontal * std::sin(azimuth), std::sin(elevation),
                                          horizontal * std::cos(azimuth)});
    }
};

// Terrain material controls, grouped so they can be passed as one argument.
struct TerrainMaterial {
    float water_level = 34.0f;
    float snow_line = 400.0f;
    // Lower means rock appears only on steeper ground, leaving more green.
    float rock_slope = 0.62f;
    // The terrain fades into the sky over the outermost fifth of this: the
    // skirt's extent (playable half extent times the skirt factor), so the
    // ground's hard edge is never seen as a cliff on the horizon.
    float half_extent = 7500.0f;
};

// Assembles the per-frame uniform block. Shadow fields are filled from the
// ShadowMap by the caller via apply_shadow(), so this header does not need to
// know about it.
SceneUniforms make_scene_uniforms(const Camera& camera, float aspect, const Lighting& lighting,
                                 const TerrainMaterial& material, float time_seconds);

}  // namespace gfx
