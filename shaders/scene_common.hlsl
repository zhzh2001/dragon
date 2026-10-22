// Shared scene uniforms. Must match gfx::SceneUniforms in scene_uniforms.h.
//
// Every vec3 is stored as a float4. It removes any doubt about where a field
// lands, and it is what lets the same C++ struct feed the old MSL and HLSL's
// constant-buffer packing without a byte of padding to argue about.
//
// The block is the uniform in slot 0 of both stages, as `scene`. A shader
// whose slot 0 is something else (the shadow passes, particles) defines
// NO_SCENE before including this; it still gets the struct-free helpers
// (scene_out), but not the lighting path, which reads `scene`.
#pragma once

#include "common.hlsl"
#include "palette.hlsl"
// Included unconditionally: the pipeline cache inlines a file the first time it
// meets it, so an include under an #if a stage skips is lost to the next one.
#include "atmosphere_common.hlsl"

struct SceneUniforms {
    float4x4 view_proj;
    float4x4 light_view_proj;

    float4 camera_position;  // .xyz
    float4 camera_right;     // .xyz
    float4 camera_up;        // .xyz
    float4 camera_forward;   // .xyz

    // .xyz points FROM the surface TOWARD the sun. .w is intensity.
    float4 sun;
    float4 sun_color;      // .rgb
    float4 sky_zenith;     // .rgb
    float4 sky_horizon;    // .rgb
    float4 fog_color;      // .rgb, .a = density (1/metres)

    // x = tan(fov_y / 2), y = aspect, z = time (seconds),
    // w = terrain half extent in metres
    float4 view_params;
    // x = water level, y = snow line, z = rock slope threshold, w = ambient
    float4 terrain_params;
    // x = world-space size of one shadow texel, y = depth bias,
    // z = strength (0 disables), w = one shadow texel in UV space
    float4 shadow_params;
    // x = sun wrap (how far past the terminator direct light reaches),
    // y = foliage translucency, z = ground bounce strength, w unused
    float4 light_params;    // x sun wrap, y foliage translucency, z ground bounce, w sky reflection (skinned)
    // The world's named colours; see palette.hlsl for the indices.
    float4 palette[PALETTE_COUNT];
    // x exposure, y contrast, z saturation, w hue preservation: the grade a
    // tier without HDR applies here, in place of the post stack (LDR_OUTPUT).
    float4 output_grade;
    float4 fog_range;  // x start, y end of a linear fixed-function fog; y 0: exp2 by fog_color.a
    // The atmosphere (ATMOSPHERE, the modern tier; gfx/atmosphere.h): x the
    // sky's luminance scale (0: the analytic sky and fog instead), y km of
    // atmosphere per world metre for the aerial perspective, z the camera's
    // radius (km), w how much of the exp2 valley fog stays on top of it.
    float4 atmosphere;
    // x the aerial perspective's far distance (km), y sky-light gain,
    // z the planet's radius (km), w the sun disc's brightness.
    float4 atmosphere2;
    // The cloud layer (modern tier): x coverage 0..1 (0: none), y its base
    // and z its thickness in world metres, w the density.
    float4 clouds;
    // The shadow cascades (modern tier, gfx/shadow_map.h): each one's light
    // matrix; x their count (0: light_view_proj alone), yzw their texels (m).
    float4x4 cascade_view_proj[3];
    float4 cascade_params;
    // The volumetric clouds (modern tier, gfx/clouds.h): x 1 when they are
    // drawn, so the sky leaves its own slab out; yzw unused.
    float4 clouds2;
    // One local light (modern tier): xyz where, w its reach (0: none);
    // local_light_color rgb, and w the membranes' glow (skinned.hlsl).
    float4 local_light;
    float4 local_light_color;
    // The valley's thermals: xz the column, y its radius, w its strength
    // (m/s); thermal_count.x how many. A cumulus caps each (cloud_coverage).
    float4 thermals[12];
    float4 thermal_count;
};

// What a world shader writes: LINEAR light, into the 16-bit scene target. The
// tonemap and the gamma live in post_composite.hlsl, after the bloom, so
// bright things glow before they clip instead of desaturating toward white.
// Every world shader goes through this one function for the same reason the
// old tonemap was shared: the sky was once written differently from the
// terrain fogging toward it, and the seam showed along the horizon.
#ifndef LDR_OUTPUT
float3 scene_out(float3 color) { return max(color, 0.0); }
#endif

// Value noise (value_noise, fbm) lives in noise.hlsl, which a shader includes
// after declaring its own textures.

#ifndef NO_SCENE
ConstantBuffer<SceneUniforms> scene : UNIFORM_SLOT(0);

