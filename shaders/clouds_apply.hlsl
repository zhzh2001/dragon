// The volumetric clouds over the scene (gfx/clouds.h), at full resolution:
// the resolved half-resolution clouds, alpha-blended as light + scene x
// transmittance. Nothing where a surface stands nearer than the slab -- the
// valley under the layer, the dragon below it -- so the half-resolution
// clouds never bleed over the edge of anything in front of them.
#include "scene_common.hlsl"
#include "clouds_common.hlsl"

#ifdef FRAGMENT_STAGE
TEXTURE2D(cloud_resolved, 0);
DEPTH2D(scene_depth, 1);

float4 fs_main(CloudVertex input) : SV_Target {
    float2 uv = input.uv;
    float3 ray = cloud_ray(uv);
    float2 slab = cloud_slab(scene.camera_position.xyz, ray);
    float surface = cloud_surface_distance(scene_depth.SampleLevel(scene_depth_sampler, uv, 0.0), ray);
    float4 c = cloud_resolved.SampleLevel(cloud_resolved_sampler, uv, 0.0);
    if (slab.x > slab.y || surface <= slab.x) discard;
    float cover = 1.0 - c.a;
    if (cover < 0.002) discard;
    // Alpha blending gives rgb * a + scene * (1 - a): the light, and the
    // scene through what the clouds let pass.
    return float4(c.rgb / cover, cover);
}
#endif
