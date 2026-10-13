// The terrain's shadow on itself and on everything over it, at every
// distance (docs/FIDELITY.md, F4): the heightfield marched toward the sun.
// The shadow map covers 1.8 km about the camera; a 900 m ridge under a 14
// degree sun throws its shadow 3.6 km, and past the map nothing did.
//
// One texel per column of the world, top-down over the terrain's whole
// extent. It holds the height of the shadow's top there -- the highest
// sun ray any ridge between the column and the sun blocks, max over t of
// (ground(t) - t tan(elevation)) -- so a point at any height is shadowed
// exactly when it is below it: the ground, a tree's crown and a dragon
// flying over the valley alike (scene_common.hlsl, terrain_sun). And the
// distance to that ridge, which widens the penumbra with it. Rebuilt when
// the sun or the terrain changes.
#define ATMO_TABLE_PASS
#include "atmosphere_common.hlsl"

struct TerrainSunUniforms {
    float4 sun;     // xy the sun's horizontal direction (x, z), z tan(elevation), w the extent's half (m)
    float4 height;  // x the height texture's size in texels, yzw unused
};

#ifdef FRAGMENT_STAGE
ConstantBuffer<TerrainSunUniforms> terrain_sun_params : UNIFORM_SLOT(0);
TEXTURE2D(heights, 0);

float ground(float2 xz) {
    float half_extent = terrain_sun_params.sun.w;
    float2 uv = xz / half_extent * 0.5 + 0.5;
    return heights.SampleLevel(heights_sampler, uv, 0.0).r;
}

float4 fs_main(AtmoVertex input) : SV_Target {
    float half_extent = terrain_sun_params.sun.w;
    float2 xz = (input.uv * 2.0 - 1.0) * half_extent;
    float2 toward = terrain_sun_params.sun.xy;
    float rise = terrain_sun_params.sun.z;
    float top = -1e4;
    float distance = 0.0;
    // Geometric steps: fine near the column, where the ridge that shades it
    // is usually close, and out to the far edge in 128 of them.
    float t = 6.0;
    for (int i = 0; i < 128; ++i) {
        float2 q = xz + toward * t;
        if (any(abs(q) > half_extent)) break;
        float blocked = ground(q) - t * rise;
        if (blocked > top) {
            top = blocked;
            distance = t;
        }
        t *= 1.065;
    }
    return float4(top, distance, 0.0, 1.0);
}
#endif
