"""Cool Frost3's graded albedo toward its sheet: white, with pale blue above.

The first grade (frost3_grade.py) lifted the generator's base colour and
took most of its blue with it: the median went from 145,162,186 to
182,199,212, a blue-to-red ratio of 1.28 down to 1.16, and the belly plates
came out beige. In the engine's sun the dragon read grey-lilac with an ivory
belly. The sheet's creature pixels have a ratio of 1.21, drawn under neutral
light, and the game's sun is warm.

This pass runs on a built model, raw or repaired (the repair keeps the base
colour):

  1. A warm pixel (red over blue) goes to its own luminance, as white: the
     beige belly, legs and ruff.
  2. Everything pale is then cooled: red down, blue up, most in the
     mid-tones, so the scales of the back are pale blue and the highlights
     stay white.

The mouth and the eyes are kept (saturated red, or dark), and so are the
green and violet of the wing's trailing edge (saturated, and not warm).

    python tools/frost3_cool.py assets/frost3.glb assets/frost3.glb
"""
import sys
from pathlib import Path

import numpy as np
from repair_model_materials import Glb, to_float, to_png_bytes, write_glb


def cool(source, output):
    glb = Glb(source)
    doc = glb.json
    tex = doc['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['index']
    index = doc['textures'][tex]['source']
    rgb = to_float(glb.image(index))[..., :3]
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    luminance = rgb @ np.array([.2126, .7152, .0722], dtype=np.float32)
    chroma = rgb.max(axis=-1) - rgb.min(axis=-1)
    # Pale: not the mouth, the eyes, or the aurora at the wing's edge.
    pale = np.clip((luminance - .35) / .2, 0, 1) * (1 - np.clip((chroma - .16) / .12, 0, 1))
    # 1. Beige to white.
    warm = np.clip((r - b + .01) / .05, 0, 1) * pale
    white = np.stack([luminance * .985, luminance * 1.0, luminance * 1.03], axis=-1)
    result = rgb + warm[..., None] * (white - rgb)
    # 2. Cool the pale hide, most in the mid-tones.
    mid = 1 - np.clip((luminance - .78) / .2, 0, 1) * .6
    gain = np.array([.89, .975, 1.045], dtype=np.float32)
    cooled = np.clip(result * gain, 0, 1)
    result = result + (pale * mid)[..., None] * (cooled - result)
    image = doc['images'][index]
    image['mimeType'] = 'image/png'
    write_glb(glb, output, {image['bufferView']: to_png_bytes(result)})


if __name__ == '__main__':
    cool(Path(sys.argv[1]), Path(sys.argv[2]))
