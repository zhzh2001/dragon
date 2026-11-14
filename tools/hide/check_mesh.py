"""Prove that a candidate differs from the shipped model only in its images.

Usage: check_mesh.py SHIPPED.glb CANDIDATE.glb

The script compares, between the two files:
- every accessor: its JSON, and the bytes it addresses;
- every bufferView that is not an image: its JSON without `byteOffset`, and
  its bytes (an image's new length moves the views after it, so the offset
  may differ; the bytes may not);
- every node, skin, mesh, scene, animation, texture and sampler, as JSON;
- the material, as JSON (the same texture slots, factors and extensions);
- every image: its JSON without the bufferView's size, and its pixel size
  and mode.

The script exits with 1 if any comparison fails.
"""

import hashlib
import io
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glbio  # noqa: E402
import meshdata  # noqa: E402

Image.MAX_IMAGE_PIXELS = None


def main():
    doc_a, blob_a = glbio.read_glb(sys.argv[1])
    doc_b, blob_b = glbio.read_glb(sys.argv[2])
    failures = []

    def check(name, ok, detail=""):
        print(f"{'ok  ' if ok else 'FAIL'} {name}{(' ' + detail) if detail else ''}")
        if not ok:
            failures.append(name)

    image_views = {image["bufferView"] for image in doc_a["images"]}
    check("the same top-level keys", sorted(doc_a.keys()) == sorted(doc_b.keys()))
    for key in ("asset", "scene", "scenes", "nodes", "skins", "meshes", "animations", "materials", "textures",
                "samplers", "images", "accessors", "extensionsUsed", "extensionsRequired", "extras"):
        if key in doc_a or key in doc_b:
            count = len(doc_a[key]) if isinstance(doc_a.get(key), list) else 1
            check(f"{key}: JSON equal", doc_a.get(key) == doc_b.get(key), f"({count})")

    same_bytes = 0
    digest = hashlib.sha256()
    for index in range(len(doc_a["accessors"])):
        a = meshdata.accessor(doc_a, blob_a, index)
        b = meshdata.accessor(doc_b, blob_b, index)
        if a.shape == b.shape and a.tobytes() == b.tobytes():
            same_bytes += 1
            digest.update(a.tobytes())
    check("accessors: data bytes equal", same_bytes == len(doc_a["accessors"]),
          f"({same_bytes} of {len(doc_a['accessors'])}, sha256 {digest.hexdigest()[:16]})")

    check("bufferViews: the same count", len(doc_a["bufferViews"]) == len(doc_b["bufferViews"]))
    views_ok = 0
    views = 0
    for index, (va, vb) in enumerate(zip(doc_a["bufferViews"], doc_b["bufferViews"])):
        if index in image_views:
            continue
        views += 1
        strip = lambda v: {k: x for k, x in v.items() if k != "byteOffset"}  # noqa: E731
        if strip(va) == strip(vb) and glbio.view_bytes(doc_a, blob_a, index) == glbio.view_bytes(doc_b, blob_b, index):
            views_ok += 1
    check("bufferViews that are not images: JSON and bytes equal", views_ok == views, f"({views_ok} of {views})")

    for index in range(len(doc_a["images"])):
        a = Image.open(io.BytesIO(glbio.image_bytes(doc_a, blob_a, index)))
        b = Image.open(io.BytesIO(glbio.image_bytes(doc_b, blob_b, index)))
        check(f"image {index}: the same size and mode", (a.size, a.mode, a.format) == (b.size, b.mode, b.format),
              f"({b.size[0]}x{b.size[1]} {b.mode} {b.format})")

    print()
    if failures:
        print(f"MESH CHECK FAILED: {len(failures)} comparison(s)")
        sys.exit(1)
    print("MESH CHECK PASSED: the candidate differs from the shipped model only in the pixels of its images")


if __name__ == "__main__":
    main()
