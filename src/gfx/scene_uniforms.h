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