// The modern tier's world maps (docs/FIDELITY.md), in the slots each world
// shader reserves after its own textures and defines before including this
// file: the atmosphere's atlas (F1) at ATMOSPHERE_SLOT, and the terrain's
// shadow heights (F4, terrain_sun.hlsl) at TERRAIN_SUN_SLOT.
#if defined(ATMOSPHERE) && defined(ATMOSPHERE_SLOT) && defined(FRAGMENT_STAGE)
#define USE_ATMOSPHERE
TEXTURE2D(atmosphere_atlas, ATMOSPHERE_SLOT);
bool atmosphere_on() { return scene.atmosphere.x > 0.0; }
#ifdef TERRAIN_SUN_SLOT
#define USE_TERRAIN_SUN
TEXTURE2D(terrain_sun_map, TERRAIN_SUN_SLOT);
#endif
#endif

#ifdef LDR_OUTPUT
// A tier without a float target (gfx/render_tier.h): the world shader
// finishes the pixel itself -- exposure, tonemap, gamma, contrast and
// saturation, the cheap part of post_composite's grade -- into 8 bits.
#if defined(FIXED_FUNCTION)
// Fixed function (docs/PORTING.md R5) has no tonemap and works in display
// space throughout: what reaches scene_out is already display-encoded and
// only clamps, as the 8-bit pipeline would. A linear light colour enters
// that space through ff_display: the exposure, then the gamma, then the
// clamp a vertex colour gets. Not the texture: a DX7-class card samples
// a colour map's 8-bit values as they are, so it is display-encoded already.
float3 scene_out(float3 color) { return saturate(color); }
float3 ff_display(float3 linear_color) { return pow(saturate(linear_color * scene.output_grade.x), (float3)(1.0 / 2.2)); }
// A linear albedo authored on the CPU (a palette colour, a vertex colour)
// as a texture would carry it: display-encoded, no exposure.
float3 ff_albedo(float3 linear_color) { return pow(saturate(linear_color), (float3)(1.0 / 2.2)); }

// What a fixed-function vertex stage hands the stages, and nothing else: one
// diffuse colour, the fog factor and one texture coordinate. D3D9's software
// vertex processing runs these very vertex programs on a card without
// vertex shaders, so the semantics are the legacy registers (oD0, oFog,
// oT0; tools/d3d9/bake_d3d9.py restores them from SPIRV-Cross's output),
// and the fog follows D3D's convention: 1 is clear, 0 is fogged out.
//
// In this order, and every fragment stage reads a prefix of it: DXC drops
// a pixel shader's unused inputs and packs the rest into consecutive
// registers, and D3D12 then refuses the pipeline whose vertex outputs no
// longer sit in those registers (E_INVALIDARG; the sky, the water, the
// props and the lines all failed to build). A stage that reads only the
// diffuse, or the diffuse and the fog, leaves the registers it does read
// where the vertex stage put them. Metal and Vulkan link by location and
// never noticed.
// The texture coordinate is two components: a card without T&L has every
// vertex transformed on the CPU and sent across the bus, and a float4 made
// each one 40 bytes instead of 32 (on the G41's PCI M6 a vertex sent costs
// about a microsecond; docs/PORTING.md, "R6, as built").
#define FF_VARYINGS          \
    float4 diffuse : COLOR0; \
    float fog : FOG;         \
    float2 uv : TEXCOORD0;

#elif defined(SM2)
float3 scene_out(float3 color) { return ldr_encode_fast(max(color, 0.0), scene.output_grade); }
#else
float3 scene_out(float3 color) { return ldr_encode(max(color, 0.0), scene.output_grade); }
#endif
#endif

#ifdef USE_TERRAIN_SUN
// The terrain's own shadow at any distance and any height (terrain_sun.hlsl):
// lit above the shadow's top in this column, dark below, the penumbra
// widening with the distance to the ridge that casts it (the sun's half
// degree, doubled for the look). Lit outside the map, and with the
// atmosphere off (scene.atmosphere2.y's sky light then 0 too: the map's
// owner, gfx::Atmosphere, is off).
float terrain_sun(float3 world_position) {
    float half_extent = scene.view_params.w;
    float2 uv = world_position.xz / max(half_extent, 1.0) * 0.5 + 0.5;
    if (scene.atmosphere.x <= 0.0 || any(uv < 0.0) || any(uv > 1.0)) return 1.0;
    float2 top = terrain_sun_map.SampleLevel(terrain_sun_map_sampler, uv, 0.0).xy;
    float penumbra = 4.0 + top.y * 0.017;
    return smoothstep(top.x - penumbra, top.x + penumbra, world_position.y);
}

// The share of the sky the ground near `world_position` sees (1 on a ridge
// or out in the open, well under it at the bottom of a gully), fading to 1
// within 60 m above the ground: the terrain's ambient occlusion, which is
// most of the light in a valley the ridges shade.
float terrain_sky(float3 world_position) {
    float half_extent = scene.view_params.w;
    float2 uv = world_position.xz / max(half_extent, 1.0) * 0.5 + 0.5;
    if (scene.atmosphere.x <= 0.0 || any(uv < 0.0) || any(uv > 1.0)) return 1.0;
    float2 sky_ground = terrain_sun_map.SampleLevel(terrain_sun_map_sampler, uv, 0.0).zw;
    float above = world_position.y - sky_ground.y;
    // The share itself, already cosine-weighted. Squared, to make it read,
    // it took a third of the light off a valley floor the ridges already
    // shade, and the valley went murky.
    return lerp(sky_ground.x, 1.0, smoothstep(0.0, 60.0, above));
}
#endif

