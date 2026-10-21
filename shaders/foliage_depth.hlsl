// Depth-only instanced plants for the shadow map. The same placement and sway
// as foliage.hlsl, so a swaying crown casts a swaying shadow.
#define NO_SCENE
#include "scene_common.hlsl"

struct FoliageParams {
    float4 wind_time_fade;
    float4 extra;
    // x, y the band where trees hand their shadow over to their impostor
    // (foliage_impostor_depth.hlsl), z, w the eye's x and z; y 0: no
    // impostors, the crown casts at every distance.
    float4 impostor;
};

struct ShadowUniforms {
    float4x4 light_view_proj;
};

struct VertexIn {
    float3 position       : TEXCOORD0;
    float3 normal         : TEXCOORD1;
    float3 color          : TEXCOORD2;
    float2 uv             : TEXCOORD3;
    float4 position_scale : TEXCOORD4;
    float4 params         : TEXCOORD5;
};

struct DepthOut {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    float material : TEXCOORD1;
    DEPTH_VARYING
};

#ifdef VERTEX_STAGE
ConstantBuffer<ShadowUniforms> shadow : UNIFORM_SLOT(0);
ConstantBuffer<FoliageParams> params : UNIFORM_SLOT(1);

DepthOut vs_main(VertexIn input) {
    float yaw = input.params.x;
    float c = cos(yaw), s = sin(yaw);
    float3 local = input.position * input.position_scale.w;
    float3 rotated = float3(c * local.x + s * local.z, local.y, -s * local.x + c * local.z);
    float height_fraction = saturate(local.y / max(input.position_scale.w * params.extra.x, 0.01));
    float t = params.wind_time_fade.y;
    float sway = params.wind_time_fade.x * height_fraction * height_fraction *
                 (sin(t * 1.3 + input.params.z) + 0.5 * sin(t * 2.9 + input.params.z * 1.7));
    rotated.x += sway;
    rotated.z += sway * 0.6;
    DepthOut o;
    float3 world = input.position_scale.xyz + rotated;
    if (params.impostor.y > 0.0) {
        // Past its own distance, measured flat as the impostor measures it,
        // the crown collapses to a point and the impostor casts instead.
        float hash = frac(input.params.z * 0.15915494);
        float flat_distance = length(input.position_scale.xz - params.impostor.zw);
        if (flat_distance >= lerp(params.impostor.x, params.impostor.y, hash)) world = input.position_scale.xyz;
    }
    o.clip_position = mul(shadow.light_view_proj, float4(world, 1.0));
    o.uv = input.uv;
    int tag = int(input.color.z + 0.5);
    o.material = float(tag >= 10 ? tag - 10 : tag);
    // A thickened crown (extra.z, the far cascade on the modern tier): its
    // coarse cards cut out a little less, as the colour pass draws them past
    // the LOD band (foliage.hlsl). Carried to the pixel stage in the
    // material's fraction.
    if (params.extra.z > 0.0 && tag < 10) o.material += 0.25 * params.extra.z;
    WRITE_DEPTH_VARYING(o);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
TEXTURE2D(leaf_card, 0);
TEXTURE2D(needle_card, 1);

// A card's shadow is the shape cut out of it, not the quad: the same alpha
// test as the colour pass, against the same textures (1 leaf, 2 needle).
DEPTH_FRAGMENT_RETURN fs_main(DepthOut input) DEPTH_FRAGMENT_SEMANTIC {
    int material = int(input.material + 0.5);
    if (material == 1 || material == 2) {
        float alpha = material == 1 ? leaf_card.Sample(leaf_card_sampler, input.uv).a
                                    : needle_card.Sample(needle_card_sampler, input.uv).a;
        if (alpha < 0.45 - 0.08 * frac(input.material) * 4.0) discard;
    }
    DEPTH_FRAGMENT_END(input);
}
#endif
