// The atmosphere's transmittance table (atmosphere_common.hlsl): how much
// light gets from a height, along a zenith angle, out of the atmosphere.
// Rebuilt only when the atmosphere's dials change.
#define ATMO_TABLE_PASS
#include "atmosphere_common.hlsl"

#ifdef FRAGMENT_STAGE
ConstantBuffer<AtmosphereUniforms> atmosphere : UNIFORM_SLOT(0);

float4 fs_main(AtmoVertex input) : SV_Target {
    float2 uv = input.clip_position.xy / ATMO_TRANSMITTANCE_SIZE;
    float r, mu;
    atmo_transmittance_params(atmosphere, uv, r, mu);
    float3 origin = float3(0.0, r, 0.0);
    float3 dir = float3(0.0, mu, sqrt(max(1.0 - mu * mu, 0.0)));
    float t_end = atmo_ray_sphere(origin, dir, atmosphere.radii.y);
    const int STEPS = 40;
    float dt = max(t_end, 0.0) / float(STEPS);
    float3 optical_depth = (float3)0.0;
    for (int i = 0; i < STEPS; ++i) {
        float3 p = origin + dir * ((float(i) + 0.5) * dt);
        AtmoMedium m = atmo_medium(atmosphere, length(p) - atmosphere.radii.x);
        optical_depth += m.extinction * dt;
    }
    return float4(exp(-optical_depth), 1.0);
}
#endif
