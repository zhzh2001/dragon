// The per-object block every lit model reads. Must match gfx::ModelUniforms
// exactly.
#pragma once

#include "scene_common.hlsl"

struct ModelUniforms {
    float4x4 model;
    // rgb multiplies vertex colour, a is an unlit emissive add.
    float4 tint;
    // x: 1 when a base-colour texture is bound, 0 to use the vertex colour.
    float4 material;
    // rgb: hue to push the albedo toward at its own luminance; a: how far.
    float4 recolour;
};
