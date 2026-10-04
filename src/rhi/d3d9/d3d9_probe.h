#pragma once

// What a D3D9 adapter's caps allow, asked before the render tier is fixed
// (app.cpp): no <d3d9.h> here, since <windows.h> defines `near` and `far`.

#include <cstdint>

namespace rhi {
namespace d3d9 {

struct ShaderCaps {
    bool valid = false;           // false: no D3D9 or no such adapter (an SSH session, say)
    uint32_t vertex_shader = 0;   // major version: 3, 2, 1, or 0 for none
    uint32_t pixel_shader = 0;
    bool hardware_tnl = false;
};

ShaderCaps probe_shader_caps(uint32_t adapter);

}  // namespace d3d9
}  // namespace rhi
