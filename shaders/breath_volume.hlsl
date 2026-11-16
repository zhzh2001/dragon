// The breath as a volume (gfx/breath_volume.h): one fullscreen pass over
// the scene. Each pixel marches its view ray through the jet's spine, a
// chain of capsules, and sums what it meets as emission and absorption.
//
// The jet's shape is a round profile about the spine, eaten by noise that
// scrolls along it, more toward the tip (Fuller et al., I3D 2007). A step is
// weighted by 1 - exp(-sigma ds), the weights of emission and absorption,
// and what is summed is the gas's heat. The pixel's colour is the mean heat
// read through a ramp: never brighter than the ramp's top, whatever the
// depth of gas. Summed puffs had no such bound, and went to one flat colour
// under a tonemap that keeps hue.
//
// Alpha blending gives rgb * a + scene * (1 - a): the light, and the scene
// through what the gas lets pass. The modern tier only.
#include "scene_common.hlsl"

#define BREATH_NODES 16

struct BreathUniforms {
    float4 params;  // x the near plane, y time (s), z the node count, w the march's steps
    float4 shape;   // x extinction (1/m), y 1 / noise size (1/m), z erosion (radii), w the noise's speed (m/s)
    float4 colour;  // x core gain, y body gain, z soot at the tip, w brightness
    float4 hot;     // rgb
    float4 cool;    // rgb
    float4 axis;    // xyz the jet's direction at the mouth
    float4 bounds;  // xyz a sphere's centre, w its radius: the whole jet
    float4 hull_a;  // xyz one end of a capsule about the whole jet, w its radius
    float4 hull_b;  // xyz the other end
    float4 nodes[BREATH_NODES];  // xyz position, w radius; the youngest first
    float4 info[BREATH_NODES];   // x age 0..1, y 1 when joined to the next node
};
ConstantBuffer<BreathUniforms> jet : UNIFORM_SLOT(1);

struct BreathVertex {
    float4 clip_position : SV_Position;
    float2 uv : TEXCOORD0;
};

#ifdef VERTEX_STAGE
BreathVertex vs_main(VERTEX_ID_INPUT) {
    const int vertex_id = VERTEX_ID;
    const float2 corner = fullscreen_corner(vertex_id);
    BreathVertex o;
    o.clip_position = float4(corner, 0.0, 1.0);
    o.uv = float2(corner.x * 0.5 + 0.5, 1.0 - (corner.y * 0.5 + 0.5));
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
DEPTH2D(scene_depth, 0);

// Value noise in three dimensions, hashed: the modern tier has the
// arithmetic, and a jet covers too few pixels to want a texture.
float breath_hash(float3 p) {
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}
float breath_noise(float3 p) {
    float3 i = floor(p);
    float3 f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(breath_hash(i), breath_hash(i + float3(1.0, 0.0, 0.0)), f.x);
    float b = lerp(breath_hash(i + float3(0.0, 1.0, 0.0)), breath_hash(i + float3(1.0, 1.0, 0.0)), f.x);
    float c = lerp(breath_hash(i + float3(0.0, 0.0, 1.0)), breath_hash(i + float3(1.0, 0.0, 1.0)), f.x);
    float d = lerp(breath_hash(i + float3(0.0, 1.0, 1.0)), breath_hash(i + float3(1.0, 1.0, 1.0)), f.x);
    return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z);
}
// The spine at `p`. x: the distance to the nearest capsule's axis in that
// capsule's radii. y: its age there, 0 at the mouth to 1 at the tip.
// z: its radius there, metres. w: metres to the nearest capsule swollen by
// `swell` radii, negative inside: a bound a step outside may take.
// `from` and `to` bound the segments tried, and `nearest` returns the one
// nearest in metres: a ray moves from one segment to its neighbour, so
// after its first sample a march tries five segments and not fifteen.
// Nearest in metres, not in radii: far off, the widest capsule is the
// nearest in radii from anywhere, and a window about it stepped clean over
// the thin end at the jaws.
float4 breath_spine(float3 p, float swell, int from, int to, out int nearest) {
    float4 best = float4(1e6, 1.0, 1.0, 1e6);
    nearest = max(from, 0);
    int count = int(jet.params.z + 0.5);
    for (int i = 0; i < BREATH_NODES - 1; ++i) {
        if (i + 1 >= count) break;
        if (i < from || i > to || jet.info[i].y < 0.5) continue;
        float3 a = jet.nodes[i].xyz;
        float3 ab = jet.nodes[i + 1].xyz - a;
        float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-6));
        float r = lerp(jet.nodes[i].w, jet.nodes[i + 1].w, t);
        float d = length(p - (a + ab * t));
        float q = d / r;
        if (d - r * swell < best.w) {
            best.w = d - r * swell;
            nearest = i;
        }
        if (q < best.x) {
            best.x = q;
            best.y = lerp(jet.info[i].x, jet.info[i + 1].x, t);
            best.z = r;
        }
    }
    return best;
}

