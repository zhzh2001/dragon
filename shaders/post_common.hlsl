// Shared by the post-process passes: a fullscreen triangle from the vertex
// id, and the uniform block every pass reads. Must match gfx::PostUniforms.
#pragma once

#include "common.hlsl"

struct PostUniforms {
    // x exposure, y contrast, z saturation, w bloom strength
    float4 grade;
    // x bloom threshold, y bloom knee, z vignette, w hue preservation
    float4 bloom;
    // rgb = multiplier on the shadows, a = how far toward it (split-toning)
    float4 shadows;
    // rgb = multiplier on the highlights, a = how far
    float4 highlights;
    // x temperature (- cool, + warm), y tint (- green, + magenta), z lift, w gamma
    float4 balance;
    // x, y = one source texel in UV; z, w = blur direction in texels
    float4 texel;
};

struct PostVertex {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
};

#ifdef VERTEX_STAGE
// Every post pass shares this vertex stage.
PostVertex vs_main(VERTEX_ID_INPUT) {
    const int vertex_id = VERTEX_ID;
    const float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    PostVertex o;
    o.clip_position = float4(positions[vertex_id], 0.0, 1.0);
    // Texture space runs downward.
    o.uv = float2(positions[vertex_id].x * 0.5 + 0.5, 1.0 - (positions[vertex_id].y * 0.5 + 0.5));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
ConstantBuffer<PostUniforms> post : UNIFORM_SLOT(0);
#endif
