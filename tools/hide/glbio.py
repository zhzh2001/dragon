"""Read and write a binary glTF (.glb) without touching anything but its images.

`read_glb` returns the JSON document and the binary chunk. `image_bytes`
returns the encoded bytes of one image. `write_glb` rebuilds the binary chunk
with new bytes for some images and leaves every other bufferView's bytes as
they were. Only `byteOffset` and `byteLength` change, and only because the
new PNGs have different lengths.
"""

import json
import struct

JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942


def read_glb(path):
    with open(path, "rb") as f:
        data = f.read()
    magic, version, _length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2:
        raise ValueError(f"{path} is not a glTF 2 binary file")
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != JSON_CHUNK:
        raise ValueError("the first chunk is not JSON")
    json_bytes = data[20:20 + json_length]
    doc = json.loads(json_bytes)
    offset = 20 + json_length
    bin_length, bin_type = struct.unpack_from("<II", data, offset)
    if bin_type != BIN_CHUNK:
        raise ValueError("the second chunk is not BIN")
    blob = data[offset + 8:offset + 8 + bin_length]
    return doc, blob


def view_bytes(doc, blob, view_index):
    view = doc["bufferViews"][view_index]
    start = view.get("byteOffset", 0)
    # The buffer's declared length excludes the chunk's padding.
    return blob[start:start + view["byteLength"]]


def image_bytes(doc, blob, image_index):
    return view_bytes(doc, blob, doc["images"][image_index]["bufferView"])


def write_glb(path, doc, blob, new_images):
    """Write `doc` with `new_images` ({image index: encoded bytes}) replaced.

    The function lays the bufferViews out again in their original order of
    offset, each aligned to four bytes, and copies the bytes of every view
    that is not a replaced image.
    """
    doc = json.loads(json.dumps(doc))  # a deep copy; the caller's stays whole
    replaced = {doc["images"][i]["bufferView"]: b for i, b in new_images.items()}
    order = sorted(range(len(doc["bufferViews"])), key=lambda i: doc["bufferViews"][i].get("byteOffset", 0))
    out = bytearray()
    for index in order:
        view = doc["bufferViews"][index]
        payload = replaced.get(index)
        if payload is None:
            start = view.get("byteOffset", 0)
            payload = blob[start:start + view["byteLength"]]
        while len(out) % 4:
            out.append(0)
        view["byteOffset"] = len(out)
        view["byteLength"] = len(payload)
        out += payload
    while len(out) % 4:
        out.append(0)
    doc["buffers"][0]["byteLength"] = len(out)
    json_bytes = json.dumps(doc, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    json_bytes += b" " * (-len(json_bytes) % 4)
    total = 12 + 8 + len(json_bytes) + 8 + len(out)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(json_bytes), JSON_CHUNK))
        f.write(json_bytes)
        f.write(struct.pack("<II", len(out), BIN_CHUNK))
        f.write(out)