float4 fs_main(BreathVertex input) : SV_Target {
    float2 uv = input.uv;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float tan_half = scene.view_params.x;
    float3 ray = normalize(scene.camera_forward.xyz + scene.camera_right.xyz * ndc.x * tan_half * scene.view_params.y +
                           scene.camera_up.xyz * ndc.y * tan_half);
    float3 eye = scene.camera_position.xyz;

    // The whole jet is inside one sphere: most pixels leave here.
    float3 oc = eye - jet.bounds.xyz;
    float b = dot(oc, ray);
    float disc = b * b - (dot(oc, oc) - jet.bounds.w * jet.bounds.w);
    if (disc <= 0.0) discard;
    float root = sqrt(disc);
    float t0 = max(-b - root, 0.0);
    float t1 = -b + root;
    // Reversed Z on an infinite projection: depth = near / view distance.
    float depth = scene_depth.SampleLevel(scene_depth_sampler, uv, 0.0);
    if (depth > 1e-7) t1 = min(t1, jet.params.x / depth / max(dot(ray, scene.camera_forward.xyz), 1e-4));
    if (t1 <= t0) discard;
    // And inside one capsule: the nearest the ray comes to its axis. From
    // the chase camera the eye is in the sphere, and this is what spares
    // the rest of the screen.
    {
        float3 d1 = ray * (t1 - t0);
        float3 d2 = jet.hull_b.xyz - jet.hull_a.xyz;
        float3 r = (eye + ray * t0) - jet.hull_a.xyz;
        float a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
        float c = dot(d1, r), bb = dot(d1, d2);
        float denom = a * e - bb * bb;
        float sn = denom > 1e-6 ? saturate((bb * f - c * e) / denom) : 0.0;
        float tn = saturate((bb * sn + f) / max(e, 1e-6));
        sn = saturate((bb * tn - c) / max(a, 1e-6));
        float3 gap = r + d1 * sn - d2 * tn;
        if (dot(gap, gap) > jet.hull_a.w * jet.hull_a.w) discard;
    }

    float time = jet.params.y;
    // The noise can push the edge this far out, in radii (kBreathOut below).
    float swell = 1.0 + min(jet.shape.z, 0.45);
    float3 hot = jet.hot.rgb;
    float peak = max(max(hot.r, hot.g), max(hot.b, 1e-3));
    float3 pale = lerp(hot / peak, float3(1.0, 1.0, 1.0), 0.45) * peak * jet.colour.x;
    float3 body = hot * jet.colour.y;
    float3 fringe = jet.cool.rgb;

    // A fixed offset a pixel, so the steps' banding becomes grain.
    float dither = frac(52.9829189 * frac(dot(input.clip_position.xy, float2(0.06711056, 0.00583715))));
    float t = t0;
    float through = 1.0;
    float heat_sum = 0.0;
    float lit_sum = 0.0;
    bool entered = false;
    int steps = int(jet.params.w + 0.5);
    int inside = 0;
    int near_segment = 0;
    int from = 0, to = BREATH_NODES;
    // The loop's bound covers the approach too. A ray that runs beside the
    // jet, as every ray from the chase camera does, nears it by a fraction
    // of its distance a step; counted against the march's own steps, such a
    // ray spent them all on the way and the flame was not there from behind.
    for (int i = 0; i < 160; ++i) {
        if (inside >= steps || t >= t1 || through < 0.02) break;
        float3 p = eye + ray * t;
        // Every eighth sample tries the whole spine again, for a jet that
        // curls back on itself.
        if ((i & 7) == 0) {
            from = 0;
            to = BREATH_NODES;
        }
        float4 s = breath_spine(p, swell, from, to, near_segment);
        from = near_segment - 2;
        to = near_segment + 2;
        if (s.w > 0.0) {
            // Outside every capsule: its distance is a step that cannot
            // pass through one. The floor grows with the distance from the
            // eye, where a thin miss is less than a pixel.
            t += max(s.w * 0.95, 0.05 + 0.015 * t);
            continue;
        }
        ++inside;
        float ds = clamp(s.z * 0.22, 0.06, 2.6);
        if (!entered) {
            t += ds * dither;
            entered = true;
            p = eye + ray * t;
            s = breath_spine(p, swell, from, to, near_segment);
        }
        ds = min(ds, t1 - t);
        float age = s.y;
        float wear = 0.4 + 0.6 * age;
        // Past where the noise can carry the edge at this age: no gas, and
        // no need to read the noise to know it.
        float out_reach = min(jet.shape.z * wear, 0.45);
        if (s.x >= 1.0 + out_reach) {
            t += ds;
            continue;
        }

        // The noise moves down the jet with the gas, and rises a little. It
        // is stretched along the jet, so an eddy is a tongue and not a ball.
        float3 np = p - jet.axis.xyz * (time * jet.shape.w) - float3(0.0, time * 1.5, 0.0);
        np -= jet.axis.xyz * (dot(np, jet.axis.xyz) * 0.5);
        // Eddies the size of the jet where they are: a hand across at the
        // jaws, ten metres at the tip. The scale goes in octaves and two
        // neighbours are blended; one scale varying with the radius would
        // shear the field.
        float level = clamp(log2(max(s.z, 0.25) * 4.0), 0.0, 6.99);
        float blend = frac(level);
        float3 base = np * (exp2(-floor(level)) * 4.0 * jet.shape.y);
        // Four octaves read once. The three finest are this scale's fBm, the
        // three coarsest the next scale's, and the weights slide from one
        // to the other.
        float o0 = breath_noise(base * 0.5);
        float o1 = breath_noise(base);
        float o2 = breath_noise(base * 2.0);
        float o3 = breath_noise(base * 4.0);
        float n = (0.5 * blend * o0 + (0.5 - 0.25 * blend) * o1 + (0.25 - 0.125 * blend) * o2 +
                   0.125 * (1.0 - blend) * o3) / 0.875;
        n = saturate((n - 0.5) * 1.9 + 0.5);
        float fine = saturate((lerp(o3, o2, blend) - 0.5) * 1.8 + 0.5);
        // Tongues: the ridges of the finest octaves, bright lines where the
        // noise crosses its middle, running along the jet.
        float ridge = 1.0 - abs(lerp(o3, o2, blend) * 2.0 - 1.0);
        ridge = ridge * ridge;
        // A jet at the jaws, torn to rags by the tip: the broad noise moves
        // the edge, the fine one frays it and opens gaps in the body.
        float q = s.x + max((n - 0.5) * 2.0 * jet.shape.z * wear, -out_reach) + (fine - 0.5) * 0.3 * wear;
        float density = saturate((1.0 - q) * 5.0);
        density *= lerp(1.0, smoothstep(0.3, 0.62, fine), wear * 0.9);
        density *= 1.0 - smoothstep(0.72, 1.0, age);

        if (density > 0.001) {
            // Hot on the axis and at the jaws, cooling outward and along,
            // and hot in the tongues: between them the body is a deep
            // orange, so the flame has lines in it and not one glow.
            float heat = saturate(1.0 - q * 1.1) * pow(1.0 - age, 1.25);
            heat *= 0.3 + 1.5 * ridge * (0.5 + 0.5 * fine);
            // Soot: past half its life the cool gas goes dark and still hides.
            float lit = 1.0 - 0.9 * jet.colour.z * smoothstep(0.5, 1.0, age) * (1.0 - saturate(heat * 3.0));
            // The gas thins as it spreads: as opaque across at any width.
            float kept = exp(-density * jet.shape.x * min(1.0, 1.5 / s.z) * ds);
            float weight = through * (1.0 - kept);
            heat_sum += weight * heat;
            lit_sum += weight * lit;
            through *= kept;
        }
        t += ds;
    }
    float cover = 1.0 - through;
    if (cover < 0.003) discard;
    // The colour is read off the mean heat the ray met, through a ramp with
    // steep steps, and the edge off the cover the same way. Summing each
    // step's own colour gave the right hues and no shapes: every boundary
    // in the gas was averaged along the ray into a glow. A ramp on the mean
    // puts an edge where the mean crosses a step, so the tongues and the
    // silhouette are lines.
    float mean_heat = heat_sum / cover;
    float3 emission = lerp(fringe * 0.55, body, smoothstep(0.04, 0.17, mean_heat));
    emission = lerp(emission, pale, smoothstep(0.28, 0.5, mean_heat));
    emission *= (lit_sum / cover) * jet.colour.w;
    return float4(emission, smoothstep(0.06, 0.45, cover));
}
#endif
