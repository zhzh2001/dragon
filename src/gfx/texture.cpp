#include "gfx/texture.h"

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

rhi::Texture* create_texture_from_image(rhi::Device& rhi, const ImageData& image,
                                        const char* debug_name, bool srgb) {
    if (!image.valid()) return nullptr;

    uint32_t levels = 1;
    for (uint32_t dimension = uint32_t(image.width > image.height ? image.width : image.height);
         dimension > 1; dimension /= 2) {
        ++levels;
    }

    rhi::TextureDesc desc;
    desc.width = uint32_t(image.width);
    desc.height = uint32_t(image.height);
    desc.mip_levels = levels;
    desc.format = srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8;
    // A colour target as well as sampled: SDL generates mips by rendering
    // into the smaller levels, so the texture has to be usable as one.
    desc.usage = rhi::TEXTURE_SAMPLED | rhi::TEXTURE_COLOR_TARGET;
    rhi::Texture* texture = rhi.create_texture(desc, debug_name);
    if (!texture) return nullptr;
    if (!rhi.upload_texture(texture, image.rgba.data(), desc.width, desc.height, levels > 1)) {
        rhi.destroy(texture);
        return nullptr;
    }
    LOG_INFO("texture '%s': %dx%d, %u mips", debug_name, image.width, image.height, levels);
    return texture;
}

rhi::Sampler* create_model_sampler(rhi::Device& rhi) {
    rhi::SamplerDesc desc;
    desc.min_filter = rhi::Filter::Linear;
    desc.mag_filter = rhi::Filter::Linear;
    desc.mip_mode = rhi::MipMode::Linear;
    desc.address_u = desc.address_v = desc.address_w = rhi::Address::Repeat;
    // Grazing angles are the norm on a wing membrane, where anisotropy earns its
    // keep.
    desc.max_anisotropy = 8.0f;
    desc.max_lod = 1000.0f;
    return rhi.create_sampler(desc);
}

}  // namespace gfx
