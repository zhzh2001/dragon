// The atmosphere's atlas after the shadow map and the two cards (the baked
// lattice takes slot 3 only on a retro tier, which has no atmosphere).
#define ATMOSPHERE_SLOT 3
#define TERRAIN_SUN_SLOT 4  // and the terrain's shadow heights after it
#include "scene_common.hlsl"

// Instanced plants: one mesh, thousands of placements. The instance carries
// where the plant stands, how big it is, which way it faces, its shade and a
// sway phase; the mesh carries the shape and the material in vertex colour.

struct FoliageParams {
    float4 wind_time_fade;  // x wind, y time, z fade start, w fade end
    float4 extra;           // x nominal height at scale 1, y LOD distance, z the LOD band (a fraction of it)
    float4 impostor;        // x, y where trees hand over to their impostors, first and last (m); 0: they do not
    float4 push;            // the downwash (gfx::Foliage::downwash): x, z centre, y radius, w push (m); w 0 calm
};

struct VertexIn {
    float3 position       : TEXCOORD0;
    float3 normal         : TEXCOORD1;
    float3 color          : TEXCOORD2;
    float2 uv             : TEXCOORD3;
    float4 position_scale : TEXCOORD4;
    float4 params         : TEXCOORD5;
};

// The vertex colour's third channel names the material (gfx/palette.h,
// FoliageMaterial): plain, a leaf card, a needle card, bark; plus 10 for a
// detail card the LOD may drop.
static const int FOLIAGE_MAT_PLAIN = 0;
static const int FOLIAGE_MAT_LEAF = 1;
static const int FOLIAGE_MAT_NEEDLE = 2;
static const int FOLIAGE_MAT_BARK = 3;
static const int FOLIAGE_DETAIL = 10;

#if defined(FIXED_FUNCTION)
// Fixed function (docs/PORTING.md R5): the plant's colour lit and finished
// per vertex (ff_lit); stage 0 is the card atlas MODULATE2X with it, its
// alpha the cut-out (the alpha test at 0.45). One texture for every draw:
// the leaf and needle maps side by side with a mid-grey, opaque strip under
// them (gfx::Foliage builds it on this tier), and the uv remapped here --
// a card into its half, bark and solids onto the grey strip, so one stage
// setup serves the whole mesh. No LOD dither: the renderer draws the coarse
// cards only on this tier. No facing flip: D3D9 lights one side.
struct VertexOut {
    float4 clip_position : SV_Position;
    FF_VARYINGS
};
#elif defined(SM2)
// SM2 (docs/PORTING.md, R4): lit per vertex through scene_common's SM2 path.
// The pixel stage keeps the distance LOD's dither, the card's cut-out and
// mottle and the shared finish; the bark's streaks are the SM2 tier's cut, and
// the crown's dark undersides move to the vertex.
struct VertexOut {
    float4 clip_position : SV_Position;
    float4 uv : TEXCOORD0;      // xy the uv, z 1 on a card, w 1 on a needle card
    float4 albedo : TEXCOORD1;  // rgb the plant's colour, a 1 on a detail card
    float4 lod : TEXCOORD2;     // x the LOD dither: kept where >= 0 (see the per-pixel path)
    SM2_LIGHT_VARYINGS
};
#else
struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
    float3 albedo : TEXCOORD2;
    float2 uv : TEXCOORD3;
    // x material, y 1 when this is a detail card, z camera distance to the
    // plant's base (for the LOD), w instance hash (for the LOD dither)
    float4 material : TEXCOORD4;
};
#endif

// Both stages read it: the vertex stage the wind and the fade, the fragment
// stage the LOD distance.
ConstantBuffer<FoliageParams> params : UNIFORM_SLOT(1);

// How far through the LOD band a plant at `distance` is, 0 before it and 1
// past it: the detail cards are gone by 1 and the coarse ones carry the
// crown alone, cut out a little less and darker, for the cards inside the
// crown they stand in for (the per-pixel path; gfx::Foliage::lod_band).
float crown_thicken(float distance) {
    float lod = params.extra.y;
    return saturate((distance - lod) / max(lod * params.extra.z, 1.0));
}

