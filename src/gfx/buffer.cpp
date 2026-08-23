#include "gfx/buffer.h"

#include <SDL3/SDL.h>

#include <cstring>

#include "core/log.h"

namespace gfx {

SDL_GPUBuffer* create_buffer_with_data(SDL_GPUDevice* gpu, const void* data, uint32_t size,
                                       SDL_GPUBufferUsageFlags usage, const char* debug_name) {
    if (size == 0) return nullptr;

    SDL_GPUBufferCreateInfo buffer_info = {};
    buffer_info.usage = usage;
    buffer_info.size = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(gpu, &buffer_info);
    if (!buffer) {
        LOG_ERROR("SDL_CreateGPUBuffer(%s) failed: %s", debug_name, SDL_GetError());
        return nullptr;
    }
    SDL_SetGPUBufferName(gpu, buffer, debug_name);

    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu, &transfer_info);
    if (!transfer) {
        LOG_ERROR("SDL_CreateGPUTransferBuffer(%s) failed: %s", debug_name, SDL_GetError());
        SDL_ReleaseGPUBuffer(gpu, buffer);
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(gpu, transfer, false);
    if (!mapped) {
        LOG_ERROR("SDL_MapGPUTransferBuffer(%s) failed: %s", debug_name, SDL_GetError());
        SDL_ReleaseGPUTransferBuffer(gpu, transfer);
        SDL_ReleaseGPUBuffer(gpu, buffer);
        return nullptr;
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(gpu, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {};
    src.transfer_buffer = transfer;
    src.offset = 0;
    SDL_GPUBufferRegion dst = {};
    dst.buffer = buffer;
    dst.offset = 0;
    dst.size = size;
    SDL_UploadToGPUBuffer(pass, &src, &dst, false);
    SDL_EndGPUCopyPass(pass);

    // Wait so the caller can release the transfer buffer immediately and rely
    // on the data being resident.
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(gpu, true, &fence, 1);
        SDL_ReleaseGPUFence(gpu, fence);
    }
    SDL_ReleaseGPUTransferBuffer(gpu, transfer);
    return buffer;
}

}  // namespace gfx
