// The trees' impostors, baked (gfx::Foliage::bake_impostors, docs/FIDELITY.md
// F7): a tree kind's whole crown, every card, drawn once from each of 8 x 8
// directions over the upper hemisphere into its frame of an atlas, by an
// orthographic camera the C++ builds per frame. Two passes over the same
// frames: the albedo -- the palette's colour with the card's mottle and the
// dark undersides, as foliage.hlsl colours a crown before it lights it --
// and the normal, turned toward the camera as foliage.hlsl turns it, with
// whether the surface lets the sun through. foliage_impostor.hlsl lights
// them in the scene. The modern tier's alone.
#include "scene_common.hlsl"

struct BakeUniforms {
    float4x4 view_proj;  // the frame's orthographic camera
    float4 eye;          // xyz toward the camera (unit), w 1 for the normal pass
    float4 sphere;       // xyz the crown's middle, w the radius holding it (m)
    float4 shade;        // x how dark the crown's depths are baked (0: not at all)
};
ConstantBuffer<BakeUniforms> bake : UNIFORM_SLOT(1);

struct VertexIn {
    float3 position       : TEXCOORD0;
    float3 normal         : TEXCOORD1;
    float3 color          : TEXCOORD2;
    float2 uv             : TEXCOORD3;
    float4 position_scale : TEXCOORD4;
    float4 params         : TEXCOORD5;
};

struct VertexOut {
    float4 clip_position : SV_Position;
    float3 normal : TEXCOORD0;
    float3 albedo : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float material : TEXCOORD3;
    float3 position : TEXCOORD4;
};

static const int FOLIAGE_MAT_LEAF = 1;
static const int FOLIAGE_MAT_NEEDLE = 2;
static const int FOLIAGE_MAT_BARK = 3;
static const int FOLIAGE_DETAIL = 10;

#ifdef VERTEX_STAGE
VertexOut vs_main(VertexIn input) {
    VertexOut o;
    // The tree at the origin, unscaled and unturned: the instance is
    // ignored, the runtime applies its own.
    o.clip_position = mul(bake.view_proj, float4(input.position, 1.0));
    o.normal = input.normal;
    int entry = int(clamp(input.color.x, 0.0, float(PALETTE_COUNT - 1)) + 0.5);
    o.albedo = scene.palette[entry].rgb * input.color.y;
    o.uv = input.uv;
    o.position = input.position;
    int tag = int(input.color.z + 0.5);
    o.material = float(tag >= FOLIAGE_DETAIL ? tag - FOLIAGE_DETAIL : tag);
    return o;
}
#endif

#ifdef FRAGMENT_STAGE
TEXTURE2D(leaf_card, 0);
TEXTURE2D(needle_card, 1);
#define NOISE_SLOT 2  // unused: the modern tier hashes its noise
#include "noise.hlsl"

float4 fs_main(VertexOut input) : SV_Target {
    int material = int(input.material + 0.5);
    float3 albedo = input.albedo;
    float3 n = normalize(input.normal);
    // Both cards are read on every pixel, so neither slot drops out of the
    // shader's reflected count.
    float4 leaf = leaf_card.Sample(leaf_card_sampler, input.uv);
    float4 needle = needle_card.Sample(needle_card_sampler, input.uv);
    float translucent = 0.0;
    if (material == FOLIAGE_MAT_LEAF || material == FOLIAGE_MAT_NEEDLE) {
        float4 card = material == FOLIAGE_MAT_LEAF ? leaf : needle;
        if (card.a < 0.45) discard;
        albedo *= card.r * 2.2;
        albedo *= 1.0 - 0.35 * saturate(-n.y);
        translucent = 1.0;
    } else if (material == FOLIAGE_MAT_BARK) {
        float streak = value_noise(float2(input.uv.x * 14.0, input.uv.y * 1.5));
        float fine = value_noise(float2(input.uv.x * 40.0, input.uv.y * 6.0));
        albedo *= 0.7 + 0.5 * streak + 0.15 * (fine - 0.5);
    }
    // A read of `scene` that changes nothing (tan of half the field of view
    // is positive), so slot 0 stays in this stage's reflection. Without it
    // the stage declared one uniform block, SDL bound only slot 0, and
    // `bake` at slot 1 was read from an unbound buffer: Metal's validation
    // stops on it, and without validation the bake read whatever was there.
    albedo *= 1.0 + min(scene.view_params.x, 0.0);
    if (dot(n, bake.eye.xyz) < 0.0) n = -n;
    if (bake.eye.w > 0.5) return float4(n * 0.5 + 0.5, translucent);
    // The crown's depths, darker: in the crown itself the cards inside are
    // in the outer cards' shadow and see little sky, and an impostor lit as
    // one flat surface came out lighter than the crowns it stands in for.
    float depth = dot(input.position - bake.sphere.xyz, bake.eye.xyz) / max(bake.sphere.w, 1e-3);
    albedo *= lerp(1.0 - bake.shade.x, 1.0, saturate(depth * 0.8 + 0.6));
    return float4(albedo, 1.0);
}
#endif
