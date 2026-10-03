// What every shader includes first: the binding conventions of SDL's GPU API,
// spelled once.
//
// Shaders are HLSL, compiled by SDL_shadercross (DXC to SPIR-V, then to MSL on
// Metal, kept as SPIR-V on Vulkan, DXIL on D3D12). SDL fixes where each kind
// of resource lives per stage, as register spaces:
//
//   vertex:   textures and samplers in space0, uniform buffers in space1
//   fragment: textures and samplers in space2, uniform buffers in space3
//
// One file holds both stages and is compiled twice, once with VERTEX_STAGE
// and once with FRAGMENT_STAGE defined (gfx/pipeline.cpp), so a block that
// both stages read -- the scene, the model -- is declared once and lands in
// the right space for whichever stage is being built. Slot numbers are what
// the C++ side pushes to: SDL_PushGPUVertexUniformData(cmd, slot, ...) and
// its fragment twin.
#pragma once

#if defined(VERTEX_STAGE)
#define UNIFORM_SPACE space1
#define RESOURCE_SPACE space0
#else
#define UNIFORM_SPACE space3
#define RESOURCE_SPACE space2
#endif

#define UNIFORM_SLOT(n) register(b##n, UNIFORM_SPACE)
#define TEXTURE_SLOT(n) register(t##n, RESOURCE_SPACE)
#define SAMPLER_SLOT(n) register(s##n, RESOURCE_SPACE)

// D3D9 (rhi/d3d9, SM3 and below) lacks two things the modern stages use.
//
// No SV_VertexID: a fullscreen triangle's vertex stage reads its index from
// TEXCOORD0 instead, a float the D3D9 backend feeds from a three-entry buffer
// of its own whenever a pipeline declares no vertex attributes.
//   VertexOut vs_main(VERTEX_ID_INPUT) { const int vertex_id = VERTEX_ID; ...
#ifdef D3D9
#define VERTEX_ID_INPUT float vertex_id_input : TEXCOORD0
#define VERTEX_ID int(vertex_id_input + 0.5)
#else
#define VERTEX_ID_INPUT uint vertex_id_input : SV_VertexID
#define VERTEX_ID int(vertex_id_input)
#endif

// No portable way to sample a depth buffer: the shadow map is an R32F colour
// target there, so the depth-only passes write depth as a colour (z / w, the
// same value a depth buffer would hold) and the shaders that read the map read
// .x either way. Elsewhere the fragment stage writes nothing.
#ifdef D3D9
#define DEPTH_VARYING float2 depth_zw : TEXCOORD7;
#define WRITE_DEPTH_VARYING(o) (o).depth_zw = (o).clip_position.zw
#define DEPTH_FRAGMENT_RETURN float4
#define DEPTH_FRAGMENT_SEMANTIC : SV_Target
#define DEPTH_FRAGMENT_END(i) return float4((i).depth_zw.x / (i).depth_zw.y, 0.0, 0.0, 1.0)
#else
#define DEPTH_VARYING
#define WRITE_DEPTH_VARYING(o)
#define DEPTH_FRAGMENT_RETURN void
#define DEPTH_FRAGMENT_SEMANTIC
#define DEPTH_FRAGMENT_END(i)
#endif
struct DepthOnlyOut {
    float4 clip_position : SV_Position;
    DEPTH_VARYING
};
#define DEPTH_ONLY_FRAGMENT \
    DEPTH_FRAGMENT_RETURN fs_main(DepthOnlyOut input) DEPTH_FRAGMENT_SEMANTIC { DEPTH_FRAGMENT_END(input); }

// The LDR tiers' in-shader finish (LDR_OUTPUT): the same curve as
// post_composite.hlsl -- exposure, Reinhard blended toward its hue-preserving
// form, gamma 2.2, then contrast about display middle grey and saturation
// about the luma -- without the bloom, split-toning, white balance and
// vignette an SM2 pixel shader has no room for.
float3 ldr_encode(float3 c, float4 grade) {
    c *= grade.x;
    const float3 LUMA = float3(0.2126, 0.7152, 0.0722);
    float l = dot(c, LUMA);
    float3 hue = c * ((l / (1.0 + l)) / max(l, 1e-4));
    float peak = max(hue.r, max(hue.g, hue.b));
    hue = peak > 1.0 ? hue / peak : hue;
    c = lerp(c / (1.0 + c), hue, grade.w);
    c = pow(c, (float3)(1.0 / 2.2));
    c = (c - 0.46) * grade.y + 0.46;
    c = lerp((float3)dot(c, LUMA), c, grade.z);
    return saturate(c);
}

// ldr_encode for SM2's 64 instructions: the same exposure, Reinhard, gamma,
// contrast and saturation, without the hue-preserving blend (a bright colour
// desaturates toward white as the per-channel curve takes it).
float3 ldr_encode_fast(float3 c, float4 grade) {
    c *= grade.x;
    c = c / (1.0 + c);
    c = pow(c, (float3)(1.0 / 2.2));
    c = (c - 0.46) * grade.y + 0.46;
    const float3 LUMA = float3(0.2126, 0.7152, 0.0722);
    c = lerp((float3)dot(c, LUMA), c, grade.z);
    return saturate(c);
}

// A sampled texture and its sampler share a slot number, which is how SDL
// binds them (SDL_BindGPUFragmentSamplers takes texture-sampler pairs).
// Slots must run 0..n-1 with every one of them read: the count SDL binds
// comes from reflection, which counts only the textures the compiled shader
// uses, while each keeps its register. A debug edit that returns early past
// slots 0 and 1 leaves a count of one, and slot 2 then reads zero.
#define TEXTURE2D(name, n) Texture2D<float4> name : TEXTURE_SLOT(n); SamplerState name##_sampler : SAMPLER_SLOT(n)
// The shadow map: a depth texture read as a plain float, compared in the
// shader (3x3 PCF in sun_visibility), not with a comparison sampler.
#define DEPTH2D(name, n) Texture2D<float> name : TEXTURE_SLOT(n); SamplerState name##_sampler : SAMPLER_SLOT(n)