#ifdef VERTEX_STAGE
// The placed, swaying vertex in world space. foliage_depth.hlsl repeats the
// placement, so a swaying crown casts a swaying shadow.
float3 place_vertex(VertexIn input, float3 camera_position, out float3 world_normal) {
    float yaw = input.params.x;
    float c = cos(yaw), s = sin(yaw);
    float3 local = input.position * input.position_scale.w;
    float3 rotated = float3(c * local.x + s * local.z, local.y, -s * local.x + c * local.z);

    // Distance fade for grass: the whole tuft shrinks into the ground rather
    // than blinking out. Trees pass fade distances far beyond the world.
    float dist = length(input.position_scale.xyz - camera_position);
    float fade = 1.0 - smoothstep(params.wind_time_fade.z, params.wind_time_fade.w, dist);
    rotated *= fade;

    // Sway grows with the square of the height fraction, so roots stay put
    // and crowns move. Two incommensurate frequencies, per-instance phase.
    float height_fraction = saturate(local.y / max(input.position_scale.w * params.extra.x, 0.01));
    float t = params.wind_time_fade.y;
    float sway = params.wind_time_fade.x * height_fraction * height_fraction *
                 (sin(t * 1.3 + input.params.z) + 0.5 * sin(t * 2.9 + input.params.z * 1.7));
    rotated.x += sway;
    rotated.z += sway * 0.6;
#ifdef ATMOSPHERE
    // The downwash: blades lie away from under a dragon's wings, flattening
    // toward the middle, fluttering fast.
    if (params.push.w > 0.0) {
        float2 away = input.position_scale.xz - params.push.xz;
        float r = length(away);
        float reach = 1.0 - smoothstep(params.push.y * 0.3, params.push.y, r);
        if (reach > 0.0) {
            float flutter = 0.8 + 0.2 * sin(t * 17.0 + input.params.z * 3.0);
            float lean = params.push.w * reach * flutter * height_fraction * height_fraction;
            rotated.xz += away / max(r, 0.5) * lean;
            rotated.y -= lean * 0.7;
        }
    }
#endif

    world_normal = float3(c * input.normal.x + s * input.normal.z, input.normal.y,
                          -s * input.normal.x + c * input.normal.z);
    return input.position_scale.xyz + rotated;
}

#if defined(FIXED_FUNCTION)
// The atlas: twice each map, the leaf map at u 0..0.5 and the needle map
// at 0.5..1 over v 0..0.5, the mid-grey opaque strip in the lower half
// (gfx::Foliage; a power of two, which the X550 class insists on).
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    float3 normal;
    float3 world = place_vertex(input, scene.camera_position.xyz, normal);
    normal = normalize(normal);
    int entry = int(clamp(input.color.x, 0.0, float(PALETTE_COUNT - 1)) + 0.5);  // a float clamp: GLSL ES 1.00 has no integer one
    float3 base = scene.palette[entry].rgb * input.color.y * input.params.y;
    float3 albedo = lerp(base, base * scene.palette[PALETTE_PLANT_WARM].rgb, input.params.w);
    int tag = int(input.color.z + 0.5);
    int material = tag >= FOLIAGE_DETAIL ? tag - FOLIAGE_DETAIL : tag;
    const bool card = material == FOLIAGE_MAT_LEAF || material == FOLIAGE_MAT_NEEDLE;
    if (card) albedo *= 1.0 - 0.35 * saturate(-normal.y);
    o.uv = card ? float2(input.uv.x * 0.5 + (material == FOLIAGE_MAT_NEEDLE ? 0.5 : 0.0), input.uv.y * 0.5)
                : float2(0.25, 0.75);
    o.clip_position = mul(scene.view_proj, float4(world, 1.0));
    o.diffuse = ff_lit(albedo, normal, material == FOLIAGE_MAT_BARK ? 0.0 : scene.light_params.y);
    o.fog = ff_fog(world);
    return o;
}
#elif defined(SM2)
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    float3 normal;
    float3 world = place_vertex(input, scene.camera_position.xyz, normal);
    normal = normalize(normal);
    int entry = int(clamp(input.color.x, 0.0, float(PALETTE_COUNT - 1)) + 0.5);  // a float clamp: GLSL ES 1.00 has no integer one
    float3 base = scene.palette[entry].rgb * input.color.y * input.params.y;
    float3 albedo = lerp(base, base * scene.palette[PALETTE_PLANT_WARM].rgb, input.params.w);
    int tag = int(input.color.z + 0.5);
    int material = tag >= FOLIAGE_DETAIL ? tag - FOLIAGE_DETAIL : tag;
    const bool card = material == FOLIAGE_MAT_LEAF || material == FOLIAGE_MAT_NEEDLE;
    // Dark undersides, judged on the geometric normal before the facing flip.
    if (card) albedo *= 1.0 - 0.35 * saturate(-normal.y);
    o.uv = float4(input.uv, card ? 1.0 : 0.0, material == FOLIAGE_MAT_NEEDLE ? 1.0 : 0.0);
    o.albedo = float4(albedo, tag >= FOLIAGE_DETAIL ? 1.0 : 0.0);
    // The LOD's dither threshold, constant over the plant: the instance's hash
    // against how far past the LOD distance its base is.
    const float lod = params.extra.y;
    const float past = (length(input.position_scale.xyz - scene.camera_position.xyz) - lod) / max(lod * 0.3, 1.0);
    o.lod = float4(frac(input.params.z * 0.15915494) - past, 0.0, 0.0, 0.0);
    o.clip_position = mul(scene.view_proj, float4(world, 1.0));
    // Two-sided: light the face toward the viewer.
    if (dot(normal, scene.camera_position.xyz - world) < 0.0) normal = -normal;
    Sm2Light light = sm2_light_vertex(world, normal, material == FOLIAGE_MAT_BARK ? 0.0 : scene.light_params.y);
    SM2_LIGHT_OUT(o, light);
    return o;
}
#else
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    float3 normal;
    float3 world = place_vertex(input, scene.camera_position.xyz, normal);
    o.world_position = world;
    o.world_normal = normalize(normal);
    // The mesh carries (palette entry, brightness) rather than a colour, so
    // the plant takes its green from the shared table. Per-instance shade
    // darkens or lightens the whole plant; the warm push (params.w) shifts a
    // crown toward yellow, which is what makes a forest read as many trees
    // rather than one green stamped a thousand times.
    int entry = int(clamp(input.color.x, 0.0, float(PALETTE_COUNT - 1)) + 0.5);  // a float clamp: GLSL ES 1.00 has no integer one
    float3 base = scene.palette[entry].rgb * input.color.y * input.params.y;
    o.albedo = lerp(base, base * scene.palette[PALETTE_PLANT_WARM].rgb, input.params.w);
    o.uv = input.uv;
    int tag = int(input.color.z + 0.5);
    int material = tag >= FOLIAGE_DETAIL ? tag - FOLIAGE_DETAIL : tag;
    o.material = float4(float(material), tag >= FOLIAGE_DETAIL ? 1.0 : 0.0,
                        length(input.position_scale.xyz - scene.camera_position.xyz),
                        frac(input.params.z * 0.15915494));  // the sway phase, reused as a hash
