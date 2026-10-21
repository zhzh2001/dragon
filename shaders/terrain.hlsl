// The atmosphere's atlas after the shadow map and the detail tile (the baked
// lattice takes slot 2 only on a retro tier, which has no atmosphere).
#define ATMOSPHERE_SLOT 2
#define TERRAIN_SUN_SLOT 3  // and the terrain's shadow heights after it
#include "scene_common.hlsl"

struct VertexIn {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 color    : TEXCOORD2;
    // x: fbm(xz * 0.0055), the patch noise, computed once on the CPU
    // (App::regenerate_terrain) for SM2's vertex stage.
    float2 uv       : TEXCOORD3;
};

// The textures, and the noise -- included once: the pipeline cache inlines an
// include only the first time it meets it, whatever #ifdef it sits under.
#ifdef FRAGMENT_STAGE
DEPTH2D(shadow_map, 0);
TEXTURE2D(detail_tile, 1);
#define NOISE_SLOT 2  // the baked lattice, on a retro tier
#ifdef ATMOSPHERE
// Scanned rock and scree (tools/build_terrain_rock.py: a normal map in rgb,
// the scan's luminance in a), after the atmosphere's two maps.
TEXTURE2D(rock_map, 4);
TEXTURE2D(scree_map, 5);
// The heights texture with the forest's canopy in g and b
// (gfx::Atmosphere::set_canopy).
TEXTURE2D(canopy_map, 6);
// The ground's scans, as the rock's: meadow grass, pine litter, river
// gravel (tools/build_terrain_rock.py).
TEXTURE2D(grass_map, 7);
TEXTURE2D(forest_map, 8);
TEXTURE2D(gravel_map, 9);
#endif
#endif
#ifdef TERRAIN_LITE
// The patches in two octaves: the third is 33 m blotches the ground scan's
// grain covers at the distances it shows.
#define FBM_OCTAVES 2
#endif
#include "noise.hlsl"

#ifdef SM2
// ---- SM2 (docs/PORTING.md, R4): the material is worked out per vertex and
// the lighting is scene_common's per-vertex path. What ps_2_0's 64
// instructions keep: one detail-tile sample, each material's channel of it,
// the four-way blend, one 2x2 shadow lookup, the fog and the tonemap. What
// they lose against the per-pixel path below: the tile's broad second scale
// and triplanar rock, the strata, the fine grain, the micro-relief normal and
// the snow glint. The grid carries the blends; the patch noise comes per
// vertex from the CPU.

#ifdef FIXED_FUNCTION
// Fixed function (docs/PORTING.md R5): the material is one colour per
// vertex -- the same blends, resolved here instead of per pixel, lit and
// finished (ff_lit) -- and the detail tile modulates it 2x (stage 1,
// MODULATE2X). The tile is greyscale on this tier (its grass channel in
// every channel, gfx::Device at load), since a stage modulates by the whole
// texture; its mips average to the 0.5 that leaves the colour alone, so
// distance fades the detail by itself.
struct VertexOut {
    float4 clip_position : SV_Position;
    FF_VARYINGS
};
#else
struct VertexOut {
    float4 clip_position : SV_Position;
    float4 detail : TEXCOORD0;  // xy the detail tile's uv, z rock, w snow
    float4 grass : TEXCOORD1;   // rgb the grass, lush to dry; a sand
    float4 rock : TEXCOORD2;    // rgb the rock, dark to light; a the detail's strength
    float4 tint : TEXCOORD3;    // rgb the vertex tint
    SM2_LIGHT_VARYINGS
};
#endif

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.clip_position = mul(scene.view_proj, float4(input.position, 1.0));
    float3 p = input.position;
    float3 normal = normalize(input.normal);
    float flatness = saturate(normal.y);
    float water_level = scene.terrain_params.x;
    float snow_line = scene.terrain_params.y;
    float rock_slope = scene.terrain_params.z;
    float patches = input.uv.x;  // fbm(p.xz * 0.0055), from the CPU

    float3 grass = lerp(scene.palette[PALETTE_GRASS].rgb, scene.palette[PALETTE_GRASS_DRY].rgb,
                        smoothstep(0.35, 0.75, patches));
    float3 rock = lerp(scene.palette[PALETTE_ROCK_DARK].rgb, scene.palette[PALETTE_ROCK].rgb,
                       smoothstep(0.25, 0.8, patches));
    float bar_top = water_level + 1.0 + 3.0 * patches;
    float sand = 1.0 - smoothstep(water_level + 0.3, bar_top, p.y);
    float slope_blend = 1.0 - smoothstep(rock_slope, rock_slope + 0.22, flatness);
    float altitude_rockiness = smoothstep(snow_line * 0.5, snow_line, p.y) * 0.6;
    float ragged_snow_line = snow_line + (patches - 0.5) * 90.0;
    float snow = smoothstep(ragged_snow_line, ragged_snow_line + 120.0, p.y) * smoothstep(0.52, 0.78, flatness);
