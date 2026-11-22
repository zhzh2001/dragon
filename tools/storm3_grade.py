"""Lift Storm3's silver membrane and preserve its dark horns and red mouth."""
import sys
from pathlib import Path

import numpy as np
from repair_model_materials import Glb, to_float, to_png_bytes, write_glb


def grade(source, output):
    glb = Glb(source)
    doc = glb.json
    texture = doc['materials'][0]['pbrMetallicRoughness']['baseColorTexture']['index']
    image_index = doc['textures'][texture]['source']
    image = doc['images'][image_index]
    rgb = to_float(glb.image(image_index))
    luminance = rgb @ np.array([.2126, .7152, .0722], dtype=np.float32)
    # Preserve the black horns and red mouth. Lift the grey membrane and cool the scales.
    light = np.clip((luminance - .13) / .36, 0, 1)
    neutral = 1 - np.clip((rgb[..., 0] - rgb[..., 2] - .035) / .13, 0, 1)
    lifted = rgb ** np.array([.58, .51, .48], dtype=np.float32)
    result = rgb + (light * neutral)[..., None] * (lifted - rgb)
    write_glb(glb, output, {image['bufferView']: to_png_bytes(result)})


if __name__ == '__main__':
    grade(Path(sys.argv[1]), Path(sys.argv[2]))
