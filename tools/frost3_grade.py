"""Lift Frost3's source albedo before repairing its data maps."""
import sys
from pathlib import Path

import numpy as np
from repair_model_materials import Glb, to_float, to_png_bytes, write_glb


def grade(source, output):
    glb = Glb(source)
    doc = glb.json
    tex = doc['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['index']
    index = doc['textures'][tex]['source']
    rgb = to_float(glb.image(index))
    # Lift the pale scales most. Preserve the eyes, mouth and saturated edge hues.
    luminance = rgb @ np.array([.2126,.7152,.0722], dtype=np.float32)
    weight = np.clip((luminance-.16)/.28, 0, 1)
    lifted = rgb ** np.array([.57,.52,.55], dtype=np.float32)
    result = rgb + weight[...,None] * (lifted-rgb)
    image = doc['images'][index]
    image['mimeType'] = 'image/png'
    write_glb(glb, output, {image['bufferView']:to_png_bytes(result)})


if __name__ == '__main__':
    grade(Path(sys.argv[1]), Path(sys.argv[2]))
