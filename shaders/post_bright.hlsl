#include "post_common.hlsl"

// Bloom, step one: the bright part of the linear scene, at half resolution.
// A soft knee below the threshold so a surface just under it contributes a
// little instead of popping in as it crosses. Four taps, which is the
// downsample.
#ifdef FRAGMENT_STAGE
TEXTURE2D(scene_color, 0);

float4 fs_main(PostVertex input) : SV_Target {
    float2 t = post.texel.xy;
    float3 c = scene_color.Sample(scene_color_sampler, input.uv + float2(-t.x, -t.y)).rgb +
               scene_color.Sample(scene_color_sampler, input.uv + float2( t.x, -t.y)).rgb +
               scene_color.Sample(scene_color_sampler, input.uv + float2(-t.x,  t.y)).rgb +
               scene_color.Sample(scene_color_sampler, input.uv + float2( t.x,  t.y)).rgb;
    c *= 0.25;
    float brightness = max(c.r, max(c.g, c.b));
    float threshold = post.bloom.x;
    float knee = max(post.bloom.y, 1e-3);
    // Quadratic soft threshold (the Unity/Jimenez form).
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
    return float4(c * contribution, 1.0);
}
#endif
