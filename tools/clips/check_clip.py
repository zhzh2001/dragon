#!/usr/bin/env python3
"""Check the additive Embercrest clip with the Python standard library."""
import argparse
import json
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
DURATION = 2.4
FPS = 30
KEYS = 73


def read_glb(path):
    data = Path(path).read_bytes()
    magic, version, length = struct.unpack_from('<4sII', data)
    if (magic, version, length) != (b'glTF', 2, len(data)):
        raise ValueError('GLB header failed. Supply a complete glTF 2 GLB.')
    chunks = {}
    offset = 12
    while offset < len(data):
        size, kind = struct.unpack_from('<I4s', data, offset)
        offset += 8
        if offset + size > len(data) or size % 4:
            raise ValueError('GLB chunk bounds failed. Rebuild the clip.')
        chunks[kind] = data[offset:offset + size]
        offset += size
    return json.loads(chunks[b'JSON']), chunks.get(b'BIN\0', b'')


def accessor(doc, binary, index):
    a = doc['accessors'][index]
    if 'sparse' in a:
        raise ValueError('Sparse accessor was unsupported. Bake dense keys.')
    formats = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}
    widths = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
    fmt = '<' + formats[a['componentType']] * widths[a['type']]
    size = struct.calcsize(fmt)
    v = doc['bufferViews'][a['bufferView']]
    if v.get('buffer', 0) != 0:
        raise ValueError('External buffer was unsupported. Embed the clip buffer.')
    stride = v.get('byteStride', size)
    start = v.get('byteOffset', 0) + a.get('byteOffset', 0)
    end = start + (a['count'] - 1) * stride + size
    if stride < size or end > v.get('byteOffset', 0) + v['byteLength'] or end > len(binary):
        raise ValueError('Accessor bounds failed. Rebuild the clip.')
    return [struct.unpack_from(fmt, binary, start + i * stride) for i in range(a['count'])]


def normalize(q):
    length = math.sqrt(sum(x * x for x in q))
    return tuple(x / length for x in q)


def inverse(q):
    q = normalize(q)
    return (-q[0], -q[1], -q[2], q[3])


def multiply(a, b):
    x, y, z, w = a
    X, Y, Z, W = b
    return (w * X + x * W + y * Z - z * Y,
            w * Y - x * Z + y * W + z * X,
            w * Z + x * Y - y * X + z * W,
            w * W - x * X - y * Y - z * Z)


def angle(a, b=(0, 0, 0, 1)):
    a, b = normalize(a), normalize(b)
    # atan2 avoids acos cancellation near rest and at the loop seam.
    q = multiply(a, inverse(b))
    return math.degrees(2 * math.atan2(math.sqrt(sum(x * x for x in q[:3])), abs(q[3])))


def rotation_vector(q):
    q = normalize(q)
    if q[3] < 0:
        q = tuple(-x for x in q)
    length = math.sqrt(sum(x * x for x in q[:3]))
    factor = math.degrees(2 * math.atan2(length, q[3])) / length if length else 0
    return tuple(x * factor for x in q[:3])


def group_limit(name):
    if name in ('root', 'chest'):
        return 'body', 6
    if name.startswith('neck_') or name == 'head':
        return 'neck/head', 10
    if name in ('jaw', 'jaw_tip'):
        return 'jaw', 8
    if name.startswith(('wing_root_', 'wing_arm_')):
        return 'wing shoulder ' + name[-1], 12
    if name.startswith(('wing_wrist_', 'wing_finger_')):
        return 'wing distal ' + name[-1], 16
    if name.startswith('tail_'):
        return 'tail', 15
    if name.startswith('toe_'):
        return ('hind toes' if name.startswith('toe_hind') else 'fore toes'), 25
    if name.startswith(('thigh_', 'shin_', 'ankle_', 'foot_')):
        return 'hind limbs', 20
    if name.startswith(('upper_arm_', 'forearm_', 'wrist_', 'hand_')):
        return 'forelimbs', 20
    raise ValueError('Unknown joint group: ' + name)


