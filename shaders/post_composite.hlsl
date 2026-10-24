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
#ifdef ATMOSPHERE
TEXTURE2D(occlusion, 2);  // gfx/ssao.h: r the ambient occlusion, half resolution
#endif

// FXAA, the spatial alternative to the temporal resolve (gfx/taa.h): Lottes'
// original filter, on the luminance as the eye will see it (the tonemapped
// and gamma-encoded luma, estimated here before the grade). It finds the
// edge's direction from four diagonal taps and blends along it, so a river's
// edge against the bank stops stair-stepping without the softness the
// temporal resolve gives a moving forest. post.texel.w turns it on.
float fxaa_luma(float3 c) {
    float l = dot(c, float3(0.2126, 0.7152, 0.0722));
    return sqrt(l / (1.0 + l));
}
float3 fxaa(float2 uv, float2 t) {
    float3 rgb_m = scene_color.SampleLevel(scene_color_sampler, uv, 0.0).rgb;
    float l_nw = fxaa_luma(scene_color.SampleLevel(scene_color_sampler, uv + float2(-0.5, -0.5) * t, 0.0).rgb);
    float l_ne = fxaa_luma(scene_color.SampleLevel(scene_color_sampler, uv + float2(0.5, -0.5) * t, 0.0).rgb);
    float l_sw = fxaa_luma(scene_color.SampleLevel(scene_color_sampler, uv + float2(-0.5, 0.5) * t, 0.0).rgb);
    float l_se = fxaa_luma(scene_color.SampleLevel(scene_color_sampler, uv + float2(0.5, 0.5) * t, 0.0).rgb);
    float l_m = fxaa_luma(rgb_m);
    float l_min = min(l_m, min(min(l_nw, l_ne), min(l_sw, l_se)));
    float l_max = max(l_m, max(max(l_nw, l_ne), max(l_sw, l_se)));
    if (l_max - l_min < max(0.0312, l_max * 0.125)) return rgb_m;  // no edge worth it
    float2 dir = float2(-((l_nw + l_ne) - (l_sw + l_se)), (l_nw + l_sw) - (l_ne + l_se));
    float reduce = max((l_nw + l_ne + l_sw + l_se) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
    float scale = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * scale, -8.0, 8.0) * t;
    float3 a = 0.5 * (scene_color.SampleLevel(scene_color_sampler, uv + dir * (1.0 / 3.0 - 0.5), 0.0).rgb +
                      scene_color.SampleLevel(scene_color_sampler, uv + dir * (2.0 / 3.0 - 0.5), 0.0).rgb);
    float3 b = a * 0.5 + 0.25 * (scene_color.SampleLevel(scene_color_sampler, uv - dir * 0.5, 0.0).rgb +
                                 scene_color.SampleLevel(scene_color_sampler, uv + dir * 0.5, 0.0).rgb);
    float l_b = fxaa_luma(b);
    return (l_b < l_min || l_b > l_max) ? a : b;
}

#ifdef ATMOSPHERE
// The lens (GAPS.md G9): Century and Ace Combat put the sun in the shot, with
// its glare and the ghosts a camera's glass makes of it. Two parts, both in
// linear light before the exposure, so they bloom and tonemap with the rest.
//
// The ghosts: the bloom's bright spots mirrored through the centre at four
// scales, each tinted as a coated lens tints them, fading toward the frame's
// edge (Chapman's screen-space flare), and a thin halo ring. The bloom is
// already thresholded, so only the sun, fire and glinting snow make any.
float3 lens_ghosts(float2 uv) {
    float2 mirrored = 1.0 - uv;
    float2 to_centre = 0.5 - mirrored;
    const float3 TINT[4] = {float3(1.0, 0.85, 0.65), float3(0.7, 0.85, 1.0), float3(0.95, 0.75, 1.0), float3(0.8, 1.0, 0.85)};
    const float SCALE[4] = {0.35, 0.75, 1.3, 1.75};
    float3 sum = (float3)0.0;
    for (int i = 0; i < 4; ++i) {
        float2 at = mirrored + to_centre * SCALE[i];
        float edge = saturate(1.0 - length(at - 0.5) * 1.6);
        // Only what is far brighter than a sunlit cloud: the clouds' own
        // bloom made a purple disc in the middle of the sky.
        float3 source = max(bloom.SampleLevel(bloom_sampler, at, 0.0).rgb - 0.5, 0.0);
        sum += source * TINT[i] * (edge * edge * edge);
    }
    float2 ring = normalize(to_centre + 1e-5) * 0.38;
    float halo = pow(saturate(1.0 - abs(length(mirrored + ring - 0.5) - 0.38) * 12.0), 2.0);
    sum += max(bloom.SampleLevel(bloom_sampler, mirrored + ring, 0.0).rgb - 0.5, 0.0) * float3(0.8, 0.85, 1.0) * halo * 0.5;
    return sum;
}

