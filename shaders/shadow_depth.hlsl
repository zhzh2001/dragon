// Depth-only pass for the directional shadow map, for rigid geometry. There is
// no colour target, so the fragment stage returns nothing and exists only for
// pipeline validation -- except on D3D9, which writes the depth as a colour
// (common.hlsl, DEPTH_ONLY_FRAGMENT). Skinned casters use shadow_skinned.hlsl.
#define NO_SCENE
#include "model_common.hlsl"

struct VertexIn {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 color    : TEXCOORD2;
};

struct ShadowUniforms {
    float4x4 light_view_proj;
};

#ifdef VERTEX_STAGE
ConstantBuffer<ShadowUniforms> shadow : UNIFORM_SLOT(0);
ConstantBuffer<ModelUniforms> model : UNIFORM_SLOT(1);

DepthOnlyOut vs_main(VertexIn input) {
    DepthOnlyOut o;
    o.clip_position = mul(shadow.light_view_proj, mul(model.model, float4(input.position, 1.0)));
    WRITE_DEPTH_VARYING(o);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH_ONLY_FRAGMENT
#endif
