// Screen-space ambient occlusion (docs/FIDELITY.md, F6), the modern tier's:
// how much of the hemisphere above each pixel the depth buffer shows blocked
// nearby -- the ground under a tree, a rock's foot, the dragon's belly over
// grass, a crease in the hide. Half resolution, twelve samples on a rotated
// spiral within a few metres, reconstructed from the reversed-Z depth; the
// blur (post_ssao_blur.hlsl) smooths it and the composite multiplies it in.
#include "common.hlsl"

struct SsaoUniforms {
    float4 projection;  // x tan(fov/2), y aspect, z the near plane (m), w the angle bias
    float4 params;      // x radius (m), y intensity, z fade start, w fade end (m)
    float4 texel;       // xy one full-resolution depth texel in uv
};

struct SsaoVertex {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
};

#ifdef VERTEX_STAGE
SsaoVertex vs_main(VERTEX_ID_INPUT) {
    const int vertex_id = VERTEX_ID;
    const float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    SsaoVertex o;
    o.clip_position = float4(positions[vertex_id], 0.0, 1.0);
    o.uv = float2(positions[vertex_id].x * 0.5 + 0.5, 1.0 - (positions[vertex_id].y * 0.5 + 0.5));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
ConstantBuffer<SsaoUniforms> ssao : UNIFORM_SLOT(0);
DEPTH2D(scene_depth, 0);

// View space (metres, -z ahead) from a uv and the reversed-Z depth there:
// the infinite projection stores near / distance.
float3 view_position(float2 uv) {
    float depth = scene_depth.SampleLevel(scene_depth_sampler, uv, 0.0);
    float d = ssao.projection.z / max(depth, 1e-7);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return float3(ndc.x * ssao.projection.x * ssao.projection.y * d, ndc.y * ssao.projection.x * d, -d);
}

float4 fs_main(SsaoVertex input) : SV_Target {
    float2 uv = input.uv;
    float depth = scene_depth.SampleLevel(scene_depth_sampler, uv, 0.0);
    if (depth <= 1e-7) return (float4)1.0;  // the sky
    float3 p = view_position(uv);
    float d = -p.z;
    if (d > ssao.params.w) return (float4)1.0;

    // The normal from the nearer-agreeing neighbour on each axis, so an edge
    // does not bend it toward what is behind.
    float2 t = ssao.texel.xy;
    float3 px0 = view_position(uv - float2(t.x, 0.0)), px1 = view_position(uv + float2(t.x, 0.0));
    float3 py0 = view_position(uv - float2(0.0, t.y)), py1 = view_position(uv + float2(0.0, t.y));
    float3 dx = abs(px1.z - p.z) < abs(p.z - px0.z) ? px1 - p : p - px0;
    float3 dy = abs(py1.z - p.z) < abs(p.z - py0.z) ? py1 - p : p - py0;
    float3 n = normalize(cross(dy, dx));
    if (dot(n, p) > 0.0) n = -n;  // toward the eye

    float radius = ssao.params.x;
    // The radius on screen, in uv, held between a few pixels and a sixth of
    // the height so a close-up does not sample the whole screen.
    float r_uv = clamp(0.5 * radius / (d * ssao.projection.x), 3.0 * t.y, 0.16);
    float2 r = float2(r_uv / ssao.projection.y, r_uv);

    float noise = frac(52.9829189 * frac(dot(input.clip_position.xy, float2(0.06711056, 0.00583715))));
    float angle = noise * 6.2831853;
    const int SAMPLES = 12;
    float occlusion = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        // A Vogel spiral, rotated per pixel.
        float s = (float(i) + 0.5) / float(SAMPLES);
        float a = float(i) * 2.39996 + angle;
        float2 o = float2(cos(a), sin(a)) * sqrt(s) * r;
        float3 q = view_position(uv + o);
        float3 v = q - p;
        float vv = dot(v, v);
        float range = radius * radius;
        // How far above the surface the sample stands, times its nearness.
        occlusion += saturate((dot(v, n) * rsqrt(max(vv, 1e-6)) - ssao.projection.w) / (1.0 - ssao.projection.w)) *
                     saturate(1.0 - vv / range);
    }
    float ao = saturate(1.0 - occlusion / float(SAMPLES) * ssao.params.y);
    ao = lerp(ao, 1.0, smoothstep(ssao.params.z, ssao.params.w, d));
    return float4(ao, d / 4096.0, 0.0, 1.0);
}
#endif
