// Immediate-mode debug lines: a coloured line list, no lighting.
#include "common.hlsl"

struct VertexIn {
    float3 position : TEXCOORD0;
    float3 color    : TEXCOORD1;
};

#ifdef FIXED_FUNCTION
// Fixed function (docs/PORTING.md R5): the colour as the diffuse (stage 0,
// SELECTARG1 DIFFUSE), the legacy registers.
struct VertexOut {
    float4 clip_position : SV_Position;
    float4 diffuse : COLOR0;  // the order FF_VARYINGS explains
    float fog : FOG;
    float4 uv : TEXCOORD0;
};
#else
struct VertexOut {
    float4 clip_position : SV_Position;
    float3 color : TEXCOORD0;
};
#endif

struct Uniforms {
    float4x4 view_proj;
};

#ifdef VERTEX_STAGE
ConstantBuffer<Uniforms> u : UNIFORM_SLOT(0);

VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.clip_position = mul(u.view_proj, float4(input.position, 1.0));
#ifdef FIXED_FUNCTION
    o.uv = (float4)0.0;
    o.diffuse = float4(input.color, 1.0);
    o.fog = 1.0;
#else
    o.color = input.color;
#endif
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
float4 fs_main(VertexOut input) : SV_Target {
#ifdef FIXED_FUNCTION
    return float4(input.diffuse.rgb, 1.0);
#else
    return float4(input.color, 1.0);
#endif
}
#endif
