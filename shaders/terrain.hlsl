#include "scene_common.hlsl"

struct VertexIn {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 color    : TEXCOORD2;
};

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float3 tint : TEXCOORD2;
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.clip_position = mul(scene.view_proj, float4(input.position, 1.0));
    o.world_position = input.position;
    o.normal = input.normal;
    o.tint = input.color;
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH2D(shadow_map, 0);
TEXTURE2D(detail_tile, 1);
#define NOISE_SLOT 2  // the baked lattice, on a retro tier
#include "noise.hlsl"

// Terrain material from height and slope. Kept in the shader so it can be
// retuned by saving the file while flying.
// The detail tile (DIRECTION.md row 7): one RGBA greyscale set -- R rock,
// G grass, B dirt and gravel, A snow -- tiling every 16 m, each channel
// averaging 0.5 so it modulates its material around its palette colour.
// Sampled at two scales (16 m and 97 m, offset) and mixed, which hides the
// repeat from altitude; the rock channel is projected three ways by the
// normal so a cliff is not a smear of the ground's projection. Its contrast
// fades out over distance, where it would only shimmer.
struct TerrainDetail {
    float4 ground;  // the xz projection, both scales mixed
    float rock;     // the rock channel, triplanar
};

TerrainDetail sample_detail(float3 p, float3 normal) {
    TerrainDetail d;
    const float fine = 1.0 / 16.0, broad = 1.0 / 97.0;
    d.ground = lerp(detail_tile.Sample(detail_tile_sampler, p.xz * fine),
                    detail_tile.Sample(detail_tile_sampler, p.xz * broad + 0.37), 0.35);
    float3 w = pow(abs(normal), (float3)4.0);
    w /= max(w.x + w.y + w.z, 1e-4);
    d.rock = d.ground.r * w.y + detail_tile.Sample(detail_tile_sampler, p.zy * fine).r * w.x +
             detail_tile.Sample(detail_tile_sampler, p.xy * fine).r * w.z;
    return d;
}

float modulate(float d, float strength) { return 1.0 + (d - 0.5) * 2.0 * strength; }

float3 terrain_albedo(float3 world_position, float3 normal, TerrainDetail detail, float strength) {
    // Colours come from the shared palette, so the ground is graded together
    // with the plants standing on it.
    const float3 SAND       = scene.palette[PALETTE_SAND].rgb;
    const float3 GRASS      = scene.palette[PALETTE_GRASS].rgb;
    const float3 GRASS_DRY  = scene.palette[PALETTE_GRASS_DRY].rgb;
    const float3 ROCK       = scene.palette[PALETTE_ROCK].rgb;
    const float3 ROCK_DARK  = scene.palette[PALETTE_ROCK_DARK].rgb;
    const float3 SNOW       = scene.palette[PALETTE_SNOW].rgb;

    float water_level = scene.terrain_params.x;
    float snow_line   = scene.terrain_params.y;
    float rock_slope  = scene.terrain_params.z;

    float height = world_position.y;
    // normal.y is 1 on flat ground and 0 on a vertical cliff.
    float flatness = saturate(normal.y);

    // Broad patches of dry/lush ground, plus fine grain that keeps large flat
    // areas from reading as a solid colour.
    float patches = fbm(world_position.xz * 0.0055);
    float grain = value_noise(world_position.xz * 0.09);

    // Each material wears its channel: 1 + (d - 0.5) * 2 * strength.
    float3 grass = lerp(GRASS, GRASS_DRY, smoothstep(0.35, 0.75, patches)) * modulate(detail.ground.g, strength);
    float3 rock = lerp(ROCK_DARK, ROCK, smoothstep(0.25, 0.8, patches)) * modulate(detail.rock, strength);

    // Strata: near-horizontal bands on steep faces, the single strongest cue
    // that a cliff is rock and not grey clay. Height-keyed noise, squeezed by
    // steepness so flat ground shows none of it.
    float strata = value_noise(float2(world_position.y * 0.11,
                                      dot(world_position.xz, float2(0.013, 0.017))));
    float steepness = 1.0 - flatness;
    rock *= 1.0 + (strata - 0.5) * 0.55 * smoothstep(0.25, 0.7, steepness);

    // A narrow gravel bar right at the waterline, then grass above it. The
    // valley floor sits only a few metres above the river, so a 10 m band
    // turned the whole floor pale beige; the bar is 3 m now, ragged with the
    // patch noise so it reads as bars rather than a contour.
    float bar_top = water_level + 1.0 + 3.0 * patches;
    float3 albedo = lerp(SAND * modulate(detail.ground.b, strength), grass,
                         smoothstep(water_level + 0.3, bar_top, height));

    // Rock takes over on anything steep, and earlier at altitude where there is
    // less soil to hold on.
    float slope_blend = 1.0 - smoothstep(rock_slope, rock_slope + 0.22, flatness);
    float altitude_rockiness = smoothstep(snow_line * 0.5, snow_line, height) * 0.6;
    albedo = lerp(albedo, rock, saturate(slope_blend + altitude_rockiness));

    // Snow only settles where it is both high and not too steep. The noise makes
    // the snow line ragged instead of a perfect contour.
    float ragged_snow_line = snow_line + (patches - 0.5) * 90.0;
    float snow_amount = smoothstep(ragged_snow_line, ragged_snow_line + 120.0, height) *
                        smoothstep(0.52, 0.78, flatness);
    albedo = lerp(albedo, SNOW * modulate(detail.ground.a, strength), snow_amount);

    // Fine grain last, so it varies every material rather than only the grass.
    return albedo * (0.88 + 0.24 * grain);
}

