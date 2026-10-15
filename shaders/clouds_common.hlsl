// The volumetric clouds' shared half (gfx/clouds.h, docs/FIDELITY.md F6):
// their uniforms, the view ray, the slab, and the density. Included after
// scene_common.hlsl by the four clouds_* passes, which push the scene's
// uniforms in slot 0 and these in slot 1.
#pragma once

struct CloudUniforms {
    float4x4 prev_view_proj;  // camera-relative, last frame's
    float4 camera_delta;      // xyz this camera minus last frame's, w the history's weight (0: reset)
    float4 params;            // x the near plane, y frame index, z steps, w extinction (1/m)
    float4 texel;             // xy one half-resolution texel, zw one full-resolution texel (uv)
    float4 weather;           // xy the weather map's centre (world xz), z its half extent, w its texel (m)
    float4 shape;             // x base scale (m), y detail scale (m), z detail, w softness
    float4 light;             // x sun gain, y sky gain
};
ConstantBuffer<CloudUniforms> vol : UNIFORM_SLOT(1);

struct CloudVertex {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
};

#ifdef VERTEX_STAGE
CloudVertex vs_main(VERTEX_ID_INPUT) {
    const int vertex_id = VERTEX_ID;
    const float2 corner = fullscreen_corner(vertex_id);
    CloudVertex o;
    o.clip_position = float4(corner, 0.0, 1.0);
    o.uv = float2(corner.x * 0.5 + 0.5, 1.0 - (corner.y * 0.5 + 0.5));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
// The view ray through `uv`, from the camera's basis, unjittered.
float3 cloud_ray(float2 uv) {
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float t = scene.view_params.x;
    return normalize(scene.camera_forward.xyz + scene.camera_right.xyz * ndc.x * t * scene.view_params.y +
                     scene.camera_up.xyz * ndc.y * t);
}

// Where the ray is inside the slab: (enter, leave) from the eye, enter > leave
// when it never is.
float2 cloud_slab(float3 eye, float3 ray) {
    float base = scene.clouds.y, top = scene.clouds.y + scene.clouds.z;
    if (abs(ray.y) < 1e-5) return (eye.y > base && eye.y < top) ? float2(0.0, 1e9) : float2(1.0, 0.0);
    float tb = (base - eye.y) / ray.y, tt = (top - eye.y) / ray.y;
    float enter = max(min(tb, tt), 0.0), leave = max(tb, tt);
    return leave > 0.0 ? float2(enter, leave) : float2(1.0, 0.0);
}

// How far along the ray the surface at this reversed-Z depth stands: the
// projection is infinite, so depth = near / view distance.
float cloud_surface_distance(float depth, float3 ray) {
    if (depth <= 1e-7) return 1e9;  // the sky
    return vol.params.x / depth / max(dot(ray, scene.camera_forward.xyz), 1e-4);
}

// The wind: the layer drifts with the ground shadows (cloud_drift), and the
// shapes turn over within it a little faster.
float3 cloud_wind() {
    float2 d = cloud_drift();
    return float3(d.x, 0.0, d.y);
}
#endif
