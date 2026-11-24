#!/usr/bin/env python3
"""Damage exported clips and require each checker group to reject its defect."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
from check_clip import read_glb
from check_wingbeat import ROOT, STUDIES
from wingbeat.glb import pack


def corrupt(model, label):
    doc, binary = read_glb(ROOT / f'assets/clips/{model}.flight.glb')
    data = bytearray(binary)
    cruise = next(a for a in doc['animations'] if a['name'] == 'flap')
    climb = next(a for a in doc['animations'] if a['name'] == 'flap_climb')

    def name(channel):
        return doc['nodes'][channel['target']['node']]['name']

    def key_offset(channel, key):
        acc = doc['accessors'][cruise['samplers'][channel['sampler']]['output']]
        view = doc['bufferViews'][acc['bufferView']]
        return view.get('byteOffset', 0) + acc.get('byteOffset', 0) + key * view.get('byteStride', 16)

    def shift(channel, index, offset):
        dest, source = key_offset(channel, index), key_offset(channel, offset)
        data[dest:dest+16] = binary[source:source+16]

    arm = next(c for c in cruise['channels'] if name(c) == 'wing_arm_r')
    if label == 'name':
        cruise['name'] = 'broken'
    elif label == 'samples':
        times = cruise['samplers'][0]['input']
        doc['accessors'][times]['count'] -= 1
    elif label == 'rest':
        doc['nodes'][0].setdefault('translation', [0, 0, 0])[0] += .01
    elif label == 'channel':
        arm['target']['path'] = 'scale'
    elif label == 'body':
        arm['target']['node'] = next(i for i, n in enumerate(doc['nodes']) if n['name'] == 'chest')
    elif label == 'seam':
        shift(arm, 60, 20)
    elif label in ('mirror', 'step'):
        i = key_offset(arm, 20)
        x, y, z, w = struct.unpack_from('<4f', binary, i)
        struct.pack_into('<4f', data, i, w, -z, y, -x)
    elif label == 'hold':
        # Pause both wrists across the seam. Each half alone is shorter than the limit.
        for c in cruise['channels']:
            if name(c).startswith('wing_wrist_'):
                for key in (57, 58, 59, 60, 0, 1, 2, 3):
                    shift(c, key, 0)
    elif label == 'amplitude':
        climb['channels'] = cruise['channels']
        climb['samplers'] = cruise['samplers']
    else:
        for c in cruise['channels']:
            if label == 'lag' and not name(c).startswith('wing_finger_'):
                continue
            if label == 'fold' and not name(c).startswith('wing_wrist_'):
                continue
            for key in range(61):
                shift(c, key, (key + 15) % 60)
    return pack(doc, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model', choices=('tide2', 'storm3', *STUDIES))
    model = parser.parse_args().model
    directory = ROOT / 'build-clip-cache' / model / 'damaged'
    directory.mkdir(parents=True, exist_ok=True)
    cases = [(1, 'name'), (1, 'samples'), (2, 'rest'), (2, 'channel'),
                         (2, 'body'), (3, 'seam'), (4, 'mirror'), (5, 'phase'),
                         (6, 'lag'), (7, 'fold'), (8, 'step'), (9, 'amplitude')]
    if model in STUDIES:
        cases = [(10, 'hold')]
    for group, label in cases:
        path = directory / f'{label}.glb'
        path.write_bytes(corrupt(model, label))
        result = subprocess.run([sys.executable, '-B', str(Path(__file__).with_name('check_wingbeat.py')),
                                 model, str(path)], capture_output=True, text=True)
        line = next((s for s in result.stdout.splitlines() if s.startswith(f'FAIL {group} ')), None)
        if result.returncode != 1 or not line:
            raise RuntimeError(f'The checker missed {label}.\n{result.stdout}{result.stderr}')
        print(f'PASS {label}, exit 1, {line}')


if __name__ == '__main__':
    main()
