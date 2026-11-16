// The joint palette and the blend, shared by skinned.hlsl and its shadow pass.
#pragma once

#ifdef PACKED_JOINTS
// A retro tier (gfx/render_tier.h): each joint as the top three rows of its
// matrix -- the fourth is always 0 0 0 1 for a rig's affine transforms -- so a
// joint costs three float4 constants, not four. D3D9's vertex shaders have 256
// constants in all: 64 packed joints take 192, beside the scene and model
// blocks, where 60 full matrices would not fit. The tier's palette split
// (anim/skin_partition.h) keeps every draw within PACKED_JOINT_LIMIT.
#if defined(LEVEL9)
// Feature level 9 on D3D11 loads a cbuffer whole into D3D9's register file:
// the sm2 tier's 50-joint palette is declared at its own size there, since
// 64 joints' 192 registers beside the other blocks put both the X550's and
// the 5060 Ti's drivers into an internal error at the first skinned draw.
static const int PACKED_JOINT_LIMIT = 50;
#else
static const int PACKED_JOINT_LIMIT = 64;
#endif
struct SkinUniforms {
    float4 rows[PACKED_JOINT_LIMIT * 3];
};
#define SKIN_JOINT(skin, j) float4x4((skin).rows[(j) * 3], (skin).rows[(j) * 3 + 1], (skin).rows[(j) * 3 + 2], float4(0.0, 0.0, 0.0, 1.0))
#else
// Joint matrices arrive as a uniform array rather than a storage buffer: 16 KB
// per draw, which avoids maintaining a per-frame buffer. Must match
// anim::MAX_JOINTS.
static const int MAX_JOINTS = 256;
struct SkinUniforms {
    float4x4 joints[MAX_JOINTS];
    // x how far the membranes bow, as a share of a panel's width (signed: up
    // under load), y the widest panel in the mesh's units. Zero: flat.
    float4 membrane;
};
#define SKIN_JOINT(skin, j) (skin).joints[j]
#define MEMBRANE_BULGE
#endif

// A vertex's membrane share from the tangent's w (App::mark_membranes): the
// top three bits of |w| - 1. The lower bits are the panel's reach and slope,
// which only membrane_bulge reads.
float membrane_share(float tangent_w) {
    return floor((abs(tangent_w) - 1.0) * 8.0) / 7.0;
}

// Where a membrane vertex sits in its panel: 0 on a spar, 1 in the middle of
// the widest panel. The sheet is thinnest there.
float membrane_panel(float tangent_w) {
    float n = (abs(tangent_w) - 1.0) * 8.0;
    return floor((n - floor(n)) * 32.0) / 31.0;
}

#ifdef MEMBRANE_BULGE
// The membrane bows between its spars (docs/showcase/HERO.md, "A membrane
// that billows"). A vertex carries how far its place in the panel moves at
// full bow (the panel's width at its middle, nothing on a finger) and the
// slope of that across the wing, both measured in the bind pose seen from
// above. The bow is along the bind pose's up, carried by the vertex's own
// bones, so it follows a fold. `offset` is added to the skinned position,
// `tilt` to the skinned normal, both in the model's space.
void membrane_bulge(float4x4 blended, float3 skinned_normal, float tangent_w, float4 membrane,
                    out float3 offset, out float3 tilt) {
    offset = (float3)0.0;
    tilt = (float3)0.0;
    if (membrane.x == 0.0 || membrane.y <= 0.0) return;
    float n = (abs(tangent_w) - 1.0) * 4194304.0;
    const float share = floor(n / 524288.0);
    n -= share * 524288.0;
    const float reach = floor(n / 16384.0);
    n -= reach * 16384.0;
    const float slope_x = floor(n / 128.0);
    const float slope_z = n - slope_x * 128.0;
    if (share < 0.5) return;
    const float amount = membrane.x * share / 7.0;
    const float3 up = normalize(mul(blended, float4(0.0, 1.0, 0.0, 0.0)).xyz);
    offset = up * (amount * membrane.y * reach / 31.0);
    const float2 slope = (float2(slope_x, slope_z) / 127.0 - 0.5) * 8.0;
    const float3 across = mul(blended, float4(slope.x, 0.0, slope.y, 0.0)).xyz;
    const float3 normal = normalize(skinned_normal);
    // The upper face's normal is up minus the bow's slope; the lower face's
    // is its opposite, which the normal's own side of `up` supplies.
    tilt = -across * (amount * dot(normal, up));
}
#endif

// The joint indices' vertex input. A UBYTE4 attribute is an integer vector on
// the modern APIs, but D3D9's shader models have no integer inputs: it arrives
// as floats 0..255. Direct3D 11 at feature level 9 (LEVEL9) has the same
// shader models and type-checks the input layout against the shader, so the
// backend feeds the bytes as R8G8B8A8_UNORM, 0..1, and the index is scaled
// back here.
#if defined(D3D9)
#define JOINT_INDEX_TYPE float4
#define JOINT_INDEX_INT(j) int4(j)
#elif defined(LEVEL9)
#define JOINT_INDEX_TYPE float4
#define JOINT_INDEX_INT(j) int4((j) * 255.0 + 0.5)
#else
#define JOINT_INDEX_TYPE uint4
#define JOINT_INDEX_INT(j) int4(j)
#endif

// Linear blend skinning. Weights are normalized on the CPU, so no rescaling
// is needed here. The indices are converted to int: vs_3_0 has no unsigned
// arithmetic.
#define SKIN_MATRIX(skin, joint_index, weight)                                  \
    (SKIN_JOINT(skin, JOINT_INDEX_INT(joint_index).x) * (weight).x +            \
     SKIN_JOINT(skin, JOINT_INDEX_INT(joint_index).y) * (weight).y +            \
     SKIN_JOINT(skin, JOINT_INDEX_INT(joint_index).z) * (weight).z +            \
     SKIN_JOINT(skin, JOINT_INDEX_INT(joint_index).w) * (weight).w)
