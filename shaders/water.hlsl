// The modern tier's slots: the shadow map (trees, creatures and the banks
// on the water), the atmosphere's atlas, the terrain's shadow heights.
#define ATMOSPHERE_SLOT 1
#define TERRAIN_SUN_SLOT 2
#include "scene_common.hlsl"
#if defined(FRAGMENT_STAGE) && defined(ATMOSPHERE)
DEPTH2D(shadow_map, 0);
#endif

// A still water surface: one quad at the water line, fresnel between a deep
// colour and the reflected sky, a little animated ripple in the normal, and a
// sun glint. It exists for the river: the valley floor sits above the water
// line everywhere else, so the terrain simply hides it there.

struct VertexIn {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 color    : TEXCOORD2;
};

#if defined(FIXED_FUNCTION)
// Fixed function (docs/PORTING.md R5): one plane, one colour -- the deep
// water toward the sky at a glancing-average fresnel -- the diffuse alone
// (stage 0, SELECTARG1 DIFFUSE) under the fog. The quad is four vertices
// across the map, so the fog is per vertex and coarse; the river is small.
struct VertexOut {
    float4 clip_position : SV_Position;
    FF_VARYINGS
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.clip_position = mul(scene.view_proj, float4(input.position, 1.0));
    o.uv = (float2)0.0;
    o.diffuse = float4(ff_display(lerp(scene.palette[PALETTE_WATER_DEEP].rgb, scene.sky_horizon.rgb, 0.2)), 1.0);
    o.fog = ff_fog(input.position);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
float4 fs_main(VertexOut input) : SV_Target {
    return float4(ff_finish(input.diffuse.rgb, input.fog), 1.0);
}
#endif
#elif defined(SM2)
// SM2 (docs/PORTING.md, R4): "fresnel only". The water is one quad across the
// map, so nothing can be lit per vertex; the pixel stage keeps the fresnel
// between the deep colour and a cheaper sky (no sun disc), and the fog, and
// drops the ripples and the glint.
struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.world_position = input.position;
    o.clip_position = mul(scene.view_proj, float4(input.position, 1.0));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
float4 fs_main(VertexOut input) : SV_Target {
    float3 to_point = input.world_position - scene.camera_position.xyz;
    float dist = length(to_point);
    float3 ray = to_point / max(dist, 1e-3);
    // A flat surface: the reflection is the ray mirrored upward.
    float3 reflection = sky_plain(float3(ray.x, abs(ray.y), ray.z));
    float fresnel = 0.04 + 0.96 * pow(1.0 - saturate(-ray.y), 5.0);
    float3 color = lerp(scene.palette[PALETTE_WATER_DEEP].rgb, reflection, fresnel);
    float density = scene.fog_color.a * exp(-max(input.world_position.y, 0.0) * 0.0018);
    float fog = saturate(1.0 - exp(-pow(dist * density, 2.0)));
    return float4(scene_out(lerp(color, sky_plain(ray), fog)), 1.0);
}
#endif
#else  // the per-pixel path

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.world_position = input.position;
    o.clip_position = mul(scene.view_proj, float4(input.position, 1.0));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
#ifdef USE_TERRAIN_SUN
// ---- the modern tier's river (docs/FIDELITY.md, F8). The heightfield the
// sun is marched over (terrain_sun.hlsl keeps the ground's height in its
// alpha) is also what the river reflects and what it lies on, so the water
// can trace its banks and the mountains in the reflection, see its bed in
// the shallows and froth at the edge, all without a second view or a
// screen copy.
float water_ground(float2 xz) {
    float half_extent = scene.view_params.w;
    float2 uv = xz / max(half_extent, 1.0) * 0.5 + 0.5;
    return terrain_sun_map.SampleLevel(terrain_sun_map_sampler, saturate(uv), 0.0).w;
}

// Ripples: three octaves of the clouds' value noise, two drifting with the
// current down the valley (-z) and one across, as a height whose gradient
// bends the normal; calmer far away, where they would only shimmer.
float3 water_normal(float2 p, float t, float distance) {
    float calm = 1.0 - smoothstep(150.0, 900.0, distance);
    float2 g = (float2)0.0;
    const float e = 0.25;
    float2 drift[3] = {float2(0.15, -1.2), float2(-0.4, -0.7), float2(0.9, 0.2)};
    float scale[3] = {1.0 / 9.0, 1.0 / 3.5, 1.0 / 1.3};
    float weight[3] = {0.55, 0.3, 0.15};
    for (int i = 0; i < 3; ++i) {
        float2 q = (p + drift[i] * t) * scale[i] + float(i) * 7.3;
        float h0 = cloud_value(q);
        g += float2(cloud_value(q + float2(e, 0.0)) - h0, cloud_value(q + float2(0.0, e)) - h0) / e * weight[i];
    }
    return normalize(float3(-g.x * 0.22 * calm, 1.0, -g.y * 0.22 * calm));
}

// The ground as a reflection sees it: no textures, no trees as such -- a
// dark forest green low down, grass and rock above by slope and height,
// lit by the sun where the terrain lets it through and by the sky, then
// hazed like any surface.
float3 water_reflected_ground(float3 p) {
    const float e = 7.0;
    float h = water_ground(p.xz);
    float3 n = normalize(float3(water_ground(p.xz - float2(e, 0.0)) - water_ground(p.xz + float2(e, 0.0)), 2.0 * e,
                                water_ground(p.xz - float2(0.0, e)) - water_ground(p.xz + float2(0.0, e))));
    float floor_height = scene.terrain_params.x;
    float steep = 1.0 - smoothstep(scene.terrain_params.z, scene.terrain_params.z + 0.25, n.y);
    float3 forest = scene.palette[PALETTE_SPRUCE].rgb * 0.8;
    float3 grass = scene.palette[PALETTE_GRASS].rgb;
    float3 rock = scene.palette[PALETTE_ROCK].rgb;
    float3 albedo = lerp(forest, grass, smoothstep(floor_height + 250.0, floor_height + 380.0, h));
    albedo = lerp(albedo, rock, max(steep, smoothstep(scene.terrain_params.y * 0.6, scene.terrain_params.y, h)));
    albedo = lerp(albedo, scene.palette[PALETTE_SNOW].rgb, smoothstep(scene.terrain_params.y, scene.terrain_params.y + 120.0, h) * n.y);
    float3 lit = albedo * (direct_sun(n, terrain_sun(p + n * 2.0)) + ambient_light(n) * terrain_sky(p));
    return apply_fog(lit, p);
}

// The reflected ray traced through the heightfield, 40 steps growing to
// 3 km; the sky where it escapes. A ray that only just clears the ground
// takes some of it, by how narrowly (the soft-shadow trick, the clearance
// over a cone widening with the distance): hit or miss alone left hard
// dark flecks wherever a ripple tipped a ray onto the bank.
float3 water_reflection(float3 origin, float3 dir) {
    float s = 3.0;
    float last = 0.0;
    float clear = 1.0;
    float3 nearest = origin;
    for (int i = 0; i < 40; ++i) {
        float3 q = origin + dir * s;
        float above = q.y - water_ground(q.xz);
        if (above < 0.0) {
            // One halving between the last step above and this one below.
            float mid = (last + s) * 0.5;
            float3 m = origin + dir * mid;
            if (m.y < water_ground(m.xz)) q = m;
            return water_reflected_ground(q);
        }
        float c = above / (2.0 + 0.12 * s);
        if (c < clear) {
            clear = c;
            nearest = q;
        }
        last = s;
        s *= 1.14;
        if (s > 3000.0) break;
    }
    float3 sky = sky_color(dir);
    if (clear >= 1.0) return sky;
    return lerp(water_reflected_ground(nearest - float3(0.0, 1.0, 0.0) * (nearest.y - water_ground(nearest.xz))), sky,
                smoothstep(0.0, 1.0, clear));
}
#endif

float4 fs_main(VertexOut input) : SV_Target {
    float t = scene.view_params.z;
    float2 p = input.world_position.xz;
#ifdef USE_TERRAIN_SUN
    if (atmosphere_on()) {
        float3 to_camera_m = scene.camera_position.xyz - input.world_position;
        float distance = length(to_camera_m);
        float3 view = -to_camera_m / max(distance, 1e-3);
        float3 n = water_normal(p, t, distance);
        float3 reflected = reflect(view, n);
        reflected.y = abs(reflected.y);
        float3 reflection = water_reflection(input.world_position + float3(0.0, 0.3, 0.0), reflected);

        // Under the surface: the bed (gravel, lit through the water) fading
        // into the deep colour with depth, reds first.
        float depth = max(scene.terrain_params.x - water_ground(p), 0.0);
        float visibility = sun_visibility(input.world_position, float3(0.0, 1.0, 0.0), shadow_map, shadow_map_sampler);
        float3 bed = scene.palette[PALETTE_SAND].rgb * 0.8 *
                     (direct_sun(float3(0.0, 1.0, 0.0), visibility) + ambient_light(float3(0.0, 1.0, 0.0)));
        float3 through = exp(-depth * float3(0.55, 0.22, 0.16));
        const float3 DEEP_M = scene.palette[PALETTE_WATER_DEEP].rgb * (ambient_light(float3(0.0, 1.0, 0.0)) + 0.3 * visibility);
        float3 under = lerp(DEEP_M, bed, through);

        float cosine = saturate(dot(-view, n));
        float fresnel = 0.02 + 0.98 * pow(1.0 - cosine, 5.0);
        float3 color = lerp(under, reflection, fresnel);

        // Froth at the edge, where the water is a hand deep, broken by the
        // ripples' own noise and drifting with them.
        float froth = (1.0 - smoothstep(0.1, 0.7, depth)) * smoothstep(0.45, 0.75, cloud_value(p * 0.6 + float2(0.0, -t * 0.8)));
        color = lerp(color, scene.palette[PALETTE_SNOW].rgb * (direct_sun(float3(0.0, 1.0, 0.0), visibility) + ambient_light(float3(0.0, 1.0, 0.0))), froth * 0.55);

        float glint = pow(saturate(dot(reflected, normalize(scene.sun.xyz))), 220.0);
        color += scene.sun_color.rgb * scene.sun.w * glint * 1.2 * visibility;
        return float4(scene_out(apply_fog(color, input.world_position)), 1.0);
    }
#endif
    // Two crossed ripple trains; small, so the reflection stays readable.
    float3 normal = normalize(float3(sin(p.x * 0.09 + t * 1.1) * 0.04 + sin(p.y * 0.13 - t * 0.8) * 0.03,
                                     1.0,
                                     cos(p.y * 0.07 + t * 0.7) * 0.04 + cos(p.x * 0.05 + t * 0.5) * 0.03));
    float3 to_camera = scene.camera_position.xyz - input.world_position;
    float dist = length(to_camera);
    float3 ray = -to_camera / max(dist, 1e-3);
    float3 reflected = reflect(ray, normal);
    reflected.y = abs(reflected.y);  // never reflect what is under the water
    float3 reflection = sky_color(reflected);

    const float3 DEEP = scene.palette[PALETTE_WATER_DEEP].rgb;
    float cosine = saturate(dot(-ray, normal));
    float fresnel = 0.04 + 0.96 * pow(1.0 - cosine, 5.0);
    float3 color = lerp(DEEP, reflection, fresnel);
    // Sun glint on the ripples.
    float glint = pow(saturate(dot(reflected, normalize(scene.sun.xyz))), 180.0);
    float glint_light = 1.0;
#ifdef USE_TERRAIN_SUN
    glint_light = terrain_sun(input.world_position);  // no glint in a mountain's shadow
#endif
    color += scene.sun_color.rgb * scene.sun.w * glint * 0.6 * glint_light;

    return float4(scene_out(apply_fog(color, input.world_position)), 1.0);
}
#endif
#endif  // SM2