#if defined(ATMOSPHERE) && defined(FRAGMENT_STAGE)
// ---- the cloud layer (docs/FIDELITY.md): a slab of cumulus over the
// valley, its coverage a function of the ground position alone, so the sky
// can march it and every lit pixel can find its shadow in one lookup.
// Its own hash, not noise.hlsl's: this file comes before a shader's
// textures, and the clouds are the modern tier's alone.
float cloud_hash(float2 p) {
    p = frac(p * float2(233.34, 851.73));
    p += dot(p, p + 23.45);
    return frac(p.x * p.y);
}
float cloud_value(float2 p) {
    float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(cloud_hash(cell), cloud_hash(cell + float2(1.0, 0.0)), f.x),
                lerp(cloud_hash(cell + float2(0.0, 1.0)), cloud_hash(cell + float2(1.0, 1.0)), f.x), f.y);
}
// The drift: 9 m/s from the west-south-west.
float2 cloud_drift() { return float2(9.0, 3.0) * scene.view_params.z; }
// The field the layer's coverage is cut from: four octaves of broad cells.
float cloud_field(float2 xz) {
    float2 p = (xz + cloud_drift()) / 2600.0;
    float n = 0.0, a = 0.5;
    for (int i = 0; i < 4; ++i) {
        n += a * cloud_value(p);
        p = p * 2.03 + float2(17.1, 9.7);
        a *= 0.5;
    }
    return n;
}
// How much cloud stands over (x, z), 0..1: the field thresholded by the
// coverage so the gaps are sky, not thin cloud.
// A cumulus over every thermal (game/thermals.h), 0..1: rising air
// condenses at its top, which is how a pilot finds one from kilometres
// away. Wider than the column, ragged with the field, fuller for a stronger
// one. The volumetric clouds' weather map reads it too (clouds_weather.hlsl).
float thermal_caps(float2 xz) {
    float caps = 0.0;
    for (int i = 0; i < 12; ++i) {
        if (float(i) >= scene.thermal_count.x) break;
        float4 t = scene.thermals[i];
        // 4.5 radii: the weather map's texels are 156 m, and a cap two of
        // them across drew as a faceted lump.
        float2 d = (xz - t.xz) / (t.y * 4.5);
        caps = max(caps, exp(-dot(d, d)) * saturate(t.w / 5.0));
    }
    if (caps <= 0.0) return 0.0;
    return saturate(caps * (0.7 + 0.6 * cloud_field(xz * 1.3)) * 1.3 - 0.2);
}
float cloud_coverage(float2 xz) {
    float cover = scene.clouds.x;
    return max(saturate((cloud_field(xz) - (1.0 - cover) * 0.85) / 0.18), thermal_caps(xz));
}
// The density at a point in the slab: the coverage, rounded at the base and
// heaped toward the top, frayed by a finer noise.
float cloud_density(float3 p, float coverage) {
    float h = saturate((p.y - scene.clouds.y) / max(scene.clouds.z, 1.0));
    float profile = smoothstep(0.0, 0.15, h) * (1.0 - smoothstep(0.35 + 0.6 * coverage, 1.0, h));
    float2 q = (p.xz + cloud_drift() * 1.3) / 420.0 + h * 1.7;
    float detail = cloud_value(q) * 0.65 + cloud_value(q * 2.4 + 5.3) * 0.35;
    return saturate(coverage * profile * 1.4 - detail * 0.45) * scene.clouds.w;
}
// The share of the sun the clouds let through to `world_position`: the
// layer's coverage where the sun's ray crosses it.
float cloud_shadow(float3 world_position) {
    if (scene.clouds.x <= 0.0) return 1.0;
    float3 sun = normalize(scene.sun.xyz);
    float middle = scene.clouds.y + scene.clouds.z * 0.4;
    if (world_position.y > middle || sun.y < 0.02) return 1.0;
    float3 crossing = world_position + sun * ((middle - world_position.y) / sun.y);
    float c = cloud_coverage(crossing.xz);
    return lerp(1.0, 0.25, smoothstep(0.05, 0.7, c));
}
#endif

