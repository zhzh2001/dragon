#pragma once

// The Direct3D 11 backend (docs/PORTING.md, "D3D11"). Windows only. Created by
// rhi::Device::create when the driver asked for is "direct3d11" (or
// "direct3d11:N" for DXGI adapter N), on the SDL window's HWND.
//
// Why it exists beside SDL's D3D12 and Vulkan: the cards that have neither --
// the Radeon HD 5450 (TeraScale), the Bay Trail IGP, the G41's GMA X4500 --
// run feature level 10 or 11 through this, at the modern tier; and WARP, the
// software rasteriser every Windows has, comes with it. Feature levels 9_x
// are a later step (the D3D9 tiers' shaders through level_9_x profiles).
//
// Shaders are the baked DXBC tools/d3d11/bake_d3d11.py writes (vs/ps_5_0 for
// feature level 11, 4_0 for 10), loaded by the same base name the other
// backends use. Everything else maps directly: a uniform block pushed to
// slot N is constant buffer bN of that stage, a fragment texture in slot N is
// tN with sampler sN, a pipeline is a vertex and pixel shader plus an input
// layout and the three state objects.

#include <d3d11.h>

#include <memory>

#include "rhi/rhi.h"

namespace rhi {
namespace d3d11 {

std::unique_ptr<Device> create(SDL_Window* window, const DeviceConfig& config);

// For Dear ImGui's D3D11 renderer (editor/imgui_layer.cpp), the one caller.
ID3D11Device* native_device(Device& device);
ID3D11DeviceContext* native_context(Device& device);

}  // namespace d3d11
}  // namespace rhi
