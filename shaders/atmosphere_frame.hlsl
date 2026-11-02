// The atmosphere's per-frame atlas (atmosphere_common.hlsl): the aerial
// perspective froxels in rows 0-127, the sky-view map in rows 128-223, the
// sky light on nine normals in row 224. One pass, branching on the row; all
// three depend on the camera and the sun, so it runs every frame, before the
// main pass: 57K texels, a few dozen steps each.
#define ATMO_TABLE_PASS
#define ATMO_TERRAIN_SHADOW  // the froxels' sun light, cut by the terrain (atmosphere_common.hlsl)
#include "scene_common.hlsl"
#include "atmosphere_common.hlsl"

#ifdef FRAGMENT_STAGE
ConstantBuffer<AtmosphereUniforms> atmosphere : UNIFORM_SLOT(1);
TEXTURE2D(transmittance_lut, 0);
TEXTURE2D(multiscatter_lut, 1);

float4 fs_main(AtmoVertex input) : SV_Target {
    float2 pix = input.uv * ATMO_ATLAS_SIZE;  // the pixel centre (AtmoVertex)
    float3 sun = normalize(scene.sun.xyz);
    float r = atmosphere.radii.x + atmosphere.radii.z;
    float3 origin = float3(0.0, r, 0.0);

    if (pix.y >= ATMO_AMBIENT_ROW) {
        // The sky light on one normal: the sky's radiance averaged over the
        // hemisphere about it, cosine-weighted (so the mean is E / pi), the
        // directions below the horizon left to the ground's bounce.
        float index = floor(pix.x);
        if (index > 8.0 || pix.y >= ATMO_AMBIENT_ROW + 1.0) return (float4)0.0;
        float azimuth = (index - 1.0) * (ATMO_PI / 4.0);
        float3 n = index < 0.5 ? float3(0.0, 1.0, 0.0) : float3(cos(azimuth), 0.0, sin(azimuth));
        float3 t = abs(n.y) > 0.5 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
        float3 b = normalize(cross(n, t));
        t = cross(b, n);
        float3 sum = (float3)0.0;
        const int SQRT_DIRS = 6;
        for (int j = 0; j < SQRT_DIRS; ++j) {
            for (int i = 0; i < SQRT_DIRS; ++i) {
                float u = (float(i) + 0.5) / float(SQRT_DIRS);
                float v = (float(j) + 0.5) / float(SQRT_DIRS);
                float radius = sqrt(u);
                float phi = 2.0 * ATMO_PI * v;
                float3 dir = normalize(t * (radius * cos(phi)) + b * (radius * sin(phi)) + n * sqrt(1.0 - u));
                if (dir.y < 0.0) continue;
                AtmoIntegral it = atmo_integrate(atmosphere, origin, dir, sun, 1e9, 16, false, true, false,
                                                 transmittance_lut, transmittance_lut_sampler,
                                                 multiscatter_lut, multiscatter_lut_sampler);
                sum += it.luminance;
            }
        }
        return float4(sum / float(SQRT_DIRS * SQRT_DIRS), 1.0);
    }

    if (pix.y < ATMO_SKY_VIEW_ROW) {
        // A froxel: its tile is the slice, its place in the tile the screen
        // position, at the texel's centre.
        float2 tile = floor(pix / ATMO_SLICE_SIZE);
        float slice = tile.y * 8.0 + tile.x;
        float2 screen_uv = (pix - tile * ATMO_SLICE_SIZE) / ATMO_SLICE_SIZE;
        float2 ndc = float2(screen_uv.x * 2.0 - 1.0, 1.0 - screen_uv.y * 2.0);
        float tan_half_fov = scene.view_params.x;
        float aspect = scene.view_params.y;
        float3 ray = normalize(scene.camera_forward.xyz + scene.camera_right.xyz * ndc.x * tan_half_fov * aspect +
                               scene.camera_up.xyz * ndc.y * tan_half_fov);
        float w = (slice + 1.0) / ATMO_SLICES;
        float distance_km = w * w * atmosphere.sun_direction.w;
        atmo_shadow_air = true;
        AtmoIntegral it = atmo_integrate(atmosphere, origin, ray, sun, distance_km, 16, false, true, false,
                                         transmittance_lut, transmittance_lut_sampler,
                                         multiscatter_lut, multiscatter_lut_sampler);
        float transmittance = dot(it.transmittance, (float3)(1.0 / 3.0));
        return float4(it.luminance, transmittance);
    }

    float2 unit = atmo_sub_uv_to_unit((pix - float2(0.0, ATMO_SKY_VIEW_ROW)) / ATMO_SKY_VIEW_SIZE, ATMO_SKY_VIEW_SIZE);
    float view_cos_zenith, light_view_cos;
    atmo_sky_view_params(atmosphere.radii.x, unit, r, view_cos_zenith, light_view_cos);
    // Azimuth is measured from the sun's: rebuild the direction about it.
    float2 sun_h = length(sun.xz) > 1e-4 ? normalize(sun.xz) : float2(1.0, 0.0);
    float2 side = float2(-sun_h.y, sun_h.x);
    float sin_zenith = sqrt(saturate(1.0 - view_cos_zenith * view_cos_zenith));
    float sin_azimuth = sqrt(saturate(1.0 - light_view_cos * light_view_cos));
    float2 h = sun_h * light_view_cos + side * sin_azimuth;
    float3 dir = float3(h.x * sin_zenith, view_cos_zenith, h.y * sin_zenith);
    // With the ground: a ray below the horizon ends on lit land, hazed by the
    // air before it, so the world past the terrain's edge reads as more
    // valley in the distance, not as a dark sea.
    AtmoIntegral it = atmo_integrate(atmosphere, origin, dir, sun, 1e9, 30, true, true, false,
                                     transmittance_lut, transmittance_lut_sampler,
                                     multiscatter_lut, multiscatter_lut_sampler);
    return float4(it.luminance, 1.0);
}
#endif