// Fraction of the sun reaching this point: 1 fully lit, 0 fully shadowed.
//
// The shadow map uses a conventional [0, 1] depth range, unlike the reversed-Z
// main pass, so nearer-to-light means a smaller depth value.
#ifdef ATMOSPHERE
// The cascaded map (the modern tier): the finest of the three boxes the
// point is inside, each a quadrant of the atlas. The bias and the normal
// offset follow the cascade's texel, so a 7 cm texel near is not biased like
// a 2 m one far; with the light 4.4 box-widths deep in every cascade, the
// depth bias is then the same in each.
float cascade_visibility(float3 world_position, float3 normal, Texture2D<float> shadow_map, SamplerState shadow_sampler) {
    float slope = saturate(1.0 - abs(dot(normal, normalize(scene.sun.xyz))));
    float texels[3] = {scene.cascade_params.y, scene.cascade_params.z, scene.cascade_params.w};
    for (int c = 0; c < 3; ++c) {
        if (float(c) >= scene.cascade_params.x) break;
        float3 offset_position = world_position + normal * texels[c] * 1.5;
        float4 light_clip = mul(scene.cascade_view_proj[c], float4(offset_position, 1.0));
        float3 ndc = light_clip.xyz / light_clip.w;
        // Inside, with a margin the 3x3 taps cannot cross into the next
        // quadrant from.
        if (any(abs(ndc.xy) > 0.97) || ndc.z < 0.0 || ndc.z > 1.0) continue;
        float2 uv = (ndc.xy * float2(0.5, -0.5) + 0.5) * 0.5 + float2(float(c % 2), float(c / 2)) * 0.5;
        // The light's depth span in metres (gfx/shadow_map.cpp): 4.4 box
        // half-widths, at least 3 km; the half-width from the texel and the
        // quadrant's size, half the atlas's.
        float half_width = texels[c] * (0.5 / scene.shadow_params.w) * 0.5;
        float range = max(half_width * 4.4, 3000.0);
        // Only within RECEIVER_DEPTH (2) half-widths of the box's centre
        // along the light: past that the cascade's casters were not drawn
        // (ShadowMap::caster_window), and the next box takes the point.
        float centre_z = (range * 0.5 - 1.0) / (range - 1.0);
        if (abs(ndc.z - centre_z) * (range - 1.0) > 2.0 * half_width) continue;
        float bias = texels[c] * (1.5 + slope * 4.0) / range;
        float step_uv = scene.shadow_params.w * 0.5;  // one atlas texel
        float lit = 0.0;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                float stored = shadow_map.SampleLevel(shadow_sampler, uv + float2(float(x), float(y)) * step_uv, 0.0);
                lit += (ndc.z - bias <= stored) ? 1.0 : 0.0;
            }
        }
        return lerp(1.0, lit / 9.0, scene.shadow_params.z);
    }
    return 1.0;
}
#endif

float sun_visibility_map(float3 world_position, float3 normal,
                         Texture2D<float> shadow_map, SamplerState shadow_sampler) {
    float strength = scene.shadow_params.z;
#ifdef ATMOSPHERE
    if (scene.cascade_params.x > 0.5 && strength > 0.0) return cascade_visibility(world_position, normal, shadow_map, shadow_sampler);
#endif

    // Offset along the normal before projecting. This removes most acne without
    // the peter-panning a large constant depth bias would cause.
    float texel_world = scene.shadow_params.x;
    float3 offset_position = world_position + normal * texel_world * 1.5;

    float4 light_clip = mul(scene.light_view_proj, float4(offset_position, 1.0));
    float3 light_ndc = light_clip.xyz / light_clip.w;

    // Outside the shadow volume there is no information, so treat it as lit
    // rather than guessing. Fog covers the transition at that distance.
    if (any(abs(light_ndc.xy) > 1.0) || light_ndc.z < 0.0 || light_ndc.z > 1.0) return 1.0;

    // NDC to UV, flipping Y because texture space runs downward.
    float2 uv = light_ndc.xy * float2(0.5, -0.5) + 0.5;
    float own_depth = light_ndc.z;

    // Slope-scaled bias: surfaces the sun grazes need much more than flat ones.
    float slope = saturate(1.0 - abs(dot(normal, normalize(scene.sun.xyz))));
    float bias = scene.shadow_params.y * (0.35 + slope * 2.5);

    // 3x3 PCF: enough to hide the texel grid without smearing contact shadows.
    float step_uv = scene.shadow_params.w;
    float lit_taps = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 sample_uv = uv + float2(float(x), float(y)) * step_uv;
            float stored = shadow_map.SampleLevel(shadow_sampler, sample_uv, 0.0);
            lit_taps += (own_depth - bias <= stored) ? 1.0 : 0.0;
        }
    }
    lit_taps /= 9.0;

    // strength controls how dark full shadow gets. Never fully black, or
    // terrain in shadow loses all its shape.
    return lerp(1.0, lit_taps, strength);
}

// The shadow map's term, and on the modern tier the terrain's at every
// distance on top of it: whichever is darker.
float sun_visibility(float3 world_position, float3 normal,
                     Texture2D<float> shadow_map, SamplerState shadow_sampler) {
    float strength = scene.shadow_params.z;
    if (strength <= 0.0) return 1.0;
    float visibility = sun_visibility_map(world_position, normal, shadow_map, shadow_sampler);
#ifdef USE_TERRAIN_SUN
    // A metre and a half up the normal, so a slope does not shade itself on
    // the map's 7 m texels.
    visibility = min(visibility, lerp(1.0, terrain_sun(world_position + normal * 1.5), strength));
#endif
#ifdef USE_ATMOSPHERE
    if (atmosphere_on()) visibility *= cloud_shadow(world_position);
#endif
    return visibility;
}

