#include "gfx/texture.h"

#include <SDL3/SDL.h>

#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#include "core/log.h"

namespace gfx {

ImageData decode_image(const uint8_t* bytes, size_t size) {
    ImageData image;
    int channels = 0;
    // Forced to 4 channels: the GPU formats we care about are RGBA, and letting
    // stb decide would mean handling three layouts downstream.
    unsigned char* pixels = stbi_load_from_memory(bytes, int(size), &image.width, &image.height,
                                                  &channels, 4);
    if (!pixels) {
        LOG_ERROR("image decode failed: %s", stbi_failure_reason());
        return {};
    }
    image.rgba.assign(pixels, pixels + size_t(image.width) * size_t(image.height) * 4);
    stbi_image_free(pixels);
    return image;
}

SDL_GPUTexture* create_texture_from_image(SDL_GPUDevice* gpu, const ImageData& image,
                                          const char* debug_name, bool srgb) {
    if (!image.valid()) return nullptr;

    uint32_t levels = 1;
    for (uint32_t dimension = uint32_t(image.width > image.height ? image.width : image.height);
         dimension > 1; dimension /= 2) {
        ++levels;
    }

    SDL_GPUTextureCreateInfo info = {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = srgb ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
                       : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    // COLOR_TARGET as well as SAMPLER: SDL generates mips by rendering into the
    // smaller levels, so the texture has to be usable as a render target.
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    info.width = uint32_t(image.width);
    info.height = uint32_t(image.height);
    info.layer_count_or_depth = 1;
    info.num_levels = levels;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(gpu, &info);
    if (!texture) {
        LOG_ERROR("SDL_CreateGPUTexture(%s) failed: %s", debug_name, SDL_GetError());
        return nullptr;
    }
    SDL_SetGPUTextureName(gpu, texture, debug_name);

    const uint32_t bytes = uint32_t(image.rgba.size());
    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = bytes;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(gpu, &transfer_info);
    if (!transfer) {
        SDL_ReleaseGPUTexture(gpu, texture);
        LOG_ERROR("SDL_CreateGPUTransferBuffer(%s) failed: %s", debug_name, SDL_GetError());
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(gpu, transfer, false);
    std::memcpy(mapped, image.rgba.data(), bytes);
    SDL_UnmapGPUTransferBuffer(gpu, transfer);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu);
    SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo source = {};
    source.transfer_buffer = transfer;
    source.pixels_per_row = uint32_t(image.width);
    source.rows_per_layer = uint32_t(image.height);
    SDL_GPUTextureRegion destination = {};
    destination.texture = texture;
    destination.w = uint32_t(image.width);
    destination.h = uint32_t(image.height);
    destination.d = 1;
    SDL_UploadToGPUTexture(pass, &source, &destination, false);
    SDL_EndGPUCopyPass(pass);

    if (levels > 1) SDL_GenerateMipmapsForGPUTexture(cmd, texture);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(gpu, true, &fence, 1);
        SDL_ReleaseGPUFence(gpu, fence);
    }
    SDL_ReleaseGPUTransferBuffer(gpu, transfer);

    LOG_INFO("texture '%s': %dx%d, %u mips", debug_name, image.width, image.height, levels);
    return texture;
}

SDL_GPUSampler* create_model_sampler(SDL_GPUDevice* gpu) {
    SDL_GPUSamplerCreateInfo info = {};
    info.min_filter = SDL_GPU_FILTER_LINEAR;
    info.mag_filter = SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    // Grazing angles are the norm on a wing membrane, where anisotropy earns its
    // keep.
    info.enable_anisotropy = true;
    info.max_anisotropy = 8.0f;
    info.max_lod = 1000.0f;
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(gpu, &info);
    if (!sampler) LOG_ERROR("SDL_CreateGPUSampler(model) failed: %s", SDL_GetError());
    return sampler;
}

}  // namespace gfx
