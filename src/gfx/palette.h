#pragma once

#include "core/math.h"

namespace gfx {

// The world's colours, named, in one place, pushed to every shader as uniforms.
//
// Half of the style seam in the early captures was that trees were coloured
// by constants in the tree generator, the ground by constants in the terrain
// shader, and the two had never been looked at side by side. With every colour
// here, a tree green can be dragged toward the ground green while flying, and
// a grade applied to the table grades the whole world.
//
// Values are LINEAR (the shared tonemap gamma-encodes at the end), restrained
// and slightly desaturated on purpose: the target is painted realism in late
// afternoon light, and saturation is what the sun and the grade add, not the
// albedo. Plant meshes do not carry colours at all any more -- their vertex
// colour is (entry index, brightness, 0), resolved against this table in the
// foliage shader, so a plant recolours with the table.
//
// Must match PALETTE_* in shaders/palette.msl exactly, in order.
enum class PaletteEntry : int {
    // Terrain materials, by height and slope.
    Sand = 0,
    Grass,
    GrassDry,
    Rock,
    RockDark,
    Snow,
    // Plants.
    Bark,
    Spruce,
    Pine,
    Broadleaf,
    Deadwood,
    GrassBlade,
    Reed,
    Bush,
    // Light and water.
    GroundBounce,  // the warm light the ground throws back up under things
    PlantWarm,     // multiplier the per-plant variation pushes a crown toward
    WaterDeep,
    Count
};

constexpr int PALETTE_COUNT = int(PaletteEntry::Count);

struct Palette {
    core::Vec4 colors[PALETTE_COUNT] = {
        core::Vec4{0.50f, 0.44f, 0.33f, 0.0f},  // Sand
        core::Vec4{0.15f, 0.21f, 0.10f, 0.0f},  // Grass
        core::Vec4{0.26f, 0.25f, 0.14f, 0.0f},  // GrassDry
        core::Vec4{0.26f, 0.23f, 0.20f, 0.0f},  // Rock
        core::Vec4{0.15f, 0.13f, 0.12f, 0.0f},  // RockDark
        core::Vec4{0.86f, 0.88f, 0.93f, 0.0f},  // Snow
        core::Vec4{0.20f, 0.14f, 0.09f, 0.0f},  // Bark
        core::Vec4{0.06f, 0.11f, 0.06f, 0.0f},  // Spruce: near-black green
        core::Vec4{0.10f, 0.17f, 0.09f, 0.0f},  // Pine
        core::Vec4{0.16f, 0.22f, 0.09f, 0.0f},  // Broadleaf: yellow-green, before the warm push
        core::Vec4{0.42f, 0.38f, 0.32f, 0.0f},  // Deadwood
        core::Vec4{0.17f, 0.24f, 0.10f, 0.0f},  // GrassBlade
        core::Vec4{0.30f, 0.34f, 0.15f, 0.0f},  // Reed
        core::Vec4{0.11f, 0.18f, 0.08f, 0.0f},  // Bush
        core::Vec4{0.36f, 0.28f, 0.18f, 0.0f},  // GroundBounce
        core::Vec4{1.20f, 1.10f, 0.75f, 0.0f},  // PlantWarm
        core::Vec4{0.04f, 0.09f, 0.10f, 0.0f},  // WaterDeep
    };

    core::Vec4& operator[](PaletteEntry entry) { return colors[int(entry)]; }
    const core::Vec4& operator[](PaletteEntry entry) const { return colors[int(entry)]; }

    static const char* name(int index) {
        static const char* NAMES[PALETTE_COUNT] = {
            "sand", "grass", "grass dry", "rock", "rock dark", "snow", "bark", "spruce",
            "pine", "broadleaf", "deadwood", "grass blade", "reed", "bush",
            "ground bounce", "plant warm", "water deep"};
        return NAMES[index];
    }
};

// Vertex colour for a plant mesh: which palette entry, and how bright.
inline core::Vec3 palette_vertex(PaletteEntry entry, float brightness = 1.0f) {
    return core::Vec3{float(int(entry)), brightness, 0.0f};
}
// Brighten or darken a palette vertex colour without touching its entry.
inline core::Vec3 palette_dim(core::Vec3 color, float factor) {
    return core::Vec3{color.x, color.y * factor, color.z};
}

}  // namespace gfx
