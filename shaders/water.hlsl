// The modern tier's slots: the shadow map (trees, creatures and the banks
// on the water), the atmosphere's atlas, the terrain's shadow heights, and
// the heights texture with the forest's canopy (gfx::Atmosphere::set_canopy).
#define ATMOSPHERE_SLOT 1
#define TERRAIN_SUN_SLOT 2
#include "scene_common.hlsl"
#if defined(FRAGMENT_STAGE) && defined(ATMOSPHERE)
DEPTH2D(shadow_map, 0);
TEXTURE2D(canopy_map, 3);
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
#ifdef ATMOSPHERE
    // Which way the river runs here (App::regenerate_terrain packs it into
    // the vertex colour), unit xz.
    float2 flow : TEXCOORD1;
#endif
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.world_position = input.position;
    o.clip_position = mul(scene.view_proj, float4(input.position, 1.0));
#ifdef ATMOSPHERE
    o.flow = input.color.xy * 2.0 - 1.0;
#endif
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
// The forest there: x its crown cover (0..1), y the height of its tops
// above the ground (m).
float2 water_canopy(float2 xz) {
    float half_extent = scene.view_params.w;
    float2 uv = xz / max(half_extent, 1.0) * 0.5 + 0.5;
    return canopy_map.SampleLevel(canopy_map_sampler, saturate(uv), 0.0).yz;
}

// The current's surface: twelve travelling wave trains, each
// moving at deep water's own speed for its length plus the current's
// (RIVER_SPEED), their heights summed as slopes. Over them, rough and calm
// patches drift downstream (value noise), as the wind and the bed make on a
// real river. And each train fades out once it is shorter than a few pixels
// on screen (`footprint`, metres a pixel): finer than that a wave only
// aliases, and the earlier noise ripples drew crawling lines from altitude
// that no anti-aliasing could hold.
static const float RIVER_SPEED = 2.4;  // m/s
float2 river_slope(float2 p, float2 flow, float t, float footprint, float calm) {
    float2 across = float2(-flow.y, flow.x);
    float2 carried = p - flow * (RIVER_SPEED * t);
    // Glassy stretches between the rough ones, and slack water along the
    // banks (`calm`): a surface rough everywhere alike read as a material,
    // not as water, and the mirror patches are where the banks reflect.
    float rough = 0.12 + 1.3 * smoothstep(0.15, 0.75, cloud_value(carried / 45.0 + 11.0) * cloud_value(carried / 17.0 - 3.0) * 1.6);
    rough *= calm;
    // The waves' domain, warped by a slow noise drifting with the current:
    // summed straight, the long trains interfered into a regular diamond
    // lattice that the sun's glitter drew across the river.
    float2 warp = float2(cloud_value(carried / 23.0 + 5.0), cloud_value(carried / 23.0 - 9.0)) - 0.5;
    float2 q = p + warp * 6.0;
    float2 slope = (float2)0.0;
    const int WAVES = 12;
    for (int i = 0; i < WAVES; ++i) {
        float fi = float(i);
        // 5 m down to 0.15 m, each length detuned from the geometric series
        // so none is a multiple of another. A river's surface is chop, not
        // swell: trains from 14 m drew long parallel crests bank to bank,
        // a wave pool that the sun's glitter hid and any shade showed.
        float wavelength = 5.0 * pow(0.72, fi) * (1.0 + 0.13 * sin(fi * 3.7 + 1.1));
        float fade = 1.0 - smoothstep(wavelength * 0.25, wavelength * 0.5, footprint);
        if (fade <= 0.0) break;
        // Every way round, a golden angle apart, so no two trains line up:
        // a river's chop is the bed's turbulence and the wind's, not a swell
        // from upstream, and trains spread about downstream alone summed to
        // crests across the river from bank to bank.
        float spread = fi * 2.39996 + 0.7;
        float2 dir = normalize(flow * cos(spread) + across * sin(spread));
        float k = 6.2831853 / wavelength;
        float omega = sqrt(9.81 * k);
        float phase = dot(q, dir) * k - omega * t - dot(flow, dir) * RIVER_SPEED * k * t + fi * 1.7;
        // Steepness falls with length, so short chop does not dwarf the swell.
        float amplitude = wavelength * 0.018;
        slope += dir * (amplitude * k * cos(phase) * fade);
    }
    return slope * rough;
}

