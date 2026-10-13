// The atmosphere (docs/FIDELITY.md, F1): Hillaire's "A Scalable and
// Production Ready Sky and Atmosphere Rendering Technique" (EGSR 2020), for
// the modern tier. Four lookup tables, each a fullscreen pass:
//
//   transmittance   256 x 64   T from a height toward a zenith angle, to space
//   multiscatter     32 x 32   the infinite-order scattering term, per height
//                              and sun angle (Hillaire's psi_ms)
//   frame atlas     256 x 232  per frame: the aerial-perspective froxels (32
//                              slices of 32 x 32 along the view frustum, an
//                              8 x 4 grid of tiles, rows 0-127), the
//                              sky-view map (256 x 96, rows 128-223), and
//                              the sky light on nine normals (row 224: up,
//                              then eight horizontal ones a 45 degrees apart)
//
// The world shaders read only the frame atlas (scene_common.hlsl, under
// ATMOSPHERE): one texture, one slot. The RHI has no 3D textures and no
// viewports, so the froxel volume is tiled in 2D and both maps share one
// target that one pass fills, branching on the row.
//
// Units are kilometres inside the atmosphere. The planet is centred at the
// origin and the camera stands at (0, R + altitude, 0); the world is flat
// and 15 km across, where the curvature drops 30 m at the edge, so world
// directions are used as they are.
#pragma once

#include "common.hlsl"

#define ATMO_PI 3.14159265

struct AtmosphereUniforms {
    float4 rayleigh;      // rgb scattering (1/km), a scale height (km)
    float4 mie;           // x scattering, y absorption (1/km), z scale height (km), w phase g
    float4 ozone;         // rgb absorption (1/km), a layer centre (km); width 30 km
    float4 ground;        // rgb albedo, a multiple-scattering factor
    float4 radii;         // x planet, y top of the atmosphere (km), z camera altitude (km), w km per world metre
    float4 sun_direction; // xyz toward the sun, w aerial-perspective far distance (km)
};

#define ATMO_TRANSMITTANCE_SIZE float2(256.0, 64.0)
#define ATMO_MULTISCATTER_SIZE float2(32.0, 32.0)
#define ATMO_ATLAS_SIZE float2(256.0, 232.0)
#define ATMO_AMBIENT_ROW 224.0
#define ATMO_SKY_VIEW_SIZE float2(256.0, 96.0)
#define ATMO_SKY_VIEW_ROW 128.0
#define ATMO_SLICES 32.0
#define ATMO_SLICE_SIZE 32.0

// A table's uv with its edge texels' centres at 0 and 1, so a lookup at the
// end of the parameter range reads that texel and nothing beyond it.
float2 atmo_unit_to_sub_uv(float2 uv, float2 size) { return (uv * (size - 1.0) + 0.5) / size; }
float2 atmo_sub_uv_to_unit(float2 uv, float2 size) { return (uv * size - 0.5) / (size - 1.0); }

// The nearer hit of a ray from `origin` along unit `dir` with a sphere about
// the origin, or -1.
float atmo_ray_sphere(float3 origin, float3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float s = sqrt(disc);
    float t0 = -b - s;
    float t1 = -b + s;
    if (t0 >= 0.0) return t0;
    if (t1 >= 0.0) return t1;
    return -1.0;
}

struct AtmoMedium {
    float3 scattering;  // total
    float3 extinction;
    float3 rayleigh_scattering;
    float mie_scattering;
};

AtmoMedium atmo_medium(AtmosphereUniforms a, float height) {
    AtmoMedium m;
    float h = max(height, 0.0);
    float rayleigh_density = exp(-h / a.rayleigh.w);
    float mie_density = exp(-h / a.mie.z);
    float ozone_density = max(0.0, 1.0 - abs(h - a.ozone.w) / 15.0);
    m.rayleigh_scattering = a.rayleigh.rgb * rayleigh_density;
    m.mie_scattering = a.mie.x * mie_density;
    float3 mie_extinction = (float3)((a.mie.x + a.mie.y) * mie_density);
    m.scattering = m.rayleigh_scattering + m.mie_scattering;
    m.extinction = m.rayleigh_scattering + mie_extinction + a.ozone.rgb * ozone_density;
    return m;
}

float atmo_rayleigh_phase(float cos_theta) { return 3.0 / (16.0 * ATMO_PI) * (1.0 + cos_theta * cos_theta); }

