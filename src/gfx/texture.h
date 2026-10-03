#pragma once

#include <cstdint>
#include <vector>

#include "rhi/rhi.h"

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
//
// `srgb` must be true for base colour and false for data maps. A normal map
// decoded through the sRGB curve gives wrong directions, and a roughness map
// read that way is visibly too glossy -- both look like shading bugs rather than
// like a colour-space mistake, so the distinction is worth being explicit about.
rhi::Texture* create_texture_from_image(rhi::Device& rhi, const ImageData& image,
                                        const char* debug_name, bool srgb = true);

// Anisotropic, repeating sampler suited to model albedo.
rhi::Sampler* create_model_sampler(rhi::Device& rhi);

}  // namespace gfx
