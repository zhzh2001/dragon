#define ATMOSPHERE_SLOT 4   // the atmosphere's atlas, after the shadow map and the three maps
#define TERRAIN_SUN_SLOT 5  // and the terrain's shadow heights after it
#include "scene_common.hlsl"

// The scanned rock kit (gfx::RockKit, tools/rockkit.md), instanced, on the
// modern tier: base colour, normal and occlusion-roughness maps, lit by the
// shared path as every world shader is. The retro tiers keep foliage.hlsl's
// untextured rocks, and never create this pipeline.

struct RockParams {
    float4 albedo_scale;  // rgb the piece's mean pulled to the terrain's rock, a the moss
    float4 contact;       // x the contact band (m), y its strength, z the terrain's tint
};

struct VertexIn {
    float3 position       : TEXCOORD0;
    float3 normal         : TEXCOORD1;
    float2 uv             : TEXCOORD2;
    float4 tangent        : TEXCOORD3;
    float4 position_scale : TEXCOORD4;
    float4 rotation       : TEXCOORD5;  // a unit quaternion
    float4 ground         : TEXCOORD6;  // the ground's plane, dot(xyz, p) = w
    float4 params         : TEXCOORD7;  // x shade, y moss, z hash, w the fade (1 whole)
};

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
    float4 world_tangent : TEXCOORD2;  // w the handedness
    float2 uv : TEXCOORD3;
    float4 ground : TEXCOORD4;
    float4 params : TEXCOORD5;
};

ConstantBuffer<RockParams> rock : UNIFORM_SLOT(1);

