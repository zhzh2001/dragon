// The volumetric clouds' resolve (gfx/clouds.h): this frame's dithered march
// blended into the history, reprojected through where the view ray enters
// the slab (or 400 m ahead, inside it) -- the one depth the march does not
// need to store, since the slab is a plane -- and clamped to this frame's
// neighbourhood, so a cloud moving across a ridge does not leave a trail.
#include "scene_common.hlsl"
#include "clouds_common.hlsl"

#ifdef FRAGMENT_STAGE
TEXTURE2D(cloud_raw, 0);
TEXTURE2D(cloud_history, 1);

float4 fs_main(CloudVertex input) : SV_Target {
    float2 uv = input.uv;
    float2 t = vol.texel.xy;
    float4 current = cloud_raw.SampleLevel(cloud_raw_sampler, uv, 0.0);
    float4 low = current, high = current;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float4 s = cloud_raw.SampleLevel(cloud_raw_sampler, uv + float2(float(x), float(y)) * t, 0.0);
            low = min(low, s);
            high = max(high, s);
        }
    }
    float3 ray = cloud_ray(uv);
    float2 slab = cloud_slab(scene.camera_position.xyz, ray);
    float distance = slab.x > slab.y ? 1e5 : (slab.x > 0.0 ? slab.x : 400.0);
    float4 before = mul(vol.prev_view_proj, float4(ray * min(distance, 1e5) + vol.camera_delta.xyz, 1.0));
    float2 before_uv = float2(before.x / before.w * 0.5 + 0.5, 0.5 - before.y / before.w * 0.5);
    float weight = vol.camera_delta.w;
    if (before.w <= 0.0 || any(before_uv < 0.0) || any(before_uv > 1.0)) weight = 0.0;
    float4 past = clamp(cloud_history.SampleLevel(cloud_history_sampler, before_uv, 0.0), low, high);
    return lerp(current, past, weight);
}
#endif
