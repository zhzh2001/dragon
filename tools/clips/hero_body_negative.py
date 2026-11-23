#!/usr/bin/env python3
"""Pass damaged scratch exports to the body checker and check each rejection."""
import argparse
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
from check_hero_body import CLIP, NAMES, ROOT, REST
from check_clip import read_glb
from hero_flight_glb import pack

CASES = (
    (1,'name','animations'), (1,'samples','malformed tracks'),
    (2,'rest','rest differences'), (2,'scale','nonrotation paths'),
    (3,'air-wing','forbidden joints'), (4,'position','seam value'),
    (4,'velocity','seam velocity'), (5,'mirror','mirror'), (6,'step','largest key step'),
    (7,'amplitude','stroke pitch'), (8,'aim','head aim diameter'),
    (3,'ground-leg','forbidden joints'), (7,'sway','neck yaw over planted'), (9,'dip','neck dip peaks'),
)


def corrupt(source, label):
    doc, binary = read_glb(source)
    data = bytearray(binary)
    name = 'walk' if label in ('ground-leg','sway','dip') else 'flap_body'
    anim = next(a for a in doc['animations'] if a['name'] == name)
    indices = {n['name']:i for i,n in enumerate(doc['nodes'])}

    def channel(joint):
        return next(c for c in anim['channels'] if c['target']['node'] == indices[joint])

    def keys(joint):
        s = anim['samplers'][channel(joint)['sampler']]
        a = doc['accessors'][s['output']]
        view = doc['bufferViews'][a['bufferView']]
        return a['count'], view.get('byteOffset',0) + a.get('byteOffset',0), view.get('byteStride',16)

    def set_key(joint, i, q):
        count, start, stride = keys(joint)
        struct.pack_into('<4f',data,start+(i%count)*stride,*q)

    def shift(joint, frames):
        count, start, stride = keys(joint)
        for i in range(count):
            pos = start + ((i+frames)%(count-1))*stride
            set_key(joint,i,struct.unpack_from('<4f',binary,pos))

    if label == 'name':
        anim['name'] = 'damaged'
    elif label == 'samples':
        a = doc['accessors'][anim['samplers'][0]['input']]
        a['count'] -= 1
    elif label == 'rest':
        doc['nodes'][indices['neck_01']]['translation'][1] += .001
    elif label == 'scale':
        channel('tail_01')['target']['path'] = 'scale'
    elif label in ('air-wing','ground-leg'):
        channel('tail_01')['target']['node'] = indices['wing_root_l' if label=='air-wing' else 'thigh_l']
    elif label == 'position':
        set_key('tail_01',-1,(.258819,0,0,.965926))
    elif label == 'velocity':
        # Reverse the incoming velocity while preserving both endpoint keys.
        count, start, stride = keys('tail_01')
        set_key('tail_01',count-2,struct.unpack_from('<4f',binary,start+stride))
    elif label == 'mirror':
        shift('thigh_r',6)
    elif label == 'step':
        set_key('tail_08',30,(.5,0,0,.8660254))
    elif label in ('amplitude','aim'):
        joints = ('thigh_l','thigh_r') if label == 'amplitude' else ('head',)
        for joint in joints:
            for i in range(keys(joint)[0]):
                set_key(joint,i,REST)
    elif label == 'sway':
        for joint in ('neck_01','neck_02','neck_03'):
            if any(c['target']['node'] == indices[joint] for c in anim['channels']):
                count, start, stride = keys(joint)
                for i in range(count):
                    x,y,z,w = struct.unpack_from('<4f',binary,start+i*stride)
                    set_key(joint,i,(x,-y,-z,w))
    elif label == 'dip':
        for joint in ('neck_01','neck_02','neck_03'):
            if any(c['target']['node'] == indices[joint] for c in anim['channels']):
                shift(joint,12)
    return pack(doc,data)


def run(source, through):
    cache = ROOT/'build-clip-cache'
    cache.mkdir(exist_ok=True)
    checker = Path(__file__).with_name('check_hero_body.py')
    options = ['--through',through] if through else []
    for group, label, phrase in CASES:
        if label in ('ground-leg','sway','dip') and through and NAMES.index(through) < NAMES.index('walk'):
            continue
        path = cache / f'hero-body-bad-{label}.glb'
        path.write_bytes(corrupt(source,label))
        proc = subprocess.run([sys.executable,'-B',str(checker),str(path),*options],
                              text=True,capture_output=True)
        failure = next((s for s in proc.stdout.splitlines() if s.startswith(f'FAIL {group} ') and phrase in s),None)
        if proc.returncode != 1 or failure is None:
            raise RuntimeError(f'Checker missed {label}.\n{proc.stdout}\n{proc.stderr}')
        print(f'PASS {label} rejected, exit 1, {failure}',flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('clip',type=Path,nargs='?',default=CLIP)
    parser.add_argument('--through',choices=NAMES)
    args = parser.parse_args()
    run(args.clip,args.through)
