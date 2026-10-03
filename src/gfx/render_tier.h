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

struct RenderTier {
    Tier tier = Tier::Modern;
    // Joints one skinned draw may address (anim/skin_partition.h). The modern
    // uniform block holds 256. A D3D9 vertex shader has 256 float4 constants,
    // a joint costs three (4x3), and the scene and model blocks need ~45:
    // 60 joints fit vs_3_0 with room, and SM2 keeps 50 for its longer
    // material constants. Fixed function skins on the CPU (R5), so its value
    // is the CPU path's, not a palette.
    uint32_t max_skin_bones = 256;

    static RenderTier make(Tier tier) {
        RenderTier t;
        t.tier = tier;
        switch (tier) {
            case Tier::Modern: break;
            case Tier::SM3: t.max_skin_bones = 60; break;
            case Tier::SM2: t.max_skin_bones = 50; break;
            case Tier::FixedFunction: t.max_skin_bones = 256; break;
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

}  // namespace gfx
