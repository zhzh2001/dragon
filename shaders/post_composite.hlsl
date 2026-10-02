#include "post_common.hlsl"

#ifdef FRAGMENT_STAGE

// The last word on every pixel of the world: bloom added, exposure, the
// tonemap, then one grade applied to everything -- which is the strongest
// glue there is between assets of different origins -- and the gamma encode.
// The HUD is drawn after this, onto the 8-bit result, so it is never graded.
//
// The tonemap is Reinhard, as it was when every world shader did this itself,
// because every lighting value in the game was tuned against it; what changed
// is that bright things now bloom before they clip instead of desaturating
// toward white, and that there is a grade at all.

float3 tonemap_channel(float3 color) { return color / (1.0 + color); }

// The same curve on the luminance alone, the colour ratios kept, and a bright
// colour that would leave the gamut scaled back by its peak channel instead of
// clipped channel by channel. Per-channel Reinhard turns every bright colour
// white; this keeps fire orange and frost blue however many puffs stack.
float3 tonemap_hue(float3 color) {
    const float3 LUMA = float3(0.2126, 0.7152, 0.0722);
    float l = dot(color, LUMA);
    float3 mapped = color * ((l / (1.0 + l)) / max(l, 1e-4));
    float peak = max(mapped.r, max(mapped.g, mapped.b));
    return peak > 1.0 ? mapped / peak : mapped;
}

float3 tonemap(float3 color, float hue_preserve) {
    return lerp(tonemap_channel(color), tonemap_hue(color), hue_preserve);
}

TEXTURE2D(scene_color, 0);
TEXTURE2D(bloom, 1);

float4 fs_main(PostVertex input) : SV_Target {
    float3 c = scene_color.Sample(scene_color_sampler, input.uv).rgb;
    float3 glow = bloom.Sample(bloom_sampler, input.uv).rgb;
    c += glow * post.grade.w;
    c *= post.grade.x;  // exposure

    // White balance in linear light, before the curve: a warm push raises red
    // and drops blue, tint trades green against magenta.
    float temperature = post.balance.x;
    float tint = post.balance.y;
    c *= float3(1.0 + temperature, 1.0 - tint * 0.5, 1.0 - temperature) *
         float3(1.0, 1.0 + tint, 1.0);

    c = tonemap(max(c, 0.0), post.bloom.w);

    // Everything from here is done on the DISPLAY-REFERRED, gamma-encoded
    // value. The first grade did its contrast in linear light about a pivot
    // of 0.18, and a shadow at 0.02 linear pushed away from that pivot went
    // to 0.005 -- a third of its brightness on screen. The whole picture
    // read a stop darker than the ungraded one, with the dark half crushed.
    // In perceptual space the same dial moves shadows and highlights by the
    // same amount the eye sees, which is what a contrast slider is for.
    c = pow(c, (float3)(1.0 / 2.2));
    const float3 LUMA = float3(0.2126, 0.7152, 0.0722);

    // Split-toning: shadows toward one colour, highlights toward another, by
    // luminance. The tone colours are normalised to unit luminance so they
    // shift hue without changing brightness -- a cool shadow is not a darker
    // shadow.
    float luma = dot(c, LUMA);
    float shadow_weight = 1.0 - smoothstep(0.0, 0.6, luma);
    float highlight_weight = smoothstep(0.5, 1.0, luma);
    float3 shadow_tone = post.shadows.rgb / max(dot(post.shadows.rgb, LUMA), 1e-3);
    float3 highlight_tone = post.highlights.rgb / max(dot(post.highlights.rgb, LUMA), 1e-3);
    c = lerp(c, c * shadow_tone, shadow_weight * post.shadows.a);
    c = lerp(c, c * highlight_tone, highlight_weight * post.highlights.a);

    // Contrast about display middle grey, saturation about the luma, lift and
    // gamma.
    const float pivot = 0.46;
    c = (c - pivot) * post.grade.y + pivot;
    luma = dot(c, LUMA);
    c = lerp((float3)luma, c, post.grade.z);
    c = c + post.balance.z * (1.0 - c);
    c = pow(max(c, 0.0), (float3)(1.0 / max(post.balance.w, 0.05)));

    // Vignette: a soft darkening toward the corners, which is where the eye
    // is not, and which frames the centre without a border.
    float2 d = input.uv - 0.5;
    float v = 1.0 - post.bloom.z * smoothstep(0.35, 0.95, length(d) * 1.4142);
    c *= v;

    return float4(saturate(c), 1.0);
}
#endif
