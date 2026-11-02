// The atmosphere's multiple-scattering table (atmosphere_common.hlsl,
// Hillaire 2020 section 5.5): for a height and a sun angle, the light that
// reaches a point after two or more scattering events, as one isotropic
// term. 64 directions over the sphere, each integrated with every event
// isotropic; the second-order light L2 and the fraction scattered again f_ms
// sum the geometric series L2 / (1 - f_ms). Rebuilt with the transmittance.
#define ATMO_TABLE_PASS
#include "atmosphere_common.hlsl"

#ifdef FRAGMENT_STAGE
ConstantBuffer<AtmosphereUniforms> atmosphere : UNIFORM_SLOT(0);
TEXTURE2D(transmittance_lut, 0);

float4 fs_main(AtmoVertex input) : SV_Target {
    float2 unit = atmo_sub_uv_to_unit(input.uv, ATMO_MULTISCATTER_SIZE);
    float sun_cos = unit.x * 2.0 - 1.0;
    float3 sun = float3(0.0, sun_cos, sqrt(saturate(1.0 - sun_cos * sun_cos)));
    float r = atmosphere.radii.x + saturate(unit.y) * (atmosphere.radii.y - atmosphere.radii.x);
    // Off the ground a hair, so the planet does not swallow every ray.
    float3 origin = float3(0.0, max(r, atmosphere.radii.x + 0.001), 0.0);

    float3 luminance = (float3)0.0;
    float3 ms_as_one = (float3)0.0;
    const int SQRT_DIRS = 8;
    for (int j = 0; j < SQRT_DIRS; ++j) {
        for (int i = 0; i < SQRT_DIRS; ++i) {
            // Uniform on the sphere: cos(theta) uniform in [-1, 1].
            float u = (float(i) + 0.5) / float(SQRT_DIRS);
            float v = (float(j) + 0.5) / float(SQRT_DIRS);
            float cos_theta = 1.0 - 2.0 * v;
            float sin_theta = sqrt(saturate(1.0 - cos_theta * cos_theta));
            float phi = 2.0 * ATMO_PI * u;
            float3 dir = float3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
            AtmoIntegral it = atmo_integrate(atmosphere, origin, dir, sun, 1e9, 20, true, false, true,
                                             transmittance_lut, transmittance_lut_sampler,
                                             transmittance_lut, transmittance_lut_sampler);
            luminance += it.luminance;
            ms_as_one += it.ms_as_one;
        }
    }
    // Each direction is a 4 pi / 64 share of the sphere, seen through the
    // isotropic phase 1 / (4 pi): the two cancel to a plain average.
    const float N = float(SQRT_DIRS * SQRT_DIRS);
    luminance /= N;
    ms_as_one /= N;
    float3 psi = luminance / max(1.0 - ms_as_one, (float3)1e-3);
    return float4(psi * atmosphere.ground.a, 1.0);
}
#endif
