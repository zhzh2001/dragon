// Billboard particles. Quads arrive already camera-facing from the CPU; the
// fragment shader shapes each by its kind (gfx::ParticleKind): a soft disc,
// a flame tongue that frays as it ages, or smoke. Light is blended ONE + ONE,
// so colour is emitted light and darkness is simply not drawing; smoke is
// a surface, alpha-blended, drawn first (gfx::ParticleSystem::draw).
#include "common.hlsl"

struct VertexIn {
    float3 position : TEXCOORD0;
    float4 color    : TEXCOORD1;
    float2 corner   : TEXCOORD2;
#ifndef FIXED_FUNCTION
    float4 shape    : TEXCOORD3;  // x seed, y age over life, z kind
#endif
};

#ifdef FIXED_FUNCTION
// Fixed function (docs/PORTING.md R5): the disc comes from a texture
// (gfx::ParticleSystem bakes (1 - r)^2 into a 64 x 64 map on this tier),
// stage 0 MODULATE with the display-encoded colour, added. No tonemap, so
// stacked puffs clip to white as they did in 2001.
struct VertexOut {
    float4 clip_position : SV_Position;
    float4 diffuse : COLOR0;  // the order FF_VARYINGS explains
    float fog : FOG;
    float2 uv : TEXCOORD0;
};
#else
struct VertexOut {
    float4 clip_position : SV_Position;
    float4 color : TEXCOORD0;
    float2 corner : TEXCOORD1;
    float4 shape : TEXCOORD2;
};
#endif

struct ParticleUniforms {
    float4x4 view_proj;
    float4 output_grade;  // the LDR tiers' in-shader grade (common.hlsl)
};

ConstantBuffer<ParticleUniforms> u : UNIFORM_SLOT(0);

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    o.clip_position = mul(u.view_proj, float4(input.position, 1.0));
#ifdef FIXED_FUNCTION
    o.uv = input.corner * 0.5 + 0.5;
    o.diffuse = float4(pow(saturate(input.color.rgb * u.output_grade.x), (float3)(1.0 / 2.2)), 1.0);
    o.fog = 1.0;
#else
    o.color = input.color;
    o.corner = input.corner;
    o.shape = input.shape;
#endif
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
#ifdef FIXED_FUNCTION
TEXTURE2D(disc, 0);
float4 fs_main(VertexOut input) : SV_Target {
    // Times the fog factor (always 1 here): the read keeps FOG in this
    // stage's input signature, between the diffuse and the uv it does use.
    return float4(saturate(input.diffuse.rgb * disc.Sample(disc_sampler, input.uv.xy).rgb) * saturate(input.fog), 1.0);
}
#else
float particle_hash(float2 p) {
    p = frac(p * float2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}

float particle_noise(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 w = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(particle_hash(i), particle_hash(i + float2(1.0, 0.0)), w.x),
                lerp(particle_hash(i + float2(0.0, 1.0)), particle_hash(i + float2(1.0, 1.0)), w.x), w.y);
}

// Billows: three octaves where there is room, one inside ps_2_0's 64.
float particle_billow(float2 p) {
#if defined(LDR_OUTPUT)
    return particle_noise(p);
#else
    return particle_noise(p) * 0.55 + particle_noise(p * 2.03 + 7.1) * 0.3 + particle_noise(p * 4.1 + 3.7) * 0.15;
#endif
}

float4 fs_main(VertexOut input) : SV_Target {
    float r = length(input.corner);
    float seed = input.shape.x;
    float age = input.shape.y;
    // Each puff turned by its seed, so the noise never lines up between them.
    float turn = seed * 6.2831853;
    float2 p = float2(cos(turn) * input.corner.x - sin(turn) * input.corner.y,
                      sin(turn) * input.corner.x + cos(turn) * input.corner.y);
    if (input.shape.z > 1.5) {
        // Smoke: a billowed disc, its edge eaten by the noise, rolling slowly.
        float billow = particle_billow(p * 1.3 + seed * 11.0 - float2(0.0, age * 0.8));
        float body = saturate(1.0 - r) * 1.15 + (billow - 0.5) * 0.9;
        float alpha = smoothstep(0.1, 0.6, body) * input.color.a;
        float3 smoke = input.color.rgb * (0.8 + 0.4 * billow);
#if defined(LDR_OUTPUT)
        return float4(ldr_encode(smoke, u.output_grade), alpha);
#else
        return float4(smoke, alpha);
#endif
    }
    float falloff;
    float cover = 0.0;  // how much of what is behind it a puff hides
    float3 light = input.color.rgb;
    if (input.shape.z > 0.5) {
        // A flame tongue: a broad billow and a fine one, climbing as it ages,
        // eat the disc from the edge in. Mostly opaque with a sharp edge, and
        // sorted back to front (gfx::ParticleSystem::upload), so stacked puffs
        // converge on the front one's colour as a fireball's surface does;
        // faint and soft, a few hundred deep, they summed toward white and
        // averaged every billow away. The last term closes the shape inside
        // the quad, whose corners strong billows otherwise reached.
        float broad = particle_billow(p * 2.2 + seed * 17.0 + float2(0.0, age * 2.5));
        float fine = particle_noise(p * 5.5 + seed * 31.0 + float2(age * 1.3, age * 3.5));
        float density = saturate(1.0 - r) * 1.4 + (broad - 0.5) * 1.3 + (fine - 0.5) * 0.55 - age * 0.6 -
                        smoothstep(0.8, 1.0, r) * 0.8;
        float body = smoothstep(0.1, 0.22, density);
        // Hottest young and deep inside: the jet at the jaws, cooling to the
        // tip; a patchy heat everywhere read as a leopard's coat.
        float heat = saturate(smoothstep(0.4, 1.1, density) * (1.25 - age * 1.6));
        // Fades out over its life.
        cover = body * 0.9 * saturate(1.6 - age * 1.6);
        // Light over cover is the colour seen: the breath's own colour dimmed
        // at the fringe (fire's cool end is already red), paler and bright
        // enough to bloom in the heat. A fixed red fringe drew frost, blight
        // and tide with black outlines.
        falloff = cover * (0.45 + 5.5 * heat * heat);
        light = lerp(light * 0.6, light * float3(1.0, 1.3, 1.5), heat);
    } else {
        // Soft disc with a hot centre: quadratic falloff squared reads as a
        // glow core rather than a flat coin.
        falloff = saturate(1.0 - r);
        falloff *= falloff;
    }
    input.color.rgb = light;
#if defined(LDR_OUTPUT)
    // Without a float target each puff is finished on its own and the
    // finished values add: brighter where puffs stack than the modern
    // tonemap-after-sum, which is the price of eight bits.
    return float4(ldr_encode(input.color.rgb * falloff, u.output_grade), cover);
#else
    return float4(input.color.rgb * falloff, cover);
#endif
}
#endif
#endif
