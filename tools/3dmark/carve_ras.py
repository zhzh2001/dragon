#!/usr/bin/env python3
"""Carve the files out of a MAX-FX .ras archive by their headers.

    tools/3dmark/carve_ras.py data.ras OUT_DIR        # LightWave objects and scenes
    tools/3dmark/carve_ras.py textures.ras OUT_DIR    # JPEG and DDS images

3DMark2001 SE (and Max Payne) keep their files in .ras archives whose
directory is encrypted, but the files themselves are stored raw. So this
does not read the directory at all: it finds each file by what it starts
with -- FORM....LWO2 for a LightWave object, LWSC for a scene, FF D8 FF for a
JPEG, "DDS " for a DirectDraw surface -- and writes it out under a name made
from its index, its size and (for an object) its first surface tag. The
filenames are lost with the directory; a scene's text names the objects it
loads, and an object's CLIP chunks name its textures, so the pairing is
recoverable by hand from there (docs/PORTING.md, "The 3DMark2001 dragon").

The output is someone else's copyrighted data: keep it under a gitignored
directory (assets/alt/), and never ship it (ATTRIBUTION.md).
"""
import io
import os
import re
import struct
import sys


def carve_objects(data, out):
    os.makedirs(os.path.join(out, "lwo"), exist_ok=True)
    os.makedirs(os.path.join(out, "lws"), exist_ok=True)
    count = 0
    for i, m in enumerate(re.finditer(rb"FORM....LWO2", data, re.DOTALL)):
        off = m.start()
        size = struct.unpack_from(">I", data, off + 4)[0]
        body = data[off:off + 8 + size]
        tags = body.find(b"TAGS")
        first = "untagged"
        if tags >= 0:
            first = body[tags + 8:tags + 8 + 40].split(b"\0")[0].decode("latin1") or first
        name = f"form{i:03d}_{re.sub(r'[^A-Za-z0-9_]', '_', first)}.lwo"
        open(os.path.join(out, "lwo", name), "wb").write(body)
        count += 1
    scenes = 0
    for i, m in enumerate(re.finditer(rb"LWSC\r?\n", data)):
        end = data.find(b"\0", m.start())
        open(os.path.join(out, "lws", f"scene{i:02d}.lws"), "wb").write(data[m.start():end if end > 0 else m.start() + 200000])
        scenes += 1
    print(f"{count} LightWave objects, {scenes} scenes")


def carve_images(data, out):
    os.makedirs(out, exist_ok=True)
    try:
        from PIL import Image
    except ImportError:
        sys.exit("needs Pillow to validate the images")
    n = 0
    last_end = 0
    for m in re.finditer(rb"\xff\xd8\xff[\xe0\xe1\xdb\xc0\xc4\xfe]", data):
        if m.start() < last_end:
            continue
        end = data.find(b"\xff\xd9", m.start())
        if end < 0:
            continue
        blob = data[m.start():end + 2]
        try:
            image = Image.open(io.BytesIO(blob))
            image.load()
        except Exception:
            continue
        open(os.path.join(out, f"jpg{n:03d}_{image.size[0]}x{image.size[1]}.jpg"), "wb").write(blob)
        n += 1
        last_end = end + 2
    starts = [m.start() for m in re.finditer(rb"DDS \x7c\x00\x00\x00", data)]
    d = 0
    for i, s in enumerate(starts):
        end = starts[i + 1] if i + 1 < len(starts) else len(data)
        blob = data[s:end]
        height, width = struct.unpack_from("<II", blob, 12)
        fourcc = blob[84:88].decode(errors="ignore").strip() or "rgb"
        try:
            Image.open(io.BytesIO(blob)).load()
        except Exception:
            continue
        open(os.path.join(out, f"dds{d:03d}_{width}x{height}_{fourcc}.dds"), "wb").write(blob)
        d += 1
    print(f"{n} JPEGs, {d} DDS")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    if data[:4] != b"RAS\0":
        sys.exit("not a .ras archive")
    if b"LWO2" in data[:4_000_000] or re.search(rb"FORM....LWO2", data, re.DOTALL):
        carve_objects(data, sys.argv[2])
    else:
        carve_images(data, sys.argv[2])