float3 rotate_by(float4 q, float3 v) {
    float3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    // Past its reach a piece shrinks toward its origin, which is under the
    // ground: it sinks out of sight rather than blinking out.
    float scale = input.position_scale.w * input.params.w;
    float3 world = input.position_scale.xyz + rotate_by(input.rotation, input.position * scale);
    o.world_position = world;
    o.world_normal = rotate_by(input.rotation, input.normal);
    o.world_tangent = float4(rotate_by(input.rotation, input.tangent.xyz), input.tangent.w);
    o.uv = input.uv;
    o.ground = input.ground;
    o.params = input.params;
    o.clip_position = mul(scene.view_proj, float4(world, 1.0));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH2D(shadow_map, 0);
TEXTURE2D(base_colour, 1);
TEXTURE2D(normal_map, 2);
TEXTURE2D(orm_map, 3);
#include "noise.hlsl"

float4 fs_main(VertexOut input) : SV_Target {
    float3 geometric = normalize(input.world_normal);
    // Two-sided: a cliff piece's back, where its rim stands out of the
    // ground, is lit as the face toward the viewer.
    float side = dot(geometric, scene.camera_position.xyz - input.world_position) < 0.0 ? -1.0 : 1.0;
    geometric *= side;
    float3 normal = geometric;
    // The tangent frame as glTF has it: bitangent = cross(normal, tangent) * w.
    float3 orthogonal = input.world_tangent.xyz - normal * dot(normal, input.world_tangent.xyz);
    if (dot(orthogonal, orthogonal) > 1e-8) {
        float3 tangent = normalize(orthogonal);
        float3 bitangent = cross(normal, tangent) * (input.world_tangent.w < 0.0 ? -1.0 : 1.0) * side;
        float3 sampled = normal_map.Sample(normal_map_sampler, input.uv).xyz * 2.0 - 1.0;
        normal = normalize(tangent * sampled.x + bitangent * sampled.y + normal * sampled.z);
    }
    float3 orm = orm_map.Sample(orm_map_sampler, input.uv).rgb;
    float occlusion = orm.r;
    float roughness = clamp(orm.g, 0.05, 1.0);

    // The scan's colour, its mean pulled to the terrain's rock (the scale
    // per piece, gfx::RockKit::colour_match): the contrast is the scan's.
    float3 albedo = base_colour.Sample(base_colour_sampler, input.uv).rgb * rock.albedo_scale.rgb * input.params.x;
    float2 q = input.world_position.xz;
    // The terrain's broken-rock tint (terrain.hlsl): the same warm and cool
    // drift over the world, so a boulder takes the colour of the face it
    // fell from.
    float warm = value_noise(q * 0.011 + 3.3);
    float3 broken = lerp(float3(0.9, 0.94, 1.0), float3(1.08, 1.0, 0.88), warm);
    albedo *= lerp((float3)1.0, broken, rock.contact.z);

    // Moss and grass on the faces turned up, near the valley floor (the
    // instance's amount, gfx::Vegetation::place_kit), broken up by noise so
    // it lies in patches and not as a coat.
    float up = smoothstep(0.55, 0.9, normal.y);
    float patches = value_noise(input.world_position.xz * 0.6 + input.world_position.y * 0.3);
    float moss = saturate(up * input.params.y * rock.albedo_scale.a * smoothstep(0.35, 0.75, patches + 0.25 * input.params.y));
    float3 moss_colour = lerp(scene.palette[PALETTE_GRASS].rgb, scene.palette[PALETTE_GRASS_DRY].rgb, patches) * 0.8;
    albedo = lerp(albedo, moss_colour, moss);

    // Where the piece goes into the ground: the ground's own colour (grass
    // on the flat, rock on the steep, as terrain.hlsl chooses) and a
    // darkening over the last `contact.x` metres, so the join is soil and
    // shade, not a cut.
    float above = dot(input.ground.xyz, input.world_position) - input.ground.w;
    float band = max(rock.contact.x, 0.01);
    float near_ground = (1.0 - smoothstep(0.0, band, above)) * rock.contact.y;
    float ground_flat = saturate(input.ground.y);
    float rocky = 1.0 - smoothstep(scene.terrain_params.z, scene.terrain_params.z + 0.22, ground_flat);
    float3 soil = lerp(lerp(scene.palette[PALETTE_GRASS_DRY].rgb, scene.palette[PALETTE_GRASS].rgb, 0.5) * 0.75,
                       scene.palette[PALETTE_ROCK].rgb, rocky);
    albedo = lerp(albedo, soil, near_ground * 0.5);
    // The contact shade: the sky and the sun both reach the foot of a rock
    // less, since the ground and the rock hide each other there. The sun's
    // share is cut by less, so a sunlit foot still reads as lit.
    occlusion *= 1.0 - 0.6 * near_ground;
    float contact_sun = 1.0 - 0.35 * near_ground;

    float3 to_sun = normalize(scene.sun.xyz);
    float3 to_camera = normalize(scene.camera_position.xyz - input.world_position);
    // The shared lighting path, as skinned.hlsl lights a scanned hide.
    float visibility = sun_visibility(input.world_position, normal, shadow_map, shadow_map_sampler);
    float3 direct = direct_sun(normal, visibility * contact_sun) + local_light(input.world_position, normal);
    float3 ambient = ambient_light_at(normal, input.world_position);
#ifdef USE_TERRAIN_SUN
    ambient *= terrain_sky(input.world_position);  // down in a gully, less sky
#endif
    // A rough dielectric's one GGX lobe: the scans' roughness is 0.86 on
    // average, so it is a faint sheen on the sunward edges.
    float3 half_vector = normalize(to_sun + to_camera);
    float alpha = roughness * roughness;
    float n_dot_h = saturate(dot(normal, half_vector));
    float denominator = n_dot_h * n_dot_h * (alpha * alpha - 1.0) + 1.0;
    float distribution = (alpha * alpha) / max(3.14159265 * denominator * denominator, 1e-4);
    float fresnel = 0.04 + 0.96 * pow(1.0 - saturate(dot(half_vector, to_camera)), 5.0);
    float specular = fresnel * distribution * saturate(dot(normal, to_sun)) * 0.25;

    float3 lit_color = albedo * (direct + ambient * occlusion) +
                       specular * scene.sun_color.rgb * scene.sun.w * visibility;
    float3 fogged = apply_fog(lit_color, input.world_position);
    return float4(scene_out(fogged), occlusion_share(ambient * occlusion, direct + ambient * occlusion));
}
#endif
