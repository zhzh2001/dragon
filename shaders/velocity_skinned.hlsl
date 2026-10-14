// The temporal resolve's motion vectors for creatures (gfx/taa.h,
// post_taa.hlsl). The resolve reprojects through depth, which takes every
// pixel for a fixed point of the world; a creature flying with the camera
// is not one, and it smeared. After the main pass, each creature is drawn
// again into the velocity target, tested against the depth it left (and
// never writing it), and each pixel gets the screen motion of its surface
// point: where this frame's pose, model and camera put it, against where
// last frame's did -- the wingbeat included.
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

struct VelocityUniforms {
    float4x4 jittered_view_proj;  // what the main pass drew with, for the depth test
    float4x4 view_proj;           // this frame's, unjittered
    float4x4 prev_view_proj;      // last frame's, unjittered
    float4x4 prev_model;          // the creature's last model matrix
};

struct VelocityOut {
    float4 clip_position : SV_Position;
    float4 now : TEXCOORD0;
    float4 before : TEXCOORD1;
};

#ifdef VERTEX_STAGE
ConstantBuffer<VelocityUniforms> velocity : UNIFORM_SLOT(0);
ConstantBuffer<ModelUniforms> model : UNIFORM_SLOT(1);
ConstantBuffer<SkinUniforms> skin : UNIFORM_SLOT(2);
ConstantBuffer<SkinUniforms> prev_skin : UNIFORM_SLOT(3);

VelocityOut vs_main(VertexIn input) {
    float4x4 blended = SKIN_MATRIX(skin, input.joint_index, input.weight);
    float4 local = mul(blended, float4(input.position, 1.0));
    float4 world = mul(model.model, local);
    VelocityOut o;
    o.clip_position = mul(velocity.jittered_view_proj, world);
    // A hair nearer than the main pass put it (reversed-Z: larger), so the
    // test against its depth passes on the creature's own pixels whatever
    // the last bit of rounding.
    o.clip_position.z *= 1.0005;
    o.now = mul(velocity.view_proj, world);
    float4x4 prev_blended = SKIN_MATRIX(prev_skin, input.joint_index, input.weight);
    o.before = mul(velocity.prev_view_proj, mul(velocity.prev_model, mul(prev_blended, float4(input.position, 1.0))));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
float4 fs_main(VelocityOut input) : SV_Target {
    float2 now = input.now.xy / input.now.w * float2(0.5, -0.5) + 0.5;
    float2 before = input.before.xy / input.before.w * float2(0.5, -0.5) + 0.5;
    // rg: the motion in uv, this frame minus last; a: 1, a creature is here.
    return float4(now - before, 0.0, 1.0);
}
#endif