float3 water_normal(float2 p, float2 flow, float t, float footprint, float calm) {
    // Flatter where a pixel covers a metre or more: the waves there are
    // only what the glint makes of them, and full slopes lit it in patches.
    float2 s = river_slope(p, flow, t, footprint, calm) * lerp(1.0, 0.45, smoothstep(0.3, 2.0, footprint));
    return normalize(float3(-s.x, 1.0, -s.y));
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
// The forest's crowns, seen from the water: their shaded and sunlit sides
// both, the green of the stand, under the same air as everything else.
float3 water_reflected_canopy(float3 p, float3 dir) {
    float3 n = normalize(float3(-dir.x, 0.5, -dir.z));  // the side of the stand facing the river
    float3 albedo = lerp(scene.palette[PALETTE_SPRUCE].rgb, scene.palette[PALETTE_BROADLEAF].rgb, 0.35);
    float3 lit = albedo * (direct_sun(n, terrain_sun(p)) * 0.6 + ambient_light(n) * terrain_sky(p));
    return apply_fog(lit, p);
}

// The reflected ray traced through the heightfield, 40 steps growing to
// 3 km; the sky where it escapes. A ray that only just clears the ground
// takes some of it, by how narrowly (the soft-shadow trick, the clearance
// over a cone widening with the distance): hit or miss alone left hard
// dark flecks wherever a ripple tipped a ray onto the bank. On its way it
// passes through the forest's crowns (water_canopy) as through a haze as
// thick as their cover, so a wooded bank mirrors as a dark band of trees.
float3 water_reflection(float3 origin, float3 dir) {
    float s = 3.0;
    float last = 0.0;
    float clear = 1.0;
    float3 nearest = origin;
    float through = 1.0;          // light still coming from beyond the crowns
    float3 in_crowns = (float3)0.0;  // where the crowns took it, weighted
    float taken = 0.0;
    float3 behind = (float3)0.0;
    bool hit = false;
    for (int i = 0; i < 40; ++i) {
        float3 q = origin + dir * s;
        float ground = water_ground(q.xz);
        float above = q.y - ground;
        if (above < 0.0) {
            // One halving between the last step above and this one below.
            float mid = (last + s) * 0.5;
            float3 m = origin + dir * mid;
            if (m.y < water_ground(m.xz)) q = m;
            behind = water_reflected_ground(q);
            hit = true;
            break;
        }
        // The crown band: from a quarter of the tops' height to the tops.
        float2 canopy = water_canopy(q.xz);
        if (canopy.x > 0.01 && above < canopy.y && above > canopy.y * 0.25) {
            float lost = through * (1.0 - exp(-canopy.x * (s - last) / 4.0));
            in_crowns += q * lost;
            taken += lost;
            through -= lost;
            if (through < 0.02) break;
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
    if (!hit) {
        float3 sky = sky_color(dir);
        behind = clear >= 1.0 ? sky
                              : lerp(water_reflected_ground(nearest - float3(0.0, 1.0, 0.0) * (nearest.y - water_ground(nearest.xz))),
                                     sky, smoothstep(0.0, 1.0, clear));
    }
    if (taken <= 0.0) return behind;
    float3 crowns = water_reflected_canopy(in_crowns / taken, dir);
    return crowns * (1.0 - through) + behind * through;
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
        float2 flow = length(input.flow) > 0.1 ? normalize(input.flow) : float2(0.0, -1.0);
        // Metres a pixel here: what the waves fade by.
        float footprint = max(length(ddx(input.world_position)), length(ddy(input.world_position)));
        float depth = max(scene.terrain_params.x - water_ground(p), 0.0);
        float3 n = water_normal(p, flow, t, footprint, lerp(0.3, 1.0, smoothstep(0.3, 2.5, depth)));
        float3 reflected = reflect(view, n);
        reflected.y = abs(reflected.y);
        float3 reflection = water_reflection(input.world_position + float3(0.0, 0.3, 0.0), reflected);

        // Under the surface: the bed (wet gravel, lit through the water)
        // fading into the water's own colour with depth, reds first. A
        // mountain river carries silt: with the clear-water extinction it
        // had, the bed lit by the open sky showed through two metres of
        // water, and in a mountain's shadow the river glowed a pool's teal,
        // brighter than the shaded valley around it. The sky there is the
        // valley's, as for the ground (terrain_sky).
        float visibility = sun_visibility(input.world_position, float3(0.0, 1.0, 0.0), shadow_map, shadow_map_sampler);
        float3 sky_in = ambient_light(float3(0.0, 1.0, 0.0)) * terrain_sky(input.world_position);
        float3 bed = scene.palette[PALETTE_SAND].rgb * 0.5 * (direct_sun(float3(0.0, 1.0, 0.0), visibility) + sky_in);
        float3 through = exp(-depth * float3(1.2, 0.75, 0.65));
        const float3 DEEP_M = scene.palette[PALETTE_WATER_DEEP].rgb * (sky_in + 0.3 * visibility);
        float3 under = lerp(DEEP_M, bed, through);

        float cosine = saturate(dot(-view, n));
        float fresnel = 0.02 + 0.98 * pow(1.0 - cosine, 5.0);
        float3 color = lerp(under, reflection, fresnel);

        // Froth at the edge, where the water is a hand deep, and in streaks
        // where the current boils, both carried downstream.
        float2 carried = p - flow * (t * RIVER_SPEED);
        float2 along = float2(dot(carried, float2(-flow.y, flow.x)), dot(carried, flow) * 0.25);
        // Streaks only near, where they are wider than a pixel or two: thin
        // wandering lines of bubbles, a noise's contour stretched along the
        // current. Its bands of high value were soft grey blots metres
        // across, which in shade read as fog lying on the water.
        float line_noise = cloud_value(along * 0.35 + 3.1);
        float streak = (1.0 - smoothstep(0.0, 0.035, abs(line_noise - 0.62))) * 0.4 *
                       smoothstep(0.35, 0.6, cloud_value(carried / 30.0 + 7.0)) * (1.0 - smoothstep(0.04, 0.12, footprint));
        float froth = max((1.0 - smoothstep(0.1, 0.7, depth)) * smoothstep(0.45, 0.75, cloud_value(carried * 0.6)) *
                              (1.0 - smoothstep(0.3, 1.0, footprint)),
                          streak);
        color = lerp(color, scene.palette[PALETTE_SNOW].rgb * (direct_sun(float3(0.0, 1.0, 0.0), visibility) + ambient_light(float3(0.0, 1.0, 0.0))), froth * 0.55);

        // The sun's glint, broader and dimmer where a pixel spans more water
        // (the waves under it, unresolved, are a rougher mirror), its energy
        // kept about the same: a sharp lobe at a distance drew hard white
        // patches.
        float sharpness = lerp(220.0, 30.0, smoothstep(0.1, 1.5, footprint));
        float glint = pow(saturate(dot(reflected, normalize(scene.sun.xyz))), sharpness) * (sharpness + 8.0) / 228.0;
        color += scene.sun_color.rgb * scene.sun.w * glint * 1.2 * visibility;
        // The shore, soft: the water thins out over its last metre of depth,
        // read from the heightfield, so the edge follows the smooth bank and
        // not the 6 m mesh's facets where the water plane crosses them, which
        // read as stair-steps at a distance however the pixels were averaged.
        float shore = smoothstep(0.05, 1.0, depth);
        // Where there is no water at all (the plane stands half a metre
        // above the line, over the bank's lowest facets), nothing: not even
        // depth, or it would hide a dragon's feet on the bank.
        if (shore < 0.01) discard;
        return float4(scene_out(apply_fog(color, input.world_position)), shore);
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