// Analytic sky. Also used by the fog in the terrain shader, so distant terrain
// dissolves into exactly the colour of the sky behind it instead of into a flat
// grey band. The retro tiers' sky, and the modern tier's with the atmosphere
// off.
float3 sky_color_analytic(float3 ray) {
    float horizon_blend = pow(saturate(1.0 - abs(ray.y)), 4.0);
    float3 base = lerp(scene.sky_zenith.rgb, scene.sky_horizon.rgb, horizon_blend);

    // Below the horizon, fade toward the fog colour so terrain edges and the
    // ground plane meet without a hard seam.
    float below = saturate(-ray.y * 6.0);
    base = lerp(base, scene.fog_color.rgb, below * 0.7);

    // Sun disc plus a wide forward-scattering glow.
    float sun_dot = saturate(dot(ray, normalize(scene.sun.xyz)));
    float glow = pow(sun_dot, 8.0) * 0.35 + pow(sun_dot, 256.0) * 3.0;
    base += scene.sun_color.rgb * glow;

    return base;
}

// ---- the atmosphere (docs/FIDELITY.md, F1): the modern tier's fragment
// stages read the frame atlas (atmosphere_common.hlsl) at ATMOSPHERE_SLOT,
// which each world shader defines before including this file -- the next
// slot after its own textures.
#ifdef USE_ATMOSPHERE

float4 atmo_atlas(float2 texel) {
    return atmosphere_atlas.SampleLevel(atmosphere_atlas_sampler, texel / ATMO_ATLAS_SIZE, 0.0);
}

// Sky radiance along `ray`, from the sky-view map.
float3 atmo_sky_radiance(float3 ray) {
    float3 sun = normalize(scene.sun.xyz);
    float2 sun_h = length(sun.xz) > 1e-4 ? normalize(sun.xz) : float2(1.0, 0.0);
    float2 ray_h = length(ray.xz) > 1e-4 ? normalize(ray.xz) : sun_h;
    float2 unit = atmo_sky_view_uv(scene.atmosphere2.z, scene.atmosphere.z, ray.y, dot(ray_h, sun_h));
    float2 texel = unit * (ATMO_SKY_VIEW_SIZE - 1.0) + 0.5 + float2(0.0, ATMO_SKY_VIEW_ROW);
    return atmo_atlas(texel).rgb * scene.atmosphere.x;
}

// One aerial-perspective slice at a screen position: a tile of the atlas,
// sampled inside its own edge texels so the filter never reaches the next.
float4 atmo_slice(float slice, float2 screen_uv) {
    float2 tile = float2(fmod(slice, 8.0), floor(slice / 8.0));
    float2 inside = clamp(screen_uv * ATMO_SLICE_SIZE, 0.5, ATMO_SLICE_SIZE - 0.5);
    return atmo_atlas(tile * ATMO_SLICE_SIZE + inside);
}

// The light the air adds between the camera and `world_position` (rgb,
// scaled) and the share of the surface's own that gets through (a).
float4 atmo_aerial(float3 world_position) {
    float4 clip = mul(scene.view_proj, float4(world_position, 1.0));
    float2 ndc = clip.xy / max(abs(clip.w), 1e-4);
    float2 screen_uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    float distance_km = length(world_position - scene.camera_position.xyz) * scene.atmosphere.y;
    float w = sqrt(saturate(distance_km / max(scene.atmosphere2.x, 1e-3)));
    // Slice s holds the distance at w = (s + 1) / 32; before the first, the
    // air fades in from nothing at the camera.
    float fs = w * ATMO_SLICES - 1.0;
    float4 result;
    if (fs < 0.0) {
        result = lerp(float4(0.0, 0.0, 0.0, 1.0), atmo_slice(0.0, screen_uv), saturate(w * ATMO_SLICES));
    } else {
        float s0 = min(floor(fs), ATMO_SLICES - 1.0);
        float s1 = min(s0 + 1.0, ATMO_SLICES - 1.0);
        result = lerp(atmo_slice(s0, screen_uv), atmo_slice(s1, screen_uv), saturate(fs - s0));
    }
    result.rgb *= scene.atmosphere.x;
    return result;
}

// The sky's light on a surface facing `normal`: row 224's integrals over
// the visible sky, up and on eight horizontal normals, blended by the
// normal's elevation. What faces down sees the ground, which ambient_light's
// bounce answers.
float3 atmo_sky_light(float3 normal) {
    float3 up = atmo_atlas(float2(0.5, ATMO_AMBIENT_ROW + 0.5)).rgb;
    float a = atan2(normal.z, normal.x);
    float f = (a < 0.0 ? a + 2.0 * ATMO_PI : a) / (ATMO_PI / 4.0);
    float i0 = fmod(floor(f), 8.0);
    float i1 = fmod(i0 + 1.0, 8.0);
    float3 h0 = atmo_atlas(float2(1.5 + i0, ATMO_AMBIENT_ROW + 0.5)).rgb;
    float3 h1 = atmo_atlas(float2(1.5 + i1, ATMO_AMBIENT_ROW + 0.5)).rgb;
    float3 side = lerp(h0, h1, frac(f));
    float3 sky = normal.y >= 0.0 ? lerp(side, up, normal.y) : side * (1.0 + normal.y);
    return sky * scene.atmosphere.x * scene.atmosphere2.y;
}

