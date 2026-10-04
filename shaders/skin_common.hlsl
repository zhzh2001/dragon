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
};
#define SKIN_JOINT(skin, j) (skin).joints[j]
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
