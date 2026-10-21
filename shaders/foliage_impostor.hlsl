// A far tree as its impostor (gfx::Foliage, docs/FIDELITY.md F7): one quad
// facing the camera, showing the four frames of the kind's baked atlas
// (foliage_impostor_bake.hlsl) about the camera's direction, blended by
// how near each is -- the nearest alone snapped a frame, 20 degrees, as
// the camera circled -- lit in the scene through the shared path with the
// baked normal. Each
// tree hands over from its crown at its own distance between `lod.x` and
// `lod.y`, by its hash, the crown's vertex stage (foliage.hlsl) taking the
// complementary share, so a tree is never both and never neither. The
// modern tier's alone.
#define ATMOSPHERE_SLOT 3
#define TERRAIN_SUN_SLOT 4
#include "scene_common.hlsl"

struct ImpostorUniforms {
    float4 kind;  // xy the kind's quadrant of the atlas (uv), z its centre's height, w its radius (m, at scale 1)
    float4 lod;   // x where the first trees hand over, y the last (m), z frames a side, w the quadrant's size (uv)
    float4 fade;  // x where the trees start to shrink away, y gone (m), z the crown's self-shadow (a light factor)
};
ConstantBuffer<ImpostorUniforms> impostor : UNIFORM_SLOT(1);

struct VertexIn {
    float3 position       : TEXCOORD0;  // the quad's corner, -1..1 in x and y
    float3 normal         : TEXCOORD1;
    float3 color          : TEXCOORD2;
    float2 uv             : TEXCOORD3;
    float4 position_scale : TEXCOORD4;
    float4 params         : TEXCOORD5;  // x yaw, y shade, z sway phase (the hash), w warm push
};

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
    float4 uv01 : TEXCOORD1;  // the four frames' uv, two a varying
    float4 uv23 : TEXCOORD2;
    float4 weights : TEXCOORD3;
    float4 tint : TEXCOORD4;  // x shade, y warm push, zw the yaw's cos and sin
};

// The upper hemisphere folded onto a square, -1..1 each way
// (gfx::Foliage::bake_impostors folds it the same way).
float2 hemi_octahedral(float3 d) {
    d.y = max(d.y, 0.0);
    d /= abs(d.x) + d.y + abs(d.z);
    return float2(d.x + d.z, d.x - d.z);
}

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    float scale = input.position_scale.w;
    float yaw = input.params.x;
    float c = cos(yaw), s = sin(yaw);
    float3 centre = input.position_scale.xyz + float3(0.0, impostor.kind.z * scale, 0.0);
    float3 to_camera = scene.camera_position.xyz - centre;
    float distance = length(to_camera);
    float3 view = to_camera / max(distance, 1e-3);
    // The camera's direction in the tree's own frame, its yaw undone.
    float3 local = float3(c * view.x - s * view.z, view.y, s * view.x + c * view.z);
    float2 e = hemi_octahedral(local);
    float frames = impostor.lod.z;
    // The frame grid, frame centres on whole numbers: the four about the
    // direction and how far it lies between them.
    float2 grid = clamp((e * 0.5 + 0.5) * frames - 0.5, 0.0, frames - 1.0);
    float2 f0 = min(floor(grid), frames - 2.0);
    float2 w = saturate(grid - f0);

    // Facing the camera, upright (from straight above, any way round).
    float3 right = normalize(cross(abs(view.y) > 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0), view));
    float3 up = cross(view, right);
    float radius = impostor.kind.w * scale;
    // Shrinking away at the end of the draw distance, as the crowns do.
    radius *= 1.0 - smoothstep(impostor.fade.x, impostor.fade.y, distance);
    float3 world = centre + (right * input.position.x + up * input.position.y) * radius;
    // Its crown stands here instead until its own distance, measured from
    // the base as foliage.hlsl measures it.
    float hash = frac(input.params.z * 0.15915494);
    float base_distance = length(input.position_scale.xyz - scene.camera_position.xyz);
    if (base_distance < lerp(impostor.lod.x, impostor.lod.y, hash)) world = centre;  // a point: nothing drawn

    o.world_position = world;
    o.clip_position = mul(scene.view_proj, float4(world, 1.0));
    float2 corner = float2(input.position.x * 0.5 + 0.5, 0.5 - input.position.y * 0.5);
    float2 base = impostor.kind.xy + corner / frames * impostor.lod.w;
    float step = impostor.lod.w / frames;
    o.uv01 = float4(base + f0 * step, base + (f0 + float2(1.0, 0.0)) * step);
    o.uv23 = float4(base + (f0 + float2(0.0, 1.0)) * step, base + (f0 + 1.0) * step);
    o.weights = float4((1.0 - w.x) * (1.0 - w.y), w.x * (1.0 - w.y), (1.0 - w.x) * w.y, w.x * w.y);
    o.tint = float4(input.params.y, input.params.w, c, s);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH2D(shadow_map, 0);
TEXTURE2D(albedo_atlas, 1);
TEXTURE2D(normal_atlas, 2);

float4 fs_main(VertexOut input) : SV_Target {
    float4 albedo = albedo_atlas.Sample(albedo_atlas_sampler, input.uv01.xy) * input.weights.x +
                    albedo_atlas.Sample(albedo_atlas_sampler, input.uv01.zw) * input.weights.y +
                    albedo_atlas.Sample(albedo_atlas_sampler, input.uv23.xy) * input.weights.z +
                    albedo_atlas.Sample(albedo_atlas_sampler, input.uv23.zw) * input.weights.w;
    float4 baked = normal_atlas.Sample(normal_atlas_sampler, input.uv01.xy) * input.weights.x +
                   normal_atlas.Sample(normal_atlas_sampler, input.uv01.zw) * input.weights.y +
                   normal_atlas.Sample(normal_atlas_sampler, input.uv23.xy) * input.weights.z +
                   normal_atlas.Sample(normal_atlas_sampler, input.uv23.zw) * input.weights.w;
    // The coverage, mip-filtered: a frame's edge thins out at a distance
    // rather than shimmering.
    if (albedo.a < 0.5) discard;
    float3 local = normalize(baked.xyz * 2.0 - 1.0);
    float c = input.tint.z, s = input.tint.w;
    float3 normal = float3(c * local.x + s * local.z, local.y, -s * local.x + c * local.z);
    float3 colour = albedo.rgb / max(albedo.a, 1e-3) * input.tint.x;
    colour = lerp(colour, colour * scene.palette[PALETTE_PLANT_WARM].rgb, input.tint.y);

    float visibility = sun_visibility(input.world_position, normal, shadow_map, shadow_map_sampler);
    float translucency = scene.light_params.y * saturate(baked.a);
    float3 ambient = ambient_light(normal);
#ifdef USE_TERRAIN_SUN
    ambient *= terrain_sky(input.world_position);
#endif
    // A crown shades itself -- its far cards in its near cards' shadow, its
    // depths seeing little sky -- which one flat surface does not: lit as
    // it is, an impostor was a quarter brighter than the crown it stood in
    // for (gfx::Foliage::impostor_self_shadow).
    float3 light = (direct_sun(normal, visibility) + ambient + translucent_sun(normal, visibility, translucency)) * impostor.fade.z;
    ambient *= impostor.fade.z;
    float3 fogged = apply_fog(colour * light, input.world_position);
    return float4(scene_out(fogged), occlusion_share(ambient, light));
}
#endif
