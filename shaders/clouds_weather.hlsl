// The volumetric clouds' weather map (gfx/clouds.h): how much cloud stands
// over each 156 m of the 80 km about the camera (r), and how tall it
// grows there (g) -- so the march reads one texel instead of four octaves
// of noise a step. The coverage is the sky's and the ground shadows' own
// field (scene_common.hlsl, cloud_coverage), ramped more softly so a heap
// thins at its edge instead of stopping.
#include "scene_common.hlsl"
#include "clouds_common.hlsl"

#ifdef FRAGMENT_STAGE
float4 fs_main(CloudVertex input) : SV_Target {
    float2 xz = vol.weather.xy + (input.uv - 0.5) * 2.0 * vol.weather.z;
    float cover = scene.clouds.x;
    float caps = thermal_caps(xz);
    float coverage = max(saturate((cloud_field(xz) - (1.0 - cover) * 0.85) / (0.18 + vol.shape.w * 0.4)), caps);
    // Towering here, flat there: a broad noise of its own; a thermal's
    // cumulus heaps up.
    float tall = max(smoothstep(0.3, 0.8, cloud_value((xz + cloud_drift()) / 7000.0 + 41.0)), caps * 0.8);
    return float4(coverage, tall, 0.0, 1.0);
}
#endif
