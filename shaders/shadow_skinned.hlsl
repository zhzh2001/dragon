// Depth-only skinned pass. Applies the same blend as skinned.hlsl, so a folded
// wing casts a folded shadow -- the ground shadow is the player's altitude cue,
// and a shadow that does not match the pose reads as a bug in the flight model.
#define NO_SCENE
#include "model_common.hlsl"

static const uint MAX_JOINTS = 256;

struct SkinUniforms {
    float4x4 joints[MAX_JOINTS];
};

struct VertexIn {
    float3 position    : TEXCOORD0;
    float3 normal      : TEXCOORD1;
    float3 color       : TEXCOORD2;
    float2 uv          : TEXCOORD3;
    uint4  joint_index : TEXCOORD4;
    float4 weight      : TEXCOORD5;
};

struct ShadowUniforms {
    float4x4 light_view_proj;
};

#ifdef VERTEX_STAGE
ConstantBuffer<ShadowUniforms> shadow : UNIFORM_SLOT(0);
ConstantBuffer<ModelUniforms> model : UNIFORM_SLOT(1);
ConstantBuffer<SkinUniforms> skin : UNIFORM_SLOT(2);

float4 vs_main(VertexIn input) : SV_Position {
    float4x4 blended = skin.joints[input.joint_index.x] * input.weight.x;
    blended += skin.joints[input.joint_index.y] * input.weight.y;
    blended += skin.joints[input.joint_index.z] * input.weight.z;
    blended += skin.joints[input.joint_index.w] * input.weight.w;
    return mul(shadow.light_view_proj, mul(model.model, mul(blended, float4(input.position, 1.0))));
}
#endif

#ifdef FRAGMENT_STAGE
void fs_main() {}
#endif
