"""Read the vertex streams of the model's one primitive as NumPy arrays."""

import numpy as np

COMPONENT = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
WIDTH = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def accessor(doc, blob, index):
    acc = doc["accessors"][index]
    view = doc["bufferViews"][acc["bufferView"]]
    dtype = np.dtype(COMPONENT[acc["componentType"]])
    width = WIDTH[acc["type"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    stride = view.get("byteStride", 0)
    count = acc["count"]
    if stride and stride != dtype.itemsize * width:
        raw = np.frombuffer(blob, dtype=np.uint8, count=stride * count, offset=start).reshape(count, stride)
        data = raw[:, :dtype.itemsize * width].copy().view(dtype).reshape(count, width)
    else:
        data = np.frombuffer(blob, dtype=dtype, count=count * width, offset=start).reshape(count, width)
    if acc.get("normalized"):
        data = data.astype(np.float32) / np.iinfo(dtype).max
    return data


def primitive(doc, blob):
    prim = doc["meshes"][0]["primitives"][0]
    attrs = prim["attributes"]
    out = {name: accessor(doc, blob, idx) for name, idx in attrs.items()}
    out["indices"] = accessor(doc, blob, prim["indices"]).reshape(-1, 3).astype(np.int64)
    return out


def joint_names(doc):
    skin = doc["skins"][0]
    return [doc["nodes"][j].get("name", str(j)) for j in skin["joints"]]
