#!/usr/bin/env python3
"""Bake the terrain's scanned detail maps from Poly Haven scans.

    tools/build_terrain_rock.py CACHE_DIR

Writes assets/textures/terrain_rock.png (from Poly Haven's rock_face_03),
terrain_scree.png (gray_rocks), terrain_grass.png (leafy_grass),
terrain_forest.png (forest_leaves_04, pine litter) and terrain_gravel.png
(dry_river_pebbles), all CC0 (ATTRIBUTION.md, "Poly Haven textures").
Each is 1024x1024 RGBA: the scan's OpenGL normal map in RGB
and its luminance in A, normalised to a mean of 0.5 and a fixed spread.
Only the detail is taken, never the colour: the palette colours the ground
(gfx/palette.h), so the grade stays in one place, as the detail tile does
(tools/build_terrain_detail.py). The modern tier's terrain shader reads
them (shaders/terrain.hlsl, docs/FIDELITY.md F3).

The 1K JPEGs are downloaded into CACHE_DIR once and reused.
"""

import os
import sys
import urllib.request

from PIL import Image, ImageStat

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k"
SETS = {
    "terrain_rock.png": "rock_face_03",
    "terrain_scree.png": "gray_rocks",
    "terrain_grass.png": "leafy_grass",
    "terrain_forest.png": "forest_leaves_04",
    "terrain_gravel.png": "dry_river_pebbles",
}
SPREAD = 0.16  # the luminance's standard deviation, as a fraction of full scale


def fetch(cache, asset, kind):
    path = os.path.join(cache, f"{asset}_{kind}_1k.jpg")
    if not os.path.exists(path):
        url = f"{BASE}/{asset}/{asset}_{kind}_1k.jpg"
        print(f"downloading {url}")
        urllib.request.urlretrieve(url, path)
    return path


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    cache = sys.argv[1]
    os.makedirs(cache, exist_ok=True)
    for out, asset in SETS.items():
        diffuse = Image.open(fetch(cache, asset, "diff")).convert("L").resize((1024, 1024), Image.LANCZOS)
        normal = Image.open(fetch(cache, asset, "nor_gl")).convert("RGB").resize((1024, 1024), Image.LANCZOS)
        stat = ImageStat.Stat(diffuse)
        mean, sd = stat.mean[0], max(stat.stddev[0], 1.0)
        scale = SPREAD * 255.0 / sd
        detail = diffuse.point(lambda v: max(0, min(255, round(127.5 + (v - mean) * scale))))
        r, g, b = normal.split()
        Image.merge("RGBA", (r, g, b, detail)).save(os.path.join(ROOT, "assets", "textures", out))
        print(f"{out}: {asset}, luminance mean {mean:.1f} sd {sd:.1f} -> 127.5 / {SPREAD * 255:.1f}")


if __name__ == "__main__":
    main()