def parent_names(nodes):
    parents = {}
    for node in nodes:
        for child in node.get('children', []):
            name = nodes[child]['name']
            if name in parents:
                raise ValueError('Joint had multiple parents: ' + name)
            parents[name] = node['name']
    return parents


def check(source, clip):
    src, _ = read_glb(source)
    doc, binary = read_glb(clip)
    errors = []

    def require(ok, message):
        if not ok:
            errors.append(message)

    joints = src['skins'][0]['joints']
    source_nodes = {src['nodes'][i]['name']: src['nodes'][i] for i in joints}
    nodes = doc['nodes']
    names = [n['name'] for n in nodes]
    require(len(nodes) == len(joints) == 62, 'Expected exactly 62 joint nodes.')
    require(len(set(names)) == len(names), 'Joint names must be unique.')
    require(set(names) == set(source_nodes), 'Joint names differed from the source skin.')
    for field in ('meshes', 'skins', 'materials', 'textures', 'images', 'extensionsRequired'):
        require(not doc.get(field), 'Forbidden GLB payload: ' + field)
    for n in nodes:
        require(not any(k in n for k in ('mesh', 'skin', 'camera', 'matrix')), 'Node must contain only joint TRS: ' + n['name'])
    require(len(doc.get('buffers', [])) == 1 and 'uri' not in doc['buffers'][0], 'Expected one embedded buffer.')
    expected_parents = parent_names(src['nodes'])
    actual_parents = parent_names(nodes)
    rest_exact = True
    for node in nodes:
        name = node['name']
        if name not in source_nodes:
            continue
        original = source_nodes[name]
        parent = expected_parents.get(name)
        parent = parent if parent in source_nodes else None
        require(actual_parents.get(name) == parent, 'Parent differed: ' + name)
        expected_children = [src['nodes'][c]['name'] for c in original.get('children', []) if c in joints]
        require([nodes[c]['name'] for c in node.get('children', [])] == expected_children, 'Child hierarchy differed: ' + name)
        for prop, default in [('translation', [0, 0, 0]), ('rotation', [0, 0, 0, 1]), ('scale', [1, 1, 1])]:
            a, b = node.get(prop, default), original.get(prop, default)
            exact = (prop in node) == (prop in original) and a == b
            exact = exact and struct.pack('<' + 'f' * len(a), *a) == struct.pack('<' + 'f' * len(b), *b)
            require(exact, 'Rest ' + prop + ' differed: ' + name)
            rest_exact &= exact
    roots = [i for i, n in enumerate(nodes) if n['name'] not in actual_parents]
    require(doc['scenes'][doc.get('scene', 0)]['nodes'] == roots, 'Scene roots differed from joint roots.')
    animations = doc.get('animations', [])
    require([a.get('name') for a in animations] == ['brake_bank_l', 'brake_bank_r'], 'Expected the two named animations in left/right order.')
    report = {'rest_exact': rest_exact, 'joint_count': len(nodes), 'animations': {}, 'errors': errors}
    tracks = {}
    for anim in animations:
        name = anim['name']
        rows = []
        channels = {}
        for channel in anim['channels']:
            require(channel['target']['path'] == 'rotation', name + ': non-rotation channel.')
            index = channel['target']['node']
            joint = nodes[index]['name']
            require(joint not in channels, name + ': duplicate channel for ' + joint)
            sampler = anim['samplers'][channel['sampler']]
            require(sampler.get('interpolation', 'LINEAR') == 'LINEAR', name + ': keys must use LINEAR interpolation.')
            ia, oa = doc['accessors'][sampler['input']], doc['accessors'][sampler['output']]
            require((ia['componentType'], ia['type']) == (5126, 'SCALAR'), name + ': invalid time accessor.')
            require((oa['componentType'], oa['type']) == (5126, 'VEC4'), name + ': invalid rotation accessor.')
            times = [v[0] for v in accessor(doc, binary, sampler['input'])]
            keys = accessor(doc, binary, sampler['output'])
            require(len(times) == len(keys) == KEYS, name + ': expected 73 keys for ' + joint)
            require(all(abs(t - i / FPS) < 2e-7 for i, t in enumerate(times)), name + ': invalid frame times for ' + joint)
            require(all(math.isfinite(x) for q in keys for x in q), name + ': non-finite quaternion for ' + joint)
            require(all(abs(sum(x * x for x in q) - 1) < 2e-6 for q in keys), name + ': non-unit quaternion for ' + joint)
            require(keys[0] == keys[-1], name + ': endpoint keys differed for ' + joint)
            require(all(sum(a * b for a, b in zip(q, r)) > 0 for q, r in zip(keys, keys[1:])), name + ': quaternion sign jump for ' + joint)
            rest = nodes[index].get('rotation', [0, 0, 0, 1])
            deltas = [multiply(q, inverse(rest)) for q in keys]
            angles = [angle(q) for q in deltas]
            vectors = [rotation_vector(q) for q in deltas[:-1]]
            mean = math.sqrt(sum((sum(v[k] for v in vectors) / len(vectors)) ** 2 for k in range(3)))
            speeds = []
            for q, r, t, u in zip(keys, keys[1:], times, times[1:]):
                theta = math.radians(angle(q, r))
                # This also bounds normalized linear interpolation between samples.
                speeds.append(math.degrees(4 * math.tan(theta / 4)) / (u - t))
            group, limit = group_limit(joint)
            peak, speed = max(angles), max(speeds)
            require(peak <= limit + 1e-4, name + ': peak offset exceeded for ' + joint)
            require(mean <= limit / 2 + 1e-4, name + ': mean offset exceeded for ' + joint)
            require(speed <= 120 + 1e-4, name + ': angular speed exceeded for ' + joint)
            channels[joint] = deltas
            rows.append({'joint': joint, 'group': group, 'limit_deg': limit, 'peak_deg': peak,
                         'mean_deg': mean, 'speed_deg_s': speed, 'angles_deg': angles})
        require(set(channels) == set(source_nodes), name + ': every joint must have one rotation track.')
        tracks[name] = channels
        report['animations'][name] = rows
    mirror_error = 0
    if set(tracks) == {'brake_bank_l', 'brake_bank_r'}:
        for joint, left in tracks['brake_bank_l'].items():
            other = joint[:-2] + ('_r' if joint.endswith('_l') else '_l') if joint.endswith(('_l', '_r')) else joint
            right = tracks['brake_bank_r'][other]
            for a, b in zip(left, right):
                mirror_error = max(mirror_error, angle((a[0], -a[1], -a[2], a[3]), b))
        require(mirror_error < 0.0001, 'Right animation did not mirror left deltas.')
    report['mirror_error_deg'] = mirror_error
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'assets/embercrest.glb')
    parser.add_argument('--clip', type=Path, default=ROOT / 'assets/clips/embercrest.brake_bank.glb')
    parser.add_argument('--report', type=Path, help='Write measurements as JSON to this path.')
    args = parser.parse_args()
    try:
        report = check(args.source, args.clip)
    except (ValueError, KeyError, IndexError, ZeroDivisionError, struct.error, OSError) as exc:
        print('FAIL:', exc)
        return 1
    print('Rest TRS exact:', report['rest_exact'], '| joints:', report['joint_count'])
    print('Mirror error: %.8f deg' % report['mirror_error_deg'])
    for name, rows in report['animations'].items():
        print('\n' + name)
        print(f"{'joint':24} {'peak':>8} {'limit':>7} {'mean':>8} {'mean cap':>9} {'deg/s':>9}")
        for r in rows:
            print(f"{r['joint']:24} {r['peak_deg']:8.3f} {r['limit_deg']:7.1f} {r['mean_deg']:8.3f} {r['limit_deg']/2:9.1f} {r['speed_deg_s']:9.3f}")
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + '\n')
    for error in report['errors']:
        print('FAIL:', error)
    print('\n' + ('FAIL' if report['errors'] else 'PASS: rest, hierarchy, channels, loop, mirror, offsets, means and speeds.'))
    return bool(report['errors'])


if __name__ == '__main__':
    sys.exit(main())
