#pragma once

#include "core/math.h"

namespace gfx {

// Per-instance data for one plant. Shared between the placement code (which
// writes it) and the renderer (which streams it to the GPU as-is), and it
// must match the instance attributes in foliage.hlsl.
struct FoliageInstance {
    core::Vec4 position_scale;  // xyz world position of the base, w scale
    core::Vec4 params;          // x yaw (radians), y shade, z sway phase, w unused
};

}  // namespace gfx
