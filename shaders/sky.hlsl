// The atmosphere's atlas, then its transmittance table for the sun disc.
#define ATMOSPHERE_SLOT 0
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
    float3 normal   : TEXCOORD1;
    float3 color    : TEXCOORD2;
    float2 uv       : TEXCOORD3;  // lat-long, into the baked sky map (gfx::WorldRenderer::bake_sky_dome)
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
    // The sky is a texture on the dome (stage 0, SELECTARG1 TEXTURE): baked
    // once (gfx::prelit::sky_diffuse) since the sun is static, so the dome
    // can be 153 vertices, which a card without T&L transforms on the CPU.
    o.uv = input.uv;
    o.diffuse = float4(input.color, 1.0);
    o.fog = 1.0;
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
TEXTURE2D(sky_map, 0);
float4 fs_main(VertexOut input) : SV_Target {
    // Times the fog factor and the diffuse alpha (both 1 here): the reads
    // keep COLOR0 and FOG in this stage's input signature ahead of the uv,
    // as in particles.hlsl. DXC drops an unused input and renumbers the
    // rest, and D3D11 at feature level 9 then refuses to link the stages
    // ("Semantic 'TEXCOORD' is defined for mismatched hardware registers")
    // and drew no sky on the X550.
    return float4(scene_out(sky_map.Sample(sky_map_sampler, input.uv.xy).rgb) * saturate(input.fog) * saturate(input.diffuse.a), 1.0);
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
#ifdef USE_ATMOSPHERE
TEXTURE2D(transmittance_lut, 1);

// The sun's disc, through the air between: the transmittance table at the
// camera's height toward the sun, which is what turns it orange near the
// horizon. Larger than the real 0.27 degrees and far dimmer than the real
// thing, so it reads as a sun and blooms without swallowing the sky; the
// limb darkens toward the edge.
float3 atmo_sun_disc(float3 ray) {
    const float ANGULAR_RADIUS = 0.0105;  // 0.6 degrees
    float3 sun = normalize(scene.sun.xyz);
    float cos_angle = dot(ray, sun);
    float edge = cos(ANGULAR_RADIUS);
    float3 transmittance = transmittance_lut.SampleLevel(
        transmittance_lut_sampler, atmo_transmittance_uv_radii(scene.atmosphere2.z, scene.atmosphere2.z + 100.0,
                                                              scene.atmosphere.z, sun.y), 0.0).rgb;
    if (cos_angle < edge) return (float3)0.0;
    float centre = saturate((cos_angle - edge) / (1.0 - edge));
    float limb = 0.6 + 0.4 * sqrt(centre);
    float soft = smoothstep(0.0, 0.08, centre);
    return transmittance * scene.atmosphere2.w * scene.atmosphere.x * limb * soft;
}
#endif

float4 fs_main(VertexOut input) : SV_Target {
    // Rebuild the view ray from the camera basis rather than inverting the
    // view-projection matrix.
    float tan_half_fov = scene.view_params.x;
    float aspect = scene.view_params.y;
    float3 ray = normalize(scene.camera_forward.xyz +
                           scene.camera_right.xyz * input.ndc.x * tan_half_fov * aspect +
                           scene.camera_up.xyz * input.ndc.y * tan_half_fov);
    float3 color = sky_color(ray);
#ifdef USE_ATMOSPHERE
    if (atmosphere_on()) color += atmo_sun_disc(ray);
#endif
    return float4(scene_out(color), 1.0);
}
#endif
#endif  // FIXED_FUNCTION