float3 sky_color(float3 ray) { return atmosphere_on() ? atmo_sky_radiance(ray) : sky_color_analytic(ray); }
#else
float3 sky_color(float3 ray) { return sky_color_analytic(ray); }
#endif

// sky_color() without the sun disc and glow: what SM2 fogs toward (its two
// pows are a fifth of a vs_2_0 lighting budget), and its water's reflection.
float3 sky_plain(float3 ray) {
    float horizon_blend = pow(saturate(1.0 - abs(ray.y)), 4.0);
    float3 base = lerp(scene.sky_zenith.rgb, scene.sky_horizon.rgb, horizon_blend);
    return lerp(base, scene.fog_color.rgb, saturate(-ray.y * 6.0) * 0.7);
}

// ---- the one lighting path.
//
// Terrain, plants, props and the skinned creature all go through these three
// functions. They used to each carry a copy with its own wrap constant and its
// own ambient mix, and the copies drifted: the foliage's wider wrap and its
// ambient term lifted every tree brighter than the ground it stood on, which
// read as three visual languages in one frame. The seam is closed by
// construction now -- there is nothing per-shader left to drift.

// Direct sun on a surface, wrapped a little past the terminator so a slope
// turning away keeps its shape instead of going flat black. `visibility` is
// the shadow-map term from sun_visibility().
float3 direct_sun(float3 normal, float visibility) {
    float wrap = scene.light_params.x;
    float3 to_sun = normalize(scene.sun.xyz);
    float wrapped = saturate((dot(normal, to_sun) + wrap) / (1.0 + wrap));
    return scene.sun_color.rgb * scene.sun.w * wrapped * visibility;
}

// The local light (SceneUniforms::local_light): a breath's fire lighting
// the jaws, the wings, the ground under it and what it burns. Inverse
// square, windowed to zero at its reach, a little wrapped (the fire is
// metres across), and unshadowed. Modern tier only: the retro tiers'
// instruction budgets are measured without it.
float3 local_light(float3 world_position, float3 normal) {
#ifdef ATMOSPHERE
    float reach = scene.local_light.w;
    if (reach <= 0.0) return (float3)0.0;
    float3 to_light = scene.local_light.xyz - world_position;
    float d2 = dot(to_light, to_light);
    float window = saturate(1.0 - d2 / (reach * reach));
    float facing = saturate(dot(normal, to_light * rsqrt(max(d2, 1e-4))) * 0.7 + 0.3);
    return scene.local_light_color.rgb * (window * window * facing / (1.0 + d2));
#else
    return (float3)0.0;
#endif
}

// Hemispheric ambient: cool sky from above, the warm bounce off sunlit ground
// from below. The split is what a low sun gives a real landscape -- shadow
// sides go cool and dark, undersides pick up warmth -- and it is the same on
// a rock, a crown and a wing membrane.
float3 ambient_light(float3 normal) {
    float sky_amount = saturate(normal.y * 0.5 + 0.5);
    float3 sky = lerp(scene.sky_horizon.rgb, scene.sky_zenith.rgb, 0.6);
    float3 bounce = scene.palette[PALETTE_GROUND_BOUNCE].rgb * scene.light_params.z *
                    scene.sun_color.rgb * saturate(scene.sun.y * 4.0);
#ifdef USE_ATMOSPHERE
    // The sky that is there, already weighted by how much of it the normal
    // sees: only the ground's share is left to the bounce.
    if (atmosphere_on()) return (atmo_sky_light(normal) + bounce * (1.0 - sky_amount)) * scene.terrain_params.w;
#endif
    return lerp(bounce, sky, sky_amount) * scene.terrain_params.w;
}

// Sunlight leaking through a thin thing lit from behind: a crown, a blade, a
// wing. Zero for solid surfaces. Reads as the glow of foliage against a low
// sun, which the wrap alone cannot give because the wrap never exceeds the
// front-lit value.
float3 translucent_sun(float3 normal, float visibility, float translucency) {
    float3 to_sun = normalize(scene.sun.xyz);
    float behind = saturate(-dot(normal, to_sun));
    return scene.sun_color.rgb * scene.sun.w * behind * translucency * visibility;
}

