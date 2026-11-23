"""Write body actions with source rest transforms and separate sample times."""
import struct
from hero_flight_glb import pack


def write(path, nodes, parents, clips):
    data = bytearray()
    doc = {'asset': {'version': '2.0', 'generator': 'Blender hero body loops',
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
            acc.update(min=[0], max=[values[-1][0]])
        doc['accessors'].append(acc)
        return index

    for name, (fps, tracks) in clips.items():
        count = len(next(iter(tracks.values())))
        times = add([(f / fps,) for f in range(count)], 'SCALAR')
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
