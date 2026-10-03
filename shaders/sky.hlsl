#include "scene_common.hlsl"

// Fullscreen sky. Drawn first with no depth test, so the terrain overwrites it.
// Three vertices generated from the vertex id -- no vertex buffer needed.

struct VertexOut {
    float4 clip_position : SV_Position;
    float2 ndc : TEXCOORD0;
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VERTEX_ID_INPUT) {
    const int vertex_id = VERTEX_ID;
    // A triangle large enough to cover the whole clip volume.
    const float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
    VertexOut o;
    o.ndc = positions[vertex_id];
    // z = 0 is the far plane under reversed-Z, which is where sky belongs.
    o.clip_position = float4(positions[vertex_id], 0.0, 1.0);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
float4 fs_main(VertexOut input) : SV_Target {
    // Rebuild the view ray from the camera basis rather than inverting the
    // view-projection matrix.
    float tan_half_fov = scene.view_params.x;
    float aspect = scene.view_params.y;
    float3 ray = normalize(scene.camera_forward.xyz +
                           scene.camera_right.xyz * input.ndc.x * tan_half_fov * aspect +
                           scene.camera_up.xyz * input.ndc.y * tan_half_fov);
    return float4(scene_out(sky_color(ray)), 1.0);
}
#endif
