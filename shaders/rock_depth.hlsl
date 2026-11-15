// Depth-only rock kit for the shadow map: rock.hlsl's placement, no fade
// (shadows stop well inside the reach, gfx::RockKit::Reach::shadow).
#define NO_SCENE
#include "scene_common.hlsl"

struct ShadowUniforms {
    float4x4 light_view_proj;
};

struct VertexIn {
    float3 position       : TEXCOORD0;
    float4 position_scale : TEXCOORD4;
    float4 rotation       : TEXCOORD5;
};

#ifdef VERTEX_STAGE
ConstantBuffer<ShadowUniforms> shadow : UNIFORM_SLOT(0);

DepthOnlyOut vs_main(VertexIn input) {
    float3 v = input.position * input.position_scale.w;
    float3 t = 2.0 * cross(input.rotation.xyz, v);
    float3 world = input.position_scale.xyz + v + input.rotation.w * t + cross(input.rotation.xyz, t);
    DepthOnlyOut o;
    o.clip_position = mul(shadow.light_view_proj, float4(world, 1.0));
    WRITE_DEPTH_VARYING(o);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH_ONLY_FRAGMENT
#endif
