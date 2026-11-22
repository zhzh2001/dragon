#define ATMOSPHERE_SLOT 1  // the atmosphere's atlas, after the shadow map
#define TERRAIN_SUN_SLOT 2  // and the terrain's shadow heights after it
#include "scene_common.hlsl"
#include "noise.hlsl"

// A breath of liquid as one stream (game/breath_matter.h): the tide's water,
// the blight's acid. The modern tier only.
//
// The stream was first a string of ellipsoids, one a slug, and read as a bag
// of pills: every slug had its own outline and its own highlight. A stream
// is one body. Here it is one tube bent along the path the slugs fly: the
// mesh is a unit cylinder (x and y the ring, z 0 to 1 along it), and the
// vertex stage lays it on a curve through the nodes below. The fragment
// stage makes it water: flowing ridges in the normal, a glint off each, the
// sky at a grazing angle, a body seen through near the jaws that whitens
// with air toward its end and tears into strands there.
//
// Must match gfx::StreamUniforms (scene_uniforms.h).

struct StreamUniforms {
    // xyz a point on the path, youngest first; w the stream's radius there.
    float4 nodes[24];
    // x how many nodes, y time (s), z the flow's speed along the stream in
    // nodes a second, w the cover at the jaws (0 clear to 1 solid).
    float4 params;
    float4 near_colour;  // rgb at the jaws; a the unlit share
    float4 far_colour;   // rgb where it has whitened; a how far it tears into strands
    float4 surface;      // x the glint's strength, y its exponent, z the sky mirrored, w light through
    // x how far the stream is drawn into beads on a thread (0 a hose's
    // stream, 1 gobs joined by strings: a thick liquid), y beads a node,
    // z how far it whitens with air (1 water, 0 none).
    float4 body;
};

struct VertexIn {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 color    : TEXCOORD2;
};

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
    float3 tangent : TEXCOORD2;
    float2 place : TEXCOORD3;  // x along 0..1, y round 0..1
};

#ifdef VERTEX_STAGE
ConstantBuffer<StreamUniforms> stream : UNIFORM_SLOT(1);