float4 fs_main(VertexOut input) : SV_Target {
    float3 normal = normalize(input.normal);
    float3 to_sun = normalize(scene.sun.xyz);

    float camera_distance = length(input.world_position - scene.camera_position.xyz);

    // Micro-relief: a noise-gradient normal perturbation that fades out by
    // 400 m. Up close the ground gets shape the mesh does not carry; far away
    // it vanishes before it can shimmer.
    float detail_fade = 1.0 - smoothstep(80.0, 400.0, camera_distance);
    if (detail_fade > 0.0) {
        float2 p = input.world_position.xz * 0.35;
        float h = 0.5;
        float gx = value_noise(p + float2(h, 0.0)) - value_noise(p - float2(h, 0.0));
        float gz = value_noise(p + float2(0.0, h)) - value_noise(p - float2(0.0, h));
        normal = normalize(normal + float3(-gx, 0.0, -gz) * (0.55 * detail_fade));
    }

    const TerrainDetail detail = sample_detail(input.world_position, normal);
    const float detail_strength = 1.0 - smoothstep(500.0, 2600.0, camera_distance);
    float3 albedo = terrain_albedo(input.world_position, normal, detail, detail_strength) * input.tint;

    // The shared lighting path: same wrap, same ambient as everything else.
    float visibility = sun_visibility(input.world_position, normal, shadow_map, shadow_map_sampler);
    float3 lit_color = albedo * (direct_sun(normal, visibility) + ambient_light(normal));

    // Snow glints: a modest specular lobe only where the surface is bright and
    // cold, which reads as crust without turning the mountains to plastic.
    float snowiness = smoothstep(0.55, 0.85, albedo.b) * step(0.6, albedo.r);
    if (snowiness > 0.0) {
        float3 to_camera = normalize(scene.camera_position.xyz - input.world_position);
        float3 half_vector = normalize(to_sun + to_camera);
        float glint = pow(saturate(dot(normal, half_vector)), 48.0);
        lit_color += scene.sun_color.rgb * scene.sun.w * glint * 0.35 * snowiness * visibility;
    }

    float3 fogged = apply_fog(lit_color, input.world_position);

    // The ground has a hard edge (the skirt's), and from altitude you can see
    // it as a cliff at the horizon. Ramp to full sky over the outermost band
    // so the terrain dissolves instead of stopping.
    float half_extent = scene.view_params.w;
    if (half_extent > 0.0) {
        // Chebyshev distance, because the map is square: this reaches 1 on all
        // four edges at once rather than only at the corners.
        float edge = max(abs(input.world_position.x), abs(input.world_position.z)) / half_extent;
        float3 ray = normalize(input.world_position - scene.camera_position.xyz);
        fogged = lerp(fogged, sky_color(ray), smoothstep(0.80, 0.99, edge));
    }

    return float4(scene_out(fogged), 1.0);
}
#endif
