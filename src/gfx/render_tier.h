#pragma once

// The rendering tier: what the content is shaped for (docs/PORTING.md, R2).
//
// One Direct3D 9 backend will serve three tiers of hardware -- shader model 3,
// shader model 2, fixed function -- and the modern build is a fourth. A tier
// is the set of content choices that hardware forces: how many joints a draw
// can address, which depth convention survives a 24-bit buffer, whether the
// scene can be HDR, how big a texture may be. Those choices are made here,
// once, and every backend renders them; `--tier` picks one on any backend,
// which is how the retro content is checked on the Mac before there is a D3D9
// device to show it.
//
// Each field records what a tier gets so far. R2 fills them in one at a time;
// a field that no code reads yet says so.

#include <cstdint>
#include <string>

namespace gfx {

enum class Tier : uint8_t { Modern, SM3, SM2, FixedFunction };

// How the main pass stores depth. Reversed-Z with an infinite far plane wants
// a float depth buffer; D3D9-class hardware has a 24-bit fixed-point one,
// where reversed-Z buys nothing, so the retro tiers use the conventional
// layout with a finite far plane and a near plane pushed out. 0.5 m to 16 km
// in 24 bits resolves about 3 m at 5 km (dz ~ d^2 / (near * 2^24)), coarser
// than the river's banks but finer than anything coplanar at that distance.
struct DepthConvention {
    bool reversed = true;
    float near_min = 0.0f;  // the camera's own near plane, raised to at least this
    // 0: infinite (reversed only). Not `near`/`far`: Windows headers define
    // both as empty macros.
    float far_plane = 0.0f;
};

struct RenderTier {
    Tier tier = Tier::Modern;
    // Joints one skinned draw may address (anim/skin_partition.h). The modern
    // uniform block holds 256. A D3D9 vertex shader has 256 float4 constants,
    // a joint costs three (4x3), and the scene and model blocks need ~45:
    // 60 joints fit vs_3_0 with room, and SM2 keeps 50 for its longer
    // material constants. Fixed function will skin on the CPU (R5); until then
    // it previews through the sm2 shaders, so it takes sm2's palette.
    uint32_t max_skin_bones = 256;
    // The main pass's depth layout (see DepthConvention).
    DepthConvention depth;
    // Whether the world renders linear light into a 16-bit float target for
    // the post stack (bloom, the full grade). SM2 and fixed-function cards have
    // no float render target, so their world shaders tonemap and grade in
    // place (LDR_OUTPUT, scene_common.hlsl) straight into the 8-bit target,
    // and there is no bloom. SM3 keeps it: its floor cards render FP16 (an
    // X1300 cannot filter it, which R3 has to meet with point sampling).
    bool hdr = true;
    // Whether value noise reads its lattice from a texture (BAKED_NOISE,
    // gfx/noise_lattice.h) instead of hashing four corners per lookup: every
    // retro tier, since the hashed terrain shader is hundreds of instructions.
    // Fixed function has no pixel shader; its preview bakes like the rest.
    bool baked_noise = false;
    // The texture budget (gfx/texture.h): the largest edge a texture keeps,
    // 0 for no cap, and whether textures are block-compressed (DXT1 and
    // DXT5, normal maps as DXT5nm, which the shaders read as
    // SWIZZLED_NORMALS). The cards on hand have 128 to 256 MB; the roster's
    // 4096^2 maps are 85 MB each with mips before the cap.
    uint32_t max_texture_size = 0;
    bool compress_textures = false;
    // The most triangles a skinned mesh keeps (anim/skin_lod.h), 0 for no
    // cap: the roster's creatures are 60 to 90 thousand. The table in
    // PORTING.md budgets about 20K at sm3, 10K at sm2 and 6K for fixed
    // function, whose CPU skinning pays per vertex.
    uint32_t max_skinned_triangles = 0;
    // Whether a draw's joint palette is packed as three rows a joint
    // (PACKED_JOINTS, shaders/skin_common.hlsl): D3D9's 256 vertex constants
    // hold 64 such joints beside the scene block, and no full matrices.
    bool packed_joints = false;
    // Lighting per vertex (SM2, shaders/scene_common.hlsl): ps_2_0's 64
    // arithmetic instructions hold a material's albedo, one shadow lookup,
    // the fog and the tonemap, not the per-pixel lighting the others do.
    // Fixed function previews through the same shaders until R5.
    bool vertex_lighting = false;
    // The shadow map's edge, 0 for no cap: PORTING.md's table budgets 1024
    // for sm3 and 512 for sm2. The cap trades sharpness near the camera, since
    // the map still covers the same area.
    uint32_t max_shadow_size = 0;
    // The terrain grid, as a multiple of the settings' cell size (6 m): the
    // modern mesh is 1.7 million triangles, and an X550 draws some 4 million
    // a second through SM2's per-vertex lighting. 2 at sm3 (12 m cells), 3 at
    // sm2 (18 m). The skirt past the map scales alike. Height queries read
    // the rendered mesh, so the ground the dragon lands on is the one drawn.
    float terrain_cell_scale = 1.0f;
    // Whether the terrain draws into the shadow map. sm2's map covers +-256 m
    // and the terrain is its largest caster by far (1.4 million triangles at
    // the modern grid), so the ground stops shadowing itself there; plants,
    // props and creatures still cast onto it.
    bool terrain_casts_shadows = true;
    // The plants' budget. After the terrain, the foliage is the vertex load: a
    // million triangles a frame at the modern settings. Trees are drawn to
    // this distance (0: the setting's own), planted this much sparser, and
    // the grass reaches this fraction of its radius.
    float tree_draw_distance = 0.0f;
    float tree_spacing_scale = 1.0f;
    float grass_radius_scale = 1.0f;
    // The rocks (gfx/mesh_lod.h): at most this many triangles each, 0 for
    // the scan's own, drawn to this fraction of their distances.
    uint32_t max_rock_triangles = 0;
    float rock_distance_scale = 1.0f;
    // A skinned mesh's distance LOD (anim::SkinnedMesh::upload_far): this
    // many triangles past this many metres; 0 triangles for none.
    uint32_t far_skinned_triangles = 0;
    float far_skinned_distance = 0.0f;