VertexOut vs_main(VertexIn input) {
    VertexOut o;
    float count = max(stream.params.x, 2.0);
    float f = saturate(input.position.z) * (count - 1.0);
    float first = min(floor(f), count - 2.0);
    float u = f - first;
    int b = int(first);
    int a = max(b - 1, 0);
    int c = b + 1;
    int d = min(b + 2, int(count) - 1);
    float4 p0 = stream.nodes[a];
    float4 p1 = stream.nodes[b];
    float4 p2 = stream.nodes[c];
    float4 p3 = stream.nodes[d];
    // Catmull-Rom through the nodes: the path is smooth where the nodes
    // are metres apart.
    float3 k1 = p2.xyz - p0.xyz;
    float3 k2 = 2.0 * p0.xyz - 5.0 * p1.xyz + 4.0 * p2.xyz - p3.xyz;
    float3 k3 = 3.0 * (p1.xyz - p2.xyz) + p3.xyz - p0.xyz;
    float3 centre = 0.5 * (2.0 * p1.xyz + k1 * u + k2 * u * u + k3 * u * u * u);
    float3 along = 0.5 * (k1 + 2.0 * k2 * u + 3.0 * k3 * u * u);
    along = normalize(along + float3(0.0, 0.0, 1e-5));
    float radius = lerp(p1.w, p2.w, u * u * (3.0 - 2.0 * u));
    // A thick liquid comes in gobs: the radius swells and thins along the
    // stream, and the gobs travel down it.
    float bead = 0.5 + 0.5 * sin((f * stream.body.y - stream.params.y * stream.params.z * stream.body.y) * 6.2831853);
    radius *= lerp(1.0, 0.3 + 1.25 * bead * bead, stream.body.x);

    float3 side = normalize(cross(along, float3(0.0, 1.0, 0.0)) + float3(1e-5, 0.0, 0.0));
    float3 lift = cross(side, along);
    float3 out_normal = side * input.position.x + lift * input.position.y;
    o.world_position = centre + out_normal * radius;
    o.world_normal = out_normal;
    o.tangent = along;
    o.place = float2(input.position.z, atan2(input.position.y, input.position.x) / 6.2831853 + 0.5);
    o.clip_position = mul(scene.view_proj, float4(o.world_position, 1.0));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH2D(shadow_map, 0);
ConstantBuffer<StreamUniforms> stream : UNIFORM_SLOT(1);

// Ridges that run along the stream and move down it: long in the flow's
// direction, short round the tube. `round` wraps, so two octaves of it are
// laid at whole counts.
float stream_ridges(float2 place, float travelled) {
    float along = place.x * stream.params.x * 1.6 - travelled;
    float a = value_noise(float2(place.y * 7.0, along));
    float b = value_noise(float2(place.y * 17.0 + 3.0, along * 2.3 + 11.0));
    // The seam where `round` wraps: both ends read the same lattice column.
    float seam = abs(place.y - 0.5) * 2.0;
    float a2 = value_noise(float2((place.y - 1.0) * 7.0, along));
    a = lerp(a, a2, smoothstep(0.8, 1.0, seam) * step(0.5, place.y));
    return a * 0.65 + b * 0.35;
}

float4 fs_main(VertexOut input) : SV_Target {
    float along = input.place.x;
    float travelled = stream.params.y * stream.params.z;
    float ridges = stream_ridges(input.place, travelled);
    // The same a little way round and along, for the slope of the ridges.
    float ridge_round = stream_ridges(input.place + float2(0.0, 0.012), travelled);
    float ridge_along = stream_ridges(input.place + float2(0.004, 0.0), travelled);

    float3 normal = normalize(input.world_normal);
    float3 tangent = normalize(input.tangent);
    float3 round_dir = normalize(cross(tangent, normal));
    normal = normalize(normal + round_dir * (ridges - ridge_round) * 5.0 + tangent * (ridges - ridge_along) * 3.0);

    float3 to_camera = normalize(scene.camera_position.xyz - input.world_position);
    if (dot(normal, to_camera) < 0.0) normal = -normal;

    // Air in the water: none at the jaws, most of it white by mid-stream,
    // and first along the ridges' crests.
    float air = saturate(smoothstep(0.02, 0.55, along) * 0.75 + (ridges - 0.45) * 1.6 * smoothstep(0.0, 0.3, along)) * stream.body.z;
    float3 albedo = lerp(stream.near_colour.rgb, stream.far_colour.rgb, air);

    float visibility = sun_visibility(input.world_position, normal, shadow_map, shadow_map_sampler);
    float3 direct = direct_sun(normal, visibility) + local_light(input.world_position, normal);
    float3 ambient = ambient_light_at(normal, input.world_position);
    float3 color = albedo * (direct + ambient);
    color += albedo * stream.surface.w * direct_sun(-normal, visibility) * 0.7;
    color += albedo * stream.near_colour.a;

    float facing = saturate(dot(normal, to_camera));
    float fresnel = pow(1.0 - facing, 4.0);
    float3 mirrored = reflect(-to_camera, normal);
    float3 sky = sky_plain(float3(mirrored.x, abs(mirrored.y), mirrored.z));
    color = lerp(color, sky, saturate(stream.surface.z * (0.04 + 0.96 * fresnel)) * (1.0 - 0.7 * air));

    float3 half_vector = normalize(normalize(scene.sun.xyz) + to_camera);
    float glint = pow(saturate(dot(normal, half_vector)), max(stream.surface.y, 1.0)) * stream.surface.x * visibility;
    color += scene.sun_color.rgb * scene.sun.w * glint;

    // Seen through at the jaws, solid where it is white, at its rim and in
    // a glint. Toward its end it tears: the troughs between ridges open.
    float cover = saturate(lerp(stream.params.w, 1.0, air) + fresnel * 0.6 + glint);
    float torn = smoothstep(0.45, 1.0, along) * stream.far_colour.a;
    cover *= 1.0 - smoothstep(0.75 - 0.75 * torn, 0.95 - 0.6 * torn, 1.0 - ridges) * step(0.001, torn);
    cover *= 1.0 - smoothstep(0.93, 1.0, along);

    float3 fogged = apply_fog(color, input.world_position);
    return float4(scene_out(fogged), cover);
}
#endif
