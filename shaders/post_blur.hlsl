#include "post_common.hlsl"

// Bloom, steps two and three: a 9-tap Gaussian along post.texel.zw (one axis
// per pass). Half resolution, so the whole blur is a few hundred microseconds.
#ifdef FRAGMENT_STAGE
TEXTURE2D(source, 0);

float4 fs_main(PostVertex input) : SV_Target {
    const float weights[5] = {0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162};
    float2 stride = post.texel.xy * post.texel.zw;
    float3 c = source.Sample(source_sampler, input.uv).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        float2 offset = stride * float(i) * 1.5;
        c += source.Sample(source_sampler, input.uv + offset).rgb * weights[i];
        c += source.Sample(source_sampler, input.uv - offset).rgb * weights[i];
    }
    return float4(c, 1.0);
}
#endif
