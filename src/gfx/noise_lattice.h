#pragma once

// The baked noise lattice: value noise for the retro tiers (docs/PORTING.md,
// R2).
//
// The ground and the bark grain with value noise (scene_common.hlsl), and the
// modern build hashes every lattice corner in the pixel shader: four hashes
// per lookup, a dozen lookups per terrain pixel. That is hundreds of
// instructions, far past ps_2_0's 64 and heavy for ps_3_0's floor cards.
// The retro tiers (RenderTier::baked_noise) read the corners from this texture
// instead: texel (i, j) holds, in R G B A, the hash at the four corners of
// lattice cell (i, j) -- (0,0), (1,0), (0,1), (1,1) -- so one point sample and
// the same smoothstep blend give the same noise. The blend stays in the
// shader, not in the bilinear filter, whose weights old hardware keeps to a
// handful of bits (stepping across a magnified cell).
//
// The lattice is the shader's own hash, so the baked noise is the modern
// noise, to 8 bits, over the window of cells [-SIZE/2, SIZE/2 - 1] in each
// axis. Past it the lattice repeats, seamlessly: the corners of the window's
// last cell wrap to its first. At 512 the window covers the patch noise
// everywhere, the fine grain across the whole map, and the micro-relief
// within 730 m of the centre; beyond those it is the same kind of noise, not
// the same values.

#include <cstdint>
#include <vector>

#include "rhi/rhi.h"

namespace gfx {

constexpr uint32_t NOISE_LATTICE_SIZE = 512;  // also NOISE_LATTICE_SIZE in scene_common.hlsl

// hash21 from scene_common.hlsl: a value in [0, 1) for a lattice point.
float noise_hash21(float x, float y);

// RGBA8, size x size, as described above.
std::vector<uint8_t> bake_noise_lattice(uint32_t size);

// The texture and the point-sampling, repeating sampler it is read with.
struct NoiseLattice {
    rhi::Texture* texture = nullptr;
    rhi::Sampler* sampler = nullptr;

    bool create(rhi::Device& rhi);
    void destroy(rhi::Device& rhi);
    bool valid() const { return texture && sampler; }
    rhi::TextureBinding binding() const {
        rhi::TextureBinding b;
        b.texture = texture;
        b.sampler = sampler;
        return b;
    }
};

}  // namespace gfx