#ifdef ATMOSPHERE
    // The modern tier's LOD goes card by card (the per-pixel path): each
    // detail card its own threshold, the plant's hash plus its shade, which
    // card_crown draws per card.
    o.material.w = frac(o.material.w + input.color.y * 913.7);
#endif
    o.clip_position = mul(scene.view_proj, float4(world, 1.0));
#ifdef ATMOSPHERE
    // Past its own handover distance, by its hash, a tree is its impostor
    // (foliage_impostor.hlsl, which draws the complementary share): its
    // crown collapses off the screen.
    if (params.impostor.y > 0.0) {
        float handover = lerp(params.impostor.x, params.impostor.y, frac(input.params.z * 0.15915494));
        if (length(input.position_scale.xyz - scene.camera_position.xyz) >= handover) o.clip_position = float4(2.0, 2.0, 2.0, 1.0);
    }
#endif
    return o;
}
#endif
#endif

#ifdef FRAGMENT_STAGE
DEPTH2D(shadow_map, 0);
TEXTURE2D(leaf_card, 1);
TEXTURE2D(needle_card, 2);
#define NOISE_SLOT 3  // the baked lattice, on a retro tier
#include "noise.hlsl"

#if defined(FIXED_FUNCTION)
float4 fs_main(VertexOut input) : SV_Target {
    // Stage 0: the atlas (bound in the leaf slot) MODULATE2X the diffuse,
    // its alpha tested at 0.45.
    float4 card = leaf_card.Sample(leaf_card_sampler, input.uv.xy);
    clip(card.a - 0.45);
    float3 color = saturate(input.diffuse.rgb * card.rgb * 2.0) * FF_KEEP_SHADOW_SLOT;
    return float4(ff_finish(color, input.fog), 1.0);
}
#elif defined(SM2)
float4 fs_main(VertexOut input) : SV_Target {
    float4 leaf = leaf_card.Sample(leaf_card_sampler, input.uv.xy);
    float4 needle = needle_card.Sample(needle_card_sampler, input.uv.xy);
    float2 card = lerp(leaf.ra, needle.ra, input.uv.w);
    // A card wears its mottle and cuts its shape out; anything else is solid.
    float3 albedo = input.albedo.rgb * lerp(1.0, card.x * 2.2, input.uv.z);
    float3 color = sm2_finish(albedo, SM2_LIGHT_IN(input), shadow_map, shadow_map_sampler);
    // Both cuts in one kill at the end, the card's shape and the distance
    // LOD's dither (see the per-pixel path): in ps_2_0 a kill ahead of the
    // shadow lookups makes a dependency chain the profile cannot hold.
    clip(min(lerp(1.0, card.y, input.uv.z) - 0.45, lerp(1.0, input.lod.x, input.albedo.a)));
    return float4(color, 1.0);
}
#else
float4 fs_main(VertexOut input) : SV_Target {
    int material = int(input.material.x + 0.5);

    // The distance LOD: past `lod_distance` the detail cards go and the coarse
    // ones carry the crown. Dithered per instance over a 30% band with the
    // instance's own hash, so a forest thins tree by tree instead of popping
    // at a line.
    float thicken = 0.0;
#ifdef ATMOSPHERE
    // The modern tier's: card by card over a band as wide as the LOD
    // distance (900 m there, gfx::Foliage::init), while the coarse cards
    // thicken a little to hold the crown's cover. Each tree went from its
    // full crown to its impostor at once at 240 m, and the impostor -- a
    // few large cards, the trunk showing through, a spruce turned into a
    // square blob -- was so unlike it that a tree changed as it crossed.
    if (input.material.y > 0.5) {
        if (crown_thicken(input.material.z) > input.material.w) discard;
    } else {
        thicken = crown_thicken(input.material.z);
    }
#else
    if (input.material.y > 0.5) {
        float lod = params.extra.y;
        float t = (input.material.z - lod) / max(lod * 0.3, 1.0);
        if (t > input.material.w) discard;
    }
#endif

    float3 albedo = input.albedo;
    float3 geometric_normal = normalize(input.world_normal);
    if (material == FOLIAGE_MAT_LEAF || material == FOLIAGE_MAT_NEEDLE) {
        // The card: a grey detail map with the leaves cut out in alpha,
        // coloured by the palette. Alpha-tested, not blended: sorted-blend
        // foliage is a rabbit hole and the 2005 look shipped on the test.
        float4 card = material == FOLIAGE_MAT_LEAF
                          ? leaf_card.Sample(leaf_card_sampler, input.uv)
                          : needle_card.Sample(needle_card_sampler, input.uv);
        if (card.a < 0.45 - 0.08 * thicken) discard;
        // The map's greys average about 0.45; normalised so the palette entry
        // is the crown's mean colour, with the map's mottle around it.
        albedo *= card.r * 2.2;
        // Dark undersides: the face turned toward the ground is in its own
        // shade. Judged on the geometric normal before the facing flip.
        float under = saturate(-geometric_normal.y);
        albedo *= 1.0 - 0.35 * under;
        // A thickened impostor stands in for the cards inside the crown
        // too, which are darker than its shell.
        albedo *= 1.0 - 0.15 * thicken;
    } else if (material == FOLIAGE_MAT_BARK) {
        // Bark: vertical streaks, in u around the trunk and stretched along
        // v, from the same value noise the ground grains with.
        float streak = value_noise(float2(input.uv.x * 14.0, input.uv.y * 1.5));
        float fine = value_noise(float2(input.uv.x * 40.0, input.uv.y * 6.0));
        albedo *= 0.7 + 0.5 * streak + 0.15 * (fine - 0.5);
    }

    float3 normal = geometric_normal;
    float3 to_camera = normalize(scene.camera_position.xyz - input.world_position);
    if (dot(normal, to_camera) < 0.0) normal = -normal;

    // The shared lighting path, plus the one thing a crown has that rock does
    // not: sun leaking through it from behind.
    float visibility = sun_visibility(input.world_position, normal, shadow_map, shadow_map_sampler);
    float translucency = material == FOLIAGE_MAT_BARK ? 0.0 : scene.light_params.y;
    float3 ambient = ambient_light(normal);
#ifdef USE_TERRAIN_SUN
    ambient *= terrain_sky(input.world_position);  // a stand in a gully sees less sky
#endif
    float3 light = direct_sun(normal, visibility) + local_light(input.world_position, normal) + ambient +
                   translucent_sun(normal, visibility, translucency);
    float3 lit_color = albedo * light;
    float3 fogged = apply_fog(lit_color, input.world_position);
    return float4(scene_out(fogged), occlusion_share(ambient, light));
}

#endif
#endif
