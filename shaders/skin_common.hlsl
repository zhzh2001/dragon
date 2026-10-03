// The joint palette and the blend, shared by skinned.hlsl and its shadow pass.
#pragma once

#ifdef PACKED_JOINTS
// A retro tier (gfx/render_tier.h): each joint as the top three rows of its
// matrix -- the fourth is always 0 0 0 1 for a rig's affine transforms -- so a
// joint costs three float4 constants, not four. D3D9's vertex shaders have 256
// constants in all: 64 packed joints take 192, beside the scene and model
// blocks, where 60 full matrices would not fit. The tier's palette split
// (anim/skin_partition.h) keeps every draw within PACKED_JOINT_LIMIT.
static const int PACKED_JOINT_LIMIT = 64;
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
};
#define SKIN_JOINT(skin, j) (skin).joints[j]
#endif

// The joint indices' vertex input. A UBYTE4 attribute is an integer vector on
// the modern APIs, but D3D9's shader models have no integer inputs: it arrives
// as floats 0..255.
#ifdef D3D9
#define JOINT_INDEX_TYPE float4
#else
#define JOINT_INDEX_TYPE uint4
#endif

// Linear blend skinning. Weights are normalized on the CPU, so no rescaling
// is needed here. The indices are converted to int: vs_3_0 has no unsigned
// arithmetic.
#define SKIN_MATRIX(skin, joint_index, weight)                       \
    (SKIN_JOINT(skin, int4(joint_index).x) * (weight).x +            \
     SKIN_JOINT(skin, int4(joint_index).y) * (weight).y +            \
     SKIN_JOINT(skin, int4(joint_index).z) * (weight).z +            \
     SKIN_JOINT(skin, int4(joint_index).w) * (weight).w)