// Cornette-Shanks, which keeps a little back-scatter Henyey-Greenstein lacks.
float atmo_mie_phase(float g, float cos_theta) {
    float k = 3.0 / (8.0 * ATMO_PI) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + cos_theta * cos_theta) / pow(max(1.0 + g * g - 2.0 * g * cos_theta, 1e-4), 1.5);
}

// ---- the transmittance table (Bruneton's parametrisation)
float2 atmo_transmittance_uv_radii(float rg, float rt, float r, float mu) {
    float H = sqrt(rt * rt - rg * rg);
    float rho = sqrt(max(r * r - rg * rg, 0.0));
    float disc = r * r * (mu * mu - 1.0) + rt * rt;
    float d = max(0.0, -r * mu + sqrt(max(disc, 0.0)));
    float d_min = rt - r;
    float d_max = rho + H;
    float x_mu = (d - d_min) / max(d_max - d_min, 1e-4);
    float x_r = rho / H;
    return atmo_unit_to_sub_uv(float2(x_mu, x_r), ATMO_TRANSMITTANCE_SIZE);
}
float2 atmo_transmittance_uv(AtmosphereUniforms a, float r, float mu) {
    return atmo_transmittance_uv_radii(a.radii.x, a.radii.y, r, mu);
}

void atmo_transmittance_params(AtmosphereUniforms a, float2 uv, out float r, out float mu) {
    float2 unit = atmo_sub_uv_to_unit(uv, ATMO_TRANSMITTANCE_SIZE);
    float rg = a.radii.x, rt = a.radii.y;
    float H = sqrt(rt * rt - rg * rg);
    float rho = H * unit.y;
    r = sqrt(rho * rho + rg * rg);
    float d_min = rt - r;
    float d_max = rho + H;
    float d = d_min + unit.x * (d_max - d_min);
    mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = clamp(mu, -1.0, 1.0);
}

// ---- the multiple-scattering table: x the sun's zenith cosine, y the height
float2 atmo_multiscatter_uv(AtmosphereUniforms a, float r, float sun_cos_zenith) {
    float2 unit = float2(sun_cos_zenith * 0.5 + 0.5, saturate((r - a.radii.x) / (a.radii.y - a.radii.x)));
    return atmo_unit_to_sub_uv(unit, ATMO_MULTISCATTER_SIZE);
}

// ---- the sky-view map: x the azimuth from the sun (squared toward it, where
// the glow changes fastest), y the zenith angle (squared toward the horizon,
// where the colour changes fastest), for a camera at radius r.
float2 atmo_sky_view_uv(float planet_radius, float r, float view_cos_zenith, float light_view_cos) {
    float v_horizon = sqrt(max(r * r - planet_radius * planet_radius, 0.0));
    float cos_beta = v_horizon / r;
    float beta = acos(clamp(cos_beta, -1.0, 1.0));
    float zenith_horizon = ATMO_PI - beta;
    float view_zenith = acos(clamp(view_cos_zenith, -1.0, 1.0));
    float2 unit;
    if (view_zenith < zenith_horizon) {
        float c = view_zenith / zenith_horizon;
        c = 1.0 - sqrt(max(1.0 - c, 0.0));
        unit.y = c * 0.5;
    } else {
        float c = (view_zenith - zenith_horizon) / max(beta, 1e-4);
        unit.y = sqrt(saturate(c)) * 0.5 + 0.5;
    }
    unit.x = sqrt(saturate(-light_view_cos * 0.5 + 0.5));
    return unit;
}

void atmo_sky_view_params(float planet_radius, float2 unit, float r, out float view_cos_zenith,
                          out float light_view_cos) {
    float v_horizon = sqrt(max(r * r - planet_radius * planet_radius, 0.0));
    float cos_beta = v_horizon / r;
    float beta = acos(clamp(cos_beta, -1.0, 1.0));
    float zenith_horizon = ATMO_PI - beta;
    if (unit.y < 0.5) {
        float c = 2.0 * unit.y;
        c = 1.0 - c;
        c = 1.0 - c * c;
        view_cos_zenith = cos(zenith_horizon * c);
    } else {
        float c = unit.y * 2.0 - 1.0;
        c *= c;
        view_cos_zenith = cos(zenith_horizon + beta * c);
    }
    float x = unit.x * unit.x;
    light_view_cos = -(x * 2.0 - 1.0);
}

