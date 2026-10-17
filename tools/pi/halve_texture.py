#!/usr/bin/env python3
"""Halve a data texture (a PNG read as linear, not sRGB) the way the engine does.

    tools/pi/halve_texture.py IN.png OUT.png --max 512

tools/pi/deploy.sh ships the Pi's terrain_detail.png at the largest size any
tier there uses (512: the sm2 and ff tiers' cap), since decoding the 1024 one
and halving it on the Pi 1B was 2 s of its start-up. gfx::TextureBudget's
halving (gfx/texture.cpp, halve, srgb false) in single precision, in the
same order -- each output byte the lround of the 2x2 mean of v/255, times
255 -- so the engine, halving the rest of the way, reaches the same mips it
reached from the 1024 file, byte for byte.
"""
import argparse
import sys

import numpy as np
from PIL import Image


def halve(rgba):
    h, w = rgba.shape[0] // 2, rgba.shape[1] // 2
    v = rgba.astype(np.float32) / np.float32(255.0)
    s = v[0:2 * h:2, 0:2 * w:2]  # the engine's order: the row, then the next
    s = s + v[0:2 * h:2, 1:2 * w:2]
    s = s + v[1:2 * h:2, 0:2 * w:2]
    s = s + v[1:2 * h:2, 1:2 * w:2]
    mean = (s * np.float32(0.25)) * np.float32(255.0)
    # lround: halves away from zero (every value here is non-negative).
    return np.floor(mean.astype(np.float64) + 0.5).astype(np.uint8)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("src")
    parser.add_argument("dst")
    parser.add_argument("--max", type=int, required=True)
    args = parser.parse_args()
    rgba = np.asarray(Image.open(args.src).convert("RGBA"))
    if rgba.shape[0] % 2 or rgba.shape[1] % 2:
        sys.exit(f"{args.src}: {rgba.shape[1]}x{rgba.shape[0]} is not even; the engine's halving of an odd edge is not reproduced here")
    while max(rgba.shape[0], rgba.shape[1]) > args.max:
        rgba = halve(rgba)
    Image.fromarray(rgba, "RGBA").save(args.dst)


if __name__ == "__main__":
    main()
