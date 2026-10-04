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
// feature level 11, 4_0 for 10, 4_0_level_9_1 for every 9_x, from the sm2
// tier's variant with LEVEL9 defined), loaded by the same base name
// the other backends use. At feature level 9 the backend also does what the
// D3D9 one does for the same cards: a vertex-id buffer for the fullscreen
// passes, an R32F colour stand-in for a sampled depth target, and one draw
// per instance below 9_3. Everything else maps directly: a uniform block pushed to
// slot N is constant buffer bN of that stage, a fragment texture in slot N is
// tN with sampler sN, a pipeline is a vertex and pixel shader plus an input
// layout and the three state objects.

#include <cstdint>
#include <memory>

#include "rhi/rhi.h"

// Forward-declared, not <d3d11.h>: that header brings <windows.h>, whose
// `near` and `far` macros break any file that names a variable so (app.cpp
// does). The backend's .cpp and Dear ImGui's glue include the real thing.
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace rhi {
namespace d3d11 {

std::unique_ptr<Device> create(SDL_Window* window, const DeviceConfig& config);

// The feature level DXGI adapter `adapter` reaches, as D3D_FEATURE_LEVEL's
// value (0x9200 for 9_2, 0xb000 for 11_0), or 0 when no device can be made.
// The app asks before it fixes the render tier: a feature level 9 device runs
// the sm2 tier (9_3 included: its pixel shaders are still ps_2_x-sized).
uint32_t probe_feature_level(uint32_t adapter);

// For Dear ImGui's D3D11 renderer (editor/imgui_layer.cpp), the one caller.
ID3D11Device* native_device(Device& device);
ID3D11DeviceContext* native_context(Device& device);

}  // namespace d3d11
}  // namespace rhi
