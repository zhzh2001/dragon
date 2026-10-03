#include "scene_common.hlsl"

// A still water surface: one quad at the water line, fresnel between a deep
// colour and the reflected sky, a little animated ripple in the normal, and a
// sun glint. It exists for the river: the valley floor sits above the water
// line everywhere else, so the terrain simply hides it there.

struct VertexIn {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 color    : TEXCOORD2;
};

#ifdef SM2
// SM2 (docs/PORTING.md, R4): "fresnel only". The water is one quad across the
// map, so nothing can be lit per vertex; the pixel stage keeps the fresnel
// between the deep colour and a cheaper sky (no sun disc), and the fog, and
// drops the ripples and the glint.
struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.world_position = input.position;
    o.clip_position = mul(scene.view_proj, float4(input.position, 1.0));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
// sky_color() without the sun disc and glow.
float3 sky_plain(float3 ray) {
    float horizon_blend = pow(saturate(1.0 - abs(ray.y)), 4.0);
    float3 base = lerp(scene.sky_zenith.rgb, scene.sky_horizon.rgb, horizon_blend);
    return lerp(base, scene.fog_color.rgb, saturate(-ray.y * 6.0) * 0.7);
}

float4 fs_main(VertexOut input) : SV_Target {
    float3 to_point = input.world_position - scene.camera_position.xyz;
    float dist = length(to_point);
    float3 ray = to_point / max(dist, 1e-3);
    // A flat surface: the reflection is the ray mirrored upward.
    float3 reflection = sky_plain(float3(ray.x, abs(ray.y), ray.z));
    float fresnel = 0.04 + 0.96 * pow(1.0 - saturate(-ray.y), 5.0);
    float3 color = lerp(scene.palette[PALETTE_WATER_DEEP].rgb, reflection, fresnel);
    float density = scene.fog_color.a * exp(-max(input.world_position.y, 0.0) * 0.0018);
    float fog = saturate(1.0 - exp(-pow(dist * density, 2.0)));
    return float4(scene_out(lerp(color, sky_plain(ray), fog)), 1.0);
}
#endif
#else  // the per-pixel path

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.world_position = input.position;
    o.clip_position = mul(scene.view_proj, float4(input.position, 1.0));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
float4 fs_main(VertexOut input) : SV_Target {
    float t = scene.view_params.z;
    float2 p = input.world_position.xz;
    // Two crossed ripple trains; small, so the reflection stays readable.
    float3 normal = normalize(float3(sin(p.x * 0.09 + t * 1.1) * 0.04 + sin(p.y * 0.13 - t * 0.8) * 0.03,
                                     1.0,
                                     cos(p.y * 0.07 + t * 0.7) * 0.04 + cos(p.x * 0.05 + t * 0.5) * 0.03));
    float3 to_camera = scene.camera_position.xyz - input.world_position;
    float dist = length(to_camera);
    float3 ray = -to_camera / max(dist, 1e-3);
    float3 reflected = reflect(ray, normal);
    reflected.y = abs(reflected.y);  // never reflect what is under the water
    float3 reflection = sky_color(reflected);

    const float3 DEEP = scene.palette[PALETTE_WATER_DEEP].rgb;
    float cosine = saturate(dot(-ray, normal));
    float fresnel = 0.04 + 0.96 * pow(1.0 - cosine, 5.0);
    float3 color = lerp(DEEP, reflection, fresnel);
    // Sun glint on the ripples.
    float glint = pow(saturate(dot(reflected, normalize(scene.sun.xyz))), 180.0);
    color += scene.sun_color.rgb * scene.sun.w * glint * 0.6;

    return float4(scene_out(apply_fog(color, input.world_position)), 1.0);
}
#endif
#endif  // SM2
