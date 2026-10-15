#define ATMOSPHERE_SLOT 1  // the atmosphere's atlas, after the shadow map
#define TERRAIN_SUN_SLOT 2  // and the terrain's shadow heights after it
#include "model_common.hlsl"

// Lit opaque geometry with per-vertex albedo: greybox props, rings, the
// generated fallback dragon. No texture support.

struct VertexIn {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 color    : TEXCOORD2;
};

#ifdef SM2
// SM2 (docs/PORTING.md, R4): lit per vertex through scene_common's SM2 path.
// Emissive and unlit fold into the per-vertex terms -- the ambient becomes
// the emissive (plus the ambient when lit), the sun drops to zero when unlit
// -- so the pixel stage is the albedo and the shared finish.
#ifdef FIXED_FUNCTION
// Fixed function (docs/PORTING.md R5): the diffuse alone (stage 0,
// SELECTARG1 DIFFUSE): the vertex colour lit and finished per vertex, or
// for an unlit object its colour times the emissive; no facing flip.
struct VertexOut {
    float4 clip_position : SV_Position;
    FF_VARYINGS
};
#else
struct VertexOut {
    float4 clip_position : SV_Position;
    float4 albedo : TEXCOORD0;
    SM2_LIGHT_VARYINGS
};
#endif

#ifdef VERTEX_STAGE
ConstantBuffer<ModelUniforms> model : UNIFORM_SLOT(1);

VertexOut vs_main(VertexIn input) {
    VertexOut o;
    float4 world = mul(model.model, float4(input.position, 1.0));
    float3 normal = normalize(mul(model.model, float4(input.normal, 0.0)).xyz);
    o.clip_position = mul(scene.view_proj, world);
    const float lit = 1.0 - model.material.w;
#ifdef FIXED_FUNCTION
    float3 albedo = input.color * model.tint.rgb;
    o.diffuse = float4(saturate(ff_lit(albedo, normal, 0.0).rgb * lit + ff_albedo(albedo) * ff_display((float3)model.tint.a)), 1.0);
    o.uv = (float2)0.0;
    o.fog = ff_fog(world.xyz);
#else
    // Thin plates seen from both sides: light the face toward the viewer.
    if (dot(normal, scene.camera_position.xyz - world.xyz) < 0.0) normal = -normal;
    o.albedo = float4(input.color * model.tint.rgb, 0.0);
    Sm2Light light = sm2_light_vertex(world.xyz, normal, 0.0);
    light.ambient.rgb = light.ambient.rgb * lit + model.tint.a;
    light.sun.rgb *= lit;
    SM2_LIGHT_OUT(o, light);
#endif
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH2D(shadow_map, 0);

#ifdef FIXED_FUNCTION
float4 fs_main(VertexOut input) : SV_Target {
    return float4(ff_finish(input.diffuse.rgb * FF_KEEP_SHADOW_SLOT, input.fog), 1.0);
}
#else
float4 fs_main(VertexOut input) : SV_Target {
    return float4(sm2_finish(input.albedo.rgb, SM2_LIGHT_IN(input), shadow_map, shadow_map_sampler), 1.0);
}
#endif
#endif
#else  // the per-pixel path

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
    float3 albedo : TEXCOORD2;
    float emissive : TEXCOORD3;
    // Carried as a varying rather than read from ModelUniforms: the fragment
    // stage does not bind the model block, and one interpolant is cheaper than
    // adding a binding to every pipeline that uses this shader.
    float unlit : TEXCOORD4;
};

#ifdef VERTEX_STAGE
ConstantBuffer<ModelUniforms> model : UNIFORM_SLOT(1);

VertexOut vs_main(VertexIn input) {
    VertexOut o;
    float4 world = mul(model.model, float4(input.position, 1.0));
    o.world_position = world.xyz;
    // Uniform scale only, so the upper 3x3 rotates normals correctly without an
    // inverse transpose.
    o.world_normal = normalize(mul(model.model, float4(input.normal, 0.0)).xyz);
    o.albedo = input.color * model.tint.rgb;
    o.emissive = model.tint.a;
    o.unlit = model.material.w;
    o.clip_position = mul(scene.view_proj, world);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH2D(shadow_map, 0);

float4 fs_main(VertexOut input) : SV_Target {
    float3 normal = normalize(input.world_normal);

    // Wings are thin plates seen from both sides, so light the face that is
    // actually turned toward the viewer.
    float3 to_camera = normalize(scene.camera_position.xyz - input.world_position);
    if (dot(normal, to_camera) < 0.0) normal = -normal;

    float visibility = sun_visibility(input.world_position, normal, shadow_map, shadow_map_sampler);
    float3 direct = direct_sun(normal, visibility);
    float3 ambient = ambient_light(normal);
#ifdef USE_TERRAIN_SUN
    ambient *= terrain_sky(input.world_position);  // down in a gully, less sky
#endif

    // Emissive is added after lighting and before fog, so a glowing checkpoint
    // still fades into the distance rather than punching through the haze.
    //
    // material.w marks the object as unlit: its colour is purely emissive, with
    // no sun or ambient term at all. Fire needs this: adding two units of
    // sunlight on top of a flame turns it into a white balloon.
    float3 lit_color = input.albedo * input.emissive;
    if (input.unlit < 0.5) lit_color += input.albedo * (direct + ambient);

    float3 fogged = apply_fog(lit_color, input.world_position);
    return float4(scene_out(fogged), input.unlit < 0.5 ? occlusion_share(ambient, direct + ambient) : 0.0);
}
#endif
#endif  // SM2