#ifdef FIXED_FUNCTION
    // The vertex colour is the finished diffuse already: the material blend
    // above and ff_lit, computed once at build (gfx/prelit.h,
    // App::regenerate_terrain), since neither the ground nor the sun moves.
    // On a card without T&L this stage runs on the CPU every frame, and the
    // blend and the light were most of it.
    o.uv = p.xz / 16.0;
    o.diffuse = float4(input.color, 1.0);
    // No edge fade: the skirt's edge is 7.5 km out, where this fog has
    // already taken it, and a card without T&L pays for every slot here.
    o.fog = ff_fog(p);
    return o;
#else
    o.grass = float4(grass, sand);
    o.rock = float4(rock, 0.0);
    o.detail = float4(p.xz / 16.0, saturate(slope_blend + altitude_rockiness), snow);
    float camera_distance = length(p - scene.camera_position.xyz);
    o.rock.a = 1.0 - smoothstep(500.0, 2600.0, camera_distance);
    o.tint = float4(input.color, 0.0);

    Sm2Light light = sm2_light_vertex(p, normal, 0.0);
    // The map's edge dissolves into the sky, which is the fog's colour.
    float half_extent = scene.view_params.w;
    if (half_extent > 0.0) {
        float edge = max(abs(p.x), abs(p.z)) / half_extent;
        light.shadow.w = max(light.shadow.w, smoothstep(0.80, 0.99, edge));
    }
    SM2_LIGHT_OUT(o, light);
    return o;
#endif
}
#endif

#ifdef FRAGMENT_STAGE
#ifdef FIXED_FUNCTION
float4 fs_main(VertexOut input) : SV_Target {
    // Stage 0 the diffuse, stage 1 the detail tile MODULATE2X with it.
    float3 d = detail_tile.Sample(detail_tile_sampler, input.uv.xy).rgb;
    float3 color = saturate(input.diffuse.rgb * d * 2.0) * FF_KEEP_SHADOW_SLOT;
    return float4(ff_finish(color, input.fog), 1.0);
}
#else
float4 fs_main(VertexOut input) : SV_Target {
    float4 d = detail_tile.Sample(detail_tile_sampler, input.detail.xy);
    // Each material wears its channel: 1 + (d - 0.5) * 2 * strength.
    float4 m = 1.0 + (d - 0.5) * (2.0 * input.rock.a);
    float3 albedo = lerp(input.grass.rgb * m.g, scene.palette[PALETTE_SAND].rgb * m.b, input.grass.a);
    albedo = lerp(albedo, input.rock.rgb * m.r, input.detail.z);
    albedo = lerp(albedo, scene.palette[PALETTE_SNOW].rgb * m.a, input.detail.w);
    return float4(sm2_finish(albedo * input.tint.rgb, SM2_LIGHT_IN(input), shadow_map, shadow_map_sampler), 1.0);
}
#endif
#endif

#else  // the per-pixel path


struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    // What erosion left (game::Terrain::erosion_at): x where the water ran,
    // y where sediment settled. The per-pixel tiers' vertex colour carries
    // it instead of a tint, which only the vertex-lit ones use.
    float2 erosion : TEXCOORD2;
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.clip_position = mul(scene.view_proj, float4(input.position, 1.0));
    o.world_position = input.position;
    o.normal = input.normal;
    o.erosion = input.color.xy;
    return o;
}
#endif

