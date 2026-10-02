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

// A sampled texture and its sampler share a slot number, which is how SDL
// binds them (SDL_BindGPUFragmentSamplers takes texture-sampler pairs).
#define TEXTURE2D(name, n) Texture2D<float4> name : TEXTURE_SLOT(n); SamplerState name##_sampler : SAMPLER_SLOT(n)
// The shadow map: a depth texture read as a plain float, compared in the
// shader (3x3 PCF in sun_visibility), not with a comparison sampler.
#define DEPTH2D(name, n) Texture2D<float> name : TEXTURE_SLOT(n); SamplerState name##_sampler : SAMPLER_SLOT(n)
