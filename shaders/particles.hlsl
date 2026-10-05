// Additive billboard particles. Quads arrive already camera-facing from the
// CPU; the fragment shader just shapes each into a soft disc. Blending is
// ONE + ONE, so colour is emitted light and darkness is simply not drawing.
#include "common.hlsl"

struct VertexIn {
    float3 position : TEXCOORD0;
    float4 color    : TEXCOORD1;
    float2 corner   : TEXCOORD2;
};

#ifdef FIXED_FUNCTION
// Fixed function (docs/PORTING.md R5): the disc comes from a texture
// (gfx::ParticleSystem bakes (1 - r)^2 into a 64 x 64 map on this tier),
// stage 0 MODULATE with the display-encoded colour, added. No tonemap, so
// stacked puffs clip to white as they did in 2001.
struct VertexOut {
    float4 clip_position : SV_Position;
    float4 diffuse : COLOR0;  // the order FF_VARYINGS explains
    float fog : FOG;
    float2 uv : TEXCOORD0;
};
#else
struct VertexOut {
    float4 clip_position : SV_Position;
    float4 color : TEXCOORD0;
    float2 corner : TEXCOORD1;
};
#endif

struct ParticleUniforms {
    float4x4 view_proj;
    float4 output_grade;  // the LDR tiers' in-shader grade (common.hlsl)
};

ConstantBuffer<ParticleUniforms> u : UNIFORM_SLOT(0);

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.clip_position = mul(u.view_proj, float4(input.position, 1.0));
#ifdef FIXED_FUNCTION
    o.uv = input.corner * 0.5 + 0.5;
    o.diffuse = float4(pow(saturate(input.color.rgb * u.output_grade.x), (float3)(1.0 / 2.2)), 1.0);
    o.fog = 1.0;
#else
    o.color = input.color;
    o.corner = input.corner;
#endif
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
#ifdef FIXED_FUNCTION
TEXTURE2D(disc, 0);
float4 fs_main(VertexOut input) : SV_Target {
    // Times the fog factor (always 1 here): the read keeps FOG in this
    // stage's input signature, between the diffuse and the uv it does use.
    return float4(saturate(input.diffuse.rgb * disc.Sample(disc_sampler, input.uv.xy).rgb) * saturate(input.fog), 1.0);
}
#else
float4 fs_main(VertexOut input) : SV_Target {
    // Soft disc with a hot centre: quadratic falloff squared reads as a glow
    // core rather than a flat coin.
    float r = length(input.corner);
    float falloff = saturate(1.0 - r);
    falloff *= falloff;
#if defined(LDR_OUTPUT)
    // Without a float target each puff is finished on its own and the
    // finished values add: brighter where puffs stack than the modern
    // tonemap-after-sum, which is the price of eight bits.
    return float4(ldr_encode(input.color.rgb * falloff, u.output_grade), 1.0);
#else
    return float4(input.color.rgb * falloff, 1.0);
#endif
}
#endif
#endif
