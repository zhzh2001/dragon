// Depth-only pass for the directional shadow map, for rigid geometry. There is
// no colour target, so the fragment stage returns nothing and exists only for
// pipeline validation. Skinned casters use shadow_skinned.hlsl.
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

float4 vs_main(VertexIn input) : SV_Position {
    return mul(shadow.light_view_proj, mul(model.model, float4(input.position, 1.0)));
}
#endif

#ifdef FRAGMENT_STAGE
void fs_main() {}
#endif
