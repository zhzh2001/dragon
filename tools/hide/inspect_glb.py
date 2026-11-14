"""Print a model's material and image layout and dump its images.

Usage: inspect_glb.py MODEL.glb OUT_DIR
"""

import io
import json
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glbio  # noqa: E402

Image.MAX_IMAGE_PIXELS = None

doc, blob = glbio.read_glb(sys.argv[1])
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
print(json.dumps(doc["materials"], indent=1))
print("images", doc["images"])
print("textures", doc["textures"], "samplers", doc.get("samplers"))
print("views", len(doc["bufferViews"]), "accessors", len(doc["accessors"]), "nodes", len(doc["nodes"]),
      "meshes", len(doc["meshes"]), "buffers", doc["buffers"], "asset", doc["asset"])
print("primitive 0", doc["meshes"][0]["primitives"][0])
for i in range(len(doc["images"])):
    raw = glbio.image_bytes(doc, blob, i)
    view = doc["bufferViews"][doc["images"][i]["bufferView"]]
    image = Image.open(io.BytesIO(raw))
    print(i, view, image.mode, image.size, sorted(image.info.keys()))
    with open(os.path.join(out, f"img{i}.png"), "wb") as f:
        f.write(raw)
    image.convert("RGB").resize((1024, 1024), Image.LANCZOS).save(os.path.join(out, f"img{i}_small.jpg"), quality=88)