#ifdef FRAGMENT_STAGE

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

float3 terrain_albedo(float3 world_position, float3 normal, TerrainDetail detail, float strength, float2 erosion) {
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
#ifndef TERRAIN_LITE
    float strata = value_noise(float2(world_position.y * 0.11,
                                      dot(world_position.xz, float2(0.013, 0.017))));
    float steepness = 1.0 - flatness;
    rock *= 1.0 + (strata - 0.5) * 0.55 * smoothstep(0.25, 0.7, steepness);
#endif

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
    // Where the water ran, the soil went with it: gullies are bare rock a
    // little further down the slope than their banks.
    float gully = smoothstep(0.45, 0.85, erosion.x);
    albedo = lerp(albedo, rock, saturate(slope_blend + altitude_rockiness + gully * 0.6 * (1.0 - flatness * 0.5)));

    // Where the sediment settled: scree and gravel fans, paler and greyer
    // than the face above them, the grain of the dirt channel on them.
    float scree = smoothstep(0.08, 0.5, erosion.y) * (1.0 - smoothstep(0.9, 0.98, flatness) * 0.5);
    float3 scree_colour = lerp(ROCK, SAND, 0.35) * 1.08 * modulate(detail.ground.b, strength);
    albedo = lerp(albedo, scree_colour, scree * 0.85);
    // The stream beds stay darker and damper than what drained into them.
    albedo *= 1.0 - 0.22 * smoothstep(0.6, 0.95, erosion.x);

    // Snow only settles where it is both high and not too steep. The noise makes
    // the snow line ragged instead of a perfect contour.
    float ragged_snow_line = snow_line + (patches - 0.5) * 90.0;
    float snow_amount = smoothstep(ragged_snow_line, ragged_snow_line + 120.0, height) *
                        smoothstep(0.52, 0.78, flatness);
    albedo = lerp(albedo, SNOW * modulate(detail.ground.a, strength), snow_amount);

    // Fine grain last, so it varies every material rather than only the grass.
    return albedo * (0.88 + 0.24 * grain);
}

#ifdef ATMOSPHERE
// A scanned map on the three world planes, blended by the normal: the bent
// normal (the "whiteout" blend, each plane's tangent normal added to the
// surface's in that plane's axes) and the luminance detail.
float4 triplanar_scan(Texture2D<float4> map, SamplerState map_sampler, float3 p, float3 n, float scale) {
    float3 w = pow(abs(n), (float3)4.0);
    w /= max(w.x + w.y + w.z, 1e-4);
    float4 sx = map.Sample(map_sampler, p.zy * scale);
    float4 sy = map.Sample(map_sampler, p.xz * scale);
    float4 sz = map.Sample(map_sampler, p.xy * scale);
    float3 tx = sx.xyz * 2.0 - 1.0, ty = sy.xyz * 2.0 - 1.0, tz = sz.xyz * 2.0 - 1.0;
    float3 nx = float3(tx.xy + n.zy, abs(tx.z) * n.x).zyx;
    float3 ny = float3(ty.xy + n.xz, abs(ty.z) * n.y).xzy;
    float3 nz = float3(tz.xy + n.xy, abs(tz.z) * n.z);
    return float4(normalize(nx * w.x + ny * w.y + nz * w.z), sx.a * w.x + sy.a * w.y + sz.a * w.z);
}

// The same on the ground plane alone, for the scans of what lies on gentle
// slopes: one fetch where the triplanar takes three, two scales mixed (the
// fine one gone by `fine`) so neither's repeat shows.
// Fetched only where it shows: on the ground's grazing angles the
// sampler's anisotropy makes every fetch several, and six a pixel cost
// 1.5 ms at 1080p.
float4 ground_scan(Texture2D<float4> map, SamplerState map_sampler, float3 p, float3 n, float scale, float fine) {
    float4 s = map.Sample(map_sampler, p.xz * scale * 0.29 + 0.37);
    if (fine > 0.0) s = lerp(s, lerp(s, map.Sample(map_sampler, p.xz * scale), 0.6), fine);
    float3 t = s.xyz * 2.0 - 1.0;
    return float4(normalize(float3(t.xy + n.xz, abs(t.z) * n.y).xzy), s.a);
}

