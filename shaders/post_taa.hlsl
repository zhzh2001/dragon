// Temporal anti-aliasing (docs/FIDELITY.md, F5): the modern tier's world
// renders each frame from a sub-pixel jitter (gfx/taa.h), and this pass
// blends the frame into the history of earlier ones, reprojected through
// the depth buffer. A static edge then converges to a supersampled one, an
// alpha-tested crown stops shimmering, and the clouds' per-pixel dither
// averages out.
//
// Reprojection is by depth alone: a pixel is taken to be a fixed point of
// the world, which is true of the ground, the trees and the sky (whose
// depth is infinite, and whose points reproject as directions: the
// homogeneous w stays 0). It is not true of a creature moving with the
// camera; there the neighbourhood clamp is what keeps the history from
// smearing: history outside the colours of the current 3x3 neighbourhood
// is pulled into them (variance clipping in YCoCg, Salvi 2016).
//
// The matrices are camera-relative, so the reconstruction never sees
// world coordinates in the kilometres.
#include "common.hlsl"

struct TaaUniforms {
    float4x4 inv_view_proj;   // this frame's, unjittered, camera-relative
    float4x4 prev_view_proj;  // last frame's, unjittered, relative to last frame's camera
    float4 camera_delta;      // xyz this camera minus last frame's, w the history's weight (0: reset)
    float4 texel;             // xy one texel in uv
};

struct TaaVertex {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
};

#ifdef VERTEX_STAGE
TaaVertex vs_main(VERTEX_ID_INPUT) {
    const int vertex_id = VERTEX_ID;
    const float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    TaaVertex o;
    o.clip_position = float4(positions[vertex_id], 0.0, 1.0);
    o.uv = float2(positions[vertex_id].x * 0.5 + 0.5, 1.0 - (positions[vertex_id].y * 0.5 + 0.5));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
ConstantBuffer<TaaUniforms> taa : UNIFORM_SLOT(0);
TEXTURE2D(current, 0);
TEXTURE2D(history, 1);
DEPTH2D(scene_depth, 2);
TEXTURE2D(creature_motion, 3);  // velocity_skinned.hlsl: rg uv motion, a 1 on a creature

// Blend in a range the eye weighs evenly: a bright sun glint must not
// dominate the average of its neighbours (and a firefly not linger).
float3 compress(float3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }
float3 expand(float3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 1e-4); }
float3 to_ycocg(float3 c) {
    return float3(dot(c, float3(0.25, 0.5, 0.25)), dot(c, float3(0.5, 0.0, -0.5)), dot(c, float3(-0.25, 0.5, -0.25)));
}
float3 from_ycocg(float3 c) { return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }

// The history, bicubic (Catmull-Rom in five bilinear taps, Jimenez's
// arrangement): resampled bilinearly every frame as the camera moves, it
// blurred a little more each time and the whole picture went soft in flight.
float3 sample_history(float2 uv) {
    float2 size = 1.0 / taa.texel.xy;
    float2 position = uv * size;
    float2 centre = floor(position - 0.5) + 0.5;
    float2 f = position - centre;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 w12 = w1 + w2;
    float2 tc12 = (centre + w2 / w12) * taa.texel.xy;
    float2 tc0 = (centre - 1.0) * taa.texel.xy;
    float2 tc3 = (centre + 2.0) * taa.texel.xy;
    float3 c = history.SampleLevel(history_sampler, float2(tc12.x, tc0.y), 0.0).rgb * (w12.x * w0.y) +
               history.SampleLevel(history_sampler, float2(tc0.x, tc12.y), 0.0).rgb * (w0.x * w12.y) +
               history.SampleLevel(history_sampler, tc12, 0.0).rgb * (w12.x * w12.y) +
               history.SampleLevel(history_sampler, float2(tc3.x, tc12.y), 0.0).rgb * (w3.x * w12.y) +
               history.SampleLevel(history_sampler, float2(tc12.x, tc3.y), 0.0).rgb * (w12.x * w3.y);
    float total = w12.x * w0.y + w0.x * w12.y + w12.x * w12.y + w3.x * w12.y + w12.x * w3.y;
    return max(c / total, 0.0);
}

float4 fs_main(TaaVertex input) : SV_Target {
    float2 uv = input.uv;
    float2 t = taa.texel.xy;

    // The nearest surface in the 3x3 (reversed-Z: the largest depth), so an
    // edge moves with the thing in front of it rather than behind it.
    float depth = 0.0;
    float2 depth_uv = uv;
    float3 m1 = (float3)0.0, m2 = (float3)0.0, centre = (float3)0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 o = float2(float(x), float(y)) * t;
            float d = scene_depth.SampleLevel(scene_depth_sampler, uv + o, 0.0);
            if (d > depth) {
                depth = d;
                depth_uv = uv + o;
            }
            float3 s = to_ycocg(compress(current.SampleLevel(current_sampler, uv + o, 0.0).rgb));
            m1 += s;
            m2 += s * s;
            if (x == 0 && y == 0) centre = s;
        }
    }

    // Where that surface was last frame.
    float2 ndc = float2(depth_uv.x * 2.0 - 1.0, 1.0 - depth_uv.y * 2.0);
    float4 p = mul(taa.inv_view_proj, float4(ndc, depth, 1.0));
    float4 before = mul(taa.prev_view_proj, float4(p.xyz + taa.camera_delta.xyz * p.w, p.w));
    float2 before_uv = float2(before.x / before.w * 0.5 + 0.5, 0.5 - before.y / before.w * 0.5);
    float2 history_uv = uv + (before_uv - depth_uv);
    // A creature's own motion, where one was drawn (at the nearest pixel, so
    // its silhouette carries it too), over the depth's.
    float4 motion = creature_motion.SampleLevel(creature_motion_sampler, depth_uv, 0.0);
    if (motion.a > 0.5) history_uv = uv - motion.xy;

    float weight = taa.camera_delta.w;
    if (before.w <= 0.0 || any(history_uv < 0.0) || any(history_uv > 1.0)) weight = 0.0;
    // Motion keeps less history: each frame's resampling softens it, and
    // there is less to gain from frames that saw something else.
    float moved = length((history_uv - uv) / taa.texel.xy);
    weight *= lerp(1.0, 0.75, saturate(moved / 8.0));

    // The history, clipped toward the neighbourhood's mean until it lies
    // inside one standard deviation of it.
    float3 mean = m1 / 9.0;
    float3 sigma = sqrt(max(m2 / 9.0 - mean * mean, 0.0));
    float3 past = to_ycocg(compress(sample_history(history_uv)));
    float3 offset = past - mean;
    // Tighter as it moves: a crown against the ground spans both colours,
    // and a loose box lets the ground's history through around a tree.
    float3 extent = max(sigma * lerp(1.1, 0.8, saturate(moved / 6.0)), 1e-4);
    float3 units = abs(offset / extent);
    float worst = max(units.x, max(units.y, units.z));
    if (worst > 1.0) past = mean + offset / worst;

    float3 result = lerp(centre, past, weight);
    // The alpha passes through: it is the share the ambient occlusion may
    // darken (occlusion_share), this frame's.
    return float4(expand(from_ycocg(result)), current.SampleLevel(current_sampler, uv, 0.0).a);
}
#endif
