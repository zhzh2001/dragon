// Immediate-mode debug lines: a coloured line list, no lighting.
#include "common.hlsl"

struct VertexIn {
    float3 position : TEXCOORD0;
    float3 color    : TEXCOORD1;
};

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 color : TEXCOORD0;
};

struct Uniforms {
    float4x4 view_proj;
};

#ifdef VERTEX_STAGE
ConstantBuffer<Uniforms> u : UNIFORM_SLOT(0);

VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.clip_position = mul(u.view_proj, float4(input.position, 1.0));
    o.color = input.color;
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
float4 fs_main(VertexOut input) : SV_Target {
    return float4(input.color, 1.0);
}
#endif
