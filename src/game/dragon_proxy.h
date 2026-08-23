#pragma once

#include "gfx/mesh.h"

namespace game {

// Dimensions of the placeholder dragon. The vertex shader's wing deformation
// reads the same numbers, so they are shared rather than duplicated.
struct DragonProxyDims {
    float body_length = 9.0f;    // nose to tail base
    float body_radius = 1.15f;
    float neck_length = 2.6f;
    float tail_length = 7.0f;
    float wing_root = 0.9f;      // |x| where the wing meets the body
    float wing_span = 11.0f;     // |x| at the wingtip
    float wing_chord = 4.6f;     // front-to-back at the root
    float wing_tip_chord = 1.5f;
};

// Builds a greybox dragon: tapered body, neck and head, tail, and two wings.
// Deliberately crude but strongly asymmetric front-to-back and coloured per
// part, because the only job it has right now is to make the dragon's
// orientation unmistakable while the flight model is being tuned.
gfx::MeshData make_dragon_proxy(const DragonProxyDims& dims = {});

// Height of the wing hinge above the body centreline. The vertex shader rotates
// the wing about this line, so it must match what the mesh was built with.
inline float dragon_wing_hinge_y(const DragonProxyDims& dims) {
    return dims.body_radius * 0.45f;
}

}  // namespace game