// The table passes' vertex stage: a fullscreen triangle, uv running down.
struct AtmoVertex {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
};
#if defined(VERTEX_STAGE) && defined(ATMO_TABLE_PASS)
AtmoVertex vs_main(VERTEX_ID_INPUT) {
    const int vertex_id = VERTEX_ID;
    const float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    AtmoVertex o;
    o.clip_position = float4(positions[vertex_id], 0.0, 1.0);
    o.uv = float2(positions[vertex_id].x * 0.5 + 0.5, 1.0 - (positions[vertex_id].y * 0.5 + 0.5));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
// The scattered light along a ray, and how much of what lies behind it gets
// through: the integral every table but the first is made of. `sun` is the
// direction toward the sun, `max_t` caps the ray (km), `ground` adds the lit
// ground the ray ends on, `multiscatter` reads psi_ms (off while it is being
// built). With `uniform_phase` every scattering event is isotropic, which is
// what the multiple-scattering table integrates; `ms_as_one` then returns the
// fraction of light scattered again (Hillaire's f_ms).
struct AtmoIntegral {
    float3 luminance;
    float3 transmittance;
    float3 ms_as_one;
};

AtmoIntegral atmo_integrate(AtmosphereUniforms a, float3 origin, float3 dir, float3 sun, float max_t, int steps,
                            bool ground, bool multiscatter, bool uniform_phase,
                            Texture2D<float4> transmittance_lut, SamplerState transmittance_sampler,
                            Texture2D<float4> multiscatter_lut, SamplerState multiscatter_sampler) {
    AtmoIntegral o;
    o.luminance = (float3)0.0;
    o.transmittance = (float3)1.0;
    o.ms_as_one = (float3)0.0;

    float t_ground = atmo_ray_sphere(origin, dir, a.radii.x);
    float t_top = atmo_ray_sphere(origin, dir, a.radii.y);
    float t_end;
    if (t_top < 0.0) return o;  // looking past the atmosphere from outside it
    t_end = t_ground > 0.0 ? min(t_top, t_ground) : t_top;
    t_end = min(t_end, max_t);

    float cos_theta = dot(sun, dir);
    float mie_phase = uniform_phase ? 1.0 / (4.0 * ATMO_PI) : atmo_mie_phase(a.mie.w, cos_theta);
    float rayleigh_phase = uniform_phase ? 1.0 / (4.0 * ATMO_PI) : atmo_rayleigh_phase(cos_theta);

    float t = 0.0;
    float dt = t_end / float(steps);
    for (int i = 0; i < steps; ++i) {
        float t_new = (float(i) + 0.3) * dt;
        float step_dt = dt;
        t = t_new;
        float3 p = origin + dir * t;
        float r = length(p);
        float3 up = p / r;
        float sun_cos = dot(sun, up);
        AtmoMedium m = atmo_medium(a, r - a.radii.x);
        float3 sample_transmittance = exp(-m.extinction * step_dt);

        float3 to_sun = transmittance_lut.SampleLevel(transmittance_sampler, atmo_transmittance_uv(a, r, sun_cos), 0.0).rgb;
        float3 phase_scattering = m.mie_scattering * mie_phase + m.rayleigh_scattering * rayleigh_phase;
        // The planet's shadow: a point the sun is below the horizon of.
        float t_earth = atmo_ray_sphere(p - up * 0.0005, sun, a.radii.x);
        float earth_shadow = t_earth >= 0.0 ? 0.0 : 1.0;
        float3 ms = (float3)0.0;
        if (multiscatter) {
            ms = multiscatter_lut.SampleLevel(multiscatter_sampler, atmo_multiscatter_uv(a, r, sun_cos), 0.0).rgb;
        }
        float3 s = earth_shadow * to_sun * phase_scattering + ms * m.scattering;
        float3 extinction = max(m.extinction, (float3)1e-7);
        // Hillaire's energy-conserving step: the light scattered in over the
        // step, attenuated within it.
        float3 s_int = (s - s * sample_transmittance) / extinction;
        o.luminance += o.transmittance * s_int;
        float3 ms_int = (m.scattering - m.scattering * sample_transmittance) / extinction;
        o.ms_as_one += o.transmittance * ms_int;
        o.transmittance *= sample_transmittance;
    }

    if (ground && t_ground > 0.0 && t_ground <= t_end + 1e-3) {
        float3 p = origin + dir * t_ground;
        float r = length(p);
        float3 up = p / r;
        float sun_cos = dot(sun, up);
        float3 to_sun = transmittance_lut.SampleLevel(transmittance_sampler, atmo_transmittance_uv(a, r, sun_cos), 0.0).rgb;
        o.luminance += to_sun * o.transmittance * saturate(sun_cos) * a.ground.rgb / ATMO_PI;
    }
    return o;
}
#endif
