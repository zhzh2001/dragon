#include "gfx/scene_uniforms.h"

using core::Vec3;
using core::Vec4;

namespace gfx {

SceneUniforms make_scene_uniforms(const Camera& camera, float aspect, const Lighting& lighting,
                                 const TerrainMaterial& material, float time_seconds) {
    SceneUniforms uniforms;
    uniforms.view_proj = camera.view_projection(aspect);
    uniforms.light_view_proj = core::Mat4::identity();

    uniforms.camera_position = Vec4{camera.position, 1.0f};
    uniforms.camera_right = Vec4{camera.right(), 0.0f};
    uniforms.camera_up = Vec4{camera.up(), 0.0f};
    uniforms.camera_forward = Vec4{camera.forward(), 0.0f};

    Vec3 sun = lighting.sun_direction();
    uniforms.sun = Vec4{sun, lighting.sun_intensity};
    uniforms.sun_color =
        Vec4{lighting.sun_color[0], lighting.sun_color[1], lighting.sun_color[2], 0.0f};
    uniforms.sky_zenith =
        Vec4{lighting.sky_zenith[0], lighting.sky_zenith[1], lighting.sky_zenith[2], 0.0f};
    uniforms.sky_horizon =
        Vec4{lighting.sky_horizon[0], lighting.sky_horizon[1], lighting.sky_horizon[2], 0.0f};
    uniforms.fog_color = Vec4{lighting.fog_color[0], lighting.fog_color[1], lighting.fog_color[2],
                              lighting.fog_density};

    uniforms.view_params =
        Vec4{std::tan(core::radians(camera.fov_y_deg) * 0.5f), aspect, time_seconds, 0.0f};
    uniforms.terrain_params =
        Vec4{material.water_level, material.snow_line, material.rock_slope, lighting.ambient};
    // Overwritten by the caller once the shadow map has been updated.
    uniforms.shadow_params = Vec4{0.0f, 0.0f, 0.0f, 0.0f};
    return uniforms;
}

}  // namespace gfx