#endif

float4 fs_main(VertexOut input) : SV_Target {
    float3 normal = normalize(input.normal);
    float3 to_sun = normalize(scene.sun.xyz);

    float camera_distance = length(input.world_position - scene.camera_position.xyz);

    // Micro-relief: a noise-gradient normal perturbation that fades out by
    // 400 m. Up close the ground gets shape the mesh does not carry; far away
    // it vanishes before it can shimmer.
    // TERRAIN_LITE (the Terrain setting at Low and below, gfx/pipeline.cpp):
    // the per-pixel shape and the scans go -- the micro-relief, the crags,
    // the rock's blotches and strata, the triplanar rock and scree -- and the
    // ground keeps one scan fetch. Each of those alone costs a phone's Adreno
    // little; together they keep the shader too big for the GPU to hold many
    // pixels in flight, and the terrain took 23 of a 50 ms frame on the K30
    // at Low; lite, 6 (docs/porting/MOBILE.md, "The terrain").
#ifndef TERRAIN_LITE
    float detail_fade = 1.0 - smoothstep(80.0, 400.0, camera_distance);
    if (detail_fade > 0.0) {
        float2 p = input.world_position.xz * 0.35;
        float h = 0.5;
        float gx = value_noise(p + float2(h, 0.0)) - value_noise(p - float2(h, 0.0));
        float gz = value_noise(p + float2(0.0, h)) - value_noise(p - float2(0.0, h));
        normal = normalize(normal + float3(-gx, 0.0, -gz) * (0.55 * detail_fade));
    }
#endif

#if defined(ATMOSPHERE) && !defined(TERRAIN_LITE)
    // Crags: rock is rough at every scale the mesh is too coarse for, and a
    // face that bends the light smoothly reads as sand. A bump of two value
    // noise octaves (24 m and 9 m) on the two vertical world planes, blended
    // by the normal like a triplanar map, out to 3.5 km. (Ridged noise in
    // the face's own frame was tried first: its ridge lines ring like
    // contours and the frame turns with the normal, and the faces came out
    // as wood grain.)
    {
        float rocky_ground = 1.0 - smoothstep(scene.terrain_params.z, scene.terrain_params.z + 0.25, saturate(normal.y));
        float crag = rocky_ground * (1.0 - smoothstep(900.0, 3500.0, camera_distance));
        if (crag > 0.001) {
            float3 p = input.world_position;
            float3 bump = (float3)0.0;
            float2 w = abs(normal.xz) + 1e-3;
            w /= w.x + w.y;
            float scale = 1.0 / 24.0, weight = 1.0;
            for (int o = 0; o < 2; ++o) {
                const float e = 0.3;
                // The x-facing plane: (z, y). The z-facing plane: (x, y).
                float2 a = float2(p.z, p.y) * scale + float(o) * 11.3;
                float2 b = float2(p.x, p.y) * scale + float(o) * 5.9;
                float a0 = value_noise(a), b0 = value_noise(b);
                float2 ga = float2(value_noise(a + float2(e, 0.0)) - a0, value_noise(a + float2(0.0, e)) - a0) / e;
                float2 gb = float2(value_noise(b + float2(e, 0.0)) - b0, value_noise(b + float2(0.0, e)) - b0) / e;
                bump += (float3(0.0, ga.y, ga.x) * w.x + float3(gb.x, gb.y, 0.0) * w.y) * weight;
                scale *= 2.6;
                weight *= 0.6;
            }
            // Only the part of the bump along the face bends it.
            bump -= normal * dot(bump, normal);
            normal = normalize(normal - bump * 0.5 * crag);
        }
    }
    // Broken rock: grey and warm blotches a few metres to a few tens across,
    // so a face is not one flat colour. (Bedded cliffs -- 11 m ledges with
    // lit tops and jointed risers -- were tried here too, after the target's
    // right-hand cliff; this terrain has few faces steep enough to be cliffs,
    // and on the rest the beds read as a ploughed field.)
    float rocky = 1.0 - smoothstep(scene.terrain_params.z, scene.terrain_params.z + 0.22, saturate(normal.y));
    float2 q = input.world_position.xz;
    float blotch = value_noise(q * 0.045) * 0.6 + value_noise(q * 0.13 + 7.1) * 0.4;
    float warm = value_noise(q * 0.011 + 3.3);
    float3 broken = lerp(float3(0.9, 0.94, 1.0), float3(1.08, 1.0, 0.88), warm) * (0.78 + 0.44 * blotch);
    float3 rock_tint = lerp((float3)1.0, broken, rocky * (1.0 - smoothstep(600.0, 3000.0, camera_distance)));
#elif defined(ATMOSPHERE)
    float rocky = 1.0 - smoothstep(scene.terrain_params.z, scene.terrain_params.z + 0.22, saturate(normal.y));
    float3 rock_tint = (float3)1.0;
#endif
#ifdef ATMOSPHERE
    // The scans (F3): rock on the rocky ground, scree where the talus
    // settled, each at 7 m and 26 m mixed so neither scale's repeat shows,
    // the fine one gone by 700 m and the broad by 2.5 km.
    float scan_rock = rocky * (1.0 - smoothstep(700.0, 2500.0, camera_distance));
    float scan_scree = smoothstep(0.08, 0.45, input.erosion.y) * (1.0 - rocky) * (1.0 - smoothstep(400.0, 1500.0, camera_distance));
    float scan_detail = 0.5;
#ifdef TERRAIN_LITE
    // The rock keeps its broad scan (one triplanar, not three), or a slope
    // reads as smooth clay; the scree goes.
    if (scan_rock > 0.001) {
        float4 scan = triplanar_scan(rock_map, rock_map_sampler, input.world_position + 31.0, normal, 1.0 / 26.0);
        float amount = saturate(scan_rock);
        normal = normalize(lerp(normal, normalize(scan.xyz), amount * 0.85));
        scan_detail = lerp(0.5, scan.a, amount);
    }
#else
    if (scan_rock + scan_scree > 0.001) {
        float fine = 1.0 - smoothstep(250.0, 700.0, camera_distance);
        float4 rock_near = triplanar_scan(rock_map, rock_map_sampler, input.world_position, normal, 1.0 / 7.0);
        float4 rock_far = triplanar_scan(rock_map, rock_map_sampler, input.world_position + 31.0, normal, 1.0 / 26.0);
        float4 rock_scan = lerp(rock_far, lerp(rock_far, rock_near, 0.6), fine);
        float4 scree_scan = triplanar_scan(scree_map, scree_map_sampler, input.world_position, normal, 1.0 / 5.0);
        float total = scan_rock + scan_scree;
        float4 scan = (rock_scan * scan_rock + scree_scan * scan_scree) / total;
        float amount = saturate(total);
        normal = normalize(lerp(normal, normalize(scan.xyz), amount * 0.85));
        scan_detail = lerp(0.5, scan.a, amount);
    }
#endif
    // And the ground's (F3): meadow grass, pine litter where the canopy
    // covers it, gravel on the bars at the waterline. The
    // grass was the palette's green over one detail tile, and a landed
    // dragon stood on what read as a lawn. Out to 500 m, past which their
    // grain is under a pixel. The canopy map is coarse (7 m),
    // which suits where litter lies: about each stand, soft-edged.
    float2 canopy_uv = input.world_position.xz / max(scene.view_params.w, 1.0) * 0.5 + 0.5;
    float canopy_cover = canopy_map.SampleLevel(canopy_map_sampler, saturate(canopy_uv), 0.0).g;
    float ground_scans = (1.0 - rocky) * (1.0 - saturate(scan_scree * 2.0)) * (1.0 - smoothstep(200.0, 500.0, camera_distance));
    if (ground_scans > 0.001) {
        float fine = 1.0 - smoothstep(40.0, 150.0, camera_distance);
        float water = scene.terrain_params.x;
        float bar = 1.0 - smoothstep(water + 0.6, water + 3.5, input.world_position.y);
        float litter = saturate(canopy_cover * 1.6) * (1.0 - bar);
        float meadow = (1.0 - bar) * (1.0 - litter);
        float4 scan = (float4)0.0;
#ifdef TERRAIN_LITE
        // One fetch: the meadow's broad scale, which is most of the ground.
        scan = ground_scan(grass_map, grass_map_sampler, input.world_position, normal, 1.0 / 3.0, 0.0);
        meadow = 1.0, litter = 0.0, bar = 0.0;
        // The maps this variant leaves alone must still be bound where the
        // renderer binds them: unread, the compiler strips them, the
        // reflection counts 7 samplers for slots up to 9, and Vulkan refused
        // the pipeline on the K30 (Metal binds by index and never noticed).
        // Never true, and not provably so.
        [branch] if (input.world_position.y < -1e30) {
            scan += scree_map.SampleLevel(scree_map_sampler, (float2)0.0, 0.0) +
                    forest_map.SampleLevel(forest_map_sampler, (float2)0.0, 0.0) +
                    gravel_map.SampleLevel(gravel_map_sampler, (float2)0.0, 0.0);
        }
#else
        if (meadow > 0.01) scan += ground_scan(grass_map, grass_map_sampler, input.world_position, normal, 1.0 / 3.0, fine) * meadow;
        if (litter > 0.01) scan += ground_scan(forest_map, forest_map_sampler, input.world_position, normal, 1.0 / 2.5, fine) * litter;
        if (bar > 0.01) scan += ground_scan(gravel_map, gravel_map_sampler, input.world_position, normal, 1.0 / 2.0, fine) * bar;
#endif
        scan /= max(meadow + litter + bar, 1e-3);
        normal = normalize(lerp(normal, normalize(scan.xyz), ground_scans * 0.7));
        scan_detail = lerp(scan_detail, scan.a, ground_scans);
    }
#endif
    const TerrainDetail detail = sample_detail(input.world_position, normal);
    float detail_strength = 1.0 - smoothstep(500.0, 2600.0, camera_distance);
#ifdef ATMOSPHERE
    // Where a scan carries the fine grain, the tile keeps only its broad
    // mottle: at 0.3 of it the ground went flat as a carpet.
    detail_strength *= 1.0 - 0.35 * ground_scans;
#endif
    float3 albedo = terrain_albedo(input.world_position, normal, detail, detail_strength, input.erosion);
#ifdef ATMOSPHERE
    albedo *= rock_tint * modulate(scan_detail, 0.9);
    // The forest floor (F7): where the canopy covers the ground, needles and
    // litter instead of lawn, and less sky through the crowns. Up close the
    // trees' own shadows and the occlusion do this tree by tree; the canopy
    // map's 7 m texels would only paint blots there, so it fades in past
    // 150 m. A forest read as trees standing on a lawn.
    float canopy = canopy_cover * smoothstep(150.0, 500.0, camera_distance) * (1.0 - rocky);
    albedo = lerp(albedo, albedo * float3(0.62, 0.58, 0.48), canopy * 0.8);
#endif

    // The shared lighting path: same wrap, same ambient as everything else.
    float visibility = sun_visibility(input.world_position, normal, shadow_map, shadow_map_sampler);
    float3 ambient = ambient_light(normal);
#ifdef USE_TERRAIN_SUN
    ambient *= terrain_sky(input.world_position);  // the gully sees less sky than the ridge
#endif
#ifdef ATMOSPHERE
    ambient *= 1.0 - 0.45 * canopy;
    // Past the shadow cascades no crown shades the ground: the canopy's
    // cover stands in for the shade under it.
    visibility *= 1.0 - 0.7 * canopy * smoothstep(1400.0, 2000.0, camera_distance);
#endif
    float3 direct = direct_sun(normal, visibility) + local_light(input.world_position, normal);
    float3 lit_color = albedo * (direct + ambient);

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

    return float4(scene_out(fogged), occlusion_share(ambient, direct + ambient));
}
#endif
#endif  // SM2
