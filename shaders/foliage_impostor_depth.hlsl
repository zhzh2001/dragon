// A far tree's shadow as its impostor (gfx::Foliage::draw_trees_depth, the
// modern tier's coarse cascade): one quad facing the sun, cut out by the
// frame of the baked atlas (foliage_impostor_bake.hlsl) nearest the sun's
// direction -- the crown's silhouette as the sun sees it. Its coarse cards
// were 4.5 million vertices in a forest at 1080p; the quads are six indices
// a tree. Each tree hands over from its crown at its own distance from the
// eye, measured flat, by the same hash and band foliage_depth.hlsl collapses
// the crown by, so a tree casts once.
#define NO_SCENE
#include "scene_common.hlsl"

struct ShadowUniforms {
    float4x4 light_view_proj;
};

struct ImpostorDepthUniforms {
    float4 kind;  // xy the kind's quadrant of the atlas (uv), z its centre's height, w its radius (m, at scale 1)
    float4 lod;   // x where the first trees hand over, y the last (m), z frames a side, w the quadrant's size (uv)
    float4 sun;   // xyz toward the sun; w unused
    float4 eye;   // x, z the eye on the ground plane
};

struct VertexIn {
    float3 position       : TEXCOORD0;  // the quad's corner, -1..1 in x and y
    float3 normal         : TEXCOORD1;
    float3 color          : TEXCOORD2;
    float2 uv             : TEXCOORD3;
    float4 position_scale : TEXCOORD4;
    float4 params         : TEXCOORD5;  // x yaw, y shade, z sway phase (the hash), w warm push
};

struct DepthOut {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
    DEPTH_VARYING
};

// The upper hemisphere folded onto a square, as foliage_impostor.hlsl.
float2 hemi_octahedral(float3 d) {
    d.y = max(d.y, 0.0);
    d /= abs(d.x) + d.y + abs(d.z);
    return float2(d.x + d.z, d.x - d.z);
}

#ifdef VERTEX_STAGE
ConstantBuffer<ShadowUniforms> shadow : UNIFORM_SLOT(0);
ConstantBuffer<ImpostorDepthUniforms> impostor : UNIFORM_SLOT(1);

DepthOut vs_main(VertexIn input) {
    float scale = input.position_scale.w;
    float yaw = input.params.x;
    float c = cos(yaw), s = sin(yaw);
    float3 centre = input.position_scale.xyz + float3(0.0, impostor.kind.z * scale, 0.0);
    float3 view = normalize(impostor.sun.xyz);
    float3 local = float3(c * view.x - s * view.z, view.y, s * view.x + c * view.z);
    float frames = impostor.lod.z;
    // The one frame nearest the sun: the sun does not move in a frame, so
    // there is no snap to hide.
    float2 grid = clamp((hemi_octahedral(local) * 0.5 + 0.5) * frames - 0.5, 0.0, frames - 1.0);
    float2 frame = floor(grid + 0.5);

    float3 right = normalize(cross(abs(view.y) > 0.999 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0), view));
    float3 up = cross(view, right);
    float radius = impostor.kind.w * scale;
    float3 world = centre + (right * input.position.x + up * input.position.y) * radius;
    // Its crown casts until its own distance (foliage_depth.hlsl).
    float hash = frac(input.params.z * 0.15915494);
    float flat_distance = length(input.position_scale.xz - impostor.eye.xz);
    if (flat_distance < lerp(impostor.lod.x, impostor.lod.y, hash)) world = centre;  // a point: nothing drawn

    DepthOut o;
    o.clip_position = mul(shadow.light_view_proj, float4(world, 1.0));
    float2 corner = float2(input.position.x * 0.5 + 0.5, 0.5 - input.position.y * 0.5);
    o.uv = impostor.kind.xy + (frame + corner) / frames * impostor.lod.w;
    WRITE_DEPTH_VARYING(o);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
TEXTURE2D(albedo_atlas, 0);

DEPTH_FRAGMENT_RETURN fs_main(DepthOut input) DEPTH_FRAGMENT_SEMANTIC {
    if (albedo_atlas.Sample(albedo_atlas_sampler, input.uv).a < 0.5) discard;
    DEPTH_FRAGMENT_END(input);
}
#endif