// Exponential-squared height fog toward the sky colour along the view ray, so
// distance dissolves into exactly what is behind it and thins with altitude
// so peaks stay legible while the valley floor still reads as far.
//
// With the atmosphere, the air between does that physically first -- the
// surface dimmed by the transmittance, the in-scattered light added -- and
// the valley fog stays on top of it at atmosphere.w of its density, toward
// the atmosphere's own sky.
#ifdef USE_ATMOSPHERE
// How much of the pixel is the surface's own light, after the air in front
// of it has dimmed it and added its glow (apply_fog sets it): the ambient
// occlusion darkens that part only (occlusion_share).
static float g_surface_share = 1.0;
#endif
float3 apply_fog(float3 lit_color, float3 world_position) {
    float3 to_point = world_position - scene.camera_position.xyz;
    float dist = length(to_point);
    float density = scene.fog_color.a * exp(-max(world_position.y, 0.0) * 0.0018);
    float3 ray = to_point / max(dist, 1e-3);
#ifdef USE_ATMOSPHERE
    float3 surface = lit_color;
    if (atmosphere_on()) {
        float4 air = atmo_aerial(world_position);
        lit_color = lit_color * air.a + air.rgb;
        surface *= air.a;
        density *= scene.atmosphere.w;
    }
#endif
    float fog = 1.0 - exp(-pow(dist * density, 2.0));
    float3 result = lerp(lit_color, sky_color(ray), saturate(fog));
#ifdef USE_ATMOSPHERE
    const float3 L = float3(0.2126, 0.7152, 0.0722);
    g_surface_share = saturate(dot(surface, L) * (1.0 - saturate(fog)) / max(dot(result, L), 1e-5));
#endif
    return result;
}

// What a world shader writes in the scene target's alpha (docs/FIDELITY.md,
// F6): the share of the pixel the ambient occlusion may darken -- the
// ambient light's part of the light that reached it, times the surface's
// share after the fog. Screen-space occlusion knows only the depth, so the
// composite multiplied it into the sun as well, and a sunlit field went grey
// in a blot around every tree. 1 on the retro tiers, which have no occlusion.
float occlusion_share(float3 ambient, float3 light) {
#ifdef USE_ATMOSPHERE
    const float3 L = float3(0.2126, 0.7152, 0.0722);
    return saturate(dot(ambient, L) / max(dot(light, L), 1e-5)) * g_surface_share;
#else
    return 1.0;
#endif
}

#ifdef FIXED_FUNCTION
// ---- fixed function (docs/PORTING.md R5): the helpers behind FF_VARYINGS,
// after the lighting functions they call.
// The pre-lit diffuse: a linear albedo lit by the modern sun, ambient and
// translucency per vertex, tonemapped, then the albedo display-encoded on
// top -- in the vertex colour, as a 2001 pre-lit pipeline carried it with
// D3DRS_LIGHTING off. The texture then modulates it.
float4 ff_lit(float3 albedo_linear, float3 normal, float translucency) {
    float3 light = direct_sun(normal, 1.0) + ambient_light(normal) + translucent_sun(normal, 1.0, translucency);
    return float4(saturate(ff_albedo(albedo_linear) * ldr_encode_fast(light, scene.output_grade)), 1.0);
}
// D3DFOG_EXP2 on the radial distance (D3DRS_RANGEFOGENABLE), density
// fog_color.a per metre, no height term: the factor D3D wants.
float ff_fog(float3 world_position) {
    float d = length(world_position - scene.camera_position.xyz);
    // A short view (the View distance setting): linear, as the cards of the
    // time did it -- clear up close, gone at the end -- where exp2 thick
    // enough to hide the cut washed out the whole valley.
    if (scene.fog_range.y > 0.0) return saturate((scene.fog_range.y - d) / (scene.fog_range.y - scene.fog_range.x));
    float x = d * scene.fog_color.a;
    return exp(-x * x);
}
#ifdef FRAGMENT_STAGE
// The fixed-function pixel pipeline's last step, emulated: the fog blend
// toward the one fog colour, then the 8-bit clamp. The colour is read from
// `scene` here, which also keeps uniform slot 0 in the stage's reflection
// (a stage that reads no field of it loses it, and the block pushed to
// slot 1 no longer lines up: the dragon once drew untextured for that).
float3 ff_finish(float3 color, float fog) {
    return scene_out(lerp(ff_display(scene.fog_color.rgb), color, saturate(fog)));
}
// A stem whose fragment stage declares the shadow map (slot 0) keeps that
// slot in reflection with this read, as NO_SHADOWS does, so the textures
// bound after it stay in their slots.
#define FF_KEEP_SHADOW_SLOT (1.0 + min(shadow_map.Sample(shadow_map_sampler, (float2)0.5), 0.0))
#endif
#endif  // FIXED_FUNCTION

#ifdef SM2
// ---- SM2: the lighting per vertex (gfx/render_tier.h, docs/PORTING.md R4).
//
// ps_2_0 has 64 arithmetic instructions. The per-pixel path above costs more
// than that before a material does anything, so on this tier the vertex stage
// lights: direct sun, ambient, the fog's colour and amount, and where the
// point falls in the shadow map. The pixel stage keeps the albedo, one 2x2
// shadow lookup, the combine and the tonemap. Four interpolators carry it,
// TEXCOORD4..7:
//   sun.rgb      direct sun (and a plant's translucency), before the shadow
//   ambient.rgb  the hemispheric ambient
//   fog.rgb      the sky along the view ray
//   shadow       xy the shadow map's uv, z the biased depth, w the fog amount
// sun.a, ambient.a and fog.a are the material's to use.
struct Sm2Light {
    float4 sun;
    float4 ambient;
    float4 fog;
    float4 shadow;
};
#define SM2_LIGHT_VARYINGS          \
    float4 sm2_sun : TEXCOORD4;     \
    float4 sm2_ambient : TEXCOORD5; \
    float4 sm2_fog : TEXCOORD6;     \
    float4 sm2_shadow : TEXCOORD7;
