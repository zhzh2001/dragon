#include "gfx/prelit.h"

#include <algorithm>
#include <cmath>

#include "gfx/palette.h"

namespace gfx::prelit {
namespace {

using core::Vec3;
using core::Vec4;

float saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
Vec3 saturate(Vec3 v) { return Vec3{saturate(v.x), saturate(v.y), saturate(v.z)}; }
float smoothstep(float a, float b, float x) {
    const float t = saturate((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}
Vec3 mix(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
Vec3 mul(Vec3 a, Vec3 b) { return Vec3{a.x * b.x, a.y * b.y, a.z * b.z}; }
Vec3 rgb(const Vec4& v) { return Vec3{v.x, v.y, v.z}; }
Vec3 pow3(Vec3 v, float e) { return Vec3{std::pow(v.x, e), std::pow(v.y, e), std::pow(v.z, e)}; }

}  // namespace

Vec3 direct_sun(const SceneUniforms& scene, Vec3 normal) {
    const float wrap = scene.light_params.x;
    const Vec3 to_sun = core::normalize(rgb(scene.sun));
    const float wrapped = saturate((core::dot(normal, to_sun) + wrap) / (1.0f + wrap));
    return rgb(scene.sun_color) * (scene.sun.w * wrapped);
}

Vec3 ambient_light(const SceneUniforms& scene, Vec3 normal) {
    const float sky_amount = saturate(normal.y * 0.5f + 0.5f);
    const Vec3 sky = mix(rgb(scene.sky_horizon), rgb(scene.sky_zenith), 0.6f);
    const Vec3 bounce = mul(rgb(scene.palette[int(PaletteEntry::GroundBounce)]), rgb(scene.sun_color)) *
                        (scene.light_params.z * saturate(scene.sun.y * 4.0f));
    return mix(bounce, sky, sky_amount) * scene.terrain_params.w;
}

Vec3 translucent_sun(const SceneUniforms& scene, Vec3 normal, float translucency) {
    const Vec3 to_sun = core::normalize(rgb(scene.sun));
    const float behind = saturate(-core::dot(normal, to_sun));
    return rgb(scene.sun_color) * (scene.sun.w * behind * translucency);
}

Vec3 ldr_encode_fast(Vec3 c, Vec4 grade) {
    c = c * grade.x;
    c = Vec3{c.x / (1.0f + c.x), c.y / (1.0f + c.y), c.z / (1.0f + c.z)};
    c = pow3(c, 1.0f / 2.2f);
    c = (c - Vec3{0.46f, 0.46f, 0.46f}) * grade.y + Vec3{0.46f, 0.46f, 0.46f};
    const float luma = core::dot(c, Vec3{0.2126f, 0.7152f, 0.0722f});
    c = mix(Vec3{luma, luma, luma}, c, grade.z);
    return saturate(c);
}

Vec3 ff_albedo(Vec3 linear_color) { return pow3(saturate(linear_color), 1.0f / 2.2f); }

Vec3 ff_lit(const SceneUniforms& scene, Vec3 albedo_linear, Vec3 normal, float translucency) {
    const Vec3 light = direct_sun(scene, normal) + ambient_light(scene, normal) + translucent_sun(scene, normal, translucency);
    return saturate(mul(ff_albedo(albedo_linear), ldr_encode_fast(light, scene.output_grade)));
}

Vec3 terrain_diffuse(const SceneUniforms& scene, Vec3 p, Vec3 normal, float patches, Vec3 tint) {
    normal = core::normalize(normal);
    const float flatness = saturate(normal.y);
    const float water_level = scene.terrain_params.x;
    const float snow_line = scene.terrain_params.y;
    const float rock_slope = scene.terrain_params.z;
    const auto pal = [&](PaletteEntry e) { return rgb(scene.palette[int(e)]); };
    const Vec3 grass = mix(pal(PaletteEntry::Grass), pal(PaletteEntry::GrassDry), smoothstep(0.35f, 0.75f, patches));
    const Vec3 rock = mix(pal(PaletteEntry::RockDark), pal(PaletteEntry::Rock), smoothstep(0.25f, 0.8f, patches));
    const float bar_top = water_level + 1.0f + 3.0f * patches;
    const float sand = 1.0f - smoothstep(water_level + 0.3f, bar_top, p.y);
    const float slope_blend = 1.0f - smoothstep(rock_slope, rock_slope + 0.22f, flatness);
    const float altitude_rockiness = smoothstep(snow_line * 0.5f, snow_line, p.y) * 0.6f;
    const float ragged_snow_line = snow_line + (patches - 0.5f) * 90.0f;
    const float snow = smoothstep(ragged_snow_line, ragged_snow_line + 120.0f, p.y) * smoothstep(0.52f, 0.78f, flatness);
    Vec3 albedo = mix(grass, pal(PaletteEntry::Sand), sand);
    albedo = mix(albedo, rock, saturate(slope_blend + altitude_rockiness));
    albedo = mix(albedo, pal(PaletteEntry::Snow), snow);
    return ff_lit(scene, mul(albedo, tint), normal, 0.0f);
}

}  // namespace gfx::prelit
