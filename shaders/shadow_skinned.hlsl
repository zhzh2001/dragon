// Depth-only skinned pass. Applies the same blend as skinned.hlsl, so a folded
// wing casts a folded shadow -- the ground shadow is the player's altitude cue,
// and a shadow that does not match the pose reads as a bug in the flight model.
#define NO_SCENE
#include "model_common.hlsl"
#include "skin_common.hlsl"

struct VertexIn {
    float3 position    : TEXCOORD0;
    float3 normal      : TEXCOORD1;
    float3 color       : TEXCOORD2;
    float2 uv          : TEXCOORD3;
    JOINT_INDEX_TYPE joint_index : TEXCOORD4;
    float4 weight      : TEXCOORD5;
};

struct ShadowUniforms {
    float4x4 light_view_proj;
};

#ifdef VERTEX_STAGE
ConstantBuffer<ShadowUniforms> shadow : UNIFORM_SLOT(0);
ConstantBuffer<ModelUniforms> model : UNIFORM_SLOT(1);
ConstantBuffer<SkinUniforms> skin : UNIFORM_SLOT(2);

DepthOnlyOut vs_main(VertexIn input) {
    float4x4 blended = SKIN_MATRIX(skin, input.joint_index, input.weight);
    DepthOnlyOut o;
    o.clip_position = mul(shadow.light_view_proj, mul(model.model, mul(blended, float4(input.position, 1.0))));
    WRITE_DEPTH_VARYING(o);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH_ONLY_FRAGMENT
#endif
