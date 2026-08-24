#pragma once

#include "core/math.h"
#include "gfx/camera.h"

namespace gfx {

// Per-frame scene constants, shared by every world shader.
//
// Must match `SceneUniforms` in shaders/scene_common.msl exactly. Every vec3 is
// stored as a Vec4 because MSL aligns float3 to 16 bytes -- using float4
// throughout removes any doubt about where a field lands, at the cost of a few
// wasted floats.
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
};

// Per-object transform plus procedural deformation parameters.
//
// Must match `ModelUniforms` in shaders/model_common.msl exactly.
struct ModelUniforms {
    core::Mat4 model = core::Mat4::identity();
    // x flap angle (radians, + wings up), y tuck 0..1,
    // z wing root |x|, w wing span |x|
    core::Vec4 wing = core::Vec4{0.0f, 0.0f, 1.0f, 1.0f};
    // x lateral tail/neck bend (radians), y body pitch vs flight path,
    // z brake flare 0..1, w wing hinge height above the body centreline
    core::Vec4 pose = core::Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    // rgb multiplies vertex colour, a is an unlit emissive add. Lets one mesh
    // serve many states -- the next checkpoint glows, the rest do not -- without
    // needing a material system.
    core::Vec4 tint = core::Vec4{1.0f, 1.0f, 1.0f, 0.0f};
};

// Sun and sky, as tunable values rather than shader constants. Sun angle is
// authored as azimuth/elevation because that is how a person thinks about time
// of day, not as a direction vector.
struct Lighting {
    float sun_azimuth_deg = 135.0f;
    float sun_elevation_deg = 26.0f;
    // Kept near 1.5: the terrain shader tonemaps with Reinhard, and anything
    // much brighter drives rock and snow albedo straight into white.
    float sun_intensity = 1.95f;
    float sun_color[3] = {1.0f, 0.92f, 0.78f};

    // LINEAR colours, not display colours. Everything now goes through the
    // shared tonemap, and these same values double as the ambient term, which
    // was always meant to be linear -- so the previous display-space values were
    // making both the sky and the ambient light too bright.
    float sky_zenith[3] = {0.012f, 0.058f, 0.303f};
    float sky_horizon[3] = {0.284f, 0.397f, 0.556f};
    float fog_color[3] = {0.274f, 0.356f, 0.492f};
    float fog_density = 0.00030f;
    float ambient = 0.55f;

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
    // Used to fade the terrain into the sky at the map boundary, so the map's
    // hard edge is not visible as a cliff on the horizon.
    float half_extent = 2500.0f;
};

// Assembles the per-frame uniform block. Shadow fields are filled from the
// ShadowMap by the caller via apply_shadow(), so this header does not need to
// know about it.
SceneUniforms make_scene_uniforms(const Camera& camera, float aspect, const Lighting& lighting,
                                 const TerrainMaterial& material, float time_seconds);

}  // namespace gfx
