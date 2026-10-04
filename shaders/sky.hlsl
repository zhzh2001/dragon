#include "scene_common.hlsl"

// Fullscreen sky, drawn after the opaque world at the far plane, so only the
// pixels nothing else covered run it.
// Three vertices generated from the vertex id -- no vertex buffer needed.

#ifdef FIXED_FUNCTION
// Fixed function (docs/PORTING.md R5): a dome instead of the fullscreen
// triangle -- a unit sphere around the camera, pushed to the far plane --
// coloured per vertex with the analytic sky, display-encoded: the diffuse
// alone (stage 0, SELECTARG1 DIFFUSE). The sun is static, so on the real
// backend this is what a baked sky texture would hold; here the vertices
// carry it, which a 64 x 32 sphere resolves well enough for a gradient and
// a glow; the disc itself goes soft (64 x 32 keeps the glow's shape).
struct VertexIn {
    float3 position : TEXCOORD0;
};

struct VertexOut {
    float4 clip_position : SV_Position;
    FF_VARYINGS
};

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    float3 ray = normalize(input.position);
    VertexOut o;
    o.clip_position = mul(scene.view_proj, float4(scene.camera_position.xyz + ray * 1000.0, 1.0));
#ifdef CONVENTIONAL_DEPTH
    o.clip_position.z = o.clip_position.w;
#else
    o.clip_position.z = 0.0;
#endif
    o.uv = (float4)0.0;
    o.diffuse = float4(ff_display(sky_color(ray)), 1.0);
    o.fog = 1.0;
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
float4 fs_main(VertexOut input) : SV_Target {
    return float4(scene_out(input.diffuse.rgb), 1.0);
}
#endif
#else

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
    // On the far plane, which is where sky belongs: z = 0 under reversed-Z,
    // z = w in a retro tier's conventional depth (gfx/render_tier.h).
#ifdef CONVENTIONAL_DEPTH
    o.clip_position = float4(positions[vertex_id], 1.0, 1.0);
#else
    o.clip_position = float4(positions[vertex_id], 0.0, 1.0);
#endif
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
#endif  // FIXED_FUNCTION
