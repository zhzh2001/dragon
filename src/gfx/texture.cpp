#include "gfx/texture.h"

#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/log.h"
#include "gfx/render_tier.h"

namespace gfx {
namespace {

// sRGB <-> linear, for averaging colour in light rather than in code values.
const float* srgb_lut() {
    static const std::array<float, 256> lut = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            const float c = float(i) / 255.0f;
            t[size_t(i)] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return t;
    }();
    return lut.data();
}

uint8_t linear_to_srgb(float l) {
    l = std::clamp(l, 0.0f, 1.0f);
    const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    return uint8_t(std::lround(c * 255.0f));
}

// One mip step: each texel the mean of its 2x2 source (an odd edge drops its
// last row or column; a 1-texel edge stays 1).
ImageData halve(const ImageData& src, bool srgb) {
    ImageData dst;
    dst.width = std::max(1, src.width / 2);
    dst.height = std::max(1, src.height / 2);
    dst.rgba.resize(size_t(dst.width) * dst.height * 4);
    const float* lut = srgb_lut();
    const int sx = src.width > 1 ? 2 : 1, sy = src.height > 1 ? 2 : 1;
    const float inv = 1.0f / float(sx * sy);
    for (int y = 0; y < dst.height; ++y) {
        for (int x = 0; x < dst.width; ++x) {
            float sum[4] = {0, 0, 0, 0};
            for (int j = 0; j < sy; ++j) {
                const uint8_t* row = &src.rgba[(size_t(y * sy + j) * src.width + size_t(x * sx)) * 4];
                for (int i = 0; i < sx; ++i) {
                    for (int c = 0; c < 4; ++c) {
                        const uint8_t v = row[i * 4 + c];
                        sum[c] += (srgb && c < 3) ? lut[v] : float(v) / 255.0f;
                    }
                }
            }
            uint8_t* out = &dst.rgba[(size_t(y) * dst.width + x) * 4];
            for (int c = 0; c < 4; ++c) {
                const float mean = sum[c] * inv;
                out[c] = (srgb && c < 3) ? linear_to_srgb(mean) : uint8_t(std::lround(mean * 255.0f));
            }
        }
    }
    return dst;
}

}  // namespace

TextureBudget active_texture_budget() {
    TextureBudget budget;
    budget.max_size = active_tier().max_texture_size;
    budget.compress = active_tier().compress_textures;
    return budget;
}

ImageData cap_image(const ImageData& image, uint32_t max_size, bool srgb) {
    ImageData out = image;
    while (max_size > 0 && uint32_t(std::max(out.width, out.height)) > max_size) out = halve(out, srgb);
    return out;
}

std::vector<ImageData> build_mips(const ImageData& image, bool srgb, bool block_aligned) {
    std::vector<ImageData> chain{image};
    while (chain.back().width > 1 || chain.back().height > 1) {
        ImageData next = halve(chain.back(), srgb);
        if (block_aligned && (next.width % 4 != 0 || next.height % 4 != 0)) break;
        chain.push_back(std::move(next));
    }
    return chain;
}

bool has_alpha(const ImageData& image) {
    for (size_t i = 3; i < image.rgba.size(); i += 4) {
        if (image.rgba[i] != 255) return true;
    }
    return false;
}

void swizzle_normal_map(ImageData& image) {
    for (size_t i = 0; i < image.rgba.size(); i += 4) {
        const uint8_t x = image.rgba[i];
        image.rgba[i] = 0;
        image.rgba[i + 2] = 0;
        image.rgba[i + 3] = x;
    }
}

std::vector<uint8_t> encode_bc(const ImageData& image, bool bc3) {
    const int bw = image.width / 4, bh = image.height / 4;
    const size_t block_bytes = bc3 ? 16 : 8;
    std::vector<uint8_t> out(size_t(bw) * bh * block_bytes);
    uint8_t block[64];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            for (int y = 0; y < 4; ++y) {
                std::memcpy(&block[y * 16], &image.rgba[(size_t(by * 4 + y) * image.width + size_t(bx * 4)) * 4], 16);
            }
            stb_compress_dxt_block(&out[(size_t(by) * bw + bx) * block_bytes], block, bc3 ? 1 : 0,
                                   STB_DXT_HIGHQUAL);
        }
    }
    return out;
}

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

namespace {

// The budgeted path: capped, mips on the CPU, maybe compressed.
rhi::Texture* create_budgeted_texture(rhi::Device& rhi, const ImageData& image, const char* debug_name,
                                      TextureKind kind, const TextureBudget& budget) {
    const bool srgb = kind == TextureKind::Colour;
    ImageData top = cap_image(image, budget.max_size, srgb);
    if (budget.compress && kind == TextureKind::NormalMap) swizzle_normal_map(top);

    // What each kind compresses to, if anything (see TextureBudget).
    rhi::Format block = rhi::Format::Invalid;
    if (budget.compress && top.width % 4 == 0 && top.height % 4 == 0) {
        switch (kind) {
            case TextureKind::Colour: block = has_alpha(top) ? rhi::Format::BC3_SRGB : rhi::Format::BC1_SRGB; break;
            case TextureKind::NormalMap: block = rhi::Format::BC3; break;
            case TextureKind::Orm: block = rhi::Format::BC1; break;
            case TextureKind::Data: break;
        }
    }
    if (block != rhi::Format::Invalid && !rhi.supports_format(block, rhi::TEXTURE_SAMPLED)) {
        LOG_WARN("texture '%s': this device has no BC formats; kept uncompressed", debug_name);
        block = rhi::Format::Invalid;
    }
    const bool compress = block != rhi::Format::Invalid;
    const bool bc3 = block == rhi::Format::BC3 || block == rhi::Format::BC3_SRGB;
    const std::vector<ImageData> mips = build_mips(top, srgb, compress);

    rhi::TextureDesc desc;
    desc.width = uint32_t(top.width);
    desc.height = uint32_t(top.height);
    desc.mip_levels = uint32_t(mips.size());
    desc.format = compress ? block : srgb ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8;
    desc.usage = rhi::TEXTURE_SAMPLED;
    rhi::Texture* texture = rhi.create_texture(desc, debug_name);
    if (!texture) return nullptr;
    size_t bytes = 0;
    for (size_t level = 0; level < mips.size(); ++level) {
        const ImageData& m = mips[level];
        const std::vector<uint8_t> blocks = compress ? encode_bc(m, bc3) : std::vector<uint8_t>{};
        const std::vector<uint8_t>& data = compress ? blocks : m.rgba;
        if (!rhi.upload_texture_level(texture, uint32_t(level), data.data(), uint32_t(data.size()),
                                      uint32_t(m.width), uint32_t(m.height))) {
            rhi.destroy(texture);
            return nullptr;
        }
        bytes += data.size();
    }
    LOG_INFO("texture '%s': %dx%d -> %dx%d %s, %zu mips, %.1f MB", debug_name, image.width, image.height,
             top.width, top.height, compress ? (bc3 ? "BC3" : "BC1") : "RGBA8", mips.size(),
             double(bytes) / (1024.0 * 1024.0));
    return texture;
}

}  // namespace

rhi::Texture* create_texture_from_image(rhi::Device& rhi, const ImageData& image,
                                        const char* debug_name, TextureKind kind) {
    if (!image.valid()) return nullptr;
    const TextureBudget budget = active_texture_budget();
    if (budget.active()) return create_budgeted_texture(rhi, image, debug_name, kind, budget);
    const bool srgb = kind == TextureKind::Colour;

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
