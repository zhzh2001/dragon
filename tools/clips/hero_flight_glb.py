"""Write dense rotation tracks with the attack clip's binary layout."""
import json
import struct


def pack(doc, data):
    header = json.dumps(doc, separators=(',', ':'), allow_nan=False).encode()
    header += b' ' * (-len(header) % 4)
    data = bytes(data) + b'\0' * (-len(data) % 4)
    return (struct.pack('<4sII', b'glTF', 2, 28 + len(header) + len(data))
            + struct.pack('<I4s', len(header), b'JSON') + header
            + struct.pack('<I4s', len(data), b'BIN\0') + data)


def write(path, nodes, parents, clips):
    data = bytearray()
    doc = {'asset': {'version': '2.0', 'generator': 'Blender hero wingbeat',
                     'extras': {'ai_generated': True}}, 'scene': 0,
           'scenes': [{'nodes': [i for i in range(len(nodes)) if i not in parents]}],
           'nodes': nodes, 'animations': [], 'accessors': [], 'bufferViews': []}

    def add(values, kind):
        start = len(data)
        for v in values:
            data.extend(struct.pack('<' + 'f' * len(v), *v))
        index = len(doc['bufferViews'])
        doc['bufferViews'].append({'buffer': 0, 'byteOffset': start, 'byteLength': len(data) - start})
        acc = {'bufferView': index, 'componentType': 5126, 'count': len(values), 'type': kind}
        if kind == 'SCALAR':
            acc.update(min=[0], max=[1])
        doc['accessors'].append(acc)
        return index

    times = add([(f / 60,) for f in range(61)], 'SCALAR')
    for name, tracks in clips.items():
        anim = {'name': name, 'samplers': [], 'channels': []}
        for i, node in enumerate(nodes):
            if node['name'] in tracks:
                out = add(tracks[node['name']], 'VEC4')
                anim['channels'].append({'sampler': len(anim['samplers']),
                                         'target': {'node': i, 'path': 'rotation'}})
                anim['samplers'].append({'input': times, 'output': out, 'interpolation': 'LINEAR'})
        doc['animations'].append(anim)
    doc['buffers'] = [{'byteLength': len(data)}]
    path.write_bytes(pack(doc, data))
