#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>

namespace gfx {

// Uploads `size` bytes to a new GPU buffer via a staging transfer buffer.
// Synchronous: submits its own copy pass and waits, so it is for load-time use
// only, never per-frame. Returns nullptr on failure.
SDL_GPUBuffer* create_buffer_with_data(SDL_GPUDevice* gpu, const void* data, uint32_t size,
                                       SDL_GPUBufferUsageFlags usage, const char* debug_name);

}  // namespace gfx
