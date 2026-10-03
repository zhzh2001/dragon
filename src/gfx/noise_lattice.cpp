#include "gfx/noise_lattice.h"

#include <cmath>

// The hash is kept unfused, as the shader writes it: a contracted multiply-add
// rounds differently, and a hash amplifies the last bit.
#ifdef __clang__
#pragma clang fp contract(off)
#endif

namespace gfx {
namespace {

float frac(float x) { return x - std::floor(x); }

}  // namespace

float noise_hash21(float x, float y) {
    x = frac(x * 123.34f);
    y = frac(y * 456.21f);
    const float d = x * (x + 45.32f) + y * (y + 45.32f);
    x += d;
    y += d;
    return frac(x * y);
}

float value_noise(float x, float y) {
    const float cx = std::floor(x), cy = std::floor(y);
    float fx = x - cx, fy = y - cy;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fy = fy * fy * (3.0f - 2.0f * fy);
    const float a = noise_hash21(cx, cy), b = noise_hash21(cx + 1.0f, cy);
    const float c = noise_hash21(cx, cy + 1.0f), d = noise_hash21(cx + 1.0f, cy + 1.0f);
    const float top = a + (b - a) * fx, bottom = c + (d - c) * fx;
    return top + (bottom - top) * fy;
}

float fbm(float x, float y, int octaves) {
    float total = 0.0f, amplitude = 0.5f;
    for (int i = 0; i < octaves; ++i) {
        total += amplitude * value_noise(x, y);
        x = x * 2.17f + 31.7f;
        y = y * 2.17f + 17.3f;
        amplitude *= 0.5f;
    }
    return total;
}

std::vector<uint8_t> bake_noise_lattice(uint32_t size) {
    const int n = int(size), half = n / 2;
    // A texel index's cell, and any cell's wrap back into the window.
    auto cell_of = [&](int t) { return t < half ? t : t - n; };
    auto wrap = [&](int c) { return ((c + half) % n + n) % n - half; };
    auto quantize = [](float v) { return uint8_t(std::lround(v * 255.0f)); };
    auto lattice = [&](int cx, int cy) { return quantize(noise_hash21(float(wrap(cx)), float(wrap(cy)))); };

    std::vector<uint8_t> rgba(size_t(size) * size * 4);
    for (int j = 0; j < n; ++j) {
        const int cy = cell_of(j);
        for (int i = 0; i < n; ++i) {
            const int cx = cell_of(i);
            uint8_t* texel = &rgba[(size_t(j) * size + size_t(i)) * 4];
            texel[0] = lattice(cx, cy);
            texel[1] = lattice(cx + 1, cy);
            texel[2] = lattice(cx, cy + 1);
            texel[3] = lattice(cx + 1, cy + 1);
        }
    }
    return rgba;
}

bool NoiseLattice::create(rhi::Device& rhi) {
    const std::vector<uint8_t> rgba = bake_noise_lattice(NOISE_LATTICE_SIZE);
    rhi::TextureDesc desc;
    desc.width = desc.height = NOISE_LATTICE_SIZE;
    desc.format = rhi::Format::RGBA8;  // data, not colour: never sRGB
    texture = rhi.create_texture(desc, "noise_lattice");
    if (!texture || !rhi.upload_texture(texture, rgba.data(), NOISE_LATTICE_SIZE, NOISE_LATTICE_SIZE, false)) {
        destroy(rhi);
        return false;
    }
    rhi::SamplerDesc s;
    s.min_filter = s.mag_filter = rhi::Filter::Nearest;
    s.mip_mode = rhi::MipMode::Nearest;
    s.address_u = s.address_v = s.address_w = rhi::Address::Repeat;
    s.max_lod = 0.0f;
    sampler = rhi.create_sampler(s);
    if (!sampler) {
        destroy(rhi);
        return false;
    }
    return true;
}

void NoiseLattice::destroy(rhi::Device& rhi) {
    rhi.destroy(texture);
    rhi.destroy(sampler);
    texture = nullptr;
    sampler = nullptr;
}

}  // namespace gfx
