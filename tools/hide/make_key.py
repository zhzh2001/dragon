"""Write two copies of the model with flat base colours, for masking renders.

Usage: make_key.py SHIPPED.glb OUT_DIR

The measurement needs to know which pixels of a render are the dragon. The
headless engine is deterministic, so two renders that differ only in the
dragon's base colour differ only on the dragon's pixels. This script writes
`key-a.glb` (magenta) and `key-b.glb` (green); `measure.py` takes the pixels
where their renders differ as the dragon.
"""

import io
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glbio  # noqa: E402

doc, blob = glbio.read_glb(sys.argv[1])
os.makedirs(sys.argv[2], exist_ok=True)
base_image = doc["textures"][doc["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]["index"]]["source"]
for name, colour in (("key-a", (255, 0, 255)), ("key-b", (0, 255, 0))):
    buffer = io.BytesIO()
    Image.new("RGB", (64, 64), colour).save(buffer, format="PNG")
    glbio.write_glb(os.path.join(sys.argv[2], name + ".glb"), doc, blob, {base_image: buffer.getvalue()})
    print("wrote", os.path.join(sys.argv[2], name + ".glb"))
