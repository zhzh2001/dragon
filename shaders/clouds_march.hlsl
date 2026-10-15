// The volumetric clouds' march (gfx/clouds.h, docs/FIDELITY.md F6), at half
// resolution: the view ray through the cumulus slab, from where it enters
// to where it leaves or meets a surface, in up to `steps` dithered steps.
// Each step's density is the weather map's coverage cut into billows by a
// tiling Perlin-Worley volume and frayed at the edges by a finer one
// (Schneider's Nubis arrangement), lit by the sun through a six-step march
// toward it -- with two more octaves of fainter, wider scattering standing
// in for the light that bounces inside a heap (Wrenninge) -- and by the
// sky from above. Out: rgb the light the clouds add, a the share of what
// is behind them that still shows.
#define ATMOSPHERE_SLOT 1
#include "scene_common.hlsl"
#include "clouds_common.hlsl"

#ifdef FRAGMENT_STAGE
DEPTH2D(scene_depth, 0);
TEXTURE2D(cloud_noise, 2);    // the volume's atlas: r shape, gba cellular fBm
TEXTURE2D(cloud_weather, 3);  // r coverage, g how tall

// The volume at `p` in periods, trilinear: two slices of the atlas, 8 x 8
// slices of 64 x 64 texels with a one-texel border (gfx/clouds.cpp).
float4 cloud_volume(float3 p) {
    float3 q = frac(p) * 64.0;
    float zf = q.z - 0.5;
    float z0 = floor(zf);
    float w = zf - z0;
    float s0 = fmod(z0 + 64.0, 64.0);
    float s1 = fmod(z0 + 65.0, 64.0);
    float2 inside = q.xy + 1.0;
    float2 uv0 = (float2(fmod(s0, 8.0), floor(s0 / 8.0)) * 66.0 + inside) / 528.0;
    float2 uv1 = (float2(fmod(s1, 8.0), floor(s1 / 8.0)) * 66.0 + inside) / 528.0;
    return lerp(cloud_noise.SampleLevel(cloud_noise_sampler, uv0, 0.0),
                cloud_noise.SampleLevel(cloud_noise_sampler, uv1, 0.0), w);
}

float2 cloud_weather_at(float2 xz) {
    float2 uv = (xz - vol.weather.xy) / (2.0 * vol.weather.z) + 0.5;
    return cloud_weather.SampleLevel(cloud_weather_sampler, uv, 0.0).rg;
}

float remap(float v, float from_low, float from_high, float to_low, float to_high) {
    return to_low + (v - from_low) / max(from_high - from_low, 1e-4) * (to_high - to_low);
}

// The density at `p` (0..1, times the layer's density dial), given the
// weather there. `detail` adds the erosion, which the sun march skips.
float cloud_volume_density(float3 p, float2 weather, bool detail) {
    float coverage = weather.x;
    if (coverage <= 0.002) return 0.0;
    float h = saturate((p.y - scene.clouds.y) / max(scene.clouds.z, 1.0));
    // A flat base and a dome that the weather raises: fair-weather cumulus
    // here, a tower there. The taper starts low, so only the heap's core
    // reaches the top and the shape rounds over.
    float top = lerp(0.5, 1.0, weather.y) * lerp(0.75, 1.0, coverage);
    float profile = smoothstep(0.0, 0.05, h) * saturate(remap(h, top * 0.2, top, 1.0, 0.0));
    float3 wind = cloud_wind();
    float4 n = cloud_volume((p + wind) / vol.shape.x);
    float fbm = dot(n.gba, float3(0.625, 0.25, 0.125));
    float shape = saturate(remap(n.r, fbm - 1.0, 1.0, 0.0, 1.0)) * profile;
    float d = saturate(remap(shape, 1.0 - coverage, 1.0, 0.0, 1.0)) * coverage;
    if (detail && d > 0.0) {
        float4 e = cloud_volume((p + wind * 1.6 + float3(0.0, -scene.view_params.z * 1.5, 0.0)) / vol.shape.y);
        float efbm = dot(e.gba, float3(0.625, 0.25, 0.125));
        // Wisps at the base, billows above.
        float erode = lerp(efbm, 1.0 - efbm, saturate(h * 5.0));
        d = saturate(remap(d, erode * vol.shape.z, 1.0, 0.0, 1.0));
    }
    return d * scene.clouds.w;
}

float henyey_greenstein(float mu, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * mu, 1e-4), 1.5));
}

