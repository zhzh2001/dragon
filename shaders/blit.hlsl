// A stretch: the source's top-left region, scaled over the whole target with
// linear filtering. The backends with a blit of their own (SDL's
// SDL_BlitGPUTexture, D3D9's StretchRect) never draw this; the D3D11 one has
// no such call and draws a fullscreen triangle with it instead
// (rhi::Device::blit, gfx::Device::finish_world).
#include "common.hlsl"

struct VertexOut {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct BlitUniforms {
    float4 scale;  // xy: the source region as a fraction of the source texture
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VERTEX_ID_INPUT) {
    const int vertex_id = VERTEX_ID;
    const float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    VertexOut o;
    o.clip_position = float4(positions[vertex_id], 0.5, 1.0);
    o.uv = float2(positions[vertex_id].x * 0.5 + 0.5, 0.5 - positions[vertex_id].y * 0.5);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
ConstantBuffer<BlitUniforms> u : UNIFORM_SLOT(0);
TEXTURE2D(source, 0);

float4 fs_main(VertexOut input) : SV_Target {
    return float4(source.Sample(source_sampler, input.uv * u.scale.xy).rgb, 1.0);
}
#endif