// The glare: rays and a broad veil about the sun when it is in the frame,
// as bright as the sun disc that shows -- five taps of the scene about it, so
// a ridge or a cloud in front puts it out.
float3 sun_glare(float2 uv, float2 sun_uv, float aspect) {
    float2 d = (uv - sun_uv) * float2(aspect, 1.0);
    float r = length(d);
    // Past 0.6 of the frame's height every term below is under a percent.
    if (r > 0.6 || any(sun_uv < -0.2) || any(sun_uv > 1.2)) return (float3)0.0;
    float2 t = post.texel.xy * 3.0;
    float3 seen = (scene_color.SampleLevel(scene_color_sampler, sun_uv, 0.0).rgb +
                   scene_color.SampleLevel(scene_color_sampler, sun_uv + float2(t.x, 0.0), 0.0).rgb +
                   scene_color.SampleLevel(scene_color_sampler, sun_uv - float2(t.x, 0.0), 0.0).rgb +
                   scene_color.SampleLevel(scene_color_sampler, sun_uv + float2(0.0, t.y), 0.0).rgb +
                   scene_color.SampleLevel(scene_color_sampler, sun_uv - float2(0.0, t.y), 0.0).rgb) * 0.2;
    // The disc is tens of units bright; the sky beside it two or three.
    float visible = smoothstep(4.0, 20.0, dot(seen, float3(0.2126, 0.7152, 0.0722)));
    if (visible <= 0.0) return (float3)0.0;
    float3 colour = seen / max(max(seen.r, max(seen.g, seen.b)), 1e-4);
    float angle = atan2(d.y, d.x);
    // Six long rays and six short ones between, turning a little with the
    // sun's place so they do not sit still on the screen.
    float turn = (sun_uv.x + sun_uv.y) * 1.3;
    float rays = pow(abs(cos(angle * 3.0 + turn)), 60.0) + 0.5 * pow(abs(cos(angle * 3.0 + turn + 1.5708)), 90.0);
    // The veil kept close: spread over the frame, it greyed every shot with
    // the sun in it, the dragon's dark hide most.
    float3 glare = colour * (rays * 0.6 / (1.0 + r * 40.0) + 0.4 * exp(-r * 9.0) + 0.04 * exp(-r * 3.0));
    return glare * visible;
}
#endif

float4 fs_main(PostVertex input) : SV_Target {
    float3 c = post.texel.w > 0.5 ? fxaa(input.uv, post.texel.xy) : scene_color.Sample(scene_color_sampler, input.uv).rgb;
#ifdef ATMOSPHERE
    // A world smaller than the screen (dynamic resolution, the Resolution
    // setting) stretched by Catmull-Rom, not bilinearly, which softened
    // every edge; FXAA's own taps already follow the edges.
    if (post.texel.w < 0.5 && post.occlusion.y > 0.5) c = sample_catmull_rom(scene_color, scene_color_sampler, input.uv, post.texel.xy);
#endif
    // Sharpening, for a picture the temporal resolve softened (gfx/taa.h):
    // the pixel pushed away from its four neighbours' mean, on its luminance
    // so edges gain no colour fringe. texel.z is the amount (0: none).
    if (post.texel.z > 0.0) {
        float2 t = post.texel.xy;
        float3 around = scene_color.Sample(scene_color_sampler, input.uv + float2(t.x, 0.0)).rgb +
                        scene_color.Sample(scene_color_sampler, input.uv - float2(t.x, 0.0)).rgb +
                        scene_color.Sample(scene_color_sampler, input.uv + float2(0.0, t.y)).rgb +
                        scene_color.Sample(scene_color_sampler, input.uv - float2(0.0, t.y)).rgb;
        const float3 L = float3(0.2126, 0.7152, 0.0722);
        float l = dot(c, L);
        float detail = l - dot(around, L) * 0.25;
        // Relative to the pixel's own brightness, and limited, so a sun
        // glint or a dark crevice does not ring.
        float gain = clamp(detail * post.texel.z / max(l, 1e-3), -0.35, 0.35);
        c *= 1.0 + gain;
    }
#ifdef ATMOSPHERE
    // The ambient occlusion, before the bloom: a darkened crevice does not
    // glow less, but it is the scene's own light that it takes away -- the
    // ambient part of it, which the world shaders leave in the alpha
    // (occlusion_share): the sun reaching a field beside a tree is not
    // occluded by the tree.
    float share = scene_color.Sample(scene_color_sampler, input.uv).a;
    c *= lerp(1.0, occlusion.Sample(occlusion_sampler, input.uv).r, post.occlusion.x * share);
#endif
    float3 glow = bloom.Sample(bloom_sampler, input.uv).rgb;
    c += glow * post.grade.w;
#ifdef ATMOSPHERE
    if (post.flare.z > 0.0) c += lens_ghosts(input.uv) * post.flare.z;
    if (post.flare.w > 0.0) c += sun_glare(input.uv, post.flare.xy, post.texel.y / max(post.texel.x, 1e-6)) * post.flare.w;
#endif
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