float4 fs_main(CloudVertex input) : SV_Target {
    float2 uv = input.uv;
    float3 ray = cloud_ray(uv);
    float3 eye = scene.camera_position.xyz;
    float2 slab = cloud_slab(eye, ray);
    // The nearest surface of the four pixels this one covers, so a cloud
    // never marches past the edge of a ridge in front of it.
    float2 t = vol.texel.zw * 0.5;
    float depth = max(max(scene_depth.SampleLevel(scene_depth_sampler, uv + float2(-t.x, -t.y), 0.0),
                          scene_depth.SampleLevel(scene_depth_sampler, uv + float2(t.x, -t.y), 0.0)),
                      max(scene_depth.SampleLevel(scene_depth_sampler, uv + float2(-t.x, t.y), 0.0),
                          scene_depth.SampleLevel(scene_depth_sampler, uv + float2(t.x, t.y), 0.0)));
    float surface = cloud_surface_distance(depth, ray);
    float t0 = slab.x;
    float t1 = min(min(slab.y, surface), t0 + 40000.0);
    if (slab.x > slab.y || t1 <= t0 || scene.clouds.x <= 0.0) return float4(0.0, 0.0, 0.0, 1.0);

    int steps = int(vol.params.z);
    float dt = (t1 - t0) / float(steps);
    // A fresh offset each frame (interleaved gradient noise, walked by the
    // golden ratio), which the resolve averages away.
    float dither = frac(52.9829189 * frac(dot(input.clip_position.xy, float2(0.06711056, 0.00583715))) +
                        vol.params.y * 0.61803399);

    float3 sun = normalize(scene.sun.xyz);
    float3 sun_light = scene.sun_color.rgb * scene.sun.w * vol.light.x;
    float3 sky_light = (atmosphere_on() ? atmo_sky_light(float3(0.0, 1.0, 0.0)) : scene.sky_zenith.rgb) * vol.light.y;
    float3 bounce = scene.palette[PALETTE_GROUND_BOUNCE].rgb * scene.sun_color.rgb * scene.sun.w * saturate(sun.y * 4.0) * 0.08;
    float mu = dot(ray, sun);
    float extinction = vol.params.w;

    float transmittance = 1.0;
    float3 light = (float3)0.0;
    float weighted_t = 0.0, weight = 0.0;
    for (int i = 0; i < 128; ++i) {
        if (i >= steps) break;
        float s = t0 + (float(i) + dither) * dt;
        float3 p = eye + ray * s;
        float2 weather = cloud_weather_at(p.xz);
        if (weather.x <= 0.002) continue;
        float density = cloud_volume_density(p, weather, true);
        if (density <= 0.001) continue;

        // Toward the sun: six steps growing to 800 m, the first short, since
        // the face a sunlit heap shows is its outer twenty metres.
        float optical = 0.0;
        float ls = 15.0;
        float3 q = p;
        for (int j = 0; j < 6; ++j) {
            q += sun * ls;
            optical += cloud_volume_density(q, cloud_weather_at(q.xz), false) * ls;
            ls *= 1.9;
        }
        optical *= extinction;
        // Three octaves of scattering, each fainter, less forward and less
        // absorbed: the light that has bounced around inside the heap.
        float scattered_sun = 0.0;
        float a = 1.0, b = 1.0, c = 1.0;
        for (int k = 0; k < 3; ++k) {
            scattered_sun += b * lerp(henyey_greenstein(mu, 0.75 * c), henyey_greenstein(mu, -0.2 * c), 0.25) * exp(-optical * a);
            a *= 0.25;
            b *= 0.45;
            c *= 0.5;
        }
        float h = saturate((p.y - scene.clouds.y) / max(scene.clouds.z, 1.0));
        // Powder: a heap's sunward edge, where little has scattered in yet,
        // is darker than its depths (Schneider).
        float powder = 1.0 - exp(-density * extinction * 120.0);
        // The phase is per steradian; times 4 pi, an isotropic cloud lit by
        // the sun returns what a white wall would in the world shaders'
        // units (albedo times the light, no 1/pi): at 4 the heaps were a
        // fifth as bright as the snow under them.
        float3 in_light = sun_light * scattered_sun * 12.566 * lerp(1.0, powder * 2.0, saturate(0.5 - 0.5 * mu)) +
                          sky_light * (0.12 + 0.88 * h * h) + bounce * (1.0 - h);
        float sigma = density * extinction;
        float through = exp(-sigma * dt);
        light += transmittance * in_light * (1.0 - through);
        weighted_t += s * transmittance * (1.0 - through);
        weight += transmittance * (1.0 - through);
        transmittance *= through;
        if (transmittance < 0.01) break;
    }
    if (weight <= 0.0) return float4(0.0, 0.0, 0.0, 1.0);
    // The air between the eye and the cloud, as for any surface, at where
    // the cloud's light came from.
    float mean_t = weighted_t / weight;
    float3 at = eye + ray * mean_t;
    float4 air = atmosphere_on() ? atmo_aerial(at) : float4(0.0, 0.0, 0.0, 1.0);
    light = light * air.a + air.rgb * (1.0 - transmittance);
    // Thinning out toward the horizon, where the slab is seen edge-on
    // through more air than the froxels reach.
    float fade = 1.0 - smoothstep(25000.0, 45000.0, mean_t);
    return float4(light * fade, lerp(1.0, transmittance, fade));
}
#endif
