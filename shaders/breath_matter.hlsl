#define ATMOSPHERE_SLOT 1  // the atmosphere's atlas, after the shadow map
#define TERRAIN_SUN_SLOT 2  // and the terrain's shadow heights after it
#include "model_common.hlsl"

// A piece of a breath of matter (game/breath_matter.h): an ice crystal, a
// rock, a slug or a drop of water or acid. The modern tier only.
//
// mesh.hlsl lights a surface with the sun's diffuse term and the sky's, and
// ice and water lit that way are chalk. What makes them read as what they
// are is here: a tight highlight from the sun, the sky mirrored at a grazing
// angle, light through the body from the far side, and for a liquid a body
// that is seen through in its middle and solid at its rim.
//
// The model block (gfx::ModelUniforms) is read as:
//   tint      rgb the colour, a an unlit share added to it
//   material  x the highlight's strength, y its exponent, z the cover (1
//             opaque; under 1 the pipeline blends), w the light through it
//   recolour  x how much of the sky it mirrors, y how far a face is
//             flattened to its own plane (a facet: 1 for a crystal)

struct VertexIn {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 color    : TEXCOORD2;
};

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
    float4 albedo : TEXCOORD2;   // a: the unlit share
    float4 surface : TEXCOORD3;  // material
    float4 extra : TEXCOORD4;    // recolour
};

#ifdef VERTEX_STAGE
ConstantBuffer<ModelUniforms> model : UNIFORM_SLOT(1);

VertexOut vs_main(VertexIn input) {
    VertexOut o;
    float4 world = mul(model.model, float4(input.position, 1.0));
    o.world_position = world.xyz;
    // A slug is a sphere stretched along its flight, so the scale is not
    // uniform and a normal needs the inverse transpose: the model's own
    // matrix applied to the normal over each axis's squared scale.
    float3 axis_x = mul(model.model, float4(1.0, 0.0, 0.0, 0.0)).xyz;
    float3 axis_y = mul(model.model, float4(0.0, 1.0, 0.0, 0.0)).xyz;
    float3 axis_z = mul(model.model, float4(0.0, 0.0, 1.0, 0.0)).xyz;
    float3 squared = max(float3(dot(axis_x, axis_x), dot(axis_y, axis_y), dot(axis_z, axis_z)), 1e-8);
    o.world_normal = normalize(mul(model.model, float4(input.normal / squared, 0.0)).xyz);
    o.albedo = float4(input.color * model.tint.rgb, model.tint.a);
    o.surface = model.material;
    o.extra = model.recolour;
    o.clip_position = mul(scene.view_proj, world);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH2D(shadow_map, 0);

float4 fs_main(VertexOut input) : SV_Target {
    float3 normal = normalize(input.world_normal);
    // A facet: the face's own plane from the position's derivatives, so a
    // crystal's sides are flat whatever its vertex normals say.
    float3 flat_normal = normalize(cross(ddy(input.world_position), ddx(input.world_position)));
    if (dot(flat_normal, normal) < 0.0) flat_normal = -flat_normal;
    normal = normalize(lerp(normal, flat_normal, saturate(input.extra.y)));

    float3 to_camera = normalize(scene.camera_position.xyz - input.world_position);
    if (dot(normal, to_camera) < 0.0) normal = -normal;

    float gloss = input.surface.x;
    float exponent = max(input.surface.y, 1.0);
    float cover = input.surface.z;
    float through = input.surface.w;
    float3 albedo = input.albedo.rgb;

    float visibility = sun_visibility(input.world_position, normal, shadow_map, shadow_map_sampler);
    float3 direct = direct_sun(normal, visibility) + local_light(input.world_position, normal);
    float3 ambient = ambient_light_at(normal, input.world_position);
    float3 color = albedo * (direct + ambient);
    // Light through the body: the far side's sun, in the body's colour.
    color += albedo * through * direct_sun(-normal, visibility) * 0.7;
    color += albedo * input.albedo.a;

    // The sky, mirrored: little face on, most at a grazing angle.
    float facing = saturate(dot(normal, to_camera));
    float fresnel = pow(1.0 - facing, 4.0);
    float3 mirrored = reflect(-to_camera, normal);
    float3 sky = sky_plain(float3(mirrored.x, abs(mirrored.y), mirrored.z));
    color = lerp(color, sky, saturate(input.extra.x * (0.05 + 0.95 * fresnel)));

    // The sun's highlight.
    float3 half_vector = normalize(normalize(scene.sun.xyz) + to_camera);
    float glint = pow(saturate(dot(normal, half_vector)), exponent) * gloss * visibility;
    color += scene.sun_color.rgb * scene.sun.w * glint;

    float3 fogged = apply_fog(color, input.world_position);
    if (cover >= 0.999) {
        return float4(scene_out(fogged), occlusion_share(ambient, direct + ambient));
    }
    // A liquid: seen through face on, solid at its rim and in its highlight.
    float solid = saturate(cover + (1.0 - cover) * (fresnel * 0.85 + glint));
    return float4(scene_out(fogged), solid);
}
#endif
