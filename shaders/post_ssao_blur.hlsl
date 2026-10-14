// The occlusion's blur (post_ssao.hlsl): 5x5 at half resolution, each tap
// weighted by how near its distance is to the centre's (kept in g), so the
// ground's occlusion does not bleed onto the tree standing in front of it.
#include "common.hlsl"

struct BlurUniforms {
    float4 texel;  // xy one half-resolution texel in uv
};

struct BlurVertex {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
};

#ifdef VERTEX_STAGE
BlurVertex vs_main(VERTEX_ID_INPUT) {
    const int vertex_id = VERTEX_ID;
    const float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    BlurVertex o;
    o.clip_position = float4(positions[vertex_id], 0.0, 1.0);
    o.uv = float2(positions[vertex_id].x * 0.5 + 0.5, 1.0 - (positions[vertex_id].y * 0.5 + 0.5));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
ConstantBuffer<BlurUniforms> blur : UNIFORM_SLOT(0);
TEXTURE2D(occlusion, 0);

float4 fs_main(BlurVertex input) : SV_Target {
    float2 centre = occlusion.SampleLevel(occlusion_sampler, input.uv, 0.0).rg;
    float sum = 0.0, weight = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            float2 s = occlusion.SampleLevel(occlusion_sampler, input.uv + float2(float(x), float(y)) * blur.texel.xy, 0.0).rg;
            float w = exp(-abs(s.g - centre.g) / max(centre.g * 0.04, 1e-5));
            sum += s.r * w;
            weight += w;
        }
    }
    return float4(sum / max(weight, 1e-5), centre.g, 0.0, 1.0);
}
#endif
