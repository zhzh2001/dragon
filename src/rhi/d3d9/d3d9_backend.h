#pragma once

// The Direct3D 9 backend (docs/PORTING.md, R3). Windows only. It is created by
// rhi::Device::create when the driver asked for is "direct3d9"
// (--gpu-driver direct3d9), on the SDL window's HWND.
//
// What D3D9 lacks is met here rather than in the renderers:
//   - shaders are the baked vs_3_0/ps_3_0 bytecode tools/d3d9/bake_d3d9.py
//     writes, with a JSON register map per shader: a uniform block pushed by
//     slot lands at its base constant register;
//   - a depth texture the renderers sample (the shadow map) is an R32F colour
//     target plus a depth surface, and the depth-only shaders write z/w into
//     it (shaders/common.hlsl, DEPTH_ONLY_FRAGMENT);
//   - a pipeline with no vertex attributes (a fullscreen triangle) is fed its
//     vertex index from a small buffer of the backend's own (VERTEX_ID_INPUT);
//   - RGBA8 data is uploaded as A8R8G8B8, swizzled on the way in and on
//     readback; sRGB is a sampler state, set from the texture's format.

#include <d3d9.h>

#include <memory>

#include "rhi/rhi.h"

namespace rhi {
namespace d3d9 {

std::unique_ptr<Device> create(SDL_Window* window, const DeviceConfig& config);

// For Dear ImGui's D3D9 renderer (editor/imgui_layer.cpp), the one caller.
IDirect3DDevice9* native_device(Device& device);

}  // namespace d3d9
}  // namespace rhi