    static RenderTier make(Tier tier) {
        RenderTier t;
        t.tier = tier;
        switch (tier) {
            case Tier::Modern: break;
            case Tier::SM3: t.max_skin_bones = 60; break;
            case Tier::SM2: t.max_skin_bones = 50; break;
            case Tier::FixedFunction: t.max_skin_bones = 50; break;
        }
        if (tier != Tier::Modern) t.depth = DepthConvention{false, 0.5f, 16000.0f};
        t.hdr = tier == Tier::Modern || tier == Tier::SM3;
        t.baked_noise = tier != Tier::Modern;
        t.max_texture_size = tier == Tier::Modern ? 0 : tier == Tier::SM3 ? 1024 : 512;
        t.compress_textures = tier != Tier::Modern;
        t.packed_joints = tier != Tier::Modern;
        t.vertex_lighting = tier == Tier::SM2 || tier == Tier::FixedFunction;
        t.max_shadow_size = tier == Tier::Modern ? 0 : tier == Tier::SM3 ? 1024 : 512;
        t.terrain_cell_scale = tier == Tier::Modern ? 1.0f : tier == Tier::SM3 ? 2.0f : 3.0f;
        t.terrain_casts_shadows = !t.vertex_lighting;
        if (tier == Tier::SM3) {
            t.tree_draw_distance = 2500.0f;
            t.grass_radius_scale = 0.7f;
            t.max_rock_triangles = 400;
            t.far_skinned_triangles = 3000;
            t.far_skinned_distance = 150.0f;
        } else if (tier != Tier::Modern) {
            t.tree_draw_distance = 800.0f;
            t.tree_spacing_scale = 1.8f;
            t.grass_radius_scale = 0.45f;
            t.max_rock_triangles = 150;
            t.rock_distance_scale = 0.5f;
            t.far_skinned_triangles = 1500;
            t.far_skinned_distance = 100.0f;
        }
        switch (tier) {
            case Tier::Modern: break;
            case Tier::SM3: t.max_skinned_triangles = 20000; break;
            case Tier::SM2: t.max_skinned_triangles = 10000; break;
            case Tier::FixedFunction: t.max_skinned_triangles = 6000; break;
        }
        return t;
    }

    // "modern", "sm3", "sm2", "ff"; false for anything else.
    static bool parse(const std::string& name, Tier* out) {
        if (name == "modern") *out = Tier::Modern;
        else if (name == "sm3") *out = Tier::SM3;
        else if (name == "sm2") *out = Tier::SM2;
        else if (name == "ff") *out = Tier::FixedFunction;
        else return false;
        return true;
    }

    const char* name() const {
        switch (tier) {
            case Tier::Modern: return "modern";
            case Tier::SM3: return "sm3";
            case Tier::SM2: return "sm2";
            case Tier::FixedFunction: return "ff";
        }
        return "modern";
    }
};

// The tier in force: process-wide, set once at start-up before the device
// makes its targets, any camera projects or any pipeline is built. Inline with
// one shared instance, so the renderer-free suites that include a camera need
// nothing extra linked.
inline RenderTier& active_tier_storage() {
    static RenderTier tier;
    return tier;
}
inline const RenderTier& active_tier() { return active_tier_storage(); }
inline void set_active_tier(const RenderTier& tier) { active_tier_storage() = tier; }
inline const DepthConvention& depth_convention() { return active_tier().depth; }

}  // namespace gfx