#define SM2_LIGHT_OUT(o, l) \
    (o).sm2_sun = (l).sun; (o).sm2_ambient = (l).ambient; (o).sm2_fog = (l).fog; (o).sm2_shadow = (l).shadow
#define SM2_LIGHT_IN(i) sm2_light((i).sm2_sun, (i).sm2_ambient, (i).sm2_fog, (i).sm2_shadow)

Sm2Light sm2_light(float4 sun, float4 ambient, float4 fog, float4 shadow) {
    Sm2Light l;
    l.sun = sun;
    l.ambient = ambient;
    l.fog = fog;
    l.shadow = shadow;
    return l;
}

// The vertex stage's half: everything but the shadow test and the albedo.
// `translucency` lets a plant's back face catch the sun (translucent_sun).
Sm2Light sm2_light_vertex(float3 world_position, float3 normal, float translucency) {
    // direct_sun, translucent_sun, ambient_light and the fog, written for
    // vs_2_0's slots: the sun direction arrives normalised from the CPU
    // (gfx::Lighting::sun_direction), so one dot serves the wrap, the
    // translucency and the shadow's slope bias, and the light's projection is
    // orthographic, so there is no divide by w.
    Sm2Light l;
    const float n_dot_l = dot(normal, scene.sun.xyz);
    const float wrap = scene.light_params.x;
    const float lit = saturate((n_dot_l + wrap) / (1.0 + wrap)) + saturate(-n_dot_l) * translucency;
    l.sun = float4(scene.sun_color.rgb * (scene.sun.w * lit), 0.0);
    l.ambient = float4(ambient_light(normal), 0.0);

    float3 to_point = world_position - scene.camera_position.xyz;
    float dist = length(to_point);
    float density = scene.fog_color.a * exp(-max(world_position.y, 0.0) * 0.0018);
    float x = dist * density;
    float fog = saturate(1.0 - exp(-x * x));
    l.fog = float4(sky_plain(to_point / max(dist, 1e-3)), 0.0);

    float3 offset_position = world_position + normal * (scene.shadow_params.x * 1.5);
    float3 light = mul(scene.light_view_proj, float4(offset_position, 1.0)).xyz;
    float bias = scene.shadow_params.y * (0.35 + saturate(1.0 - abs(n_dot_l)) * 2.5);
    l.shadow = float4(light.xy * float2(0.5, -0.5) + 0.5, light.z - bias, fog);
    return l;
}

#ifdef FRAGMENT_STAGE
// The pixel stage's half: the shadow (2x2 taps; outside the map is lit), the
// combine, the fog and the tonemap.
float3 sm2_finish(float3 albedo, Sm2Light l, Texture2D<float> shadow_map, SamplerState shadow_sampler) {
#ifdef NO_SHADOWS
    // Shadows off (the Shadows setting's Off, docs/PORTING.md R7): ps_2_0 has
    // no branch to skip the lookup with, so it is compiled out -- about a
    // third of the terrain's pixel shader. The map is still read once, at a
    // fixed texel and folded to nothing, because a slot the shader does not
    // read drops out of its reflection, and the slots must run 0..n-1.
    float lit = 1.0 + min(shadow_map.Sample(shadow_sampler, (float2)0.5), 0.0);
#else
    float2 uv = l.shadow.xy;
    float half_texel = scene.shadow_params.w * 0.5;
    float4 stored = float4(shadow_map.Sample(shadow_sampler, uv + float2(-half_texel, -half_texel)),
                           shadow_map.Sample(shadow_sampler, uv + float2(half_texel, -half_texel)),
                           shadow_map.Sample(shadow_sampler, uv + float2(-half_texel, half_texel)),
                           shadow_map.Sample(shadow_sampler, uv + float2(half_texel, half_texel)));
    float lit = dot(step((float4)l.shadow.z, stored), (float4)0.25);
    // Outside the shadow volume -- past the map's edges, or beyond its far
    // plane, where the cleared 1.0 would read as shadow -- there is no
    // information: lit, as sun_visibility() has it.
    float2 inside = step(abs(uv - 0.5), (float2)0.5);
    lit = lerp(1.0, lit, inside.x * inside.y * step(l.shadow.z, 1.0) * scene.shadow_params.z);
#endif
    float3 color = albedo * (l.ambient.rgb + l.sun.rgb * lit);
    return scene_out(lerp(color, l.fog.rgb, l.shadow.w));
}
#endif
#endif  // SM2
#endif  // NO_SCENE
