// Additive billboard particles. Quads arrive already camera-facing from the
// CPU; the fragment shader just shapes each into a soft disc. Blending is
// ONE + ONE, so colour is emitted light and darkness is simply not drawing.
#include "common.hlsl"

struct VertexIn {
    float3 position : TEXCOORD0;
    float4 color    : TEXCOORD1;
    float2 corner   : TEXCOORD2;
};

struct VertexOut {
    float4 clip_position : SV_Position;
    float4 color : TEXCOORD0;
    float2 corner : TEXCOORD1;
};

struct ParticleUniforms {
    float4x4 view_proj;
    float4 output_grade;  // the LDR tiers' in-shader grade (common.hlsl)
};

ConstantBuffer<ParticleUniforms> u : UNIFORM_SLOT(0);

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.clip_position = mul(u.view_proj, float4(input.position, 1.0));
    o.color = input.color;
    o.corner = input.corner;
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
float4 fs_main(VertexOut input) : SV_Target {
    // Soft disc with a hot centre: quadratic falloff squared reads as a glow
    // core rather than a flat coin.
    float r = length(input.corner);
    float falloff = saturate(1.0 - r);
    falloff *= falloff;
#ifdef LDR_OUTPUT
    // Without a float target each puff is finished on its own and the
    // finished values add: brighter where puffs stack than the modern
    // tonemap-after-sum, which is the price of eight bits.
    return float4(ldr_encode(input.color.rgb * falloff, u.output_grade), 1.0);
#else
    return float4(input.color.rgb * falloff, 1.0);
#endif
}
#endif
