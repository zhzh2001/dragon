#!/usr/bin/env python3
"""Build trial copies of the hero with Tripo's re-textured hide.

    python3 tools/hide/tripo_hide_trial.py

Tripo's Texture tool painted the hero's own mesh on its own UVs
(docs/MODEL_GENERATION.md, "Re-texturing the hero on its own UVs"), so its
hide base colour can replace the hero's texel for texel. It returns base
colour only; the hero keeps its normal and roughness maps. Two files come
out, beside the hero and ignored by git like every model:

  assets/embercrest-hero-tripo.glb         Tripo's hide colour whole.
  assets/embercrest-hero-tripo-detail.glb  The hero's own broad colour (its
      dark back and crest) with Tripo's fine detail (its scale edges): the
      hero's hide blurred, times Tripo's hide over Tripo's hide blurred.

Each gets copies of the hero's .rig.cfg and .flight.cfg. Needs Pillow and
numpy (~/.local/py314/bin/python3 has both).
"""
import io
import os
import shutil
import sys

import numpy as np
from PIL import Image, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glbio  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HERO = os.path.join(ROOT, "assets/embercrest-hero.glb")
TRIPO = os.path.join(ROOT, "assets/tripo-raw/tripo-hero-retex-03.glb")
BLUR = 20  # pixels at 4096: wider than a scale, narrower than the back's dark band


def image_named(doc, name):
    for i, image in enumerate(doc["images"]):
        if image.get("name") == name:
            return i
    raise KeyError(name)


def linear(img):
    a = np.asarray(img.convert("RGB"), dtype=np.float32) / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def encode(a):
    a = np.clip(a, 0.0, 1.0)
    s = np.where(a <= 0.0031308, a * 12.92, 1.055 * a ** (1.0 / 2.4) - 0.055)
    out = io.BytesIO()
    Image.fromarray((s * 255.0 + 0.5).astype(np.uint8)).save(out, "JPEG", quality=92, subsampling=0)
    return out.getvalue()


def blurred(a):
    img = Image.fromarray((np.clip(a, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8))
    return np.asarray(img.filter(ImageFilter.GaussianBlur(BLUR)), dtype=np.float32) / 255.0


def main():
    hero_doc, hero_blob = glbio.read_glb(HERO)
    tripo_doc, tripo_blob = glbio.read_glb(TRIPO)
    hide = image_named(hero_doc, "hide_base")
    tripo_bytes = glbio.image_bytes(tripo_doc, tripo_blob, image_named(tripo_doc, "hide_base_color"))
    own = Image.open(io.BytesIO(glbio.image_bytes(hero_doc, hero_blob, hide)))
    tripo = Image.open(io.BytesIO(tripo_bytes))
    if tripo.size != own.size:
        tripo = tripo.resize(own.size, Image.LANCZOS)
    own_l, tripo_l = linear(own), linear(tripo)
    print("hide %dx%d; mean linear: hero %s, tripo %s" % (
        own.size[0], own.size[1], np.round(own_l.mean(axis=(0, 1)), 3), np.round(tripo_l.mean(axis=(0, 1)), 3)))

    # The fine detail as a ratio on the luminance, so a scale's edge darkens
    # the hero's colour and does not shift its hue.
    luma = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    tripo_y = tripo_l @ luma
    ratio = tripo_y / np.maximum(blurred(np.repeat(tripo_y[..., None], 3, axis=2))[..., 0], 0.02)
    ratio = np.clip(ratio, 0.35, 2.2)
    detail = blurred(own_l) * ratio[..., None]

    outputs = {
        "embercrest-hero-tripo.glb": encode(tripo_l),
        "embercrest-hero-tripo-detail.glb": encode(detail),
    }
    for name, data in outputs.items():
        path = os.path.join(ROOT, "assets", name)
        glbio.write_glb(path, hero_doc, hero_blob, {hide: data})
        for ext in (".rig.cfg", ".flight.cfg"):
            shutil.copyfile(HERO + ext, path + ext)
        print("wrote", path, os.path.getsize(path))


if __name__ == "__main__":
    main()
