"""Repair undefined tangent vectors at degenerate UV triangles in exported GLB.

Only zero/non-finite tangent XYZ values are replaced. Embedded images, UVs,
positions, normals, indices and skin data are left byte-for-byte unchanged.
"""
import json
import math
import struct
from pathlib import Path


def repair_tangents(path):
    path = Path(path)
    data = bytearray(path.read_bytes())
    magic, version, total = struct.unpack_from('<4sII', data)
    assert magic == b'glTF' and version == 2 and total == len(data)
    offset = 12
    document = None
    binary_start = None
    while offset < len(data):
        length, kind = struct.unpack_from('<II', data, offset)
        if kind == 0x4e4f534a:
            document = json.loads(data[offset + 8:offset + 8 + length])
        elif kind == 0x004e4942:
            binary_start = offset + 8
        offset += 8 + length
    assert document is not None and binary_start is not None

    def address(accessor_id, index, components):
        accessor = document['accessors'][accessor_id]
        assert accessor['componentType'] == 5126 and 'sparse' not in accessor
        view = document['bufferViews'][accessor['bufferView']]
        assert view.get('buffer', 0) == 0
        return (binary_start + view.get('byteOffset', 0) +
                accessor.get('byteOffset', 0) + index * view.get('byteStride', 4 * components))

    repaired = 0
    visited = set()
    for mesh in document['meshes']:
        for primitive in mesh['primitives']:
            attrs = primitive['attributes']
            if 'TANGENT' not in attrs:
                continue
            tangents, normals = attrs['TANGENT'], attrs['NORMAL']
            if tangents in visited:
                continue
            visited.add(tangents)
            count = document['accessors'][tangents]['count']
            assert document['accessors'][normals]['count'] == count
            for index in range(count):
                at = address(tangents, index, 4)
                tangent = struct.unpack_from('<4f', data, at)
                if all(math.isfinite(x) for x in tangent[:3]) and sum(x*x for x in tangent[:3]) > 1e-12:
                    continue
                normal = struct.unpack_from('<3f', data, address(normals, index, 3))
                length = math.sqrt(sum(x*x for x in normal))
                assert math.isfinite(length) and length > 1e-8
                normal = [x / length for x in normal]
                axis = min(range(3), key=lambda i: abs(normal[i]))
                # Project the least-parallel cardinal axis onto the tangent
                # plane. UV-singular triangles have no unique tangent direction.
                replacement = [(1.0 if i == axis else 0.0) - normal[axis] * normal[i] for i in range(3)]
                length = math.sqrt(sum(x*x for x in replacement))
                handedness = -1.0 if tangent[3] < 0 else 1.0
                struct.pack_into('<4f', data, at, *(x / length for x in replacement), handedness)
                repaired += 1
    if repaired:
        path.write_bytes(data)
    return repaired


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('glb', type=Path)
    print('Repaired tangent vectors:', repair_tangents(parser.parse_args().glb))
