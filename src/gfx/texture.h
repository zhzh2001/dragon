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

// The tier's texture budget (gfx/render_tier.h), applied at upload so no
// call site has to know about it.
//
// A capped texture is halved with a box filter until its longer edge fits,
// and its mip chain is built on the CPU (averaged in linear light for
// colour). With compression on, and edges that are multiples of four:
//   - Colour: BC1 when every texel is opaque, BC3 when any is not.
//   - NormalMap: DXT5nm -- x moved to alpha, y kept in green, red and blue
//     zeroed so the colour block spends its endpoints on y alone, BC3. The
//     shaders rebuild z (SWIZZLED_NORMALS). The swizzle is applied even when
//     the device cannot compress, since the shaders expect it.
//   - Orm: BC1. Occlusion, roughness and metallic share the block's two
//     endpoints, which costs a few code values of crosstalk, as period
//     engines packing gloss into DXT accepted.
//   - Data (the terrain detail, the card masks): RGBA8. Four independent
//     channels including alpha, small, and BC1 would smear them together.
enum class TextureKind : uint8_t { Colour, Data, NormalMap, Orm };

struct TextureBudget {
    uint32_t max_size = 0;  // 0: no cap
    bool compress = false;
    bool active() const { return max_size != 0 || compress; }
};
TextureBudget active_texture_budget();

// The pieces, exposed for tests/test_texture_budget.cpp.
// Halves `image` (a 2x2 box, in linear light when `srgb`; an odd edge drops
// its last row or column) until its longer edge is at most `max_size`.
ImageData cap_image(const ImageData& image, uint32_t max_size, bool srgb);
// The image and its mips. With `block_aligned` the chain stops before a level
// whose edges are not both multiples of four, as a block format needs;
// otherwise it runs down to 1x1.
std::vector<ImageData> build_mips(const ImageData& image, bool srgb, bool block_aligned);
// 4x4 blocks, row by row: 8 bytes each for BC1, 16 for BC3. Edges must be
// multiples of four.
std::vector<uint8_t> encode_bc(const ImageData& image, bool bc3);
bool has_alpha(const ImageData& image);
// The DXT5nm layout, in place: (x, y, z, a) -> (0, y, 0, x).
void swizzle_normal_map(ImageData& image);

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
//
// Under an active budget (a retro tier) the image is capped and maybe
// compressed first, as described at TextureBudget.
rhi::Texture* create_texture_from_image(rhi::Device& rhi, const ImageData& image,
                                        const char* debug_name, TextureKind kind);
// `srgb` true is Colour, false is Data.
inline rhi::Texture* create_texture_from_image(rhi::Device& rhi, const ImageData& image,
                                               const char* debug_name, bool srgb = true) {
    return create_texture_from_image(rhi, image, debug_name, srgb ? TextureKind::Colour : TextureKind::Data);
}

// Anisotropic, repeating sampler suited to model albedo.
rhi::Sampler* create_model_sampler(rhi::Device& rhi);

}  // namespace gfx
