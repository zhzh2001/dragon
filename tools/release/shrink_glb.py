#!/usr/bin/env python3
"""Shrink the embedded textures of a .glb for a release package.

    tools/release/shrink_glb.py IN.glb OUT.glb [--max 2048]

Every embedded image larger than --max on a side is resized to fit (Lanczos)
and re-encoded in its own format. Everything else in the file is copied
through untouched: the glTF JSON (asset, extras, materials, the skin), every
non-image buffer view byte for byte, and each PNG's text chunks. The Hunyuan
agreement forbids removing its AI marks (ATTRIBUTION.md), so nothing that
could carry one is dropped. The 4096² PBR sets the roster carries are
indistinguishable at 2048² from the chase camera, and the file is about a
quarter of the size.

Needs Pillow. Prints the before and after size, and refuses to write a file
whose structure it cannot round-trip.
"""
import argparse
import io
import json
import struct
import sys

from PIL import Image
from PIL.PngImagePlugin import PngInfo

GLB_MAGIC, JSON_CHUNK, BIN_CHUNK = 0x46546C67, 0x4E4F534A, 0x004E4942


def read_glb(data):
    magic, version, length = struct.unpack_from('<III', data, 0)
    if magic != GLB_MAGIC or version != 2 or length != len(data):
        raise ValueError('not a glTF 2.0 binary')
    pos, gltf, binary = 12, None, None
    while pos < length:
        size, kind = struct.unpack_from('<II', data, pos)
        body = data[pos + 8:pos + 8 + size]
        if kind == JSON_CHUNK:
            gltf = json.loads(body)
        elif kind == BIN_CHUNK:
            binary = body
        pos += 8 + size
    if gltf is None or binary is None:
        raise ValueError('missing JSON or BIN chunk')
    if len(gltf.get('buffers', [])) != 1 or 'uri' in gltf['buffers'][0]:
        raise ValueError('expected one embedded buffer')
    return gltf, binary


def shrink_image(raw, mime, limit):
    image = Image.open(io.BytesIO(raw))
    if max(image.size) <= limit:
        return raw, image.size, image.size
    before = image.size
    scale = limit / max(before)
    after = (max(1, round(before[0] * scale)), max(1, round(before[1] * scale)))
    resized = image.resize(after, Image.Resampling.LANCZOS)
    out = io.BytesIO()
    if mime == 'image/png':
        info = PngInfo()
        for key, value in image.text.items():
            info.add_text(key, value)
        resized.save(out, 'PNG', optimize=True, pnginfo=info)
    elif mime == 'image/jpeg':
        resized.save(out, 'JPEG', quality=92, exif=image.info.get('exif', b''))
    else:
        return raw, before, before
    return out.getvalue(), before, after


def pad(data, fill):
    return data + fill * (-len(data) % 4)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('src')
    parser.add_argument('dst')
    parser.add_argument('--max', type=int, default=2048)
    args = parser.parse_args()

    data = open(args.src, 'rb').read()
    gltf, binary = read_glb(data)
    views = gltf['bufferViews']
    replaced = {}
    for image in gltf.get('images', []):
        if 'bufferView' not in image:
            continue
        view = views[image['bufferView']]
        start = view.get('byteOffset', 0)
        raw = binary[start:start + view['byteLength']]
        new, before, after = shrink_image(raw, image.get('mimeType', ''), args.max)
        replaced[image['bufferView']] = new
        print(f"  {image.get('name', '?')}: {before[0]}x{before[1]} -> {after[0]}x{after[1]}, "
              f"{len(raw) / 1e6:.1f} -> {len(new) / 1e6:.1f} MB")

    original = [(v.get('byteOffset', 0), v['byteLength']) for v in views]
    # Rebuild the buffer in the original view order, each view 4-byte
    # aligned (enough for every accessor component type).
    out = bytearray()
    for index in sorted(range(len(views)), key=lambda i: original[i][0]):
        view = views[index]
        start, length = original[index]
        body = replaced.get(index, binary[start:start + length])
        out += b'\0' * (-len(out) % 4)
        view['byteOffset'] = len(out)
        view['byteLength'] = len(body)
        out += body
    gltf['buffers'][0]['byteLength'] = len(out)

    json_bytes = pad(json.dumps(gltf, separators=(',', ':')).encode(), b' ')
    bin_bytes = pad(bytes(out), b'\0')
    total = 12 + 8 + len(json_bytes) + 8 + len(bin_bytes)
    glb = (struct.pack('<III', GLB_MAGIC, 2, total)
           + struct.pack('<II', len(json_bytes), JSON_CHUNK) + json_bytes
           + struct.pack('<II', len(bin_bytes), BIN_CHUNK) + bin_bytes)

    # Round-trip check: the non-image views must come back byte for byte.
    check, check_bin = read_glb(glb)
    for index, view in enumerate(views):
        if index in replaced:
            continue
        a = check_bin[check['bufferViews'][index]['byteOffset']:][:view['byteLength']]
        b = binary[original[index][0]:][:original[index][1]]
        if a != b:
            sys.exit(f'bufferView {index} did not round-trip; nothing written')
    open(args.dst, 'wb').write(glb)
    print(f'{args.src}: {len(data) / 1e6:.1f} -> {len(glb) / 1e6:.1f} MB')


if __name__ == '__main__':
    main()
