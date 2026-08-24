#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

namespace gfx {

// Decoded 8-bit RGBA image, before upload.
struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    bool valid() const { return width > 0 && height > 0 && !rgba.empty(); }
};

// Decodes a PNG or JPEG held in memory.
ImageData decode_image(const uint8_t* bytes, size_t size);

// Uploads an image as a sampled 2D texture with a full mip chain.
//
// Mips are not optional here: a 4K dragon texture viewed from across a valley
// aliases into shimmering noise without them, and that reads as the model being
// broken rather than as a sampling artefact.
SDL_GPUTexture* create_texture_from_image(SDL_GPUDevice* gpu, const ImageData& image,
                                          const char* debug_name);

// Anisotropic, repeating sampler suited to model albedo.
SDL_GPUSampler* create_model_sampler(SDL_GPUDevice* gpu);

}  // namespace gfx
