#include "model_common.hlsl"

// Skinned character rendering.
//
// Joint matrices arrive as a uniform array rather than a storage buffer: 16 KB
// per draw, which avoids maintaining a per-frame buffer. Must match
// anim::MAX_JOINTS.
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
    float4 tangent     : TEXCOORD6;
};

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
    float3 albedo : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float emissive : TEXCOORD4;
    float3 world_tangent : TEXCOORD5;
    float tangent_sign : TEXCOORD6;
};

// Both stages read the model block: the vertex stage its transform and tint,
// the fragment stage which maps are bound and the recolour.
ConstantBuffer<ModelUniforms> model : UNIFORM_SLOT(1);

#ifdef VERTEX_STAGE
ConstantBuffer<SkinUniforms> skin : UNIFORM_SLOT(2);

VertexOut vs_main(VertexIn input) {
    // Linear blend skinning. Weights are normalized on the CPU, so no rescaling
    // is needed here.
    float4x4 blended = skin.joints[input.joint_index.x] * input.weight.x;
    blended += skin.joints[input.joint_index.y] * input.weight.y;
    blended += skin.joints[input.joint_index.z] * input.weight.z;
    blended += skin.joints[input.joint_index.w] * input.weight.w;

    float4 skinned_position = mul(blended, float4(input.position, 1.0));
    // Blending matrices this way is only correct for rotation and uniform scale,
    // which is all a rig uses. Rotating the normal by the same blend is the usual
    // approximation and is right up to non-uniform scale.
    float3 skinned_normal = mul(blended, float4(input.normal, 0.0)).xyz;
    float3 skinned_tangent = mul(blended, float4(input.tangent.xyz, 0.0)).xyz;

    float4 world = mul(model.model, skinned_position);

    VertexOut o;
    o.world_position = world.xyz;
    o.world_normal = normalize(mul(model.model, float4(skinned_normal, 0.0)).xyz);
    o.world_tangent = mul(model.model, float4(skinned_tangent, 0.0)).xyz;
    // Handedness survives skinning: it is a property of the UV layout, not of
    // the pose.
    o.tangent_sign = input.tangent.w;
    o.albedo = input.color * model.tint.rgb;
    o.uv = input.uv;
    o.emissive = model.tint.a;
    o.clip_position = mul(scene.view_proj, world);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH2D(shadow_map, 0);
TEXTURE2D(base_colour, 1);
TEXTURE2D(normal_map, 2);
TEXTURE2D(orm_map, 3);

float4 fs_main(VertexOut input) : SV_Target {
    float3 normal = normalize(input.world_normal);
    float3 to_sun = normalize(scene.sun.xyz);
    float3 to_camera = normalize(scene.camera_position.xyz - input.world_position);

    // Wing membranes are thin and seen from both sides, so light whichever face
    // is actually turned toward the viewer. Done before the normal map so the
    // tangent frame is flipped consistently with the geometric normal.
    float side = dot(normal, to_camera) < 0.0 ? -1.0 : 1.0;
    normal *= side;

    // A degenerate tangent would normalize to NaN, so it is rejected before any
    // of the frame is built from it.
    if (model.material.y > 0.5 && dot(input.world_tangent, input.world_tangent) > 1e-8) {
        // Gram-Schmidt: skinning leaves the tangent very slightly non-orthogonal
        // to the normal, and the error shows up as shading seams along blend
        // boundaries.
        float3 orthogonal = input.world_tangent - normal * dot(normal, input.world_tangent);
        if (dot(orthogonal, orthogonal) > 1e-8) {
            float3 tangent = normalize(orthogonal);
            float3 bitangent = cross(normal, tangent) * input.tangent_sign * side;
            float3 sampled = normal_map.Sample(normal_map_sampler, input.uv).xyz * 2.0 - 1.0;
            normal = normalize(tangent * sampled.x + bitangent * sampled.y + normal * sampled.z);
        }
    }

    // glTF packs occlusion in R, roughness in G, metallic in B. Scales are 1 in
    // this model's materials, so the sample is used directly.
    float occlusion = 1.0;
    float roughness = 0.65;
    float metallic = 0.0;
    if (model.material.z > 0.5) {
        float3 orm = orm_map.Sample(orm_map_sampler, input.uv).rgb;
        occlusion = orm.r;
        roughness = clamp(orm.g, 0.05, 1.0);
        metallic = orm.b;
    }

    // The shared lighting path: the creature is lit by the same sun, wrap and
    // ambient as the ground it flies over, which is what keeps a scanned-grade
    // asset from looking pasted onto a painted world.
    float visibility = sun_visibility(input.world_position, normal, shadow_map, shadow_map_sampler);
    float3 direct = direct_sun(normal, visibility);
    float3 ambient = ambient_light(normal);

    // The texture is sRGB, so the sample arrives already linear.
    float3 albedo = input.albedo;
    if (model.material.x > 0.5) {
        albedo *= base_colour.Sample(base_colour_sampler, input.uv).rgb;
    }
    // Recolour at constant luminance: the texture's scales and shading stay,
    // the hue goes where the draw asks. This is how one dragon model becomes
    // four distinguishable bots.
    if (model.recolour.a > 0.001) {
        float luminance = dot(albedo, float3(0.30, 0.59, 0.11));
        albedo = lerp(albedo, luminance * model.recolour.rgb, model.recolour.a);
    }

    // Scaly hide is not a mirror, so the specular lobe is deliberately modest:
    // a single GGX highlight, no image-based lighting, no environment probe.
    // Enough to tell wet-looking horn from matte membrane, which is all the
    // roughness map is really being asked to convey.
    float3 half_vector = normalize(to_sun + to_camera);
    float alpha = roughness * roughness;
    float n_dot_h = saturate(dot(normal, half_vector));
    float denominator = n_dot_h * n_dot_h * (alpha * alpha - 1.0) + 1.0;
    float distribution = (alpha * alpha) / max(3.14159265 * denominator * denominator, 1e-4);
    // Fresnel, with dielectrics at the usual 4% and metals tinted by albedo.
    float3 f0 = lerp((float3)0.04, albedo, metallic);
    float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - saturate(dot(half_vector, to_camera)), 5.0);
    float3 specular = fresnel * distribution * saturate(dot(normal, to_sun)) * 0.25;

    // Metals have no diffuse response.
    float3 diffuse = albedo * (1.0 - metallic);
    float3 lit_color = diffuse * (direct + ambient * occlusion) +
                       specular * scene.sun_color.rgb * scene.sun.w * visibility +
                       albedo * input.emissive;

    return float4(scene_out(apply_fog(lit_color, input.world_position)), 1.0);
}
#endif
