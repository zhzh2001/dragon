// Cheap value noise for breaking up flat material bands -- terrain patches,
// bark streaks. Not for shaping geometry: purely a surface tint, so it can be
// crude and fast. Shared, so the ground and the trunks standing on it grain
// the same way.
//
// A retro tier (BAKED_NOISE) reads the lattice's corners from a texture
// instead of hashing them (gfx/noise_lattice.h): the same noise to 8 bits,
// for one point sample in place of four hashes. That texture takes fragment
// slot NOISE_SLOT, the slot after the including shader's own textures; the
// shader defines it and includes this file in its fragment stage, beside
// those declarations.
#pragma once

// The vertex stage has no texture fetch on SM2 (vs_2_0), so it always hashes.
#if defined(BAKED_NOISE) && !defined(VERTEX_STAGE)
#define NOISE_LATTICE_SIZE 512.0  // gfx::NOISE_LATTICE_SIZE
TEXTURE2D(noise_lattice, NOISE_SLOT);

float value_noise(float2 p) {
    float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    // R G B A: the corners (0,0), (1,0), (0,1), (1,1). Repeat addressing
    // wraps a negative cell onto its texel.
    float4 k = noise_lattice.Sample(noise_lattice_sampler, (cell + 0.5) / NOISE_LATTICE_SIZE);
    return lerp(lerp(k.r, k.g, f.x), lerp(k.b, k.a, f.x), f.y);
}
#else
float hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float value_noise(float2 p) {
    float2 cell = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);  // smooth the interpolation
    float a = hash21(cell);
    float b = hash21(cell + float2(1.0, 0.0));
    float c = hash21(cell + float2(0.0, 1.0));
    float d = hash21(cell + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
#endif

// Three octaves: single-octave value noise reads as soft blobs, fbm reads as
// ground. Still cheap enough to call several times per fragment. A shader may
// define FBM_OCTAVES before including this to spend less (SM2's terrain
// vertex stage, where a hashed octave is some 40 of vs_2_0's 256 slots).
#ifndef FBM_OCTAVES
#define FBM_OCTAVES 3
#endif
float fbm(float2 p) {
    float total = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < FBM_OCTAVES; ++i) {
        total += amplitude * value_noise(p);
        p = p * 2.17 + float2(31.7, 17.3);
        amplitude *= 0.5;
    }
    return total;
}
