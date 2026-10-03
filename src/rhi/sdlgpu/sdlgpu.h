#pragma once

// The SDL GPU backend's native handles, for the one consumer that has to see
// them: Dear ImGui's renderer (imgui_impl_sdlgpu3), which records into SDL's
// own command buffer and render pass. Nothing else in the engine includes
// this file; a D3D9 build swaps it for imgui_impl_dx9 and its own handles.

#include <SDL3/SDL_gpu.h>

#include "rhi/rhi.h"

namespace rhi::sdlgpu {

SDL_GPUDevice* native_device(Device& device);
SDL_GPUCommandBuffer* native_command_buffer(Device& device);
SDL_GPURenderPass* native_pass(Pass* pass);
SDL_GPUTextureFormat native_format(Format format);

}  // namespace rhi::sdlgpu
